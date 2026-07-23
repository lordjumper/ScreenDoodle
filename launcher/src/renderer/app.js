'use strict';

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => Array.from(document.querySelectorAll(sel));

const ICONS = {
  play: '<svg viewBox="0 0 24 24"><path d="M7 4.8v14.4L19 12z" fill="currentColor" stroke="none"/></svg>',
  download: '<svg viewBox="0 0 24 24"><path d="M12 3.5v11m0 0 4-4m-4 4-4-4M4.5 19.5h15"/></svg>',
  check: '<svg viewBox="0 0 24 24"><path d="m5 12.5 4.5 4.5L19 7.5"/></svg>',
  spin: '<svg viewBox="0 0 24 24" class="spin"><path d="M12 3.5a8.5 8.5 0 1 0 8.5 8.5"/></svg>'
};

const state = {
  app: null,
  update: null,
  config: null,
  busy: false,
  capturing: null
};

function toast(msg, kind = '') {
  const el = $('#toast');
  el.textContent = msg;
  el.className = `toast show ${kind}`;
  el.hidden = false;
  clearTimeout(toast._t);
  toast._t = setTimeout(() => {
    el.classList.remove('show');
    setTimeout(() => (el.hidden = true), 220);
  }, 3400);
}

function confirmBox(title, body, confirmLabel = 'Confirm') {
  return new Promise((resolve) => {
    const modal = $('#confirm');
    $('#confirmTitle').textContent = title;
    $('#confirmBody').textContent = body;
    $('#confirmYes').textContent = confirmLabel;
    modal.hidden = false;

    const done = (answer) => {
      modal.hidden = true;
      $('#confirmYes').onclick = null;
      $('#confirmNo').onclick = null;
      modal.onclick = null;
      document.removeEventListener('keydown', onKey, true);
      resolve(answer);
    };
    const onKey = (e) => {
      if (e.key === 'Escape') { e.stopPropagation(); done(false); }
      if (e.key === 'Enter')  { e.stopPropagation(); done(true); }
    };

    $('#confirmYes').onclick = () => done(true);
    $('#confirmNo').onclick = () => done(false);
    modal.onclick = (e) => { if (e.target === modal) done(false); };
    document.addEventListener('keydown', onKey, true);
    $('#confirmNo').focus();
  });
}

async function call(fn, ...args) {
  const res = await fn(...args);
  if (!res || !res.ok) throw new Error((res && res.error) || 'Unknown error');
  return res.data;
}

function fmtBytes(n) {
  if (!n) return '';
  const u = ['B', 'KB', 'MB', 'GB'];
  const i = Math.min(Math.floor(Math.log(n) / Math.log(1024)), u.length - 1);
  return `${(n / Math.pow(1024, i)).toFixed(i ? 1 : 0)} ${u[i]}`;
}

$('#btnMin').onclick = () => window.sdl.window.minimize();
$('#btnClose').onclick = () => window.sdl.window.close();

function openSheet(id) {
  $(`#${id}`).hidden = false;
  if (id === 'sheetSettings') loadSettings();
  if (id === 'sheetUpdates') renderUpdates();
}

function closeSheets() {
  $$('.sheet').forEach((s) => (s.hidden = true));
  if (state.capturing) stopCapture();
}

$('#btnUpdates').onclick = () => openSheet('sheetUpdates');
$('#btnSettings').onclick = () => openSheet('sheetSettings');
$$('[data-close]').forEach((b) => (b.onclick = closeSheets));

window.addEventListener('keydown', (e) => {
  if (e.key !== 'Escape') return;
  if (state.capturing) return;
  if (!$('#confirm').hidden) return;
  closeSheets();
});

function renderMode() {
  const s = state.app;
  if (!s) return;
  const installing = !!s.needsInstall;

  $('#viewInstall').hidden = !installing;
  $('.home').hidden = installing;
  $('#btnUpdates').hidden = installing;
  $('#btnSettings').hidden = installing;

  if (installing) {
    const upgrade = !!s.installed;
    $('#installVersion').textContent = upgrade
      ? `VERSION ${s.version} · UPDATE`
      : `VERSION ${s.version}`;
    if (!$('#installDir').value) {
      $('#installDir').value = s.installDir || s.defaultInstallDir;
    }

    const old = s.legacy;
    $('#legacyNote').hidden = !(old && old.found);
    if (old && old.found) {
      $('#legacyNote').textContent =
        `ScreenDoodle ${old.version || ''} is already installed at ${old.dir}. ` +
        'It will be removed first, so you are left with a single copy.';
    }
    setCta('#btnInstall', '#installIcon', '#installLabel', 'download',
           upgrade ? 'Update' : 'Install', false);
  }
}

