'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');
const { execFile, spawn } = require('child_process');
const { app, shell } = require('electron');

const P = require('./paths');

const UNINSTALL_KEY =
  'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ScreenDoodle';
const LAUNCHER_EXE = 'ScreenDoodle.exe';

function reg(args) {
  return new Promise((resolve) => {
    execFile('reg', args, { windowsHide: true }, (err, stdout) => resolve(err ? null : stdout));
  });
}

async function detect() {
  const out = await reg(['query', UNINSTALL_KEY, '/v', 'InstallLocation']);
  const m = out && /InstallLocation\s+REG_SZ\s+(.+)/i.exec(out);
  const dir = m ? m[1].trim() : null;
  if (!dir) return { installed: false, installDir: null };

  const exe = path.join(dir, LAUNCHER_EXE);
  if (!fs.existsSync(exe)) return { installed: false, installDir: null };

  const ver = await reg(['query', UNINSTALL_KEY, '/v', 'DisplayVersion']);
  const vm = ver && /DisplayVersion\s+REG_SZ\s+(.+)/i.exec(ver);
  return {
    installed: true,
    installDir: dir,
    exePath: exe,
    version: vm ? vm[1].trim() : null
  };
}

function runningInstalled() {
  return app.isPackaged && !P.isPortable();
}

function withoutAsar(fn) {
  const prev = process.noAsar;
  process.noAsar = true;
  try {
    return fn();
  } finally {
    process.noAsar = prev;
  }
}

function walk(dir, out = []) {
  return withoutAsar(() => {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, e.name);
      if (e.isDirectory()) walk(full, out);
      else out.push(full);
    }
    return out;
  });
}

function copyTree(src, dest, onProgress) {
  const files = walk(src);
  const total = files.length;
  let done = 0;

  withoutAsar(() => {
    for (const file of files) {
      const rel = path.relative(src, file);
      const target = path.join(dest, rel);
      fs.mkdirSync(path.dirname(target), { recursive: true });
      fs.copyFileSync(file, target);
      done++;
      if (onProgress && (done % 12 === 0 || done === total)) onProgress(done, total);
    }
  });
  return total;
}

function startMenuDir() {
  return path.join(
    process.env.APPDATA || path.join(os.homedir(), 'AppData', 'Roaming'),
    'Microsoft', 'Windows', 'Start Menu', 'Programs'
  );
}

function desktopDir() {
  return path.join(os.homedir(), 'Desktop');
}

function makeShortcut(linkPath, target, icon) {
  try {
    fs.mkdirSync(path.dirname(linkPath), { recursive: true });
    return shell.writeShortcutLink(linkPath, 'create', {
      target,
      icon,
      iconIndex: 0,
      cwd: path.dirname(target),
      description: 'ScreenDoodle — on-screen drawing'
    });
  } catch (_) {
    return false;
  }
}

