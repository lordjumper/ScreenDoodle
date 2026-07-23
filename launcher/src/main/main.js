'use strict';

const { app, BrowserWindow, ipcMain, shell, Menu, dialog } = require('electron');
const path = require('path');
const fs = require('fs');
const { execFile, spawn } = require('child_process');

const gh = require('./github');
const overlay = require('./overlay');
const appconfig = require('./appconfig');
const installer = require('./install');
const P = require('./paths');

const ICON = path.join(__dirname, '..', '..', 'assets', 'ScreenDoodle.ico');

let win = null;

app.commandLine.appendSwitch('disable-features',
  'HardwareMediaKeyHandling,MediaSessionService,WebRtcHideLocalIpsWithMdns');
app.disableHardwareAcceleration();

const LC = process.env.SDL_TRACE ? (...a) => console.log('[lifecycle]', ...a) : () => {};

if (P.isPortable()) {
  LC('portable: skipping single-instance lock');
} else if (!app.requestSingleInstanceLock()) {
  app.quit();
} else {
  app.on('second-instance', (_e, argv) => {
    LC('second-instance argv =', JSON.stringify(argv.slice(1)));
    showWindow();
    if (argv.includes('--settings')) sendToUi('ui:open-settings');
  });
}

const wantsSettings = () => process.argv.includes('--settings');
const wantsUninstall = () => process.argv.includes('--uninstall');

function sendToUi(channel) {
  if (win && !win.isDestroyed()) win.webContents.send(channel);
}

function createWindow() {
  win = new BrowserWindow({
    width: 470,
    height: 445,
    resizable: false,
    maximizable: false,
    frame: false,
    show: false,
    backgroundColor: '#fcf5ef',
    titleBarStyle: 'hidden',
    icon: ICON,
    webPreferences: {
      preload: path.join(__dirname, '..', 'preload', 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
      spellcheck: false,
      backgroundThrottling: true,
      devTools: !app.isPackaged
    }
  });

  win.loadFile(path.join(__dirname, '..', 'renderer', 'index.html'));
  win.once('ready-to-show', () => {
    win.show();

    if (wantsUninstall()) sendToUi('ui:confirm-uninstall');
    else if (wantsSettings()) sendToUi('ui:open-settings');
  });
  win.on('closed', () => (win = null));

  win.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https:\/\//.test(url)) shell.openExternal(url);
    return { action: 'deny' };
  });
  win.webContents.on('will-navigate', (e) => e.preventDefault());
}

function showWindow() {
  if (!win) return createWindow();
  if (win.isMinimized()) win.restore();
  win.show();
  win.focus();
}

function publishLauncherPath() {
  const KEY = 'HKCU\\Software\\ScreenDoodle';
  const reg = (args) => execFile('reg', args, { windowsHide: true }, () => {});

  reg(['add', KEY, '/v', 'LauncherPath', '/t', 'REG_SZ', '/d', process.execPath, '/f']);
  if (app.isPackaged) {
    reg(['delete', KEY, '/v', 'LauncherArgs', '/f']);
  } else {
    reg(['add', KEY, '/v', 'LauncherArgs', '/t', 'REG_SZ',
         '/d', `"${app.getAppPath()}"`, '/f']);
  }
}

app.whenReady().then(() => {
  Menu.setApplicationMenu(null);
  fs.mkdirSync(P.launcherDir, { recursive: true });
  publishLauncherPath();
  createWindow();
});

app.on('window-all-closed', () => app.quit());

function handle(channel, fn) {
  ipcMain.handle(channel, async (_evt, ...args) => {
    try {
      return { ok: true, data: await fn(...args) };
    } catch (err) {
      return { ok: false, error: err && err.message ? err.message : String(err) };
    }
  });
}

const emit = (stage, payload) => sendPayload('install:event', { stage, ...payload });
function sendPayload(channel, payload) {
  if (win && !win.isDestroyed()) win.webContents.send(channel, payload);
}

ipcMain.on('win:minimize', () => win && win.minimize());
ipcMain.on('win:close', () => win && win.close());

handle('app:state', async () => {
  const inst = await installer.detect();
  const legacy = P.isPortable() ? await installer.detectLegacy() : { found: false };
  return {
    legacy,
    version: overlay.currentVersion(),
    overlayAvailable: overlay.isAvailable(),
    overlayPath: overlay.exePath(),
    running: await overlay.isRunning(),
    autostart: await appconfig.getAutostart(),
    configPath: appconfig.configPath,
    releasesPage: gh.releasesPage,
    packaged: app.isPackaged,

    portable: P.isPortable(),
    needsInstall: P.isPortable(),
    installed: inst.installed,
    installDir: inst.installDir,
    defaultInstallDir: P.defaultInstallDir
  };
});

handle('app:install', async (opts) => {
  const res = await installer.install(opts || {}, emit);
  if (opts && opts.autostart) {
    try {
      await appconfig.setAutostart(
        true, path.join(res.installDir, 'resources', 'overlay', P.overlayExeName));
    } catch (_) {}
  }
  return res;
});

handle('app:finishInstall', async (exePath) => {
  const env = { ...process.env };
  delete env.PORTABLE_EXECUTABLE_FILE;
  delete env.PORTABLE_EXECUTABLE_DIR;
  delete env.PORTABLE_EXECUTABLE_APP_FILENAME;

  const child = spawn(exePath, [], {
    detached: true,
    stdio: 'ignore',
    cwd: path.dirname(exePath),
    env
  });
  child.unref();
  setTimeout(() => app.quit(), 400);
  return true;
});

handle('app:uninstall', async () => {
  const res = await installer.uninstall();
  setTimeout(() => app.quit(), 400);
  return res;
});

handle('app:chooseDir', async () => {
  const res = await dialog.showOpenDialog(win, {
    title: 'Choose where to install ScreenDoodle',
    properties: ['openDirectory', 'createDirectory'],
    defaultPath: P.defaultInstallDir
  });
  if (res.canceled || !res.filePaths.length) return null;
  return path.join(res.filePaths[0], 'ScreenDoodle');
});

handle('app:checkUpdate', () => overlay.checkUpdate());
handle('app:update', () => overlay.update(emit));
handle('app:launch', () => overlay.launch());
handle('app:stop', () => overlay.stop());

handle('config:get', async () => ({
  config: appconfig.read(),
  autostart: await appconfig.getAutostart(),
  path: appconfig.configPath
}));

handle('config:set', async (patch) => appconfig.write(patch));
handle('config:reset', async () => appconfig.reset());
handle('config:autostart', async (enabled) =>
  appconfig.setAutostart(enabled, overlay.exePath()));

handle('shell:openExternal', async (url) => {
  if (!/^https:\/\//.test(url)) throw new Error('Refusing to open a non-https URL');
  await shell.openExternal(url);
  return true;
});

handle('shell:revealConfig', async () => {
  fs.mkdirSync(appconfig.configDir, { recursive: true });
  if (!fs.existsSync(appconfig.configPath)) appconfig.write({});
  shell.showItemInFolder(appconfig.configPath);
  return true;
});
