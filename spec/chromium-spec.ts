import { expect } from 'chai';
import { BrowserWindow, WebContents, webFrameMain, session, ipcMain, app, protocol, webContents, dialog, MessageBoxOptions } from 'electron/main';
import { clipboard } from 'electron/common';
import { closeAllWindows } from './lib/window-helpers';
import * as https from 'node:https';
import * as http from 'node:http';
import * as path from 'node:path';
import * as fs from 'node:fs';
import * as url from 'node:url';
import * as ChildProcess from 'node:child_process';
import { EventEmitter, once } from 'node:events';
import { ifit, ifdescribe, defer, itremote, listen } from './lib/spec-helpers';
import { PipeTransport } from './pipe-transport';
import * as ws from 'ws';
import { setTimeout } from 'node:timers/promises';
import { AddressInfo } from 'node:net';
import { MediaAccessPermissionRequest } from 'electron';

const features = process._linkedBinding('electron_common_features');

const fixturesPath = path.resolve(__dirname, 'fixtures');
const certPath = path.join(fixturesPath, 'certificates');

describe('reporting api', () => {
  it('sends a report for an intervention', async () => {
    const reporting = new EventEmitter();

    // The Reporting API only works on https with valid certs. To dodge having
    // to set up a trusted certificate, hack the validator.
    session.defaultSession.setCertificateVerifyProc((req, cb) => {
      cb(0);
    });

    const options = {
      key: fs.readFileSync(path.join(certPath, 'server.key')),
      cert: fs.readFileSync(path.join(certPath, 'server.pem')),
      ca: [
        fs.readFileSync(path.join(certPath, 'rootCA.pem')),
        fs.readFileSync(path.join(certPath, 'intermediateCA.pem'))
      ],
      requestCert: true,
      rejectUnauthorized: false
    };

    const server = https.createServer(options, (req, res) => {
      if (req.url?.endsWith('report')) {
        let data = '';
        req.on('data', (d) => { data += d.toString('utf-8'); });
        req.on('end', () => {
          reporting.emit('report', JSON.parse(data));
        });
      }

      const { port } = server.address() as any;
      res.setHeader('Reporting-Endpoints', `default="https://localhost:${port}/report"`);
      res.setHeader('Content-Type', 'text/html');

      res.end('<script>window.navigator.vibrate(1)</script>');
    });

    await listen(server);
    const bw = new BrowserWindow({ show: false });

    try {
      const reportGenerated = once(reporting, 'report');
      await bw.loadURL(`https://localhost:${(server.address() as AddressInfo).port}/a`);

      const [reports] = await reportGenerated;
      expect(reports).to.be.an('array').with.lengthOf(1);
      const { type, url, body } = reports[0];
      expect(type).to.equal('intervention');
      expect(url).to.equal(url);
      expect(body.id).to.equal('NavigatorVibrate');
      expect(body.message).to.match(/Blocked call to navigator.vibrate because user hasn't tapped on the frame or any embedded frame yet/);
    } finally {
      bw.destroy();
      server.close();
    }
  });
});

describe('window.postMessage', () => {
  afterEach(async () => {
    await closeAllWindows();
  });

  it('sets the source and origin correctly', async () => {
    const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
    w.loadURL(`file://${fixturesPath}/pages/window-open-postMessage-driver.html`);
    const [, message] = await once(ipcMain, 'complete');
    expect(message.data).to.equal('testing');
    expect(message.origin).to.equal('file://');
    expect(message.sourceEqualsOpener).to.equal(true);
    expect(message.eventOrigin).to.equal('file://');
  });
});

describe('focus handling', () => {
  let webviewContents: WebContents;
  let w: BrowserWindow;

  beforeEach(async () => {
    w = new BrowserWindow({
      show: true,
      webPreferences: {
        nodeIntegration: true,
        webviewTag: true,
        contextIsolation: false
      }
    });

    const webviewReady = once(w.webContents, 'did-attach-webview') as Promise<[any, WebContents]>;
    await w.loadFile(path.join(fixturesPath, 'pages', 'tab-focus-loop-elements.html'));
    const [, wvContents] = await webviewReady;
    webviewContents = wvContents;
    await once(webviewContents, 'did-finish-load');
    w.focus();
  });

  afterEach(() => {
    webviewContents = null as unknown as WebContents;
    w.destroy();
    w = null as unknown as BrowserWindow;
  });

  const expectFocusChange = async () => {
    const [, focusedElementId] = await once(ipcMain, 'focus-changed');
    return focusedElementId;
  };

  describe('a TAB press', () => {
    const tabPressEvent: any = {
      type: 'keyDown',
      keyCode: 'Tab'
    };

    it('moves focus to the next focusable item', async () => {
      let focusChange = expectFocusChange();
      w.webContents.sendInputEvent(tabPressEvent);
      let focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-element-1', `should start focused in element-1, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      w.webContents.sendInputEvent(tabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-element-2', `focus should've moved to element-2, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      w.webContents.sendInputEvent(tabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-wv-element-1', `focus should've moved to the webview's element-1, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      webviewContents.sendInputEvent(tabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-wv-element-2', `focus should've moved to the webview's element-2, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      webviewContents.sendInputEvent(tabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-element-3', `focus should've moved to element-3, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      w.webContents.sendInputEvent(tabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-element-1', `focus should've looped back to element-1, it's instead in ${focusedElementId}`);
    });
  });

  describe('a SHIFT + TAB press', () => {
    const shiftTabPressEvent: any = {
      type: 'keyDown',
      modifiers: ['Shift'],
      keyCode: 'Tab'
    };

    it('moves focus to the previous focusable item', async () => {
      let focusChange = expectFocusChange();
      w.webContents.sendInputEvent(shiftTabPressEvent);
      let focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-element-3', `should start focused in element-3, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      w.webContents.sendInputEvent(shiftTabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-wv-element-2', `focus should've moved to the webview's element-2, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      webviewContents.sendInputEvent(shiftTabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-wv-element-1', `focus should've moved to the webview's element-1, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      webviewContents.sendInputEvent(shiftTabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-element-2', `focus should've moved to element-2, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      w.webContents.sendInputEvent(shiftTabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-element-1', `focus should've moved to element-1, it's instead in ${focusedElementId}`);

      focusChange = expectFocusChange();
      w.webContents.sendInputEvent(shiftTabPressEvent);
      focusedElementId = await focusChange;
      expect(focusedElementId).to.equal('BUTTON-element-3', `focus should've looped back to element-3, it's instead in ${focusedElementId}`);
    });
  });
});

describe('web security', () => {
  afterEach(closeAllWindows);
  let server: http.Server;
  let serverUrl: string;
  before(async () => {
    server = http.createServer((req, res) => {
      res.setHeader('Content-Type', 'text/html');
      res.end('<body>');
    });
    serverUrl = (await listen(server)).url;
  });
  after(() => {
    server.close();
  });

  it('engages CORB when web security is not disabled', async () => {
    const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: true, nodeIntegration: true, contextIsolation: false } });
    const p = once(ipcMain, 'success');
    await w.loadURL(`data:text/html,<script>
        const s = document.createElement('script')
        s.src = "${serverUrl}"
        // The script will not load under ORB, refs https://chromium-review.googlesource.com/c/chromium/src/+/3785025.
        // Before ORB an empty response is sent, which is now replaced by a network error.
        s.onerror = () => { require('electron').ipcRenderer.send('success') }
        document.documentElement.appendChild(s)
      </script>`);
    await p;
  });

  it('bypasses CORB when web security is disabled', async () => {
    const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: false, nodeIntegration: true, contextIsolation: false } });
    const p = once(ipcMain, 'success');
    await w.loadURL(`data:text/html,
      <script>
        window.onerror = (e) => { require('electron').ipcRenderer.send('success', e) }
      </script>
      <script src="${serverUrl}"></script>`);
    await p;
  });

  it('engages CORS when web security is not disabled', async () => {
    const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: true, nodeIntegration: true, contextIsolation: false } });
    const p = once(ipcMain, 'response');
    await w.loadURL(`data:text/html,<script>
        (async function() {
          try {
            await fetch('${serverUrl}');
            require('electron').ipcRenderer.send('response', 'passed');
          } catch {
            require('electron').ipcRenderer.send('response', 'failed');
          }
        })();
      </script>`);
    const [, response] = await p;
    expect(response).to.equal('failed');
  });

  it('bypasses CORS when web security is disabled', async () => {
    const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: false, nodeIntegration: true, contextIsolation: false } });
    const p = once(ipcMain, 'response');
    await w.loadURL(`data:text/html,<script>
        (async function() {
          try {
            await fetch('${serverUrl}');
            require('electron').ipcRenderer.send('response', 'passed');
          } catch {
            require('electron').ipcRenderer.send('response', 'failed');
          }
        })();
      </script>`);
    const [, response] = await p;
    expect(response).to.equal('passed');
  });

  describe('accessing file://', () => {
    async function loadFile (w: BrowserWindow) {
      const thisFile = url.format({
        pathname: __filename.replaceAll('\\', '/'),
        protocol: 'file',
        slashes: true
      });
      await w.loadURL(`data:text/html,<script>
          function loadFile() {
            return new Promise((resolve) => {
              fetch('${thisFile}').then(
                () => resolve('loaded'),
                () => resolve('failed')
              )
            });
          }
        </script>`);
      return await w.webContents.executeJavaScript('loadFile()');
    }

    it('is forbidden when web security is enabled', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: true } });
      const result = await loadFile(w);
      expect(result).to.equal('failed');
    });

    it('is allowed when web security is disabled', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: false } });
      const result = await loadFile(w);
      expect(result).to.equal('loaded');
    });
  });

  describe('wasm-eval csp', () => {
    async function loadWasm (csp: string) {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          sandbox: true,
          enableBlinkFeatures: 'WebAssemblyCSP'
        }
      });
      await w.loadURL(`data:text/html,<head>
          <meta http-equiv="Content-Security-Policy" content="default-src 'self'; script-src 'self' 'unsafe-inline' ${csp}">
        </head>
        <script>
          function loadWasm() {
            const wasmBin = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0])
            return new Promise((resolve) => {
              WebAssembly.instantiate(wasmBin).then(() => {
                resolve('loaded')
              }).catch((error) => {
                resolve(error.message)
              })
            });
          }
        </script>`);
      return await w.webContents.executeJavaScript('loadWasm()');
    }

    it('wasm codegen is disallowed by default', async () => {
      const r = await loadWasm('');
      expect(r).to.equal('WebAssembly.instantiate(): Refused to compile or instantiate WebAssembly module because \'unsafe-eval\' is not an allowed source of script in the following Content Security Policy directive: "script-src \'self\' \'unsafe-inline\'"');
    });

    it('wasm codegen is allowed with "wasm-unsafe-eval" csp', async () => {
      const r = await loadWasm("'wasm-unsafe-eval'");
      expect(r).to.equal('loaded');
    });
  });

  describe('csp', () => {
    for (const sandbox of [true, false]) {
      describe(`when sandbox: ${sandbox}`, () => {
        for (const contextIsolation of [true, false]) {
          describe(`when contextIsolation: ${contextIsolation}`, () => {
            it('prevents eval from running in an inline script', async () => {
              const w = new BrowserWindow({
                show: false,
                webPreferences: { sandbox, contextIsolation }
              });
              w.loadURL(`data:text/html,<head>
              <meta http-equiv="Content-Security-Policy" content="default-src 'self'; script-src 'self' 'unsafe-inline'">
            </head>
            <script>
              try {
                // We use console.log here because it is easier than making a
                // preload script, and the behavior under test changes when
                // contextIsolation: false
                console.log(eval('true'))
              } catch (e) {
                console.log(e.message)
              }
            </script>`);
              const [,, message] = await once(w.webContents, 'console-message');
              expect(message).to.match(/Refused to evaluate a string/);
            });

            it('does not prevent eval from running in an inline script when there is no csp', async () => {
              const w = new BrowserWindow({
                show: false,
                webPreferences: { sandbox, contextIsolation }
              });
              w.loadURL(`data:text/html,
            <script>
              try {
                // We use console.log here because it is easier than making a
                // preload script, and the behavior under test changes when
                // contextIsolation: false
                console.log(eval('true'))
              } catch (e) {
                console.log(e.message)
              }
            </script>`);
              const [,, message] = await once(w.webContents, 'console-message');
              expect(message).to.equal('true');
            });

            it('prevents eval from running in executeJavaScript', async () => {
              const w = new BrowserWindow({
                show: false,
                webPreferences: { sandbox, contextIsolation }
              });
              w.loadURL('data:text/html,<head><meta http-equiv="Content-Security-Policy" content="default-src \'self\'; script-src \'self\' \'unsafe-inline\'"></meta></head>');
              await expect(w.webContents.executeJavaScript('eval("true")')).to.be.rejected();
            });

            it('does not prevent eval from running in executeJavaScript when there is no csp', async () => {
              const w = new BrowserWindow({
                show: false,
                webPreferences: { sandbox, contextIsolation }
              });
              w.loadURL('data:text/html,');
              expect(await w.webContents.executeJavaScript('eval("true")')).to.be.true();
            });
          });
        }
      });
    }
  });

  it('does not crash when multiple WebContent are created with web security disabled', () => {
    const options = { show: false, webPreferences: { webSecurity: false } };
    const w1 = new BrowserWindow(options);
    w1.loadURL(serverUrl);
    const w2 = new BrowserWindow(options);
    w2.loadURL(serverUrl);
  });
});