$('#btnBrowse').onclick = async () => {
  try {
    const dir = await call(window.sdl.chooseDir);
    if (dir) $('#installDir').value = dir;
  } catch (err) {
    toast(err.message, 'err');
  }
};

$('#btnInstall').onclick = async () => {
  if (state.busy) return;
  const dir = $('#installDir').value.trim();
  if (!dir) return toast('Choose an install location.', 'err');

  state.busy = true;
  $('#installProgress').hidden = false;
  setCta('#btnInstall', '#installIcon', '#installLabel', 'spin', 'Installing…', true);

  sink = {
    bar: $('#installProgress'),
    fill: $('#installProgressFill'),
    text: $('#installProgressText')
  };

  try {
    const res = await call(window.sdl.install, {
      installDir: dir,
      desktopShortcut: $('#optDesktop').checked,
      autostart: $('#optAutostart').checked
    });
    sink.fill.classList.remove('indeterminate');
    sink.fill.style.width = '100%';
    setCta('#btnInstall', '#installIcon', '#installLabel', 'check', 'Starting…', true);
    $('#installProgressText').textContent = 'Installed';

    await call(window.sdl.finishInstall, res.exePath);
  } catch (err) {
    state.busy = false;
    sink = null;
    $('#installProgress').hidden = true;
    setCta('#btnInstall', '#installIcon', '#installLabel', 'download', 'Install', false);
    toast(err.message, 'err');
  }
};

function setCta(btnSel, iconSel, labelSel, icon, label, disabled) {
  $(iconSel).innerHTML = ICONS[icon] || '';
  $(labelSel).textContent = label;
  $(btnSel).disabled = !!disabled;
}

function renderHome() {
  const s = state.app;
  const u = state.update;
  if (!s || state.busy) return;

  const dot = $('#statusDot');
  const text = $('#statusText');
  const keys = $('#ctaKeys');

  $('#updateBadge').hidden = !(u && u.updateAvailable);

  if (!s.overlayAvailable) {
    dot.className = 'status-dot';
    text.textContent = `version ${s.version} · overlay missing`;
    keys.hidden = true;
    $('#hint').textContent = s.packaged
      ? 'REINSTALL SCREENDOODLE TO RESTORE THE OVERLAY'
      : 'BUILD THE OVERLAY WITH CMAKE FIRST';
    setCta('#btnPrimary', '#ctaIcon', '#ctaLabel', 'play', 'Overlay unavailable', true);
    return;
  }

  if (u && u.updateAvailable) {
    dot.className = 'status-dot update';
    text.textContent = `version ${s.version} · update ready`;
  } else if (u) {
    dot.className = 'status-dot ok';
    text.textContent = `version ${s.version} · stable`;
  } else {
    dot.className = 'status-dot ok';
    text.textContent = `version ${s.version} · offline`;
  }

  const combo = state.config && state.config.hotkeys ? state.config.hotkeys.toggle : null;
  keys.hidden = !combo;
  if (combo) keys.textContent = combo;

  $('#hint').textContent = s.running
    ? 'SCREENDOODLE IS RUNNING IN YOUR TRAY'
    : 'PRESS THE HOTKEY TO DRAW ANYWHERE ON SCREEN';

  setCta('#btnPrimary', '#ctaIcon', '#ctaLabel', 'play',
         s.running ? 'Overlay running' : 'Launch overlay', false);
}

$('#btnPrimary').onclick = async () => {
  if (!state.app || state.busy) return;
  try {
    const r = await call(window.sdl.launch);
    toast(r.alreadyRunning ? 'Overlay is already running.' : 'Overlay launched.');
    await refresh();
  } catch (err) {
    toast(err.message, 'err');
  }
};

