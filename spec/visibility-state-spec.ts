import { BaseWindow, BrowserWindow, BrowserWindowConstructorOptions, ipcMain, WebContents, WebContentsView } from 'electron/main';

import { expect } from 'chai';

import * as cp from 'node:child_process';
import { once } from 'node:events';
import * as path from 'node:path';
import { setTimeout } from 'node:timers/promises';

import { ifdescribe } from './lib/spec-helpers';
import { closeWindow } from './lib/window-helpers';

// visibilityState specs pass on linux with a real window manager but on CI
// the environment does not let these specs pass
ifdescribe(process.platform !== 'linux')('document.visibilityState', () => {
  let w: BaseWindow & {webContents: WebContents};

  afterEach(() => {
    return closeWindow(w);
  });

  const load = () => w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'chromium', 'visibilitystate.html'));

  const itWithOptions = (name: string, options: BrowserWindowConstructorOptions, fn: Mocha.Func) => {
    it(name, async function (...args) {
      w = new BrowserWindow({
        ...options,
        paintWhenInitiallyHidden: false,
        webPreferences: {
          ...(options.webPreferences || {}),
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      await Promise.resolve(fn.apply(this, args));
    });

    it(name + ' with BaseWindow', async function (...args) {
      const baseWindow = new BaseWindow({
        ...options
      });
      const wcv = new WebContentsView({ webPreferences: { ...(options.webPreferences ?? {}), nodeIntegration: true, contextIsolation: false } });
      baseWindow.contentView = wcv;
      w = Object.assign(baseWindow, { webContents: wcv.webContents });
      await Promise.resolve(fn.apply(this, args));
    });
  };

  itWithOptions('should be visible when the window is initially shown by default', {}, async () => {
    load();
    const [, state] = await once(ipcMain, 'initial-visibility-state');
    expect(state).to.equal('visible');
  });

  itWithOptions('should be visible when the window is initially shown', {
    show: true
  }, async () => {
    load();
    const [, state] = await once(ipcMain, 'initial-visibility-state');
    expect(state).to.equal('visible');
  });

  itWithOptions('should be hidden when the window is initially hidden', {
    show: false
  }, async () => {
    load();
    const [, state] = await once(ipcMain, 'initial-visibility-state');
    expect(state).to.equal('hidden');
  });

  itWithOptions('should be visible when the window is initially hidden but shown before the page is loaded', {
    show: false
  }, async () => {
    w.show();
    load();
    const [, state] = await once(ipcMain, 'initial-visibility-state');
    expect(state).to.equal('visible');
  });

  itWithOptions('should be hidden when the window is initially shown but hidden before the page is loaded', {
    show: true
  }, async () => {
    // TODO(MarshallOfSound): Figure out if we can work around this 1 tick issue for users
    if (process.platform === 'darwin' || process.platform === 'win32') {
      // Wait for a tick, the window being "shown" takes 1 tick on macOS
      await setTimeout(10000);
    }
    w.hide();
    load();
    const [, state] = await once(ipcMain, 'initial-visibility-state');
    expect(state).to.equal('hidden');
  });

  itWithOptions('should be toggle between visible and hidden as the window is hidden and shown', {}, async () => {
    load();
    const [, initialState] = await once(ipcMain, 'initial-visibility-state');
    expect(initialState).to.equal('visible');
    w.hide();
    await once(ipcMain, 'visibility-change-hidden');
    w.show();
    await once(ipcMain, 'visibility-change-visible');
  });

  itWithOptions('should become hidden when a window is minimized', {}, async () => {
    load();
    const [, initialState] = await once(ipcMain, 'initial-visibility-state');
    expect(initialState).to.equal('visible');
    w.minimize();
    const p = once(ipcMain, 'visibility-change-hidden');
    w.minimize();
    await p;
  });

  itWithOptions('should become visible when a window is restored', {}, async () => {
    load();
    const [, initialState] = await once(ipcMain, 'initial-visibility-state');
    expect(initialState).to.equal('visible');
    w.minimize();
    await once(ipcMain, 'visibility-change-hidden');
    w.restore();
    await once(ipcMain, 'visibility-change-visible');
  });

  ifdescribe(process.platform === 'darwin')('on platforms that support occlusion detection', () => {
    let child: cp.ChildProcess;

    const makeOtherWindow = (opts: { x: number; y: number; width: number; height: number; }) => {
      child = cp.spawn(process.execPath, [path.resolve(__dirname, 'fixtures', 'chromium', 'other-window.js'), `${opts.x}`, `${opts.y}`, `${opts.width}`, `${opts.height}`]);
      return new Promise<void>(resolve => {
        child.stdout!.on('data', (chunk) => {
          if (chunk.toString().includes('__ready__')) resolve();
        });
      });
    };

    afterEach(() => {
      if (child && !child.killed) {
        child.kill('SIGTERM');
      }
    });

    itWithOptions('should be visible when two windows are on screen', {
      x: 0,
      y: 0,
      width: 200,
      height: 200
    }, async () => {
      await makeOtherWindow({
        x: 200,
        y: 0,
        width: 200,
        height: 200
      });
      load();
      const [, state] = await once(ipcMain, 'initial-visibility-state');
      expect(state).to.equal('visible');
    });

    itWithOptions('should be visible when two windows are on screen that overlap partially', {
      x: 50,
      y: 50,
      width: 150,
      height: 150
    }, async () => {
      await makeOtherWindow({
        x: 100,
        y: 0,
        width: 200,
        height: 200
      });
      load();
      const [, state] = await once(ipcMain, 'initial-visibility-state');
      expect(state).to.equal('visible');
    });

    itWithOptions('should be hidden when a second window completely occludes the current window', {
      x: 50,
      y: 50,
      width: 50,
      height: 50
    }, async function () {
      this.timeout(240000);
      load();
      const [, state] = await once(ipcMain, 'initial-visibility-state');
      expect(state).to.equal('visible');
      makeOtherWindow({
        x: 0,
        y: 0,
        width: 300,
        height: 300
      });
      await once(ipcMain, 'visibility-change-hidden');
    });
  });
});