describe('command line switches', () => {
  let appProcess: ChildProcess.ChildProcessWithoutNullStreams | undefined;
  afterEach(() => {
    if (appProcess && !appProcess.killed) {
      appProcess.kill();
      appProcess = undefined;
    }
  });
  describe('--lang switch', () => {
    const currentLocale = app.getLocale();
    const currentSystemLocale = app.getSystemLocale();
    const currentPreferredLanguages = JSON.stringify(app.getPreferredSystemLanguages());
    const testLocale = async (locale: string, result: string, printEnv: boolean = false) => {
      const appPath = path.join(fixturesPath, 'api', 'locale-check');
      const args = [appPath, `--set-lang=${locale}`];
      if (printEnv) {
        args.push('--print-env');
      }
      appProcess = ChildProcess.spawn(process.execPath, args);

      let output = '';
      appProcess.stdout.on('data', (data) => { output += data; });
      let stderr = '';
      appProcess.stderr.on('data', (data) => { stderr += data; });

      const [code, signal] = await once(appProcess, 'exit');
      if (code !== 0) {
        throw new Error(`Process exited with code "${code}" signal "${signal}" output "${output}" stderr "${stderr}"`);
      }

      output = output.replaceAll(/(\r\n|\n|\r)/gm, '');
      expect(output).to.equal(result);
    };

    it('should set the locale', async () => testLocale('fr', `fr|${currentSystemLocale}|${currentPreferredLanguages}`));
    it('should set the locale with country code', async () => testLocale('zh-CN', `zh-CN|${currentSystemLocale}|${currentPreferredLanguages}`));
    it('should not set an invalid locale', async () => testLocale('asdfkl', `${currentLocale}|${currentSystemLocale}|${currentPreferredLanguages}`));

    const lcAll = String(process.env.LC_ALL);
    ifit(process.platform === 'linux')('current process has a valid LC_ALL env', async () => {
      // The LC_ALL env should not be set to DOM locale string.
      expect(lcAll).to.not.equal(app.getLocale());
    });
    ifit(process.platform === 'linux')('should not change LC_ALL', async () => testLocale('fr', lcAll, true));
    ifit(process.platform === 'linux')('should not change LC_ALL when setting invalid locale', async () => testLocale('asdfkl', lcAll, true));
    ifit(process.platform === 'linux')('should not change LC_ALL when --lang is not set', async () => testLocale('', lcAll, true));
  });

  describe('--remote-debugging-pipe switch', () => {
    it('should expose CDP via pipe', async () => {
      const electronPath = process.execPath;
      appProcess = ChildProcess.spawn(electronPath, ['--remote-debugging-pipe'], {
        stdio: ['inherit', 'inherit', 'inherit', 'pipe', 'pipe']
      }) as ChildProcess.ChildProcessWithoutNullStreams;
      const stdio = appProcess.stdio as unknown as [NodeJS.ReadableStream, NodeJS.WritableStream, NodeJS.WritableStream, NodeJS.WritableStream, NodeJS.ReadableStream];
      const pipe = new PipeTransport(stdio[3], stdio[4]);
      const versionPromise = new Promise(resolve => { pipe.onmessage = resolve; });
      pipe.send({ id: 1, method: 'Browser.getVersion', params: {} });
      const message = (await versionPromise) as any;
      expect(message.id).to.equal(1);
      expect(message.result.product).to.contain('Chrome');
      expect(message.result.userAgent).to.contain('Electron');
    });
    it('should override --remote-debugging-port switch', async () => {
      const electronPath = process.execPath;
      appProcess = ChildProcess.spawn(electronPath, ['--remote-debugging-pipe', '--remote-debugging-port=0'], {
        stdio: ['inherit', 'inherit', 'pipe', 'pipe', 'pipe']
      }) as ChildProcess.ChildProcessWithoutNullStreams;
      let stderr = '';
      appProcess.stderr.on('data', (data: string) => { stderr += data; });
      const stdio = appProcess.stdio as unknown as [NodeJS.ReadableStream, NodeJS.WritableStream, NodeJS.WritableStream, NodeJS.WritableStream, NodeJS.ReadableStream];
      const pipe = new PipeTransport(stdio[3], stdio[4]);
      const versionPromise = new Promise(resolve => { pipe.onmessage = resolve; });
      pipe.send({ id: 1, method: 'Browser.getVersion', params: {} });
      const message = (await versionPromise) as any;
      expect(message.id).to.equal(1);
      expect(stderr).to.not.include('DevTools listening on');
    });
    it('should shut down Electron upon Browser.close CDP command', async () => {
      const electronPath = process.execPath;
      appProcess = ChildProcess.spawn(electronPath, ['--remote-debugging-pipe'], {
        stdio: ['inherit', 'inherit', 'inherit', 'pipe', 'pipe']
      }) as ChildProcess.ChildProcessWithoutNullStreams;
      const stdio = appProcess.stdio as unknown as [NodeJS.ReadableStream, NodeJS.WritableStream, NodeJS.WritableStream, NodeJS.WritableStream, NodeJS.ReadableStream];
      const pipe = new PipeTransport(stdio[3], stdio[4]);
      pipe.send({ id: 1, method: 'Browser.close', params: {} });
      await once(appProcess, 'exit');
    });
  });

  describe('--remote-debugging-port switch', () => {
    it('should display the discovery page', (done) => {
      const electronPath = process.execPath;
      let output = '';
      appProcess = ChildProcess.spawn(electronPath, ['--remote-debugging-port=']);
      appProcess.stdout.on('data', (data) => {
        console.log(data);
      });

      appProcess.stderr.on('data', (data) => {
        console.log(data);
        output += data;
        const m = /DevTools listening on ws:\/\/127.0.0.1:(\d+)\//.exec(output);
        if (m) {
          appProcess!.stderr.removeAllListeners('data');
          const port = m[1];
          http.get(`http://127.0.0.1:${port}`, (res) => {
            try {
              expect(res.statusCode).to.eql(200);
              expect(parseInt(res.headers['content-length']!)).to.be.greaterThan(0);
              done();
            } catch (e) {
              done(e);
            } finally {
              res.destroy();
            }
          });
        }
      });
    });
  });
});

