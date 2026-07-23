'use strict';

const path = require('path');
const os = require('os');
const fs = require('fs');
const { app } = require('electron');

const roaming = process.env.APPDATA || path.join(os.homedir(), 'AppData', 'Roaming');

const launcherDir = path.join(roaming, 'ScreenDoodleLauncher');

const appConfigDir = path.join(roaming, 'ScreenDoodle');

const OVERLAY_EXE = 'ScreenDoodleOverlay.exe';

function overlayPath() {
  const packaged = path.join(process.resourcesPath || '', 'overlay', OVERLAY_EXE);
  if (fs.existsSync(packaged)) return packaged;

  const dev = path.join(__dirname, '..', '..', '..', 'build', 'Release', OVERLAY_EXE);
  return path.resolve(dev);
}

const isPortable = () => !!process.env.PORTABLE_EXECUTABLE_FILE;

const appRootDir = () => path.dirname(process.execPath);

const defaultInstallDir = path.join(
  process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'),
  'Programs',
  'ScreenDoodle'
);

module.exports = {
  isPortable,
  appRootDir,
  defaultInstallDir,
  launcherDir,
  launcherState: path.join(launcherDir, 'state.json'),
  newsCache: path.join(launcherDir, 'news.json'),
  downloadDir: path.join(launcherDir, 'downloads'),
  appConfigDir,
  appConfig: path.join(appConfigDir, 'config.json'),
  overlayPath,
  overlayExeName: OVERLAY_EXE,
  isPackaged: () => app.isPackaged
};
