'use strict';

const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');
const P = require('./paths');

const RUN_KEY = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run';
const RUN_VALUE = 'ScreenDoodle';

const DEFAULTS = {
  tool: 'pen',
  thicknessIdx: 1,
  color: { r: 235, g: 60, b: 60 },
  anchor: 'right',
  hotkeys: {
    toggle: 'Ctrl+Alt+D',
    undo: 'Ctrl+Shift+Z',
    clear: 'Ctrl+Shift+X'
  }
};

const TOOLS = ['pen', 'pencil', 'highlighter', 'eraser', 'text'];
const ANCHORS = ['left', 'right'];

function clampInt(v, lo, hi, fallback) {
  const n = parseInt(v, 10);
  if (Number.isNaN(n)) return fallback;
  return Math.min(hi, Math.max(lo, n));
}

function normalise(raw) {
  const c = raw && typeof raw === 'object' ? raw : {};
  const col = c.color && typeof c.color === 'object' ? c.color : {};
  const hk = c.hotkeys && typeof c.hotkeys === 'object' ? c.hotkeys : {};
  return {
    tool: TOOLS.includes(c.tool) ? c.tool : DEFAULTS.tool,
    thicknessIdx: clampInt(c.thicknessIdx, 0, 4, DEFAULTS.thicknessIdx),
    color: {
      r: clampInt(col.r, 0, 255, DEFAULTS.color.r),
      g: clampInt(col.g, 0, 255, DEFAULTS.color.g),
      b: clampInt(col.b, 0, 255, DEFAULTS.color.b)
    },
    anchor: ANCHORS.includes(c.anchor) ? c.anchor : DEFAULTS.anchor,
    hotkeys: {
      toggle: normaliseHotkey(hk.toggle, DEFAULTS.hotkeys.toggle),
      undo: normaliseHotkey(hk.undo, DEFAULTS.hotkeys.undo),
      clear: normaliseHotkey(hk.clear, DEFAULTS.hotkeys.clear)
    }
  };
}

function normaliseHotkey(value, fallback) {
  if (typeof value !== 'string' || !value.trim()) return fallback;
  const parts = value.split('+').map((p) => p.trim().toLowerCase()).filter(Boolean);
  const mods = [];
  let key = null;
  for (const p of parts) {
    if (p === 'ctrl' || p === 'control') mods.push('Ctrl');
    else if (p === 'alt') mods.push('Alt');
    else if (p === 'shift') mods.push('Shift');
    else if (p === 'win' || p === 'meta') mods.push('Win');
    else key = p;
  }
  if (!key) return fallback;

  const order = ['Ctrl', 'Alt', 'Shift', 'Win'];
  const uniq = order.filter((m) => mods.includes(m));
  if (!uniq.length) return fallback;

  const named = keyToken(key);
  if (!named) return fallback;
  return [...uniq, named].join('+');
}

function keyToken(k) {
  if (/^[a-z0-9]$/.test(k)) return k.toUpperCase();
  if (/^f([1-9]|1[0-2])$/.test(k)) return k.toUpperCase();
  const named = {
    space: 'Space',
    tab: 'Tab',
    esc: 'Escape',
    escape: 'Escape',
    insert: 'Insert',
    delete: 'Delete',
    home: 'Home',
    end: 'End',
    pageup: 'PageUp',
    pagedown: 'PageDown',
    up: 'Up',
    down: 'Down',
    left: 'Left',
    right: 'Right'
  };
  return named[k] || null;
}

function read() {
  try {
    return normalise(JSON.parse(fs.readFileSync(P.appConfig, 'utf8')));
  } catch (_) {
    return { ...DEFAULTS, color: { ...DEFAULTS.color }, hotkeys: { ...DEFAULTS.hotkeys } };
  }
}

function write(patch) {
  const merged = normalise({ ...read(), ...patch });
  fs.mkdirSync(P.appConfigDir, { recursive: true });

  const tmp = `${P.appConfig}.tmp`;
  fs.writeFileSync(tmp, JSON.stringify(merged, null, 2), 'utf8');
  fs.renameSync(tmp, P.appConfig);
  return merged;
}

function reset() {
  return write({ ...DEFAULTS, color: { ...DEFAULTS.color }, hotkeys: { ...DEFAULTS.hotkeys } });
}

function getAutostart() {
  return new Promise((resolve) => {
    execFile('reg', ['query', RUN_KEY, '/v', RUN_VALUE], { windowsHide: true }, (err, stdout) => {
      resolve(!err && !!stdout && stdout.includes(RUN_VALUE));
    });
  });
}

function setAutostart(enabled, exePath) {
  return new Promise((resolve, reject) => {
    if (enabled) {
      if (!exePath) return reject(new Error('The overlay is unavailable, so autostart cannot be enabled.'));
      execFile(
        'reg',
        ['add', RUN_KEY, '/v', RUN_VALUE, '/t', 'REG_SZ', '/d', `"${exePath}"`, '/f'],
        { windowsHide: true },
        (err) => (err ? reject(new Error('Could not write the startup entry.')) : resolve(true))
      );
    } else {
      execFile('reg', ['delete', RUN_KEY, '/v', RUN_VALUE, '/f'], { windowsHide: true }, () =>
        resolve(false)
      );
    }
  });
}

module.exports = {
  DEFAULTS,
  TOOLS,
  ANCHORS,
  read,
  write,
  reset,
  getAutostart,
  setAutostart,
  configPath: P.appConfig,
  configDir: P.appConfigDir,
  normaliseHotkey
};