describe('chromium features', () => {
  afterEach(closeAllWindows);

  describe('accessing key names also used as Node.js module names', () => {
    it('does not crash', (done) => {
      const w = new BrowserWindow({ show: false });
      w.webContents.once('did-finish-load', () => { done(); });
      w.webContents.once('render-process-gone', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'external-string.html'));
    });
  });

  describe('first party sets', () => {
    const fps = [
      'https://fps-member1.glitch.me',
      'https://fps-member2.glitch.me',
      'https://fps-member3.glitch.me'
    ];

    it('loads first party sets', async () => {
      const appPath = path.join(fixturesPath, 'api', 'first-party-sets', 'base');
      const fpsProcess = ChildProcess.spawn(process.execPath, [appPath]);

      let output = '';
      fpsProcess.stdout.on('data', data => { output += data; });
      await once(fpsProcess, 'exit');

      expect(output).to.include(fps.join(','));
    });

    it('loads sets from the command line', async () => {
      const appPath = path.join(fixturesPath, 'api', 'first-party-sets', 'command-line');
      const args = [appPath, `--use-first-party-set=${fps}`];
      const fpsProcess = ChildProcess.spawn(process.execPath, args);

      let output = '';
      fpsProcess.stdout.on('data', data => { output += data; });
      await once(fpsProcess, 'exit');

      expect(output).to.include(fps.join(','));
    });
  });

  describe('loading jquery', () => {
    it('does not crash', (done) => {
      const w = new BrowserWindow({ show: false });
      w.webContents.once('did-finish-load', () => { done(); });
      w.webContents.once('render-process-gone', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(__dirname, 'fixtures', 'pages', 'jquery.html'));
    });
  });

  describe('navigator.keyboard', () => {
    afterEach(closeAllWindows);

    it('getLayoutMap() should return a KeyboardLayoutMap object', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
      const size = await w.webContents.executeJavaScript(`
        navigator.keyboard.getLayoutMap().then(map => map.size)
      `);

      expect(size).to.be.a('number');
    });

    it('should lock the keyboard', async () => {
      const w = new BrowserWindow({ show: true });
      await w.loadFile(path.join(fixturesPath, 'pages', 'modal.html'));

      // Test that without lock, with ESC:
      // - the window leaves fullscreen
      // - the dialog is not closed
      const enterFS1 = once(w, 'enter-full-screen');
      await w.webContents.executeJavaScript('document.body.requestFullscreen()', true);
      await enterFS1;

      await w.webContents.executeJavaScript('document.getElementById(\'favDialog\').showModal()', true);
      const open1 = await w.webContents.executeJavaScript('document.getElementById(\'favDialog\').open');
      expect(open1).to.be.true();

      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Escape' });
      await setTimeout(1000);
      const openAfter1 = await w.webContents.executeJavaScript('document.getElementById(\'favDialog\').open');
      expect(openAfter1).to.be.true();
      expect(w.isFullScreen()).to.be.false();

      // Test that with lock, with ESC:
      // - the window does not leave fullscreen
      // - the dialog is closed
      const enterFS2 = once(w, 'enter-full-screen');
      await w.webContents.executeJavaScript(`
        document.body.requestFullscreen();
      `, true);

      await enterFS2;

      // Request keyboard lock after window has gone fullscreen
      // otherwise it will result in blink::kKeyboardLockRequestFailedErrorMsg.
      await w.webContents.executeJavaScript(`
        navigator.keyboard.lock(['Escape']);
      `, true);

      await w.webContents.executeJavaScript('document.getElementById(\'favDialog\').showModal()', true);
      const open2 = await w.webContents.executeJavaScript('document.getElementById(\'favDialog\').open');
      expect(open2).to.be.true();

      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Escape' });
      await setTimeout(1000);
      const openAfter2 = await w.webContents.executeJavaScript('document.getElementById(\'favDialog\').open');
      expect(openAfter2).to.be.false();
      expect(w.isFullScreen()).to.be.true();
    });
  });

  describe('navigator.languages', () => {
    it('should return the system locale only', async () => {
      const appLocale = app.getLocale();
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');
      const languages = await w.webContents.executeJavaScript('navigator.languages');
      expect(languages.length).to.be.greaterThan(0);
      expect(languages).to.contain(appLocale);
    });
  });

  describe('navigator.serviceWorker', () => {
    it('should register for file scheme', (done) => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          partition: 'sw-file-scheme-spec',
          contextIsolation: false
        }
      });
      w.webContents.on('ipc-message', (event, channel, message) => {
        if (channel === 'reload') {
          w.webContents.reload();
        } else if (channel === 'error') {
          done(message);
        } else if (channel === 'response') {
          expect(message).to.equal('Hello from serviceWorker!');
          session.fromPartition('sw-file-scheme-spec').clearStorageData({
            storages: ['serviceworkers']
          }).then(() => done());
        }
      });
      w.webContents.on('render-process-gone', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'service-worker', 'index.html'));
    });

    it('should register for intercepted file scheme', (done) => {
      const customSession = session.fromPartition('intercept-file');
      customSession.protocol.interceptBufferProtocol('file', (request, callback) => {
        let file = url.parse(request.url).pathname!;
        if (file[0] === '/' && process.platform === 'win32') file = file.slice(1);

        const content = fs.readFileSync(path.normalize(file));
        const ext = path.extname(file);
        let type = 'text/html';

        if (ext === '.js') type = 'application/javascript';
        callback({ data: content, mimeType: type } as any);
      });

      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          session: customSession,
          contextIsolation: false
        }
      });
      w.webContents.on('ipc-message', (event, channel, message) => {
        if (channel === 'reload') {
          w.webContents.reload();
        } else if (channel === 'error') {
          done(`unexpected error : ${message}`);
        } else if (channel === 'response') {
          expect(message).to.equal('Hello from serviceWorker!');
          customSession.clearStorageData({
            storages: ['serviceworkers']
          }).then(() => {
            customSession.protocol.uninterceptProtocol('file');
            done();
          });
        }
      });
      w.webContents.on('render-process-gone', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'service-worker', 'index.html'));
    });

    it('should register for custom scheme', (done) => {
      const customSession = session.fromPartition('custom-scheme');
      customSession.protocol.registerFileProtocol(serviceWorkerScheme, (request, callback) => {
        let file = url.parse(request.url).pathname!;
        if (file[0] === '/' && process.platform === 'win32') file = file.slice(1);

        callback({ path: path.normalize(file) } as any);
      });

      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          session: customSession,
          contextIsolation: false
        }
      });
      w.webContents.on('ipc-message', (event, channel, message) => {
        if (channel === 'reload') {
          w.webContents.reload();
        } else if (channel === 'error') {
          done(`unexpected error : ${message}`);
        } else if (channel === 'response') {
          expect(message).to.equal('Hello from serviceWorker!');
          customSession.clearStorageData({
            storages: ['serviceworkers']
          }).then(() => {
            customSession.protocol.uninterceptProtocol(serviceWorkerScheme);
            done();
          });
        }
      });
      w.webContents.on('render-process-gone', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'service-worker', 'custom-scheme-index.html'));
    });

    it('should not allow nodeIntegrationInWorker', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          nodeIntegrationInWorker: true,
          partition: 'sw-file-scheme-worker-spec',
          contextIsolation: false
        }
      });

      await w.loadURL(`file://${fixturesPath}/pages/service-worker/empty.html`);

      const data = await w.webContents.executeJavaScript(`
        navigator.serviceWorker.register('worker-no-node.js', {
          scope: './'
        }).then(() => navigator.serviceWorker.ready)

        new Promise((resolve) => {
          navigator.serviceWorker.onmessage = event => resolve(event.data);
        });
      `);

      expect(data).to.equal('undefined undefined undefined undefined');
    });
  });

  describe('navigator.geolocation', () => {
    ifit(features.isFakeLocationProviderEnabled())('returns error when permission is denied', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          partition: 'geolocation-spec',
          contextIsolation: false
        }
      });
      const message = once(w.webContents, 'ipc-message');
      w.webContents.session.setPermissionRequestHandler((wc, permission, callback) => {
        if (permission === 'geolocation') {
          callback(false);
        } else {
          callback(true);
        }
      });
      w.loadFile(path.join(fixturesPath, 'pages', 'geolocation', 'index.html'));
      const [, channel] = await message;
      expect(channel).to.equal('success', 'unexpected response from geolocation api');
    });

    ifit(!features.isFakeLocationProviderEnabled())('returns position when permission is granted', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          partition: 'geolocation-spec'
        }
      });
      w.webContents.session.setPermissionRequestHandler((_wc, _permission, callback) => {
        callback(true);
      });
      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const position = await w.webContents.executeJavaScript(`new Promise((resolve, reject) =>
        navigator.geolocation.getCurrentPosition(
          x => resolve({coords: x.coords, timestamp: x.timestamp}),
          err => reject(new Error(err.message))))`);
      expect(position).to.have.property('coords');
      expect(position).to.have.property('timestamp');
    });
  });

  describe('File System API,', () => {
    afterEach(closeAllWindows);
    afterEach(() => {
      session.defaultSession.setPermissionRequestHandler(null);
    });

    it('allows access by default to reading an OPFS file', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          partition: 'file-system-spec',
          contextIsolation: false
        }
      });

      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const result = await w.webContents.executeJavaScript(`
        new Promise(async (resolve, reject) => {
          const root = await navigator.storage.getDirectory();
          const fileHandle = await root.getFileHandle('test', { create: true });
          const { name, size } = await fileHandle.getFile();
          resolve({ name, size });
        }
      )`, true);
      expect(result).to.deep.equal({ name: 'test', size: 0 });
    });

    it('fileHandle.queryPermission by default has permission to read and write to OPFS files', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          partition: 'file-system-spec',
          contextIsolation: false
        }
      });

      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const status = await w.webContents.executeJavaScript(`
        new Promise(async (resolve, reject) => {
          const root = await navigator.storage.getDirectory();
          const fileHandle = await root.getFileHandle('test', { create: true });
          const status = await fileHandle.queryPermission({ mode: 'readwrite' });
          resolve(status);
        }
      )`, true);
      expect(status).to.equal('granted');
    });

    it('fileHandle.requestPermission automatically grants permission to read and write to OPFS files', async () => {
      const w = new BrowserWindow({
        webPreferences: {
          nodeIntegration: true,
          partition: 'file-system-spec',
          contextIsolation: false
        }
      });

      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const status = await w.webContents.executeJavaScript(`
        new Promise(async (resolve, reject) => {
          const root = await navigator.storage.getDirectory();
          const fileHandle = await root.getFileHandle('test', { create: true });
          const status = await fileHandle.requestPermission({ mode: 'readwrite' });
          resolve(status);
        }
      )`, true);
      expect(status).to.equal('granted');
    });

    it('requests permission when trying to create a writable file handle', (done) => {
      const writablePath = path.join(fixturesPath, 'file-system', 'test-writable.html');
      const testFile = path.join(fixturesPath, 'file-system', 'test.txt');

      const w = new BrowserWindow({
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false,
          sandbox: false
        }
      });

      w.webContents.session.setPermissionRequestHandler((wc, permission, callback, details) => {
        expect(permission).to.equal('fileSystem');

        const { href } = url.pathToFileURL(writablePath);
        expect(details).to.deep.equal({
          fileAccessType: 'writable',
          isDirectory: false,
          isMainFrame: true,
          filePath: testFile,
          requestingUrl: href
        });

        callback(true);
      });

      ipcMain.once('did-create-file-handle', async () => {
        const result = await w.webContents.executeJavaScript(`
          new Promise((resolve, reject) => {
            try {
              const writable = fileHandle.createWritable();
              resolve(true);
            } catch {
              resolve(false);
            }
          })
        `, true);
        expect(result).to.be.true();
        done();
      });

      w.loadFile(writablePath);

      w.webContents.once('did-finish-load', () => {
        // @ts-expect-error Undocumented testing method.
        clipboard._writeFilesForTesting([testFile]);
        w.webContents.paste();
      });
    });
  });

  describe('web workers', () => {
    let appProcess: ChildProcess.ChildProcessWithoutNullStreams | undefined;

    afterEach(() => {
      if (appProcess && !appProcess.killed) {
        appProcess.kill();
        appProcess = undefined;
      }
    });

    it('Worker with nodeIntegrationInWorker has access to self.module.paths', async () => {
      const appPath = path.join(__dirname, 'fixtures', 'apps', 'self-module-paths');

      appProcess = ChildProcess.spawn(process.execPath, [appPath]);

      const [code] = await once(appProcess, 'exit');
      expect(code).to.equal(0);
    });

    it('Worker can work', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const data = await w.webContents.executeJavaScript(`
        const worker = new Worker('../workers/worker.js');
        const message = 'ping';
        const eventPromise = new Promise((resolve) => { worker.onmessage = resolve; });
        worker.postMessage(message);
        eventPromise.then(t => t.data)
      `);
      expect(data).to.equal('ping');
    });

    it('Worker has no node integration by default', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const data = await w.webContents.executeJavaScript(`
        const worker = new Worker('../workers/worker_node.js');
        new Promise((resolve) => { worker.onmessage = e => resolve(e.data); })
      `);
      expect(data).to.equal('undefined undefined undefined undefined');
    });

    it('Worker has node integration with nodeIntegrationInWorker', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          nodeIntegrationInWorker: true,
          contextIsolation: false
        }
      });

      w.loadURL(`file://${fixturesPath}/pages/worker.html`);
      const [, data] = await once(ipcMain, 'worker-result');
      expect(data).to.equal('object function object function');
    });

    it('Worker has access to fetch-dependent interfaces with nodeIntegrationInWorker', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          nodeIntegrationInWorker: true,
          contextIsolation: false
        }
      });

      w.loadURL(`file://${fixturesPath}/pages/worker-fetch.html`);
      const [, data] = await once(ipcMain, 'worker-fetch-result');
      expect(data).to.equal('function function function function function');
    });

    describe('SharedWorker', () => {
      it('can work', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
        const data = await w.webContents.executeJavaScript(`
          const worker = new SharedWorker('../workers/shared_worker.js');
          const message = 'ping';
          const eventPromise = new Promise((resolve) => { worker.port.onmessage = e => resolve(e.data); });
          worker.port.postMessage(message);
          eventPromise
        `);
        expect(data).to.equal('ping');
      });

      it('has no node integration by default', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
        const data = await w.webContents.executeJavaScript(`
          const worker = new SharedWorker('../workers/shared_worker_node.js');
          new Promise((resolve) => { worker.port.onmessage = e => resolve(e.data); })
        `);
        expect(data).to.equal('undefined undefined undefined undefined');
      });

      it('does not have node integration with nodeIntegrationInWorker', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            nodeIntegrationInWorker: true,
            contextIsolation: false
          }
        });

        await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
        const data = await w.webContents.executeJavaScript(`
          const worker = new SharedWorker('../workers/shared_worker_node.js');
          new Promise((resolve) => { worker.port.onmessage = e => resolve(e.data); })
        `);
        expect(data).to.equal('undefined undefined undefined undefined');
      });
    });
  });

  describe('form submit', () => {
    let server: http.Server;
    let serverUrl: string;

    before(async () => {
      server = http.createServer((req, res) => {
        let body = '';
        req.on('data', (chunk) => {
          body += chunk;
        });
        res.setHeader('Content-Type', 'application/json');
        req.on('end', () => {
          res.end(`body:${body}`);
        });
      });
      serverUrl = (await listen(server)).url;
    });
    after(async () => {
      server.close();
      await closeAllWindows();
    });

    for (const isSandboxEnabled of [true, false]) {
      describe(`sandbox=${isSandboxEnabled}`, () => {
        it('posts data in the same window', async () => {
          const w = new BrowserWindow({
            show: false,
            webPreferences: {
              sandbox: isSandboxEnabled
            }
          });

          await w.loadFile(path.join(fixturesPath, 'pages', 'form-with-data.html'));

          const loadPromise = once(w.webContents, 'did-finish-load');

          w.webContents.executeJavaScript(`
            const form = document.querySelector('form')
            form.action = '${serverUrl}';
            form.submit();
          `);

          await loadPromise;

          const res = await w.webContents.executeJavaScript('document.body.innerText');
          expect(res).to.equal('body:greeting=hello');
        });

        it('posts data to a new window with target=_blank', async () => {
          const w = new BrowserWindow({
            show: false,
            webPreferences: {
              sandbox: isSandboxEnabled
            }
          });

          await w.loadFile(path.join(fixturesPath, 'pages', 'form-with-data.html'));

          const windowCreatedPromise = once(app, 'browser-window-created') as Promise<[any, BrowserWindow]>;

          w.webContents.executeJavaScript(`
            const form = document.querySelector('form')
            form.action = '${serverUrl}';
            form.target = '_blank';
            form.submit();
          `);

          const [, newWin] = await windowCreatedPromise;

          const res = await newWin.webContents.executeJavaScript('document.body.innerText');
          expect(res).to.equal('body:greeting=hello');
        });
      });
    }
  });

  describe('window.open', () => {
    for (const show of [true, false]) {
      it(`shows the child regardless of parent visibility when parent {show=${show}}`, async () => {
        const w = new BrowserWindow({ show });

        // toggle visibility
        if (show) {
          w.hide();
        } else {
          w.show();
        }

        defer(() => { w.close(); });

        const promise = once(app, 'browser-window-created') as Promise<[any, BrowserWindow]>;
        w.loadFile(path.join(fixturesPath, 'pages', 'window-open.html'));
        const [, newWindow] = await promise;
        expect(newWindow.isVisible()).to.equal(true);
      });
    }

    // FIXME(zcbenz): This test is making the spec runner hang on exit on Windows.
    ifit(process.platform !== 'win32')('disables node integration when it is disabled on the parent window', async () => {
      const windowUrl = url.pathToFileURL(path.join(fixturesPath, 'pages', 'window-opener-no-node-integration.html'));
      windowUrl.searchParams.set('p', `${fixturesPath}/pages/window-opener-node.html`);

      const w = new BrowserWindow({ show: false });
      w.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));

      const { eventData } = await w.webContents.executeJavaScript(`(async () => {
        const message = new Promise(resolve => window.addEventListener('message', resolve, {once: true}));
        const b = window.open(${JSON.stringify(windowUrl)}, '', 'show=false')
        const e = await message
        b.close();
        return {
          eventData: e.data
        }
      })()`);

      expect(eventData.isProcessGlobalUndefined).to.be.true();
    });

    it('disables node integration when it is disabled on the parent window for chrome devtools URLs', async () => {
      // NB. webSecurity is disabled because native window.open() is not
      // allowed to load devtools:// URLs.
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, webSecurity: false } });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript(`
        { b = window.open('devtools://devtools/bundled/inspector.html', '', 'nodeIntegration=no,show=no'); null }
      `);
      const [, contents] = await once(app, 'web-contents-created') as [any, WebContents];
      const typeofProcessGlobal = await contents.executeJavaScript('typeof process');
      expect(typeofProcessGlobal).to.equal('undefined');
    });

    it('can disable node integration when it is enabled on the parent window', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript(`
        { b = window.open('about:blank', '', 'nodeIntegration=no,show=no'); null }
      `);
      const [, contents] = await once(app, 'web-contents-created') as [any, WebContents];
      const typeofProcessGlobal = await contents.executeJavaScript('typeof process');
      expect(typeofProcessGlobal).to.equal('undefined');
    });

    // TODO(jkleinsc) fix this flaky test on WOA
    ifit(process.platform !== 'win32' || process.arch !== 'arm64')('disables JavaScript when it is disabled on the parent window', async () => {
      const w = new BrowserWindow({ show: true, webPreferences: { nodeIntegration: true } });
      w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      const windowUrl = require('node:url').format({
        pathname: `${fixturesPath}/pages/window-no-javascript.html`,
        protocol: 'file',
        slashes: true
      });
      w.webContents.executeJavaScript(`
        { b = window.open(${JSON.stringify(windowUrl)}, '', 'javascript=no,show=no'); null }
      `);
      const [, contents] = await once(app, 'web-contents-created') as [any, WebContents];
      await once(contents, 'did-finish-load');
      // Click link on page
      contents.sendInputEvent({ type: 'mouseDown', clickCount: 1, x: 1, y: 1 });
      contents.sendInputEvent({ type: 'mouseUp', clickCount: 1, x: 1, y: 1 });
      const [, window] = await once(app, 'browser-window-created') as [any, BrowserWindow];
      const preferences = window.webContents.getLastWebPreferences();
      expect(preferences!.javascript).to.be.false();
    });

    it('defines a window.location getter', async () => {
      let targetURL: string;
      if (process.platform === 'win32') {
        targetURL = `file:///${fixturesPath.replaceAll('\\', '/')}/pages/base-page.html`;
      } else {
        targetURL = `file://${fixturesPath}/pages/base-page.html`;
      }
      const w = new BrowserWindow({ show: false });
      w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      w.webContents.executeJavaScript(`{ b = window.open(${JSON.stringify(targetURL)}); null }`);
      const [, window] = await once(app, 'browser-window-created') as [any, BrowserWindow];
      await once(window.webContents, 'did-finish-load');
      expect(await w.webContents.executeJavaScript('b.location.href')).to.equal(targetURL);
    });

    it('defines a window.location setter', async () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      w.webContents.executeJavaScript('{ b = window.open("about:blank"); null }');
      const [, { webContents }] = await once(app, 'browser-window-created') as [any, BrowserWindow];
      await once(webContents, 'did-finish-load');
      // When it loads, redirect
      w.webContents.executeJavaScript(`{ b.location = ${JSON.stringify(`file://${fixturesPath}/pages/base-page.html`)}; null }`);
      await once(webContents, 'did-finish-load');
    });

    it('defines a window.location.href setter', async () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      w.webContents.executeJavaScript('{ b = window.open("about:blank"); null }');
      const [, { webContents }] = await once(app, 'browser-window-created') as [any, BrowserWindow];
      await once(webContents, 'did-finish-load');
      // When it loads, redirect
      w.webContents.executeJavaScript(`{ b.location.href = ${JSON.stringify(`file://${fixturesPath}/pages/base-page.html`)}; null }`);
      await once(webContents, 'did-finish-load');
    });

    it('open a blank page when no URL is specified', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('{ b = window.open(); null }');
      const [, { webContents }] = await once(app, 'browser-window-created') as [any, BrowserWindow];
      await once(webContents, 'did-finish-load');
      expect(await w.webContents.executeJavaScript('b.location.href')).to.equal('about:blank');
    });

    it('open a blank page when an empty URL is specified', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('{ b = window.open(\'\'); null }');
      const [, { webContents }] = await once(app, 'browser-window-created') as [any, BrowserWindow];
      await once(webContents, 'did-finish-load');
      expect(await w.webContents.executeJavaScript('b.location.href')).to.equal('about:blank');
    });

    it('does not throw an exception when the frameName is a built-in object property', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('{ b = window.open(\'\', \'__proto__\'); null }');
      const frameName = await new Promise((resolve) => {
        w.webContents.setWindowOpenHandler(details => {
          setImmediate(() => resolve(details.frameName));
          return { action: 'allow' };
        });
      });
      expect(frameName).to.equal('__proto__');
    });

    it('works when used in conjunction with the vm module', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });

      await w.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));

      const { contextObject } = await w.webContents.executeJavaScript(`(async () => {
        const vm = require('node:vm');
        const contextObject = { count: 1, type: 'gecko' };
        window.open('');
        vm.runInNewContext('count += 1; type = "chameleon";', contextObject);
        return { contextObject };
      })()`);

      expect(contextObject).to.deep.equal({ count: 2, type: 'chameleon' });
    });

    // FIXME(nornagon): I'm not sure this ... ever was correct?
    xit('inherit options of parent window', async () => {
      const w = new BrowserWindow({ show: false, width: 123, height: 456 });
      w.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      const url = `file://${fixturesPath}/pages/window-open-size.html`;
      const { width, height, eventData } = await w.webContents.executeJavaScript(`(async () => {
        const message = new Promise(resolve => window.addEventListener('message', resolve, {once: true}));
        const b = window.open(${JSON.stringify(url)}, '', 'show=false')
        const e = await message
        b.close();
        const width = outerWidth;
        const height = outerHeight;
        return {
          width,
          height,
          eventData: e.data
        }
      })()`);
      expect(eventData).to.equal(`size: ${width} ${height}`);
      expect(eventData).to.equal('size: 123 456');
    });

    it('does not override child options', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      const windowUrl = `file://${fixturesPath}/pages/window-open-size.html`;
      const { eventData } = await w.webContents.executeJavaScript(`(async () => {
        const message = new Promise(resolve => window.addEventListener('message', resolve, {once: true}));
        const b = window.open(${JSON.stringify(windowUrl)}, '', 'show=no,width=350,height=450')
        const e = await message
        b.close();
        return { eventData: e.data }
      })()`);
      expect(eventData).to.equal('size: 350 450');
    });

    it('loads preload script after setting opener to null', async () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.setWindowOpenHandler(() => ({
        action: 'allow',
        overrideBrowserWindowOptions: {
          webPreferences: {
            preload: path.join(fixturesPath, 'module', 'preload.js')
          }
        }
      }));
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('window.child = window.open(); child.opener = null');
      const [, { webContents }] = await once(app, 'browser-window-created');
      const [,, message] = await once(webContents, 'console-message');
      expect(message).to.equal('{"require":"function","module":"object","exports":"object","process":"object","Buffer":"function"}');
    });

    it('disables the <webview> tag when it is disabled on the parent window', async () => {
      const windowUrl = url.pathToFileURL(path.join(fixturesPath, 'pages', 'window-opener-no-webview-tag.html'));
      windowUrl.searchParams.set('p', `${fixturesPath}/pages/window-opener-webview.html`);

      const w = new BrowserWindow({ show: false });
      w.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));

      const { eventData } = await w.webContents.executeJavaScript(`(async () => {
        const message = new Promise(resolve => window.addEventListener('message', resolve, {once: true}));
        const b = window.open(${JSON.stringify(windowUrl)}, '', 'webviewTag=no,contextIsolation=no,nodeIntegration=yes,show=no')
        const e = await message
        b.close();
        return { eventData: e.data }
      })()`);
      expect(eventData.isWebViewGlobalUndefined).to.be.true();
    });

    it('throws an exception when the arguments cannot be converted to strings', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      await expect(
        w.webContents.executeJavaScript('window.open(\'\', { toString: null })')
      ).to.eventually.be.rejected();

      await expect(
        w.webContents.executeJavaScript('window.open(\'\', \'\', { toString: 3 })')
      ).to.eventually.be.rejected();
    });

    it('does not throw an exception when the features include webPreferences', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      await expect(
        w.webContents.executeJavaScript('window.open(\'\', \'\', \'show=no,webPreferences=\'); null')
      ).to.eventually.be.fulfilled();
    });
  });

  describe('window.opener', () => {
    it('is null for main window', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      w.loadFile(path.join(fixturesPath, 'pages', 'window-opener.html'));
      const [, channel, opener] = await once(w.webContents, 'ipc-message');
      expect(channel).to.equal('opener');
      expect(opener).to.equal(null);
    });

    it('is not null for window opened by window.open', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      w.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));

      const windowUrl = `file://${fixturesPath}/pages/window-opener.html`;
      const eventData = await w.webContents.executeJavaScript(`
        const b = window.open(${JSON.stringify(windowUrl)}, '', 'show=no');
        new Promise(resolve => window.addEventListener('message', resolve, {once: true})).then(e => e.data);
      `);
      expect(eventData).to.equal('object');
    });
  });

  describe('window.opener.postMessage', () => {
    it('sets source and origin correctly', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));

      const windowUrl = `file://${fixturesPath}/pages/window-opener-postMessage.html`;
      const { sourceIsChild, origin } = await w.webContents.executeJavaScript(`
        const b = window.open(${JSON.stringify(windowUrl)}, '', 'show=no');
        new Promise(resolve => window.addEventListener('message', resolve, {once: true})).then(e => ({
          sourceIsChild: e.source === b,
          origin: e.origin
        }));
      `);

      expect(sourceIsChild).to.be.true();
      expect(origin).to.equal('file://');
    });

    it('supports windows opened from a <webview>', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { webviewTag: true } });
      w.loadURL('about:blank');
      const childWindowUrl = url.pathToFileURL(path.join(fixturesPath, 'pages', 'webview-opener-postMessage.html'));
      childWindowUrl.searchParams.set('p', `${fixturesPath}/pages/window-opener-postMessage.html`);
      const message = await w.webContents.executeJavaScript(`
        const webview = new WebView();
        webview.allowpopups = true;
        webview.setAttribute('webpreferences', 'contextIsolation=no');
        webview.src = ${JSON.stringify(childWindowUrl)}
        const consoleMessage = new Promise(resolve => webview.addEventListener('console-message', resolve, {once: true}));
        document.body.appendChild(webview);
        consoleMessage.then(e => e.message)
      `);

      expect(message).to.equal('message');
    });

    describe('targetOrigin argument', () => {
      let serverURL: string;
      let server: any;

      beforeEach(async () => {
        server = http.createServer((req, res) => {
          res.writeHead(200);
          const filePath = path.join(fixturesPath, 'pages', 'window-opener-targetOrigin.html');
          res.end(fs.readFileSync(filePath, 'utf8'));
        });
        serverURL = (await listen(server)).url;
      });

      afterEach(() => {
        server.close();
      });

      it('delivers messages that match the origin', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
        w.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
        const data = await w.webContents.executeJavaScript(`
          window.open(${JSON.stringify(serverURL)}, '', 'show=no,contextIsolation=no,nodeIntegration=yes');
          new Promise(resolve => window.addEventListener('message', resolve, {once: true})).then(e => e.data)
        `);
        expect(data).to.equal('deliver');
      });
    });
  });

  describe('Storage Access API', () => {
    afterEach(closeAllWindows);
    afterEach(() => {
      session.defaultSession.setPermissionCheckHandler(null);
      session.defaultSession.setPermissionRequestHandler(null);
    });

    it('can determine if a permission is granted for "storage-access"', async () => {
      session.defaultSession.setPermissionCheckHandler(
        (_wc, permission) => permission === 'storage-access'
      );

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'a.html'));

      const permission = await w.webContents.executeJavaScript(`
        navigator.permissions.query({ name: 'storage-access' })
          .then(permission => permission.state).catch(err => err.message);
      `, true);

      expect(permission).to.eq('granted');
    });

    it('can determine if a permission is denied for "storage-access"', async () => {
      session.defaultSession.setPermissionCheckHandler(
        (_wc, permission) => permission !== 'storage-access'
      );

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'a.html'));

      const permission = await w.webContents.executeJavaScript(`
        navigator.permissions.query({ name: 'storage-access' })
          .then(permission => permission.state).catch(err => err.message);
      `, true);

      expect(permission).to.eq('denied');
    });

    it('can determine if a permission is granted for "top-level-storage-access"', async () => {
      session.defaultSession.setPermissionCheckHandler(
        (_wc, permission) => permission === 'top-level-storage-access'
      );

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'a.html'));

      const permission = await w.webContents.executeJavaScript(`
        navigator.permissions.query({
          name: 'top-level-storage-access',
          requestedOrigin: "https://www.example.com",
        }).then(permission => permission.state).catch(err => err.message);
      `, true);

      expect(permission).to.eq('granted');
    });

    it('can determine if a permission is denied for "top-level-storage-access"', async () => {
      session.defaultSession.setPermissionCheckHandler(
        (_wc, permission) => permission !== 'top-level-storage-access'
      );

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'a.html'));

      const permission = await w.webContents.executeJavaScript(`
        navigator.permissions.query({
          name: 'top-level-storage-access',
          requestedOrigin: "https://www.example.com",
        }).then(permission => permission.state).catch(err => err.message);
      `, true);

      expect(permission).to.eq('denied');
    });

    it('can grant a permission request for "top-level-storage-access"', async () => {
      session.defaultSession.setPermissionRequestHandler(
        (_wc, permission, callback) => {
          callback(permission === 'top-level-storage-access');
        }
      );

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'button.html'));

      // requestStorageAccessFor returns a Promise that fulfills with undefined
      // if the access to third-party cookies was granted and rejects if access was denied.
      const permission = await w.webContents.executeJavaScript(`
        new Promise((resolve, reject) => {
          const button = document.getElementById('button');
          button.addEventListener("click", () => {
            document.requestStorageAccessFor('https://myfakesite').then(
              (res) => { resolve('granted') },
              (err) => { resolve('denied') },
            );
          });
          button.click();
        });
      `, true);

      expect(permission).to.eq('granted');
    });

    it('can deny a permission request for "top-level-storage-access"', async () => {
      session.defaultSession.setPermissionRequestHandler(
        (_wc, permission, callback) => {
          callback(permission !== 'top-level-storage-access');
        }
      );

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'button.html'));

      // requestStorageAccessFor returns a Promise that fulfills with undefined
      // if the access to third-party cookies was granted and rejects if access was denied.
      const permission = await w.webContents.executeJavaScript(`
        new Promise((resolve, reject) => {
          const button = document.getElementById('button');
          button.addEventListener("click", () => {
            document.requestStorageAccessFor('https://myfakesite').then(
              (res) => { resolve('granted') },
              (err) => { resolve('denied') },
            );
          });
          button.click();
        });
      `, true);

      expect(permission).to.eq('denied');
    });
  });

  describe('IdleDetection', () => {
    afterEach(closeAllWindows);
    afterEach(() => {
      session.defaultSession.setPermissionCheckHandler(null);
      session.defaultSession.setPermissionRequestHandler(null);
    });

    it('can grant a permission request', async () => {
      session.defaultSession.setPermissionRequestHandler(
        (_wc, permission, callback) => {
          callback(permission === 'idle-detection');
        }
      );

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'button.html'));
      const permission = await w.webContents.executeJavaScript(`
        new Promise((resolve, reject) => {
          const button = document.getElementById('button');
          button.addEventListener("click", async () => {
            const permission = await IdleDetector.requestPermission();
            resolve(permission);
          });
          button.click();
        });
      `, true);

      expect(permission).to.eq('granted');
    });

    it('can deny a permission request', async () => {
      session.defaultSession.setPermissionRequestHandler(
        (_wc, permission, callback) => {
          callback(permission !== 'idle-detection');
        }
      );

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'button.html'));
      const permission = await w.webContents.executeJavaScript(`
        new Promise((resolve, reject) => {
          const button = document.getElementById('button');
          button.addEventListener("click", async () => {
            const permission = await IdleDetector.requestPermission();
            resolve(permission);
          });
          button.click();
        });
      `, true);

      expect(permission).to.eq('denied');
    });

    it('can allow the IdleDetector to start', async () => {
      session.defaultSession.setPermissionCheckHandler((wc, permission) => {
        return permission === 'idle-detection';
      });

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
      const result = await w.webContents.executeJavaScript(`
        const detector = new IdleDetector({ threshold: 60000 });
        detector.start().then(() => {
          return 'success';
        }).catch(e => e.message);
      `, true);

      expect(result).to.eq('success');
    });

    it('can prevent the IdleDetector from starting', async () => {
      session.defaultSession.setPermissionCheckHandler((wc, permission) => {
        return permission !== 'idle-detection';
      });

      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
      const result = await w.webContents.executeJavaScript(`
        const detector = new IdleDetector({ threshold: 60000 });
        detector.start().then(() => {
          console.log('success')
        }).catch(e => e.message);
      `, true);

      expect(result).to.eq('Idle detection permission denied');
    });
  });

  describe('navigator.mediaDevices', () => {
    afterEach(closeAllWindows);
    afterEach(() => {
      session.defaultSession.setPermissionCheckHandler(null);
      session.defaultSession.setPermissionRequestHandler(null);
    });

    it('can return labels of enumerated devices', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
      const labels = await w.webContents.executeJavaScript('navigator.mediaDevices.enumerateDevices().then(ds => ds.map(d => d.label))');
      expect(labels.some((l: any) => l)).to.be.true();
    });

    it('does not return labels of enumerated devices when permission denied', async () => {
      session.defaultSession.setPermissionCheckHandler(() => false);
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
      const labels = await w.webContents.executeJavaScript('navigator.mediaDevices.enumerateDevices().then(ds => ds.map(d => d.label))');
      expect(labels.some((l: any) => l)).to.be.false();
    });

    it('returns the same device ids across reloads', async () => {
      const ses = session.fromPartition('persist:media-device-id');
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          session: ses,
          contextIsolation: false
        }
      });
      w.loadFile(path.join(fixturesPath, 'pages', 'media-id-reset.html'));
      const [, firstDeviceIds] = await once(ipcMain, 'deviceIds');
      w.webContents.reload();
      const [, secondDeviceIds] = await once(ipcMain, 'deviceIds');
      expect(firstDeviceIds).to.deep.equal(secondDeviceIds);
    });

    it('can return new device id when cookie storage is cleared', async () => {
      const ses = session.fromPartition('persist:media-device-id');
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          session: ses,
          contextIsolation: false
        }
      });
      w.loadFile(path.join(fixturesPath, 'pages', 'media-id-reset.html'));
      const [, firstDeviceIds] = await once(ipcMain, 'deviceIds');
      await ses.clearStorageData({ storages: ['cookies'] });
      w.webContents.reload();
      const [, secondDeviceIds] = await once(ipcMain, 'deviceIds');
      expect(firstDeviceIds).to.not.deep.equal(secondDeviceIds);
    });

    it('provides a securityOrigin to the request handler', async () => {
      session.defaultSession.setPermissionRequestHandler(
        (wc, permission, callback, details) => {
          if ((details as MediaAccessPermissionRequest).securityOrigin !== undefined) {
            callback(true);
          } else {
            callback(false);
          }
        }
      );
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
      const labels = await w.webContents.executeJavaScript(`navigator.mediaDevices.getUserMedia({
          video: {
            mandatory: {
              chromeMediaSource: "desktop",
              minWidth: 1280,
              maxWidth: 1280,
              minHeight: 720,
              maxHeight: 720
            }
          }
        }).then((stream) => stream.getVideoTracks())`);
      expect(labels.some((l: any) => l)).to.be.true();
    });

    it('fails with "not supported" for getDisplayMedia', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
      const { ok, err } = await w.webContents.executeJavaScript('navigator.mediaDevices.getDisplayMedia({video: true}).then(s => ({ok: true}), e => ({ok: false, err: e.message}))', true);
      expect(ok).to.be.false();
      expect(err).to.equal('Not supported');
    });
  });

  describe('window.opener access', () => {
    const scheme = 'app';

    const fileUrl = `file://${fixturesPath}/pages/window-opener-location.html`;
    const httpUrl1 = `${scheme}://origin1`;
    const httpUrl2 = `${scheme}://origin2`;
    const fileBlank = `file://${fixturesPath}/pages/blank.html`;
    const httpBlank = `${scheme}://origin1/blank`;

    const table = [
      { parent: fileBlank, child: httpUrl1, nodeIntegration: false, openerAccessible: false },
      { parent: fileBlank, child: httpUrl1, nodeIntegration: true, openerAccessible: false },

      // {parent: httpBlank, child: fileUrl, nodeIntegration: false, openerAccessible: false}, // can't window.open()
      // {parent: httpBlank, child: fileUrl, nodeIntegration: true, openerAccessible: false}, // can't window.open()

      // NB. this is different from Chrome's behavior, which isolates file: urls from each other
      { parent: fileBlank, child: fileUrl, nodeIntegration: false, openerAccessible: true },
      { parent: fileBlank, child: fileUrl, nodeIntegration: true, openerAccessible: true },

      { parent: httpBlank, child: httpUrl1, nodeIntegration: false, openerAccessible: true },
      { parent: httpBlank, child: httpUrl1, nodeIntegration: true, openerAccessible: true },

      { parent: httpBlank, child: httpUrl2, nodeIntegration: false, openerAccessible: false },
      { parent: httpBlank, child: httpUrl2, nodeIntegration: true, openerAccessible: false }
    ];
    const s = (url: string) => url.startsWith('file') ? 'file://...' : url;

    before(() => {
      protocol.registerFileProtocol(scheme, (request, callback) => {
        if (request.url.includes('blank')) {
          callback(`${fixturesPath}/pages/blank.html`);
        } else {
          callback(`${fixturesPath}/pages/window-opener-location.html`);
        }
      });
    });
    after(() => {
      protocol.unregisterProtocol(scheme);
    });
    afterEach(closeAllWindows);

    describe('when opened from main window', () => {
      for (const { parent, child, nodeIntegration, openerAccessible } of table) {
        for (const sandboxPopup of [false, true]) {
          const description = `when parent=${s(parent)} opens child=${s(child)} with nodeIntegration=${nodeIntegration} sandboxPopup=${sandboxPopup}, child should ${openerAccessible ? '' : 'not '}be able to access opener`;
          it(description, async () => {
            const w = new BrowserWindow({ show: true, webPreferences: { nodeIntegration: true, contextIsolation: false } });
            w.webContents.setWindowOpenHandler(() => ({
              action: 'allow',
              overrideBrowserWindowOptions: {
                webPreferences: {
                  sandbox: sandboxPopup
                }
              }
            }));
            await w.loadURL(parent);
            const childOpenerLocation = await w.webContents.executeJavaScript(`new Promise(resolve => {
              window.addEventListener('message', function f(e) {
                resolve(e.data)
              })
              window.open(${JSON.stringify(child)}, "", "show=no,nodeIntegration=${nodeIntegration ? 'yes' : 'no'}")
            })`);
            if (openerAccessible) {
              expect(childOpenerLocation).to.be.a('string');
            } else {
              expect(childOpenerLocation).to.be.null();
            }
          });
        }
      }
    });

    describe('when opened from <webview>', () => {
      for (const { parent, child, nodeIntegration, openerAccessible } of table) {
        const description = `when parent=${s(parent)} opens child=${s(child)} with nodeIntegration=${nodeIntegration}, child should ${openerAccessible ? '' : 'not '}be able to access opener`;
        it(description, async () => {
          // This test involves three contexts:
          //  1. The root BrowserWindow in which the test is run,
          //  2. A <webview> belonging to the root window,
          //  3. A window opened by calling window.open() from within the <webview>.
          // We are testing whether context (3) can access context (2) under various conditions.

          // This is context (1), the base window for the test.
          const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, webviewTag: true, contextIsolation: false } });
          await w.loadURL('about:blank');

          const parentCode = `new Promise((resolve) => {
            // This is context (3), a child window of the WebView.
            const child = window.open(${JSON.stringify(child)}, "", "show=no,contextIsolation=no,nodeIntegration=yes")
            window.addEventListener("message", e => {
              resolve(e.data)
            })
          })`;
          const childOpenerLocation = await w.webContents.executeJavaScript(`new Promise((resolve, reject) => {
            // This is context (2), a WebView which will call window.open()
            const webview = new WebView()
            webview.setAttribute('nodeintegration', '${nodeIntegration ? 'on' : 'off'}')
            webview.setAttribute('webpreferences', 'contextIsolation=no')
            webview.setAttribute('allowpopups', 'on')
            webview.src = ${JSON.stringify(parent + '?p=' + encodeURIComponent(child))}
            webview.addEventListener('dom-ready', async () => {
              webview.executeJavaScript(${JSON.stringify(parentCode)}).then(resolve, reject)
            })
            document.body.appendChild(webview)
          })`);
          if (openerAccessible) {
            expect(childOpenerLocation).to.be.a('string');
          } else {
            expect(childOpenerLocation).to.be.null();
          }
        });
      }
    });
  });

  describe('storage', () => {
    describe('custom non standard schemes', () => {
      const protocolName = 'storage';
      let contents: WebContents;
      before(() => {
        protocol.registerFileProtocol(protocolName, (request, callback) => {
          const parsedUrl = url.parse(request.url);
          let filename;
          switch (parsedUrl.pathname) {
            case '/localStorage' : filename = 'local_storage.html'; break;
            case '/sessionStorage' : filename = 'session_storage.html'; break;
            case '/indexedDB' : filename = 'indexed_db.html'; break;
            case '/cookie' : filename = 'cookie.html'; break;
            default : filename = '';
          }
          callback({ path: `${fixturesPath}/pages/storage/${filename}` });
        });
      });

      after(() => {
        protocol.unregisterProtocol(protocolName);
      });

      beforeEach(() => {
        contents = (webContents as typeof ElectronInternal.WebContents).create({
          nodeIntegration: true,
          contextIsolation: false
        });
      });

      afterEach(() => {
        contents.destroy();
        contents = null as any;
      });

      it('cannot access localStorage', async () => {
        const response = once(ipcMain, 'local-storage-response');
        contents.loadURL(protocolName + '://host/localStorage');
        const [, error] = await response;
        expect(error).to.equal('Failed to read the \'localStorage\' property from \'Window\': Access is denied for this document.');
      });

      it('cannot access sessionStorage', async () => {
        const response = once(ipcMain, 'session-storage-response');
        contents.loadURL(`${protocolName}://host/sessionStorage`);
        const [, error] = await response;
        expect(error).to.equal('Failed to read the \'sessionStorage\' property from \'Window\': Access is denied for this document.');
      });

      it('cannot access indexedDB', async () => {
        const response = once(ipcMain, 'indexed-db-response');
        contents.loadURL(`${protocolName}://host/indexedDB`);
        const [, error] = await response;
        expect(error).to.equal('Failed to execute \'open\' on \'IDBFactory\': access to the Indexed Database API is denied in this context.');
      });

      it('cannot access cookie', async () => {
        const response = once(ipcMain, 'cookie-response');
        contents.loadURL(`${protocolName}://host/cookie`);
        const [, error] = await response;
        expect(error).to.equal('Failed to set the \'cookie\' property on \'Document\': Access is denied for this document.');
      });
    });

    describe('can be accessed', () => {
      let server: http.Server;
      let serverUrl: string;
      let serverCrossSiteUrl: string;
      before(async () => {
        server = http.createServer((req, res) => {
          const respond = () => {
            if (req.url === '/redirect-cross-site') {
              res.setHeader('Location', `${serverCrossSiteUrl}/redirected`);
              res.statusCode = 302;
              res.end();
            } else if (req.url === '/redirected') {
              res.end('<html><script>window.localStorage</script></html>');
            } else {
              res.end();
            }
          };
          setTimeout().then(respond);
        });
        serverUrl = (await listen(server)).url;
        serverCrossSiteUrl = serverUrl.replace('127.0.0.1', 'localhost');
      });

      after(() => {
        server.close();
        server = null as any;
      });

      afterEach(closeAllWindows);

      const testLocalStorageAfterXSiteRedirect = (testTitle: string, extraPreferences = {}) => {
        it(testTitle, async () => {
          const w = new BrowserWindow({
            show: false,
            ...extraPreferences
          });
          let redirected = false;
          w.webContents.on('render-process-gone', () => {
            expect.fail('renderer crashed / was killed');
          });
          w.webContents.on('did-redirect-navigation', (event, url) => {
            expect(url).to.equal(`${serverCrossSiteUrl}/redirected`);
            redirected = true;
          });
          await w.loadURL(`${serverUrl}/redirect-cross-site`);
          expect(redirected).to.be.true('didnt redirect');
        });
      };

      testLocalStorageAfterXSiteRedirect('after a cross-site redirect');
      testLocalStorageAfterXSiteRedirect('after a cross-site redirect in sandbox mode', { sandbox: true });
    });

    describe('DOM storage quota increase', () => {
      for (const storageName of ['localStorage', 'sessionStorage']) {
        it(`allows saving at least 40MiB in ${storageName}`, async () => {
          const w = new BrowserWindow({ show: false });
          w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
          // Although JavaScript strings use UTF-16, the underlying
          // storage provider may encode strings differently, muddling the
          // translation between character and byte counts. However,
          // a string of 40 * 2^20 characters will require at least 40MiB
          // and presumably no more than 80MiB, a size guaranteed to
          // to exceed the original 10MiB quota yet stay within the
          // new 100MiB quota.
          // Note that both the key name and value affect the total size.
          const testKeyName = '_electronDOMStorageQuotaIncreasedTest';
          const length = 40 * Math.pow(2, 20) - testKeyName.length;
          await w.webContents.executeJavaScript(`
            ${storageName}.setItem(${JSON.stringify(testKeyName)}, 'X'.repeat(${length}));
          `);
          // Wait at least one turn of the event loop to help avoid false positives
          // Although not entirely necessary, the previous version of this test case
          // failed to detect a real problem (perhaps related to DOM storage data caching)
          // wherein calling `getItem` immediately after `setItem` would appear to work
          // but then later (e.g. next tick) it would not.
          await setTimeout(1);
          try {
            const storedLength = await w.webContents.executeJavaScript(`${storageName}.getItem(${JSON.stringify(testKeyName)}).length`);
            expect(storedLength).to.equal(length);
          } finally {
            await w.webContents.executeJavaScript(`${storageName}.removeItem(${JSON.stringify(testKeyName)});`);
          }
        });

        it(`throws when attempting to use more than 128MiB in ${storageName}`, async () => {
          const w = new BrowserWindow({ show: false });
          w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
          await expect((async () => {
            const testKeyName = '_electronDOMStorageQuotaStillEnforcedTest';
            const length = 128 * Math.pow(2, 20) - testKeyName.length;
            try {
              await w.webContents.executeJavaScript(`
                ${storageName}.setItem(${JSON.stringify(testKeyName)}, 'X'.repeat(${length}));
              `);
            } finally {
              await w.webContents.executeJavaScript(`${storageName}.removeItem(${JSON.stringify(testKeyName)});`);
            }
          })()).to.eventually.be.rejected();
        });
      }
    });

    describe('persistent storage', () => {
      it('can be requested', async () => {
        const w = new BrowserWindow({ show: false });
        w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
        const grantedBytes = await w.webContents.executeJavaScript(`new Promise(resolve => {
          navigator.webkitPersistentStorage.requestQuota(1024 * 1024, resolve);
        })`);
        expect(grantedBytes).to.equal(1048576);
      });
    });
  });

  ifdescribe(features.isPDFViewerEnabled())('PDF Viewer', () => {
    const pdfSource = url.format({
      pathname: path.join(__dirname, 'fixtures', 'cat.pdf').replaceAll('\\', '/'),
      protocol: 'file',
      slashes: true
    });

    it('successfully loads a PDF file', async () => {
      const w = new BrowserWindow({ show: false });

      w.loadURL(pdfSource);
      await once(w.webContents, 'did-finish-load');
    });

    it('opens when loading a pdf resource as top level navigation', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL(pdfSource);
      const [, contents] = await once(app, 'web-contents-created') as [any, WebContents];
      await once(contents, 'did-navigate');
      expect(contents.getURL()).to.equal('chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/index.html');
    });

    it('opens when loading a pdf resource in a iframe', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.join(__dirname, 'fixtures', 'pages', 'pdf-in-iframe.html'));
      const [, contents] = await once(app, 'web-contents-created') as [any, WebContents];
      await once(contents, 'did-navigate');
      expect(contents.getURL()).to.equal('chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/index.html');
    });
  });

  describe('window.history', () => {
    describe('window.history.pushState', () => {
      it('should push state after calling history.pushState() from the same url', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
        // History should have current page by now.
        expect(w.webContents.navigationHistory.length()).to.equal(1);

        const waitCommit = once(w.webContents, 'navigation-entry-committed');
        w.webContents.executeJavaScript('window.history.pushState({}, "")');
        await waitCommit;
        // Initial page + pushed state.
        expect(w.webContents.navigationHistory.length()).to.equal(2);
      });
    });

    describe('window.history.back', () => {
      it('should not allow sandboxed iframe to modify main frame state', async () => {
        const w = new BrowserWindow({ show: false });
        w.loadURL('data:text/html,<iframe sandbox="allow-scripts"></iframe>');
        await Promise.all([
          once(w.webContents, 'navigation-entry-committed'),
          once(w.webContents, 'did-frame-navigate'),
          once(w.webContents, 'did-navigate')
        ]);

        w.webContents.executeJavaScript('window.history.pushState(1, "")');
        await Promise.all([
          once(w.webContents, 'navigation-entry-committed'),
          once(w.webContents, 'did-navigate-in-page')
        ]);

        w.webContents.once('navigation-entry-committed' as any, () => {
          expect.fail('Unexpected navigation-entry-committed');
        });
        w.webContents.once('did-navigate-in-page', () => {
          expect.fail('Unexpected did-navigate-in-page');
        });
        await w.webContents.mainFrame.frames[0].executeJavaScript('window.history.back()');
        expect(await w.webContents.executeJavaScript('window.history.state')).to.equal(1);
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(1);
      });
    });
  });

  describe('chrome:// pages', () => {
    const urls = [
      'chrome://accessibility',
      'chrome://gpu',
      'chrome://media-internals',
      'chrome://tracing',
      'chrome://webrtc-internals',
      'chrome://process-internals'
    ];

    for (const url of urls) {
      describe(url, () => {
        it('loads the page successfully', async () => {
          const w = new BrowserWindow({ show: false });
          await w.loadURL(url);
          const host = url.substring('chrome://'.length);
          const pageExists = await w.webContents.executeJavaScript(
            `window.hasOwnProperty('chrome') && window.location.host === '${host}'`
          );
          expect(pageExists).to.be.true();
        });
      });
    }
  });

  describe('document.hasFocus', () => {
    it('has correct value when multiple windows are opened', async () => {
      const w1 = new BrowserWindow({ show: true });
      const w2 = new BrowserWindow({ show: true });
      const w3 = new BrowserWindow({ show: false });
      await w1.loadFile(path.join(__dirname, 'fixtures', 'blank.html'));
      await w2.loadFile(path.join(__dirname, 'fixtures', 'blank.html'));
      await w3.loadFile(path.join(__dirname, 'fixtures', 'blank.html'));
      expect(webContents.getFocusedWebContents()?.id).to.equal(w2.webContents.id);
      let focus = false;
      focus = await w1.webContents.executeJavaScript(
        'document.hasFocus()'
      );
      expect(focus).to.be.false();
      focus = await w2.webContents.executeJavaScript(
        'document.hasFocus()'
      );
      expect(focus).to.be.true();
      focus = await w3.webContents.executeJavaScript(
        'document.hasFocus()'
      );
      expect(focus).to.be.false();
    });
  });

  // https://developer.mozilla.org/en-US/docs/Web/API/NetworkInformation
  describe('navigator.connection', () => {
    it('returns the correct value', async () => {
      const w = new BrowserWindow({ show: false });

      w.webContents.session.enableNetworkEmulation({
        latency: 500,
        downloadThroughput: 6400,
        uploadThroughput: 6400
      });

      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const rtt = await w.webContents.executeJavaScript('navigator.connection.rtt');
      expect(rtt).to.be.a('number');

      const downlink = await w.webContents.executeJavaScript('navigator.connection.downlink');
      expect(downlink).to.be.a('number');

      const effectiveTypes = ['slow-2g', '2g', '3g', '4g'];
      const effectiveType = await w.webContents.executeJavaScript('navigator.connection.effectiveType');
      expect(effectiveTypes).to.include(effectiveType);
    });
  });

  describe('navigator.userAgentData', () => {
    // These tests are done on an http server because navigator.userAgentData
    // requires a secure context.
    let server: http.Server;
    let serverUrl: string;
    before(async () => {
      server = http.createServer((req, res) => {
        res.setHeader('Content-Type', 'text/html');
        res.end('');
      });
      serverUrl = (await listen(server)).url;
    });
    after(() => {
      server.close();
    });

    describe('is not empty', () => {
      it('by default', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadURL(serverUrl);
        const platform = await w.webContents.executeJavaScript('navigator.userAgentData.platform');
        expect(platform).not.to.be.empty();
      });

      it('when there is a session-wide UA override', async () => {
        const ses = session.fromPartition(`${Math.random()}`);
        ses.setUserAgent('foobar');
        const w = new BrowserWindow({ show: false, webPreferences: { session: ses } });
        await w.loadURL(serverUrl);
        const platform = await w.webContents.executeJavaScript('navigator.userAgentData.platform');
        expect(platform).not.to.be.empty();
      });

      it('when there is a WebContents-specific UA override', async () => {
        const w = new BrowserWindow({ show: false });
        w.webContents.setUserAgent('foo');
        await w.loadURL(serverUrl);
        const platform = await w.webContents.executeJavaScript('navigator.userAgentData.platform');
        expect(platform).not.to.be.empty();
      });

      it('when there is a WebContents-specific UA override at load time', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadURL(serverUrl, {
          userAgent: 'foo'
        });
        const platform = await w.webContents.executeJavaScript('navigator.userAgentData.platform');
        expect(platform).not.to.be.empty();
      });
    });

    describe('brand list', () => {
      it('contains chromium', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadURL(serverUrl);
        const brands = await w.webContents.executeJavaScript('navigator.userAgentData.brands');
        expect(brands.map((b: any) => b.brand)).to.include('Chromium');
      });
    });
  });

  describe('Badging API', () => {
    it('does not crash', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      await w.webContents.executeJavaScript('navigator.setAppBadge(42)');
      await w.webContents.executeJavaScript('navigator.setAppBadge()');
      await w.webContents.executeJavaScript('navigator.clearAppBadge()');
    });
  });

  describe('navigator.webkitGetUserMedia', () => {
    it('calls its callbacks', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      await w.webContents.executeJavaScript(`new Promise((resolve) => {
        navigator.webkitGetUserMedia({
          audio: true,
          video: false
        }, () => resolve(),
        () => resolve());
      })`);
    });
  });

  describe('navigator.language', () => {
    it('should not be empty', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');
      expect(await w.webContents.executeJavaScript('navigator.language')).to.not.equal('');
    });
  });

  describe('heap snapshot', () => {
    it('does not crash', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      await w.webContents.executeJavaScript('process._linkedBinding(\'electron_common_v8_util\').takeHeapSnapshot()');
    });
  });

  // This is intentionally disabled on arm macs: https://chromium-review.googlesource.com/c/chromium/src/+/4143761
  ifdescribe(process.platform === 'darwin' && process.arch !== 'arm64')('webgl', () => {
    it('can be gotten as context in canvas', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const canWebglContextBeCreated = await w.webContents.executeJavaScript(`
        document.createElement('canvas').getContext('webgl') != null;
      `);
      expect(canWebglContextBeCreated).to.be.true();
    });
  });

  describe('iframe', () => {
    it('does not have node integration', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const result = await w.webContents.executeJavaScript(`
        const iframe = document.createElement('iframe')
        iframe.src = './set-global.html';
        document.body.appendChild(iframe);
        new Promise(resolve => iframe.onload = e => resolve(iframe.contentWindow.test))
      `);
      expect(result).to.equal('undefined undefined undefined');
    });
  });

  describe('websockets', () => {
    it('has user agent', async () => {
      const server = http.createServer();
      const { port } = await listen(server);
      const wss = new ws.Server({ server: server });
      const finished = new Promise<string | undefined>((resolve, reject) => {
        wss.on('error', reject);
        wss.on('connection', (ws, upgradeReq) => {
          resolve(upgradeReq.headers['user-agent']);
        });
      });
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript(`
        new WebSocket('ws://127.0.0.1:${port}');
      `);
      expect(await finished).to.include('Electron');
    });
  });

  describe('fetch', () => {
    it('does not crash', async () => {
      const server = http.createServer((req, res) => {
        res.end('test');
      });
      defer(() => server.close());
      const { port } = await listen(server);
      const w = new BrowserWindow({ show: false });
      w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const x = await w.webContents.executeJavaScript(`
        fetch('http://127.0.0.1:${port}').then((res) => res.body.getReader())
          .then((reader) => {
            return reader.read().then((r) => {
              reader.cancel();
              return r.value;
            });
          })
      `);
      expect(x).to.deep.equal(new Uint8Array([116, 101, 115, 116]));
    });
  });

  describe('Promise', () => {
    before(() => {
      ipcMain.handle('ping', (e, arg) => arg);
    });
    after(() => {
      ipcMain.removeHandler('ping');
    });
    itremote('resolves correctly in Node.js calls', async () => {
      await new Promise<void>((resolve, reject) => {
        class XElement extends HTMLElement {}
        customElements.define('x-element', XElement);
        setImmediate(() => {
          let called = false;
          Promise.resolve().then(() => {
            if (called) resolve();
            else reject(new Error('wrong sequence'));
          });
          document.createElement('x-element');
          called = true;
        });
      });
    });

    itremote('resolves correctly in Electron calls', async () => {
      await new Promise<void>((resolve, reject) => {
        class YElement extends HTMLElement {}
        customElements.define('y-element', YElement);
        require('electron').ipcRenderer.invoke('ping').then(() => {
          let called = false;
          Promise.resolve().then(() => {
            if (called) resolve();
            else reject(new Error('wrong sequence'));
          });
          document.createElement('y-element');
          called = true;
        });
      });
    });
  });

  describe('synchronous prompts', () => {
    describe('window.alert(message, title)', () => {
      itremote('throws an exception when the arguments cannot be converted to strings', () => {
        expect(() => {
          window.alert({ toString: null });
        }).to.throw('Cannot convert object to primitive value');
      });

      it('shows a message box', async () => {
        const w = new BrowserWindow({ show: false });
        w.loadURL('about:blank');
        const p = once(w.webContents, '-run-dialog');
        w.webContents.executeJavaScript('alert("hello")', true);
        const [info] = await p;
        expect(info.frame).to.equal(w.webContents.mainFrame);
        expect(info.messageText).to.equal('hello');
        expect(info.dialogType).to.equal('alert');
      });

      it('does not crash if a webContents is destroyed while an alert is showing', async () => {
        const w = new BrowserWindow({ show: false });
        w.loadURL('about:blank');
        const p = once(w.webContents, '-run-dialog');
        w.webContents.executeJavaScript('alert("hello")', true);
        await p;
        w.webContents.close();
      });
    });

    describe('window.confirm(message, title)', () => {
      itremote('throws an exception when the arguments cannot be converted to strings', () => {
        expect(() => {
          (window.confirm as any)({ toString: null }, 'title');
        }).to.throw('Cannot convert object to primitive value');
      });

      it('shows a message box', async () => {
        const w = new BrowserWindow({ show: false });
        w.loadURL('about:blank');
        const p = once(w.webContents, '-run-dialog');
        const resultPromise = w.webContents.executeJavaScript('confirm("hello")', true);
        const [info, cb] = await p;
        expect(info.frame).to.equal(w.webContents.mainFrame);
        expect(info.messageText).to.equal('hello');
        expect(info.dialogType).to.equal('confirm');
        cb(true, '');
        const result = await resultPromise;
        expect(result).to.be.true();
      });
    });

    describe('safeDialogs web preference', () => {
      const originalShowMessageBox = dialog.showMessageBox;
      afterEach(() => {
        dialog.showMessageBox = originalShowMessageBox;
        if (protocol.isProtocolHandled('https')) protocol.unhandle('https');
        if (protocol.isProtocolHandled('file')) protocol.unhandle('file');
      });
      it('does not show the checkbox if not enabled', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { safeDialogs: false } });
        w.loadURL('about:blank');
        // 1. The first alert() doesn't show the safeDialogs message.
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: false });
        await w.webContents.executeJavaScript('alert("hi")');

        let recordedOpts: MessageBoxOptions | undefined;
        dialog.showMessageBox = (bw, opts?: MessageBoxOptions) => {
          recordedOpts = opts;
          return Promise.resolve({ response: 0, checkboxChecked: false });
        };
        await w.webContents.executeJavaScript('alert("hi")');
        expect(recordedOpts?.checkboxLabel).to.equal('');
      });

      it('is respected', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { safeDialogs: true } });
        w.loadURL('about:blank');
        // 1. The first alert() doesn't show the safeDialogs message.
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: false });
        await w.webContents.executeJavaScript('alert("hi")');

        // 2. The second alert() shows the message with a checkbox. Respond that we checked it.
        let recordedOpts: MessageBoxOptions | undefined;
        dialog.showMessageBox = (bw, opts?: MessageBoxOptions) => {
          recordedOpts = opts;
          return Promise.resolve({ response: 0, checkboxChecked: true });
        };
        await w.webContents.executeJavaScript('alert("hi")');
        expect(recordedOpts?.checkboxLabel).to.be.a('string').with.length.above(0);

        // 3. The third alert() shouldn't show a dialog.
        dialog.showMessageBox = () => Promise.reject(new Error('unexpected showMessageBox'));
        await w.webContents.executeJavaScript('alert("hi")');
      });

      it('shows the safeDialogMessage', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { safeDialogs: true, safeDialogsMessage: 'foo bar' } });
        w.loadURL('about:blank');
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: false });
        await w.webContents.executeJavaScript('alert("hi")');
        let recordedOpts: MessageBoxOptions | undefined;
        dialog.showMessageBox = (bw, opts?: MessageBoxOptions) => {
          recordedOpts = opts;
          return Promise.resolve({ response: 0, checkboxChecked: true });
        };
        await w.webContents.executeJavaScript('alert("hi")');
        expect(recordedOpts?.checkboxLabel).to.equal('foo bar');
      });

      it('has persistent state across navigations', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { safeDialogs: true } });
        w.loadURL('about:blank');
        // 1. The first alert() doesn't show the safeDialogs message.
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: false });
        await w.webContents.executeJavaScript('alert("hi")');

        // 2. The second alert() shows the message with a checkbox. Respond that we checked it.
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: true });
        await w.webContents.executeJavaScript('alert("hi")');

        // 3. The third alert() shouldn't show a dialog.
        dialog.showMessageBox = () => Promise.reject(new Error('unexpected showMessageBox'));
        await w.webContents.executeJavaScript('alert("hi")');

        // 4. After navigating to the same origin, message boxes should still be hidden.
        w.loadURL('about:blank');
        await w.webContents.executeJavaScript('alert("hi")');
      });

      it('is separated by origin', async () => {
        protocol.handle('https', () => new Response(''));
        const w = new BrowserWindow({ show: false, webPreferences: { safeDialogs: true } });
        w.loadURL('https://example1');
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: false });
        await w.webContents.executeJavaScript('alert("hi")');
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: true });
        await w.webContents.executeJavaScript('alert("hi")');
        dialog.showMessageBox = () => Promise.reject(new Error('unexpected showMessageBox'));
        await w.webContents.executeJavaScript('alert("hi")');

        // A different origin is allowed to show message boxes after navigation.
        w.loadURL('https://example2');
        let dialogWasShown = false;
        dialog.showMessageBox = () => {
          dialogWasShown = true;
          return Promise.resolve({ response: 0, checkboxChecked: false });
        };
        await w.webContents.executeJavaScript('alert("hi")');
        expect(dialogWasShown).to.be.true();

        // Navigating back to the first origin means alerts are blocked again.
        w.loadURL('https://example1');
        dialog.showMessageBox = () => Promise.reject(new Error('unexpected showMessageBox'));
        await w.webContents.executeJavaScript('alert("hi")');
      });

      it('treats different file: paths as different origins', async () => {
        protocol.handle('file', () => new Response(''));
        const w = new BrowserWindow({ show: false, webPreferences: { safeDialogs: true } });
        w.loadURL('file:///path/1');
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: false });
        await w.webContents.executeJavaScript('alert("hi")');
        dialog.showMessageBox = () => Promise.resolve({ response: 0, checkboxChecked: true });
        await w.webContents.executeJavaScript('alert("hi")');
        dialog.showMessageBox = () => Promise.reject(new Error('unexpected showMessageBox'));
        await w.webContents.executeJavaScript('alert("hi")');

        w.loadURL('file:///path/2');
        let dialogWasShown = false;
        dialog.showMessageBox = () => {
          dialogWasShown = true;
          return Promise.resolve({ response: 0, checkboxChecked: false });
        };
        await w.webContents.executeJavaScript('alert("hi")');
        expect(dialogWasShown).to.be.true();
      });
    });
    describe('disableDialogs web preference', () => {
      const originalShowMessageBox = dialog.showMessageBox;
      afterEach(() => {
        dialog.showMessageBox = originalShowMessageBox;
        if (protocol.isProtocolHandled('https')) protocol.unhandle('https');
      });
      it('is respected', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { disableDialogs: true } });
        w.loadURL('about:blank');
        dialog.showMessageBox = () => Promise.reject(new Error('unexpected message box'));
        await w.webContents.executeJavaScript('alert("hi")');
      });
    });
  });

  describe('window.history', () => {
    describe('window.history.go(offset)', () => {
      itremote('throws an exception when the argument cannot be converted to a string', () => {
        expect(() => {
          (window.history.go as any)({ toString: null });
        }).to.throw('Cannot convert object to primitive value');
      });
    });
  });

  describe('console functions', () => {
    itremote('should exist', () => {
      expect(console.log, 'log').to.be.a('function');
      expect(console.error, 'error').to.be.a('function');
      expect(console.warn, 'warn').to.be.a('function');
      expect(console.info, 'info').to.be.a('function');
      expect(console.debug, 'debug').to.be.a('function');
      expect(console.trace, 'trace').to.be.a('function');
      expect(console.time, 'time').to.be.a('function');
      expect(console.timeEnd, 'timeEnd').to.be.a('function');
    });
  });

  // FIXME(nornagon): this is broken on CI, it triggers:
  // [FATAL:speech_synthesis.mojom-shared.h(237)] The outgoing message will
  // trigger VALIDATION_ERROR_UNEXPECTED_NULL_POINTER at the receiving side
  // (null text in SpeechSynthesisUtterance struct).
  describe('SpeechSynthesis', () => {
    itremote('should emit lifecycle events', async () => {
      const sentence = `long sentence which will take at least a few seconds to
          utter so that it's possible to pause and resume before the end`;
      const utter = new SpeechSynthesisUtterance(sentence);
      // Create a dummy utterance so that speech synthesis state
      // is initialized for later calls.
      speechSynthesis.speak(new SpeechSynthesisUtterance());
      speechSynthesis.cancel();
      speechSynthesis.speak(utter);
      // paused state after speak()
      expect(speechSynthesis.paused).to.be.false();
      await new Promise((resolve) => { utter.onstart = resolve; });
      // paused state after start event
      expect(speechSynthesis.paused).to.be.false();

      speechSynthesis.pause();
      // paused state changes async, right before the pause event
      expect(speechSynthesis.paused).to.be.false();
      await new Promise((resolve) => { utter.onpause = resolve; });
      expect(speechSynthesis.paused).to.be.true();

      speechSynthesis.resume();
      await new Promise((resolve) => { utter.onresume = resolve; });
      // paused state after resume event
      expect(speechSynthesis.paused).to.be.false();

      await new Promise((resolve) => { utter.onend = resolve; });
    });
  });
});