function renderUpdates() {
  const u = state.update;
  const banner = $('#updateBanner');
  const notes = $('#notesList');
  notes.textContent = '';

  if (!u) {
    banner.className = 'banner';
    $('#bannerIcon').innerHTML = '';
    $('#bannerTitle').textContent = 'Could not reach GitHub';
    $('#bannerSub').textContent = state.app ? `You're on ${state.app.version}` : '';
    setCta('#btnUpdateAction', '#updIcon', '#updLabel', 'download', 'Retry', false);
    $('#notesLabel').textContent = "WHAT'S NEW";
    notes.appendChild(note('Release notes need a connection.', true));
    return;
  }

  const latest = u.latest;
  $('#notesLabel').textContent = `WHAT'S NEW IN ${latest.tag}`;

  if (u.updateAvailable) {
    banner.className = 'banner is-update';
    $('#bannerIcon').innerHTML = ICONS.download;
    $('#bannerTitle').textContent = `Version ${latest.version} available`;
    $('#bannerSub').textContent = `You're on ${u.installedVersion} · ${fmtBytes(u.assetSize)}`;
    setCta('#btnUpdateAction', '#updIcon', '#updLabel', 'download', 'Download & install', false);
  } else {
    banner.className = 'banner is-current';
    $('#bannerIcon').innerHTML = ICONS.check;
    $('#bannerTitle').textContent = "You're up to date";
    $('#bannerSub').textContent = `Version ${u.installedVersion} is the latest`;
    setCta('#btnUpdateAction', '#updIcon', '#updLabel', 'check', 'Check again', false);
  }

  const lines = (latest.body || '')
    .split('\n')
    .map((l) => l.replace(/^[\s>*\-+#]+/, '').replace(/[*_`]/g, '').trim())
    .filter(Boolean)
    .slice(0, 12);

  if (!lines.length) notes.appendChild(note(`${latest.tag} shipped without release notes.`, true));
  else for (const line of lines) notes.appendChild(note(line));
}

function note(text, empty = false) {
  const li = document.createElement('li');
  if (empty) li.className = 'empty';
  li.textContent = text;
  return li;
}

$('#btnUpdateAction').onclick = async () => {
  if (state.busy) return;

  if (!state.update || !state.update.updateAvailable) {
    toast('Checking…');
    await refresh();
    renderUpdates();
    toast(state.update && state.update.updateAvailable
      ? `Update available: ${state.update.latest.tag}`
      : "You're on the latest version.");
    return;
  }

  const ok = await confirmBox(
    'Install update?',
    `ScreenDoodle ${state.update.latest.version} will be downloaded and the installer ` +
    'will start. The launcher and overlay both close during the update.',
    'Download & install'
  );
  if (!ok) return;

  await runJob(window.sdl.update, '#updProgress', '#updProgressFill', '#updProgressText');
};

$('#btnAllReleases').onclick = () => {
  if (!state.app) return;
  window.sdl.openExternal(state.app.releasesPage).catch((e) => toast(e.message, 'err'));
};

let sink = null;

async function runJob(fn, barSel, fillSel, textSel) {
  sink = { bar: $(barSel), fill: $(fillSel), text: textSel ? $(textSel) : null };
  state.busy = true;
  sink.bar.hidden = false;
  setCta('#btnPrimary', '#ctaIcon', '#ctaLabel', 'spin', 'Working…', true);
  setCta('#btnUpdateAction', '#updIcon', '#updLabel', 'spin', 'Working…', true);

  try {
    await call(fn);
    await refresh();
    toast('Done.');
  } catch (err) {
    toast(err.message, 'err');
  } finally {
    state.busy = false;
    sink.bar.hidden = true;
    sink.fill.classList.remove('indeterminate');
    sink.fill.style.width = '0%';
    if (sink.text) sink.text.textContent = '';
    sink = null;
    renderHome();
    renderUpdates();
  }
}

window.sdl.onInstallEvent((e) => {
  if (!sink) return;
  const determinate = e.phase === 'downloading';

  if (e.stage === 'status') {
    sink.fill.classList.toggle('indeterminate', !determinate);
    if (!determinate) sink.fill.style.width = '';
    if (sink.text && e.text) sink.text.textContent = e.text;
    $('#updLabel').textContent = determinate ? 'Downloading…' : 'Working…';
  } else if (e.stage === 'progress') {
    if (determinate && typeof e.percent === 'number') {
      sink.fill.classList.remove('indeterminate');
      sink.fill.style.width = `${Math.round(e.percent * 100)}%`;
    }
    if (sink.text && e.text) sink.text.textContent = e.text;
  }
});

const SWATCHES = [
  [28, 28, 30], [180, 86, 58], [79, 138, 91], [58, 106, 168], [214, 168, 62]
];

function buildSwatches() {
  const wrap = $('#setColor');
  wrap.textContent = '';
  for (const [r, g, b] of SWATCHES) {
    const btn = document.createElement('button');
    btn.style.background = `rgb(${r}, ${g}, ${b})`;
    btn.dataset.rgb = `${r},${g},${b}`;
    btn.title = `rgb(${r}, ${g}, ${b})`;
    btn.onclick = () => saveConfig({ color: { r, g, b } });
    wrap.appendChild(btn);
  }
}

async function loadSettings() {
  try {
    const { config, autostart, path } = await call(window.sdl.getConfig);
    state.config = config;

    $('#setAutostart').checked = !!autostart;
    $('#rowUninstall').hidden = !(state.app && state.app.installed);
    $('#footVersion').textContent =
      `ScreenDoodle ${state.app ? state.app.version : '—'}\n${path}`;

    paintSettings(config);
  } catch (err) {
    toast(err.message, 'err');
  }
}

function paintSettings(c) {
  setSeg('#setTool', c.tool);
  setSeg('#setThickness', String(c.thicknessIdx));
  setSeg('#setAnchor', c.anchor);

  const key = `${c.color.r},${c.color.g},${c.color.b}`;
  $$('#setColor button').forEach((b) => b.classList.toggle('is-active', b.dataset.rgb === key));

  $$('.keys[data-hk]').forEach((el) => {
    if (el.classList.contains('is-capturing')) return;
    paintKeys(el, c.hotkeys[el.dataset.hk] || '');
  });
}

function paintKeys(el, combo) {
  el.textContent = '';
  const parts = combo ? combo.split('+') : ['—'];
  for (const p of parts) {
    const k = document.createElement('kbd');
    k.textContent = p;
    el.appendChild(k);
  }
}

function setSeg(sel, value) {
  $$(`${sel} button`).forEach((b) => b.classList.toggle('is-active', b.dataset.val === value));
}

function bindSeg(sel, toPatch) {
  $(sel).addEventListener('click', (e) => {
    const btn = e.target.closest('button');
    if (!btn) return;
    saveConfig(toPatch(btn.dataset.val));
  });
}

bindSeg('#setTool', (v) => ({ tool: v }));
bindSeg('#setThickness', (v) => ({ thicknessIdx: parseInt(v, 10) }));
bindSeg('#setAnchor', (v) => ({ anchor: v }));

async function saveConfig(patch) {
  try {
    state.config = await call(window.sdl.setConfig, patch);
    paintSettings(state.config);
    renderHome();
    toast('Saved — restart the overlay to apply.');
  } catch (err) {
    toast(err.message, 'err');
  }
}

$('#setAutostart').onchange = async (e) => {
  const want = e.target.checked;
  try {
    const now = await call(window.sdl.setAutostart, want);
    e.target.checked = !!now;
    toast(now ? 'ScreenDoodle will start with Windows.' : 'Startup entry removed.');
  } catch (err) {
    e.target.checked = !want;
    toast(err.message, 'err');
  }
};

$('#btnRevealConfig').onclick = () =>
  call(window.sdl.revealConfig).catch((e) => toast(e.message, 'err'));

$('#btnStopOverlay').onclick = async () => {
  try {
    const stopped = await call(window.sdl.stop);
    await refresh();
    toast(stopped ? 'Overlay closed.' : 'Overlay was not running.');
  } catch (err) {
    toast(err.message, 'err');
  }
};

$('#btnResetConfig').onclick = async () => {
  const ok = await confirmBox(
    'Reset settings?',
    'Hotkeys, default tool, colour, stroke weight and toolbar side all go back ' +
    'to their defaults. Your startup setting is not affected.',
    'Reset'
  );
  if (!ok) return;
  try {
    state.config = await call(window.sdl.resetConfig);
    paintSettings(state.config);
    renderHome();
    toast('Settings reset.');
  } catch (err) {
    toast(err.message, 'err');
  }
};

const KEYNAMES = {
  Escape: 'Escape', ' ': 'Space', ArrowUp: 'Up', ArrowDown: 'Down',
  ArrowLeft: 'Left', ArrowRight: 'Right', PageUp: 'PageUp', PageDown: 'PageDown',
  Home: 'Home', End: 'End', Insert: 'Insert', Delete: 'Delete', Tab: 'Tab'
};

$$('.keys[data-hk]').forEach((el) => {
  el.onclick = () => {
    if (state.capturing) stopCapture();
    state.capturing = el.dataset.hk;
    el.classList.add('is-capturing');
    paintKeys(el, 'Press…');
  };
});

function stopCapture() {
  state.capturing = null;
  $$('.keys[data-hk]').forEach((el) => el.classList.remove('is-capturing'));
  if (state.config) paintSettings(state.config);
}

window.addEventListener('keydown', (e) => {
  if (!state.capturing) return;
  e.preventDefault();
  e.stopPropagation();

  if (e.key === 'Escape') return stopCapture();
  if (['Control', 'Alt', 'Shift', 'Meta'].includes(e.key)) return;

  const mods = [];
  if (e.ctrlKey) mods.push('Ctrl');
  if (e.altKey) mods.push('Alt');
  if (e.shiftKey) mods.push('Shift');
  if (e.metaKey) mods.push('Win');

  if (!mods.length) {
    toast('Global hotkeys need at least one modifier.', 'err');
    return;
  }

  let key = null;
  if (/^[a-zA-Z0-9]$/.test(e.key)) key = e.key.toUpperCase();
  else if (/^F([1-9]|1[0-2])$/.test(e.key)) key = e.key;
  else if (KEYNAMES[e.key]) key = KEYNAMES[e.key];

  if (!key) {
    toast(`"${e.key}" can't be used as a hotkey.`, 'err');
    return;
  }

  const which = state.capturing;
  const combo = [...mods, key].join('+');

  const clash = Object.entries(state.config.hotkeys).find(([k, v]) => k !== which && v === combo);
  if (clash) {
    toast(`${combo} is already used by "${clash[0]}".`, 'err');
    return;
  }

  stopCapture();
  saveConfig({ hotkeys: { ...state.config.hotkeys, [which]: combo } });
}, true);

async function refresh() {
  state.app = await call(window.sdl.getState);
  renderMode();
  if (state.app.needsInstall) return;

  try {
    state.update = await call(window.sdl.checkUpdate);
  } catch (_) {
    state.update = null;
  }
  renderHome();
}

async function doUninstall() {
  const ok = await confirmBox(
    'Uninstall ScreenDoodle?',
    'The app, its shortcuts and its startup entry are removed. ' +
    'Your settings file is kept, so reinstalling restores your hotkeys and defaults.',
    'Uninstall'
  );
  if (!ok) return;
  try {
    await call(window.sdl.uninstall);
    toast('Uninstalling…');
  } catch (err) {
    toast(err.message, 'err');
  }
}

$('#btnUninstall').onclick = doUninstall;

async function boot() {
  buildSwatches();
  window.sdl.onOpenSettings(() => openSheet('sheetSettings'));
  window.sdl.onConfirmUninstall(() => doUninstall());

  try {
    state.config = (await call(window.sdl.getConfig)).config;
  } catch (_) {}

  try {
    await refresh();
  } catch (err) {
    toast(err.message, 'err');
  }

  setInterval(async () => {
    if (state.busy || document.hidden || !state.app) return;
    try {
      const s = await call(window.sdl.getState);
      if (s.running !== state.app.running) {
        state.app = s;
        renderHome();
      }
    } catch (_) {}
  }, 4000);

  document.addEventListener('visibilitychange', () => {
    if (!document.hidden && !state.busy) refresh().catch(() => {});
  });
}

boot();