function killLauncherIn(dir) {
  const target = dir.replace(/'/g, "''");
  const ps =
    "Get-Process -Name 'ScreenDoodle' -ErrorAction SilentlyContinue | " +
    `Where-Object { $_.Path -and $_.Path.StartsWith('${target}') } | ` +
    'Stop-Process -Force -ErrorAction SilentlyContinue';
  return new Promise((resolve) =>
    execFile('powershell',
      ['-NoProfile', '-NonInteractive', '-Command', ps],
      { windowsHide: true }, () => resolve()));
}

async function install(opts, emit) {
  const dir = (opts && opts.installDir) || P.defaultInstallDir;
  const src = P.appRootDir();

  if (!P.isPortable()) {
    throw new Error('Already running an installed copy.');
  }

  const rel = path.relative(src, dir);
  if (rel === '' || (!rel.startsWith('..') && !path.isAbsolute(rel))) {
    throw new Error('Choose a folder outside the running application.');
  }

  emit('status', { phase: 'installing', text: 'Preparing…' });

  await new Promise((r) =>
    execFile('taskkill', ['/IM', P.overlayExeName, '/F'], { windowsHide: true }, () => r()));
  await killLauncherIn(dir);
  await new Promise((r) => setTimeout(r, 700));

  fs.mkdirSync(dir, { recursive: true });

  try {
    const probe = path.join(dir, '.write-test');
    fs.writeFileSync(probe, '');
    fs.unlinkSync(probe);
  } catch (_) {
    throw new Error(`Cannot write to ${dir}. Pick another folder.`);
  }

  emit('status', { phase: 'copying', text: 'Copying files…' });
  const count = copyTree(src, dir, (done, total) => {
    emit('progress', {
      phase: 'copying',
      percent: done / total,
      text: `${done} / ${total} files`
    });
  });

  const exe = path.join(dir, LAUNCHER_EXE);
  if (!fs.existsSync(exe)) {
    throw new Error('Copy finished but ScreenDoodle.exe is missing from the target.');
  }

  emit('status', { phase: 'shortcuts', text: 'Creating shortcuts…' });
  makeShortcut(path.join(startMenuDir(), 'ScreenDoodle.lnk'), exe, exe);
  if (opts && opts.desktopShortcut) {
    makeShortcut(path.join(desktopDir(), 'ScreenDoodle.lnk'), exe, exe);
  }

  emit('status', { phase: 'registering', text: 'Registering…' });
  const sizeKb = withoutAsar(() =>
    Math.round(walk(dir).reduce((n, f) => n + fs.statSync(f).size, 0) / 1024));
  const set = (name, type, value) =>
    reg(['add', UNINSTALL_KEY, '/v', name, '/t', type, '/d', String(value), '/f']);

  await set('DisplayName', 'REG_SZ', 'ScreenDoodle');
  await set('DisplayVersion', 'REG_SZ', app.getVersion());
  await set('Publisher', 'REG_SZ', 'lordjumper');
  await set('InstallLocation', 'REG_SZ', dir);
  await set('DisplayIcon', 'REG_SZ', exe);
  await set('UninstallString', 'REG_SZ', `"${exe}" --uninstall`);
  await set('EstimatedSize', 'REG_DWORD', sizeKb);
  await set('NoModify', 'REG_DWORD', 1);
  await set('NoRepair', 'REG_DWORD', 1);
  await set('URLInfoAbout', 'REG_SZ', 'https://github.com/lordjumper/ScreenDoodle');

  emit('status', { phase: 'done', text: 'Installed' });
  return { installed: true, installDir: dir, exePath: exe, files: count };
}

async function uninstall(installDir) {
  const dir = installDir || (await detect()).installDir;
  if (!dir) throw new Error('No installed copy found.');

  for (const link of [
    path.join(startMenuDir(), 'ScreenDoodle.lnk'),
    path.join(desktopDir(), 'ScreenDoodle.lnk')
  ]) {
    try { fs.rmSync(link, { force: true }); } catch (_) {}
  }

  await reg(['delete', UNINSTALL_KEY, '/f']);
  await reg(['delete', 'HKCU\\Software\\ScreenDoodle', '/f']);
  await reg(['delete',
    'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run', '/v', 'ScreenDoodle', '/f']);

  try {
    await new Promise((r) =>
      execFile('taskkill', ['/IM', P.overlayExeName, '/F'], { windowsHide: true }, () => r()));
  } catch (_) {}

  const helper = path.join(os.tmpdir(), `sd-cleanup-${Date.now()}.exe`);
  fs.copyFileSync(path.join(dir, 'resources', 'overlay', P.overlayExeName), helper);

  const child = spawn(helper, ['--cleanup', dir], {
    detached: true,
    stdio: 'ignore',
    windowsHide: true,
    cwd: os.tmpdir()
  });
  child.unref();

  return { removed: true, installDir: dir };
}

module.exports = {
  detect,
  runningInstalled,
  install,
  uninstall,
  UNINSTALL_KEY
};