describe('font fallback', () => {
  async function getRenderedFonts (html: string) {
    const w = new BrowserWindow({ show: false });
    try {
      await w.loadURL(`data:text/html,${html}`);
      w.webContents.debugger.attach();
      const sendCommand = (method: string, commandParams?: any) => w.webContents.debugger.sendCommand(method, commandParams);
      const { nodeId } = (await sendCommand('DOM.getDocument')).root.children[0];
      await sendCommand('CSS.enable');
      const { fonts } = await sendCommand('CSS.getPlatformFontsForNode', { nodeId });
      return fonts;
    } finally {
      w.close();
    }
  }

  it('should use Helvetica for sans-serif on Mac, and Arial on Windows and Linux', async () => {
    const html = '<body style="font-family: sans-serif">test</body>';
    const fonts = await getRenderedFonts(html);
    expect(fonts).to.be.an('array');
    expect(fonts).to.have.length(1);
    if (process.platform === 'win32') {
      expect(fonts[0].familyName).to.equal('Arial');
    } else if (process.platform === 'darwin') {
      expect(fonts[0].familyName).to.equal('Helvetica');
    } else if (process.platform === 'linux') {
      expect(fonts[0].familyName).to.equal('DejaVu Sans');
    } // I think this depends on the distro? We don't specify a default.
  });

  ifit(process.platform !== 'linux')('should fall back to Japanese font for sans-serif Japanese script', async function () {
    const html = `
    <html lang="ja-JP">
      <head>
        <meta charset="utf-8" />
      </head>
      <body style="font-family: sans-serif">test 智史</body>
    </html>
    `;
    const fonts = await getRenderedFonts(html);
    expect(fonts).to.be.an('array');
    expect(fonts).to.have.length(1);
    if (process.platform === 'win32') { expect(fonts[0].familyName).to.be.oneOf(['Meiryo', 'Yu Gothic']); } else if (process.platform === 'darwin') { expect(fonts[0].familyName).to.equal('Hiragino Kaku Gothic ProN'); }
  });
});

