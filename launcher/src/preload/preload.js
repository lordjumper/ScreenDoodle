'use strict';

const { contextBridge, ipcRenderer } = require('electron');

const invoke = (channel, ...args) => ipcRenderer.invoke(channel, ...args);

contextBridge.exposeInMainWorld('sdl', {
  window: {
    minimize: () => ipcRenderer.send('win:minimize'),
    close: () => ipcRenderer.send('win:close')
  },

  getState: () => invoke('app:state'),
  checkUpdate: () => invoke('app:checkUpdate'),
  update: () => invoke('app:update'),
  launch: () => invoke('app:launch'),
  stop: () => invoke('app:stop'),

  install: (opts) => invoke('app:install', opts),
  finishInstall: (exePath) => invoke('app:finishInstall', exePath),
  uninstall: () => invoke('app:uninstall'),
  chooseDir: () => invoke('app:chooseDir'),

  getConfig: () => invoke('config:get'),
  setConfig: (patch) => invoke('config:set', patch),
  resetConfig: () => invoke('config:reset'),
  setAutostart: (enabled) => invoke('config:autostart', enabled),

  openExternal: (url) => invoke('shell:openExternal', url),
  revealConfig: () => invoke('shell:revealConfig'),

  onOpenSettings: (cb) => {
    ipcRenderer.on('ui:open-settings', () => cb());
  },

  onConfirmUninstall: (cb) => {
    ipcRenderer.on('ui:confirm-uninstall', () => cb());
  },

  onInstallEvent: (cb) => {
    const listener = (_e, payload) => cb(payload);
    ipcRenderer.on('install:event', listener);
    return () => ipcRenderer.removeListener('install:event', listener);
  }
});
