'use strict';

const fs = require('fs');
const path = require('path');
const { execFile, spawn } = require('child_process');
const { app } = require('electron');

const gh = require('./github');
const P = require('./paths');

function run(cmd, args) {
  return new Promise((resolve, reject) => {
    execFile(cmd, args, { windowsHide: true }, (err, stdout, stderr) => {
      if (err) return reject(new Error(stderr || err.message));
      resolve(stdout);
    });
  });
}

function humanBytes(n) {
  if (!n) return '0 B';
  const u = ['B', 'KB', 'MB', 'GB'];
  const i = Math.min(Math.floor(Math.log(n) / Math.log(1024)), u.length - 1);
  return `${(n / Math.pow(1024, i)).toFixed(i ? 1 : 0)} ${u[i]}`;
}

function exePath() {
  return P.overlayPath();
}

function isAvailable() {
  return fs.existsSync(exePath());
}

async function isRunning() {
  try {
    const out = await run('tasklist', ['/FI', `IMAGENAME eq ${P.overlayExeName}`, '/NH']);
    return out.toLowerCase().includes(P.overlayExeName.toLowerCase());
  } catch (_) {
    return false;
  }
}

async function stop() {
  if (!(await isRunning())) return false;
  try {
    await run('taskkill', ['/IM', P.overlayExeName, '/F']);
  } catch (_) {}
  await new Promise((r) => setTimeout(r, 500));
  return true;
}

async function launch() {
  const exe = exePath();
  if (!fs.existsSync(exe)) {
    throw new Error(
      app.isPackaged
        ? 'The overlay is missing from this install. Reinstall ScreenDoodle.'
        : 'Overlay not built yet. Run: cmake --build build --config Release'
    );
  }
  if (await isRunning()) return { alreadyRunning: true, exePath: exe };

  const child = spawn(exe, [], {
    detached: true,
    stdio: 'ignore',
    cwd: path.dirname(exe),
    windowsHide: false
  });
  child.unref();
  return { alreadyRunning: false, exePath: exe };
}

function currentVersion() {
  return app.getVersion();
}

async function checkUpdate() {
  const release = await gh.latestRelease();
  const asset = gh.pickAsset(release);
  const installed = currentVersion();
  return {
    installedVersion: installed,
    latest: release,
    assetName: asset ? asset.name : null,
    assetSize: asset ? asset.size : 0,
    updateAvailable: !!asset && gh.compareVersions(installed, release.version) < 0
  };
}

async function update(emit) {
  emit('status', { phase: 'resolving', text: 'Checking for the latest release…' });
  const release = await gh.latestRelease();
  const asset = gh.pickAsset(release);
  if (!asset) throw new Error(`Release ${release.tag} has no Windows installer attached.`);

  if (gh.compareVersions(currentVersion(), release.version) >= 0) {
    emit('status', { phase: 'done', text: 'Already up to date.' });
    return { updated: false, version: currentVersion() };
  }

  emit('status', {
    phase: 'downloading',
    text: `Downloading ${release.tag} · ${humanBytes(asset.size)}`
  });

  const dest = path.join(P.downloadDir, `${release.tag}-${asset.name}`);
  let lastTick = 0;
  const dl = await gh.download(asset.url, dest, (received, total) => {
    const now = Date.now();
    if (now - lastTick < 80 && received !== total) return;
    lastTick = now;
    emit('progress', {
      phase: 'downloading',
      percent: total ? received / total : 0,
      text: `${humanBytes(received)} / ${humanBytes(total || asset.size)}`
    });
  });

  emit('status', { phase: 'installing', text: 'Starting installer…' });
  await stop();

  const child = spawn(dl.path, [], { detached: true, stdio: 'ignore' });
  child.unref();

  return { updated: true, version: release.version, installer: dl.path };
}

module.exports = {
  exePath,
  isAvailable,
  isRunning,
  stop,
  launch,
  currentVersion,
  checkUpdate,
  update,
  humanBytes
};