describe('iframe using HTML fullscreen API while window is OS-fullscreened', () => {
  const fullscreenChildHtml = fs.promises.readFile(
    path.join(fixturesPath, 'pages', 'fullscreen-oopif.html')
  );
  let w: BrowserWindow;
  let server: http.Server;
  let crossSiteUrl: string;

  beforeEach(async () => {
    server = http.createServer(async (_req, res) => {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.write(await fullscreenChildHtml);
      res.end();
    });

    const serverUrl = (await listen(server)).url;
    crossSiteUrl = serverUrl.replace('127.0.0.1', 'localhost');

    w = new BrowserWindow({
      show: true,
      fullscreen: true,
      webPreferences: {
        nodeIntegration: true,
        nodeIntegrationInSubFrames: true,
        contextIsolation: false
      }
    });
  });

  afterEach(async () => {
    await closeAllWindows();
    (w as any) = null;
    server.close();
  });

  ifit(process.platform !== 'darwin')('can fullscreen from out-of-process iframes (non-macOS)', async () => {
    const fullscreenChange = once(ipcMain, 'fullscreenChange');
    const html =
      `<iframe style="width: 0" frameborder=0 src="${crossSiteUrl}" allowfullscreen></iframe>`;
    w.loadURL(`data:text/html,${html}`);
    await fullscreenChange;

    const fullscreenWidth = await w.webContents.executeJavaScript(
      "document.querySelector('iframe').offsetWidth"
    );
    expect(fullscreenWidth > 0).to.be.true();

    await w.webContents.executeJavaScript(
      "document.querySelector('iframe').contentWindow.postMessage('exitFullscreen', '*')"
    );

    await setTimeout(500);

    const width = await w.webContents.executeJavaScript(
      "document.querySelector('iframe').offsetWidth"
    );
    expect(width).to.equal(0);
  });

  ifit(process.platform === 'darwin')('can fullscreen from out-of-process iframes (macOS)', async () => {
    await once(w, 'enter-full-screen');
    const fullscreenChange = once(ipcMain, 'fullscreenChange');
    const html =
      `<iframe style="width: 0" frameborder=0 src="${crossSiteUrl}" allowfullscreen></iframe>`;
    w.loadURL(`data:text/html,${html}`);
    await fullscreenChange;

    const fullscreenWidth = await w.webContents.executeJavaScript(
      "document.querySelector('iframe').offsetWidth"
    );
    expect(fullscreenWidth > 0).to.be.true();

    await w.webContents.executeJavaScript(
      "document.querySelector('iframe').contentWindow.postMessage('exitFullscreen', '*')"
    );
    await once(w.webContents, 'leave-html-full-screen');

    const width = await w.webContents.executeJavaScript(
      "document.querySelector('iframe').offsetWidth"
    );
    expect(width).to.equal(0);

    w.setFullScreen(false);
    await once(w, 'leave-full-screen');
  });

  // TODO(jkleinsc) fix this flaky test on WOA
  ifit(process.platform !== 'win32' || process.arch !== 'arm64')('can fullscreen from in-process iframes', async () => {
    if (process.platform === 'darwin') await once(w, 'enter-full-screen');

    const fullscreenChange = once(ipcMain, 'fullscreenChange');
    w.loadFile(path.join(fixturesPath, 'pages', 'fullscreen-ipif.html'));
    await fullscreenChange;

    const fullscreenWidth = await w.webContents.executeJavaScript(
      "document.querySelector('iframe').offsetWidth"
    );
    expect(fullscreenWidth > 0).to.true();

    await w.webContents.executeJavaScript('document.exitFullscreen()');
    const width = await w.webContents.executeJavaScript(
      "document.querySelector('iframe').offsetWidth"
    );
    expect(width).to.equal(0);
  });
});

describe('navigator.serial', () => {
  let w: BrowserWindow;
  before(async () => {
    w = new BrowserWindow({
      show: false
    });
    await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
  });

  const getPorts: any = () => {
    return w.webContents.executeJavaScript(`
      navigator.serial.requestPort().then(port => port.toString()).catch(err => err.toString());
    `, true);
  };

  const notFoundError = 'NotFoundError: Failed to execute \'requestPort\' on \'Serial\': No port selected by the user.';

  after(closeAllWindows);
  afterEach(() => {
    session.defaultSession.setPermissionCheckHandler(null);
    session.defaultSession.removeAllListeners('select-serial-port');
  });

  it('does not return a port if select-serial-port event is not defined', async () => {
    w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
    const port = await getPorts();
    expect(port).to.equal(notFoundError);
  });

  it('does not return a port when permission denied', async () => {
    w.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
      callback(portList[0].portId);
    });
    session.defaultSession.setPermissionCheckHandler(() => false);
    const port = await getPorts();
    expect(port).to.equal(notFoundError);
  });

  it('does not crash when select-serial-port is called with an invalid port', async () => {
    w.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
      callback('i-do-not-exist');
    });
    const port = await getPorts();
    expect(port).to.equal(notFoundError);
  });

  it('returns a port when select-serial-port event is defined', async () => {
    let havePorts = false;
    w.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
      if (portList.length > 0) {
        havePorts = true;
        callback(portList[0].portId);
      } else {
        callback('');
      }
    });
    const port = await getPorts();
    if (havePorts) {
      expect(port).to.equal('[object SerialPort]');
    } else {
      expect(port).to.equal(notFoundError);
    }
  });

  it('navigator.serial.getPorts() returns values', async () => {
    let havePorts = false;

    w.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
      if (portList.length > 0) {
        havePorts = true;
        callback(portList[0].portId);
      } else {
        callback('');
      }
    });
    await getPorts();
    if (havePorts) {
      const grantedPorts = await w.webContents.executeJavaScript('navigator.serial.getPorts()');
      expect(grantedPorts).to.not.be.empty();
    }
  });

  it('supports port.forget()', async () => {
    let forgottenPortFromEvent = {};
    let havePorts = false;

    w.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
      if (portList.length > 0) {
        havePorts = true;
        callback(portList[0].portId);
      } else {
        callback('');
      }
    });

    w.webContents.session.on('serial-port-revoked', (event, details) => {
      forgottenPortFromEvent = details.port;
    });

    await getPorts();
    if (havePorts) {
      const grantedPorts = await w.webContents.executeJavaScript('navigator.serial.getPorts()');
      if (grantedPorts.length > 0) {
        const forgottenPort = await w.webContents.executeJavaScript(`
          navigator.serial.getPorts().then(async(ports) => {
            const portInfo = await ports[0].getInfo();
            await ports[0].forget();
            if (portInfo.usbVendorId && portInfo.usbProductId) {
              return {
                vendorId: '' + portInfo.usbVendorId,
                productId: '' + portInfo.usbProductId
              }
            } else {
              return {};
            }
          })
        `);
        const grantedPorts2 = await w.webContents.executeJavaScript('navigator.serial.getPorts()');
        expect(grantedPorts2.length).to.be.lessThan(grantedPorts.length);
        if (forgottenPort.vendorId && forgottenPort.productId) {
          expect(forgottenPortFromEvent).to.include(forgottenPort);
        }
      }
    }
  });
});

describe('window.getScreenDetails', () => {
  let w: BrowserWindow;
  before(async () => {
    w = new BrowserWindow({
      show: false
    });
    await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
  });

  after(closeAllWindows);
  afterEach(() => {
    session.defaultSession.setPermissionRequestHandler(null);
  });

  const getScreenDetails: any = () => {
    return w.webContents.executeJavaScript('window.getScreenDetails().then(data => data.screens).catch(err => err.message)', true);
  };

  it('returns screens when a PermissionRequestHandler is not defined', async () => {
    const screens = await getScreenDetails();
    expect(screens).to.not.equal('Read permission denied.');
  });

  it('returns an error when permission denied', async () => {
    session.defaultSession.setPermissionRequestHandler((wc, permission, callback) => {
      if (permission === 'window-management') {
        callback(false);
      } else {
        callback(true);
      }
    });
    const screens = await getScreenDetails();
    expect(screens).to.equal('Permission denied.');
  });

  it('returns screens when permission is granted', async () => {
    session.defaultSession.setPermissionRequestHandler((wc, permission, callback) => {
      if (permission === 'window-management') {
        callback(true);
      } else {
        callback(false);
      }
    });
    const screens = await getScreenDetails();
    expect(screens).to.not.equal('Permission denied.');
  });
});

describe('navigator.clipboard.read', () => {
  let w: BrowserWindow;
  before(async () => {
    w = new BrowserWindow();
    await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
  });

  const readClipboard: any = () => {
    return w.webContents.executeJavaScript(`
      navigator.clipboard.read().then(clipboard => clipboard.toString()).catch(err => err.message);
    `, true);
  };

  after(closeAllWindows);
  afterEach(() => {
    session.defaultSession.setPermissionRequestHandler(null);
  });

  it('returns clipboard contents when a PermissionRequestHandler is not defined', async () => {
    const clipboard = await readClipboard();
    expect(clipboard).to.not.contain('Read permission denied.');
  });

  it('returns an error when permission denied', async () => {
    session.defaultSession.setPermissionRequestHandler((wc, permission, callback) => {
      if (permission === 'clipboard-read') {
        callback(false);
      } else {
        callback(true);
      }
    });
    const clipboard = await readClipboard();
    expect(clipboard).to.contain('Read permission denied.');
  });

  it('returns clipboard contents when permission is granted', async () => {
    session.defaultSession.setPermissionRequestHandler((wc, permission, callback) => {
      if (permission === 'clipboard-read') {
        callback(true);
      } else {
        callback(false);
      }
    });
    const clipboard = await readClipboard();
    expect(clipboard).to.not.contain('Read permission denied.');
  });
});

describe('navigator.clipboard.write', () => {
  let w: BrowserWindow;
  before(async () => {
    w = new BrowserWindow();
    await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
  });

  const writeClipboard: any = () => {
    return w.webContents.executeJavaScript(`
      navigator.clipboard.writeText('Hello World!').catch(err => err.message);
    `, true);
  };

  after(closeAllWindows);
  afterEach(() => {
    session.defaultSession.setPermissionRequestHandler(null);
  });

  it('returns clipboard contents when a PermissionRequestHandler is not defined', async () => {
    const clipboard = await writeClipboard();
    expect(clipboard).to.be.undefined();
  });

  it('returns an error when permission denied', async () => {
    session.defaultSession.setPermissionRequestHandler((wc, permission, callback) => {
      if (permission === 'clipboard-sanitized-write') {
        callback(false);
      } else {
        callback(true);
      }
    });
    const clipboard = await writeClipboard();
    expect(clipboard).to.contain('Write permission denied.');
  });

  it('returns clipboard contents when permission is granted', async () => {
    session.defaultSession.setPermissionRequestHandler((wc, permission, callback) => {
      if (permission === 'clipboard-sanitized-write') {
        callback(true);
      } else {
        callback(false);
      }
    });
    const clipboard = await writeClipboard();
    expect(clipboard).to.be.undefined();
  });
});

ifdescribe((process.platform !== 'linux' || app.isUnityRunning()))('navigator.setAppBadge/clearAppBadge', () => {
  let w: BrowserWindow;

  const expectedBadgeCount = 42;

  const fireAppBadgeAction: any = (action: string, value: any) => {
    return w.webContents.executeJavaScript(`
      navigator.${action}AppBadge(${value}).then(() => 'success').catch(err => err.message)`);
  };

  // For some reason on macOS changing the badge count doesn't happen right away, so wait
  // until it changes.
  async function waitForBadgeCount (value: number) {
    let badgeCount = app.getBadgeCount();
    while (badgeCount !== value) {
      await setTimeout(10);
      badgeCount = app.getBadgeCount();
    }
    return badgeCount;
  }

  describe('in the renderer', () => {
    before(async () => {
      w = new BrowserWindow({
        show: false
      });
      await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
    });

    after(() => {
      app.badgeCount = 0;
      closeAllWindows();
    });

    it('setAppBadge can set a numerical value', async () => {
      const result = await fireAppBadgeAction('set', expectedBadgeCount);
      expect(result).to.equal('success');
      expect(waitForBadgeCount(expectedBadgeCount)).to.eventually.equal(expectedBadgeCount);
    });

    it('setAppBadge can set an empty(dot) value', async () => {
      const result = await fireAppBadgeAction('set');
      expect(result).to.equal('success');
      expect(waitForBadgeCount(0)).to.eventually.equal(0);
    });

    it('clearAppBadge can clear a value', async () => {
      let result = await fireAppBadgeAction('set', expectedBadgeCount);
      expect(result).to.equal('success');
      expect(waitForBadgeCount(expectedBadgeCount)).to.eventually.equal(expectedBadgeCount);
      result = await fireAppBadgeAction('clear');
      expect(result).to.equal('success');
      expect(waitForBadgeCount(0)).to.eventually.equal(0);
    });
  });

  describe('in a service worker', () => {
    beforeEach(async () => {
      w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          partition: 'sw-file-scheme-spec',
          contextIsolation: false
        }
      });
    });

    afterEach(() => {
      app.badgeCount = 0;
      closeAllWindows();
    });

    it('setAppBadge can be called in a ServiceWorker', (done) => {
      w.webContents.on('ipc-message', (event, channel, message) => {
        if (channel === 'reload') {
          w.webContents.reload();
        } else if (channel === 'error') {
          done(message);
        } else if (channel === 'response') {
          expect(message).to.equal('SUCCESS setting app badge');
          expect(waitForBadgeCount(expectedBadgeCount)).to.eventually.equal(expectedBadgeCount);
          session.fromPartition('sw-file-scheme-spec').clearStorageData({
            storages: ['serviceworkers']
          }).then(() => done());
        }
      });
      w.webContents.on('render-process-gone', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'service-worker', 'badge-index.html'), { search: '?setBadge' });
    });

    it('clearAppBadge can be called in a ServiceWorker', (done) => {
      w.webContents.on('ipc-message', (event, channel, message) => {
        if (channel === 'reload') {
          w.webContents.reload();
        } else if (channel === 'setAppBadge') {
          expect(message).to.equal('SUCCESS setting app badge');
          expect(waitForBadgeCount(expectedBadgeCount)).to.eventually.equal(expectedBadgeCount);
        } else if (channel === 'error') {
          done(message);
        } else if (channel === 'response') {
          expect(message).to.equal('SUCCESS clearing app badge');
          expect(waitForBadgeCount(expectedBadgeCount)).to.eventually.equal(expectedBadgeCount);
          session.fromPartition('sw-file-scheme-spec').clearStorageData({
            storages: ['serviceworkers']
          }).then(() => done());
        }
      });
      w.webContents.on('render-process-gone', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'service-worker', 'badge-index.html'), { search: '?clearBadge' });
    });
  });
});

describe('navigator.bluetooth', () => {
  let w: BrowserWindow;
  before(async () => {
    w = new BrowserWindow({
      show: false,
      webPreferences: {
        enableBlinkFeatures: 'WebBluetooth'
      }
    });
    await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
  });

  after(closeAllWindows);

  it('can request bluetooth devices', async () => {
    const bluetooth = await w.webContents.executeJavaScript(`
    navigator.bluetooth.requestDevice({ acceptAllDevices: true}).then(device => "Found a device!").catch(err => err.message);`, true);
    expect(bluetooth).to.be.oneOf(['Found a device!', 'Bluetooth adapter not available.', 'User cancelled the requestDevice() chooser.']);
  });
});

describe('navigator.hid', () => {
  let w: BrowserWindow;
  let server: http.Server;
  let serverUrl: string;
  before(async () => {
    w = new BrowserWindow({
      show: false
    });
    await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
    server = http.createServer((req, res) => {
      res.setHeader('Content-Type', 'text/html');
      res.end('<body>');
    });
    serverUrl = (await listen(server)).url;
  });

  const requestDevices: any = () => {
    return w.webContents.executeJavaScript(`
      navigator.hid.requestDevice({filters: []}).then(device => device.toString()).catch(err => err.toString());
    `, true);
  };

  after(() => {
    server.close();
    closeAllWindows();
  });
  afterEach(() => {
    session.defaultSession.setPermissionCheckHandler(null);
    session.defaultSession.setDevicePermissionHandler(null);
    session.defaultSession.removeAllListeners('select-hid-device');
  });

  it('does not return a device if select-hid-device event is not defined', async () => {
    w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
    const device = await requestDevices();
    expect(device).to.equal('');
  });

  it('does not return a device when permission denied', async () => {
    let selectFired = false;
    w.webContents.session.on('select-hid-device', (event, details, callback) => {
      selectFired = true;
      callback();
    });
    session.defaultSession.setPermissionCheckHandler(() => false);
    const device = await requestDevices();
    expect(selectFired).to.be.false();
    expect(device).to.equal('');
  });

  it('returns a device when select-hid-device event is defined', async () => {
    let haveDevices = false;
    let selectFired = false;
    w.webContents.session.on('select-hid-device', (event, details, callback) => {
      expect(details.frame).to.have.property('frameTreeNodeId').that.is.a('number');
      selectFired = true;
      if (details.deviceList.length > 0) {
        haveDevices = true;
        callback(details.deviceList[0].deviceId);
      } else {
        callback();
      }
    });
    const device = await requestDevices();
    expect(selectFired).to.be.true();
    if (haveDevices) {
      expect(device).to.contain('[object HIDDevice]');
    } else {
      expect(device).to.equal('');
    }
    if (haveDevices) {
      // Verify that navigation will clear device permissions
      const grantedDevices = await w.webContents.executeJavaScript('navigator.hid.getDevices()');
      expect(grantedDevices).to.not.be.empty();
      w.loadURL(serverUrl);
      const [,,,,, frameProcessId, frameRoutingId] = await once(w.webContents, 'did-frame-navigate');
      const frame = webFrameMain.fromId(frameProcessId, frameRoutingId);
      expect(!!frame).to.be.true();
      if (frame) {
        const grantedDevicesOnNewPage = await frame.executeJavaScript('navigator.hid.getDevices()');
        expect(grantedDevicesOnNewPage).to.be.empty();
      }
    }
  });

  it('returns a device when DevicePermissionHandler is defined', async () => {
    let haveDevices = false;
    let selectFired = false;
    let gotDevicePerms = false;
    w.webContents.session.on('select-hid-device', (event, details, callback) => {
      selectFired = true;
      if (details.deviceList.length > 0) {
        const foundDevice = details.deviceList.find((device) => {
          if (device.name && device.name !== '' && device.serialNumber && device.serialNumber !== '') {
            haveDevices = true;
            return true;
          }
        });
        if (foundDevice) {
          callback(foundDevice.deviceId);
          return;
        }
      }
      callback();
    });
    session.defaultSession.setDevicePermissionHandler(() => {
      gotDevicePerms = true;
      return true;
    });
    await w.webContents.executeJavaScript('navigator.hid.getDevices();', true);
    const device = await requestDevices();
    expect(selectFired).to.be.true();
    if (haveDevices) {
      expect(device).to.contain('[object HIDDevice]');
      expect(gotDevicePerms).to.be.true();
    } else {
      expect(device).to.equal('');
    }
  });

  it('excludes a device when a exclusionFilter is specified', async () => {
    const exclusionFilters = <any>[];
    let haveDevices = false;
    let checkForExcludedDevice = false;

    w.webContents.session.on('select-hid-device', (event, details, callback) => {
      if (details.deviceList.length > 0) {
        details.deviceList.find((device) => {
          if (device.name && device.name !== '' && device.serialNumber && device.serialNumber !== '') {
            if (checkForExcludedDevice) {
              const compareDevice = {
                vendorId: device.vendorId,
                productId: device.productId
              };
              expect(compareDevice).to.not.equal(exclusionFilters[0], 'excluded device should not be returned');
            } else {
              haveDevices = true;
              exclusionFilters.push({
                vendorId: device.vendorId,
                productId: device.productId
              });
              return true;
            }
          }
        });
      }
      callback();
    });

    await requestDevices();
    if (haveDevices) {
      // We have devices to exclude, so check if exclusionFilters work
      checkForExcludedDevice = true;
      await w.webContents.executeJavaScript(`
        navigator.hid.requestDevice({filters: [], exclusionFilters: ${JSON.stringify(exclusionFilters)}}).then(device => device.toString()).catch(err => err.toString());

      `, true);
    }
  });

  it('supports device.forget()', async () => {
    let deletedDeviceFromEvent;
    let haveDevices = false;
    w.webContents.session.on('select-hid-device', (event, details, callback) => {
      if (details.deviceList.length > 0) {
        haveDevices = true;
        callback(details.deviceList[0].deviceId);
      } else {
        callback();
      }
    });
    w.webContents.session.on('hid-device-revoked', (event, details) => {
      deletedDeviceFromEvent = details.device;
    });
    await requestDevices();
    if (haveDevices) {
      const grantedDevices = await w.webContents.executeJavaScript('navigator.hid.getDevices()');
      if (grantedDevices.length > 0) {
        const deletedDevice = await w.webContents.executeJavaScript(`
          navigator.hid.getDevices().then(devices => {
            devices[0].forget();
            return {
              vendorId: devices[0].vendorId,
              productId: devices[0].productId,
              name: devices[0].productName
            }
          })
        `);
        const grantedDevices2 = await w.webContents.executeJavaScript('navigator.hid.getDevices()');
        expect(grantedDevices2.length).to.be.lessThan(grantedDevices.length);
        if (deletedDevice.name !== '' && deletedDevice.productId && deletedDevice.vendorId) {
          expect(deletedDeviceFromEvent).to.include(deletedDevice);
        }
      }
    }
  });
});

describe('navigator.usb', () => {
  let w: BrowserWindow;
  let server: http.Server;
  let serverUrl: string;
  before(async () => {
    w = new BrowserWindow({
      show: false
    });
    await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
    server = http.createServer((req, res) => {
      res.setHeader('Content-Type', 'text/html');
      res.end('<body>');
    });
    serverUrl = (await listen(server)).url;
  });

  const requestDevices: any = () => {
    return w.webContents.executeJavaScript(`
      navigator.usb.requestDevice({filters: []}).then(device => device.toString()).catch(err => err.toString());
    `, true);
  };

  const getDevices: any = () => {
    return w.webContents.executeJavaScript(`
      navigator.usb.getDevices().then(devices => devices.map(device => device.toString())).catch(err => err.toString());
    `, true);
  };

  const notFoundError = 'NotFoundError: Failed to execute \'requestDevice\' on \'USB\': No device selected.';

  after(() => {
    server.close();
    closeAllWindows();
  });

  afterEach(() => {
    session.defaultSession.setPermissionCheckHandler(null);
    session.defaultSession.setDevicePermissionHandler(null);
    session.defaultSession.removeAllListeners('select-usb-device');
  });

  it('does not crash when using in-memory partitions', async () => {
    const sesWin = new BrowserWindow({
      webPreferences: {
        partition: 'test-partition'
      }
    });

    await sesWin.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
    server = http.createServer((req, res) => {
      res.setHeader('Content-Type', 'text/html');
      res.end('<body>');
    });

    serverUrl = (await listen(server)).url;

    const devices = await getDevices();
    expect(devices).to.be.an('array').that.is.empty();
  });

  it('does not return a device if select-usb-device event is not defined', async () => {
    w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
    const device = await requestDevices();
    expect(device).to.equal(notFoundError);
  });

  it('does not return a device when permission denied', async () => {
    let selectFired = false;
    w.webContents.session.on('select-usb-device', (event, details, callback) => {
      selectFired = true;
      callback();
    });
    session.defaultSession.setPermissionCheckHandler(() => false);
    const device = await requestDevices();
    expect(selectFired).to.be.false();
    expect(device).to.equal(notFoundError);
  });

  it('returns a device when select-usb-device event is defined', async () => {
    let haveDevices = false;
    let selectFired = false;
    w.webContents.session.on('select-usb-device', (event, details, callback) => {
      expect(details.frame).to.have.property('frameTreeNodeId').that.is.a('number');
      selectFired = true;
      if (details.deviceList.length > 0) {
        haveDevices = true;
        callback(details.deviceList[0].deviceId);
      } else {
        callback();
      }
    });
    const device = await requestDevices();
    expect(selectFired).to.be.true();
    if (haveDevices) {
      expect(device).to.contain('[object USBDevice]');
    } else {
      expect(device).to.equal(notFoundError);
    }
    if (haveDevices) {
      // Verify that navigation will clear device permissions
      const grantedDevices = await w.webContents.executeJavaScript('navigator.usb.getDevices()');
      expect(grantedDevices).to.not.be.empty();
      w.loadURL(serverUrl);
      const [,,,,, frameProcessId, frameRoutingId] = await once(w.webContents, 'did-frame-navigate');
      const frame = webFrameMain.fromId(frameProcessId, frameRoutingId);
      expect(!!frame).to.be.true();
      if (frame) {
        const grantedDevicesOnNewPage = await frame.executeJavaScript('navigator.usb.getDevices()');
        expect(grantedDevicesOnNewPage).to.be.empty();
      }
    }
  });

  it('returns a device when DevicePermissionHandler is defined', async () => {
    let haveDevices = false;
    let selectFired = false;
    let gotDevicePerms = false;
    w.webContents.session.on('select-usb-device', (event, details, callback) => {
      selectFired = true;
      if (details.deviceList.length > 0) {
        const foundDevice = details.deviceList.find((device) => {
          if (device.productName && device.productName !== '' && device.serialNumber && device.serialNumber !== '') {
            haveDevices = true;
            return true;
          }
        });
        if (foundDevice) {
          callback(foundDevice.deviceId);
          return;
        }
      }
      callback();
    });
    session.defaultSession.setDevicePermissionHandler(() => {
      gotDevicePerms = true;
      return true;
    });
    await w.webContents.executeJavaScript('navigator.usb.getDevices();', true);
    const device = await requestDevices();
    expect(selectFired).to.be.true();
    if (haveDevices) {
      expect(device).to.contain('[object USBDevice]');
      expect(gotDevicePerms).to.be.true();
    } else {
      expect(device).to.equal(notFoundError);
    }
  });

  it('supports device.forget()', async () => {
    let deletedDeviceFromEvent;
    let haveDevices = false;
    w.webContents.session.on('select-usb-device', (event, details, callback) => {
      if (details.deviceList.length > 0) {
        haveDevices = true;
        callback(details.deviceList[0].deviceId);
      } else {
        callback();
      }
    });
    w.webContents.session.on('usb-device-revoked', (event, details) => {
      deletedDeviceFromEvent = details.device;
    });
    await requestDevices();
    if (haveDevices) {
      const grantedDevices = await w.webContents.executeJavaScript('navigator.usb.getDevices()');
      if (grantedDevices.length > 0) {
        const deletedDevice: Electron.USBDevice = await w.webContents.executeJavaScript(`
          navigator.usb.getDevices().then(devices => {
            devices[0].forget();
            return {
              vendorId: devices[0].vendorId,
              productId: devices[0].productId,
              productName: devices[0].productName
            }
          })
        `);
        const grantedDevices2 = await w.webContents.executeJavaScript('navigator.usb.getDevices()');
        expect(grantedDevices2.length).to.be.lessThan(grantedDevices.length);
        if (deletedDevice.productName !== '' && deletedDevice.productId && deletedDevice.vendorId) {
          expect(deletedDeviceFromEvent).to.include(deletedDevice);
        }
      }
    }
  });
});
