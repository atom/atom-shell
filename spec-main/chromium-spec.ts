import { expect } from 'chai';
import { BrowserWindow, WebContents, webFrameMain, session, ipcMain, app, protocol, webContents } from 'electron/main';
import { emittedOnce } from './events-helpers';
import { closeAllWindows } from './window-helpers';
import * as https from 'https';
import * as http from 'http';
import * as path from 'path';
import * as fs from 'fs';
import * as url from 'url';
import * as ChildProcess from 'child_process';
import { EventEmitter } from 'events';
import { promisify } from 'util';
import { ifit, ifdescribe, delay, defer } from './spec-helpers';
import { AddressInfo } from 'net';
import { PipeTransport } from './pipe-transport';
const timersPromises = require('timers/promises');

const features = process._linkedBinding('electron_common_features');

const fixturesPath = path.resolve(__dirname, '..', 'spec', 'fixtures');

describe('reporting api', () => {
  // TODO(nornagon): this started failing a lot on CI. Figure out why and fix
  // it.
  it.skip('sends a report for a deprecation', async () => {
    const reports = new EventEmitter();

    // The Reporting API only works on https with valid certs. To dodge having
    // to set up a trusted certificate, hack the validator.
    session.defaultSession.setCertificateVerifyProc((req, cb) => {
      cb(0);
    });
    const certPath = path.join(fixturesPath, 'certificates');
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
      if (req.url === '/report') {
        let data = '';
        req.on('data', (d) => { data += d.toString('utf-8'); });
        req.on('end', () => {
          reports.emit('report', JSON.parse(data));
        });
      }
      res.setHeader('Report-To', JSON.stringify({
        group: 'default',
        max_age: 120,
        endpoints: [{ url: `https://localhost:${(server.address() as any).port}/report` }]
      }));
      res.setHeader('Content-Type', 'text/html');
      // using the deprecated `webkitRequestAnimationFrame` will trigger a
      // "deprecation" report.
      res.end('<script>webkitRequestAnimationFrame(() => {})</script>');
    });
    await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
    const bw = new BrowserWindow({
      show: false
    });
    try {
      const reportGenerated = emittedOnce(reports, 'report');
      const url = `https://localhost:${(server.address() as any).port}/a`;
      await bw.loadURL(url);
      const [report] = await reportGenerated;
      expect(report).to.be.an('array');
      expect(report[0].type).to.equal('deprecation');
      expect(report[0].url).to.equal(url);
      expect(report[0].body.id).to.equal('PrefixedRequestAnimationFrame');
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
    const [, message] = await emittedOnce(ipcMain, 'complete');
    expect(message.data).to.equal('testing');
    expect(message.origin).to.equal('file://');
    expect(message.sourceEqualsOpener).to.equal(true);
    expect(message.eventOrigin).to.equal('file://');
  });
});

describe('focus handling', () => {
  let webviewContents: WebContents = null as unknown as WebContents;
  let w: BrowserWindow = null as unknown as BrowserWindow;

  beforeEach(async () => {
    w = new BrowserWindow({
      show: true,
      webPreferences: {
        nodeIntegration: true,
        webviewTag: true,
        contextIsolation: false
      }
    });

    const webviewReady = emittedOnce(w.webContents, 'did-attach-webview');
    await w.loadFile(path.join(fixturesPath, 'pages', 'tab-focus-loop-elements.html'));
    const [, wvContents] = await webviewReady;
    webviewContents = wvContents;
    await emittedOnce(webviewContents, 'did-finish-load');
    w.focus();
  });

  afterEach(() => {
    webviewContents = null as unknown as WebContents;
    w.destroy();
    w = null as unknown as BrowserWindow;
  });

  const expectFocusChange = async () => {
    const [, focusedElementId] = await emittedOnce(ipcMain, 'focus-changed');
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
    await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
    serverUrl = `http://localhost:${(server.address() as any).port}`;
  });
  after(() => {
    server.close();
  });

  it('engages CORB when web security is not disabled', async () => {
    const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: true, nodeIntegration: true, contextIsolation: false } });
    const p = emittedOnce(ipcMain, 'success');
    await w.loadURL(`data:text/html,<script>
        const s = document.createElement('script')
        s.src = "${serverUrl}"
        // The script will load successfully but its body will be emptied out
        // by CORB, so we don't expect a syntax error.
        s.onload = () => { require('electron').ipcRenderer.send('success') }
        document.documentElement.appendChild(s)
      </script>`);
    await p;
  });

  it('bypasses CORB when web security is disabled', async () => {
    const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: false, nodeIntegration: true, contextIsolation: false } });
    const p = emittedOnce(ipcMain, 'success');
    await w.loadURL(`data:text/html,
      <script>
        window.onerror = (e) => { require('electron').ipcRenderer.send('success', e) }
      </script>
      <script src="${serverUrl}"></script>`);
    await p;
  });

  it('engages CORS when web security is not disabled', async () => {
    const w = new BrowserWindow({ show: false, webPreferences: { webSecurity: true, nodeIntegration: true, contextIsolation: false } });
    const p = emittedOnce(ipcMain, 'response');
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
    const p = emittedOnce(ipcMain, 'response');
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
        pathname: __filename.replace(/\\/g, '/'),
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
      expect(r).to.equal('WebAssembly.instantiate(): Wasm code generation disallowed by embedder');
    });

    it('wasm codegen is allowed with "wasm-unsafe-eval" csp', async () => {
      const r = await loadWasm("'wasm-unsafe-eval'");
      expect(r).to.equal('loaded');
    });
  });

  it('does not crash when multiple WebContent are created with web security disabled', () => {
    const options = { show: false, webPreferences: { webSecurity: false } };
    const w1 = new BrowserWindow(options);
    w1.loadURL(serverUrl);
    const w2 = new BrowserWindow(options);
    w2.loadURL(serverUrl);
  });
});

ifdescribe(process.platform === 'linux' && !process.arch.includes('arm'))('subprocesses', () => {
  let appProcess: ChildProcess.ChildProcessWithoutNullStreams | undefined;
  const appPath = path.join(fixturesPath, 'api', 'gdk-backend-check');
  beforeEach(() => {
    ChildProcess.execFileSync(path.join(appPath, 'before-test.sh'), { cwd: appPath, encoding: 'utf8' });
  });
  afterEach(() => {
    ChildProcess.execFileSync(path.join(appPath, 'after-test.sh'), { cwd: appPath, encoding: 'utf8' });
    if (appProcess && !appProcess.killed) {
      appProcess.kill();
      appProcess = undefined;
    }
  });

  it('does not propagate GDK_BACKEND', async () => {
    appProcess = ChildProcess.spawn(process.execPath, [path.join(appPath, 'main.js')], { env: { ...process.env, GDK_BACKEND: '' } });

    let output = '';
    appProcess.stdout.on('data', (data) => { output += data; });
    let stderr = '';
    appProcess.stderr.on('data', (data) => { stderr += data; });

    const [code, signal] = await emittedOnce(appProcess, 'exit');
    if (code !== 0) {
      throw new Error(`Process exited with code "${code}" signal "${signal}" output "${output}" stderr "${stderr}"`);
    }

    for await (const startTime of timersPromises.setInterval(10, Date.now())) {
      if (fs.existsSync('/tmp/groot-says')) {
        let gdkBackend = fs.readFileSync('/tmp/groot-says', 'utf8');
        gdkBackend = gdkBackend.trim();

        expect(gdkBackend).to.be.empty();
        break;
      }
      const now = Date.now();
      if ((now - startTime) > 5000) {
        throw new Error('The gdk backend test failed timing out on waiting for the file');
      }
    }
  });

  it('successfully honors GDK_BACKEND set in the subproc', async () => {
    appProcess = ChildProcess.spawn(process.execPath, [path.join(appPath, 'main.js')], { env: { ...process.env, GDK_BACKEND: 'groot' } });

    let output = '';
    appProcess.stdout.on('data', (data) => { output += data; });
    let stderr = '';
    appProcess.stderr.on('data', (data) => { stderr += data; });

    const [code, signal] = await emittedOnce(appProcess, 'exit');
    if (code !== 0) {
      throw new Error(`Process exited with code "${code}" signal "${signal}" output "${output}" stderr "${stderr}"`);
    }

    for await (const startTime of timersPromises.setInterval(10, Date.now())) {
      if (fs.existsSync('/tmp/groot-says')) {
        let gdkBackend = fs.readFileSync('/tmp/groot-says', 'utf8');
        gdkBackend = gdkBackend.trim();

        expect(gdkBackend).to.not.be.empty();
        expect(gdkBackend).to.equal('groot');
        break;
      }
      const now = Date.now();
      if ((now - startTime) > 5000) {
        throw new Error('The gdk backend test failed timing out on waiting for the file');
      }
    }
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

      const [code, signal] = await emittedOnce(appProcess, 'exit');
      if (code !== 0) {
        throw new Error(`Process exited with code "${code}" signal "${signal}" output "${output}" stderr "${stderr}"`);
      }

      output = output.replace(/(\r\n|\n|\r)/gm, '');
      expect(output).to.equal(result);
    };

    it('should set the locale', async () => testLocale('fr', 'fr'));
    it('should not set an invalid locale', async () => testLocale('asdfkl', currentLocale));

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
      await new Promise(resolve => { appProcess!.on('exit', resolve); });
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
      w.webContents.once('crashed', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'external-string.html'));
    });
  });

  describe('loading jquery', () => {
    it('does not crash', (done) => {
      const w = new BrowserWindow({ show: false });
      w.webContents.once('did-finish-load', () => { done(); });
      w.webContents.once('crashed', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(__dirname, 'fixtures', 'pages', 'jquery.html'));
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
      w.webContents.on('crashed', () => done(new Error('WebContents crashed.')));
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
      w.webContents.on('crashed', () => done(new Error('WebContents crashed.')));
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
      w.webContents.on('crashed', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'service-worker', 'custom-scheme-index.html'));
    });

    it('should not crash when nodeIntegration is enabled', (done) => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          nodeIntegrationInWorker: true,
          partition: 'sw-file-scheme-worker-spec',
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
          session.fromPartition('sw-file-scheme-worker-spec').clearStorageData({
            storages: ['serviceworkers']
          }).then(() => done());
        }
      });

      w.webContents.on('crashed', () => done(new Error('WebContents crashed.')));
      w.loadFile(path.join(fixturesPath, 'pages', 'service-worker', 'index.html'));
    });
  });

  describe('navigator.geolocation', () => {
    before(function () {
      if (!features.isFakeLocationProviderEnabled()) {
        return this.skip();
      }
    });

    it('returns error when permission is denied', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          partition: 'geolocation-spec',
          contextIsolation: false
        }
      });
      const message = emittedOnce(w.webContents, 'ipc-message');
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

      const [code] = await emittedOnce(appProcess, 'exit');
      expect(code).to.equal(0);
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
      await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
      serverUrl = `http://localhost:${(server.address() as any).port}`;
    });
    after(async () => {
      server.close();
      await closeAllWindows();
    });

    [true, false].forEach((isSandboxEnabled) =>
      describe(`sandbox=${isSandboxEnabled}`, () => {
        it('posts data in the same window', async () => {
          const w = new BrowserWindow({
            show: false,
            webPreferences: {
              sandbox: isSandboxEnabled
            }
          });

          await w.loadFile(path.join(fixturesPath, 'pages', 'form-with-data.html'));

          const loadPromise = emittedOnce(w.webContents, 'did-finish-load');

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

          const windowCreatedPromise = emittedOnce(app, 'browser-window-created');

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
      })
    );
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

        const newWindow = emittedOnce(w.webContents, 'new-window');
        w.loadFile(path.join(fixturesPath, 'pages', 'window-open.html'));
        const [,,,, options] = await newWindow;
        expect(options.show).to.equal(true);
      });
    }

    it('disables node integration when it is disabled on the parent window for chrome devtools URLs', async () => {
      // NB. webSecurity is disabled because native window.open() is not
      // allowed to load devtools:// URLs.
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, webSecurity: false } });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript(`
        { b = window.open('devtools://devtools/bundled/inspector.html', '', 'nodeIntegration=no,show=no'); null }
      `);
      const [, contents] = await emittedOnce(app, 'web-contents-created');
      const typeofProcessGlobal = await contents.executeJavaScript('typeof process');
      expect(typeofProcessGlobal).to.equal('undefined');
    });

    it('can disable node integration when it is enabled on the parent window', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript(`
        { b = window.open('about:blank', '', 'nodeIntegration=no,show=no'); null }
      `);
      const [, contents] = await emittedOnce(app, 'web-contents-created');
      const typeofProcessGlobal = await contents.executeJavaScript('typeof process');
      expect(typeofProcessGlobal).to.equal('undefined');
    });

    // TODO(jkleinsc) fix this flaky test on WOA
    ifit(process.platform !== 'win32' || process.arch !== 'arm64')('disables JavaScript when it is disabled on the parent window', async () => {
      const w = new BrowserWindow({ show: true, webPreferences: { nodeIntegration: true } });
      w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      const windowUrl = require('url').format({
        pathname: `${fixturesPath}/pages/window-no-javascript.html`,
        protocol: 'file',
        slashes: true
      });
      w.webContents.executeJavaScript(`
        { b = window.open(${JSON.stringify(windowUrl)}, '', 'javascript=no,show=no'); null }
      `);
      const [, contents] = await emittedOnce(app, 'web-contents-created');
      await emittedOnce(contents, 'did-finish-load');
      // Click link on page
      contents.sendInputEvent({ type: 'mouseDown', clickCount: 1, x: 1, y: 1 });
      contents.sendInputEvent({ type: 'mouseUp', clickCount: 1, x: 1, y: 1 });
      const [, window] = await emittedOnce(app, 'browser-window-created');
      const preferences = window.webContents.getLastWebPreferences();
      expect(preferences.javascript).to.be.false();
    });

    it('defines a window.location getter', async () => {
      let targetURL: string;
      if (process.platform === 'win32') {
        targetURL = `file:///${fixturesPath.replace(/\\/g, '/')}/pages/base-page.html`;
      } else {
        targetURL = `file://${fixturesPath}/pages/base-page.html`;
      }
      const w = new BrowserWindow({ show: false });
      w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      w.webContents.executeJavaScript(`{ b = window.open(${JSON.stringify(targetURL)}); null }`);
      const [, window] = await emittedOnce(app, 'browser-window-created');
      await emittedOnce(window.webContents, 'did-finish-load');
      expect(await w.webContents.executeJavaScript('b.location.href')).to.equal(targetURL);
    });

    it('defines a window.location setter', async () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      w.webContents.executeJavaScript('{ b = window.open("about:blank"); null }');
      const [, { webContents }] = await emittedOnce(app, 'browser-window-created');
      await emittedOnce(webContents, 'did-finish-load');
      // When it loads, redirect
      w.webContents.executeJavaScript(`{ b.location = ${JSON.stringify(`file://${fixturesPath}/pages/base-page.html`)}; null }`);
      await emittedOnce(webContents, 'did-finish-load');
    });

    it('defines a window.location.href setter', async () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.loadFile(path.resolve(__dirname, 'fixtures', 'blank.html'));
      w.webContents.executeJavaScript('{ b = window.open("about:blank"); null }');
      const [, { webContents }] = await emittedOnce(app, 'browser-window-created');
      await emittedOnce(webContents, 'did-finish-load');
      // When it loads, redirect
      w.webContents.executeJavaScript(`{ b.location.href = ${JSON.stringify(`file://${fixturesPath}/pages/base-page.html`)}; null }`);
      await emittedOnce(webContents, 'did-finish-load');
    });

    it('open a blank page when no URL is specified', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('{ b = window.open(); null }');
      const [, { webContents }] = await emittedOnce(app, 'browser-window-created');
      await emittedOnce(webContents, 'did-finish-load');
      expect(await w.webContents.executeJavaScript('b.location.href')).to.equal('about:blank');
    });

    it('open a blank page when an empty URL is specified', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('{ b = window.open(\'\'); null }');
      const [, { webContents }] = await emittedOnce(app, 'browser-window-created');
      await emittedOnce(webContents, 'did-finish-load');
      expect(await w.webContents.executeJavaScript('b.location.href')).to.equal('about:blank');
    });

    it('does not throw an exception when the frameName is a built-in object property', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('{ b = window.open(\'\', \'__proto__\'); null }');
      const [, , frameName] = await emittedOnce(w.webContents, 'new-window');

      expect(frameName).to.equal('__proto__');
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
      const [, channel, opener] = await emittedOnce(w.webContents, 'ipc-message');
      expect(channel).to.equal('opener');
      expect(opener).to.equal(null);
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
      const [, firstDeviceIds] = await emittedOnce(ipcMain, 'deviceIds');
      const [, secondDeviceIds] = await emittedOnce(ipcMain, 'deviceIds', () => w.webContents.reload());
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
      const [, firstDeviceIds] = await emittedOnce(ipcMain, 'deviceIds');
      await ses.clearStorageData({ storages: ['cookies'] });
      const [, secondDeviceIds] = await emittedOnce(ipcMain, 'deviceIds', () => w.webContents.reload());
      expect(firstDeviceIds).to.not.deep.equal(secondDeviceIds);
    });

    it('provides a securityOrigin to the request handler', async () => {
      session.defaultSession.setPermissionRequestHandler(
        (wc, permission, callback, details) => {
          if (details.securityOrigin !== undefined) {
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
            case '/WebSQL' : filename = 'web_sql.html'; break;
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
        contents = (webContents as any).create({
          nodeIntegration: true,
          contextIsolation: false
        });
      });

      afterEach(() => {
        (contents as any).destroy();
        contents = null as any;
      });

      it('cannot access localStorage', async () => {
        const response = emittedOnce(ipcMain, 'local-storage-response');
        contents.loadURL(protocolName + '://host/localStorage');
        const [, error] = await response;
        expect(error).to.equal('Failed to read the \'localStorage\' property from \'Window\': Access is denied for this document.');
      });

      it('cannot access sessionStorage', async () => {
        const response = emittedOnce(ipcMain, 'session-storage-response');
        contents.loadURL(`${protocolName}://host/sessionStorage`);
        const [, error] = await response;
        expect(error).to.equal('Failed to read the \'sessionStorage\' property from \'Window\': Access is denied for this document.');
      });

      it('cannot access WebSQL database', async () => {
        const response = emittedOnce(ipcMain, 'web-sql-response');
        contents.loadURL(`${protocolName}://host/WebSQL`);
        const [, error] = await response;
        expect(error).to.equal('Failed to execute \'openDatabase\' on \'Window\': Access to the WebDatabase API is denied in this context.');
      });

      it('cannot access indexedDB', async () => {
        const response = emittedOnce(ipcMain, 'indexed-db-response');
        contents.loadURL(`${protocolName}://host/indexedDB`);
        const [, error] = await response;
        expect(error).to.equal('Failed to execute \'open\' on \'IDBFactory\': access to the Indexed Database API is denied in this context.');
      });

      it('cannot access cookie', async () => {
        const response = emittedOnce(ipcMain, 'cookie-response');
        contents.loadURL(`${protocolName}://host/cookie`);
        const [, error] = await response;
        expect(error).to.equal('Failed to set the \'cookie\' property on \'Document\': Access is denied for this document.');
      });
    });

    describe('can be accessed', () => {
      let server: http.Server;
      let serverUrl: string;
      let serverCrossSiteUrl: string;
      before((done) => {
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
          setTimeout(respond, 0);
        });
        server.listen(0, '127.0.0.1', () => {
          serverUrl = `http://127.0.0.1:${(server.address() as AddressInfo).port}`;
          serverCrossSiteUrl = `http://localhost:${(server.address() as AddressInfo).port}`;
          done();
        });
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
          w.webContents.on('crashed', () => {
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

    describe('enableWebSQL webpreference', () => {
      const origin = `${standardScheme}://fake-host`;
      const filePath = path.join(fixturesPath, 'pages', 'storage', 'web_sql.html');
      const sqlPartition = 'web-sql-preference-test';
      const sqlSession = session.fromPartition(sqlPartition);
      const securityError = 'An attempt was made to break through the security policy of the user agent.';
      let contents: WebContents, w: BrowserWindow;

      before(() => {
        sqlSession.protocol.registerFileProtocol(standardScheme, (request, callback) => {
          callback({ path: filePath });
        });
      });

      after(() => {
        sqlSession.protocol.unregisterProtocol(standardScheme);
      });

      afterEach(async () => {
        if (contents) {
          (contents as any).destroy();
          contents = null as any;
        }
        await closeAllWindows();
        (w as any) = null;
      });

      it('default value allows websql', async () => {
        contents = (webContents as any).create({
          session: sqlSession,
          nodeIntegration: true,
          contextIsolation: false
        });
        contents.loadURL(origin);
        const [, error] = await emittedOnce(ipcMain, 'web-sql-response');
        expect(error).to.be.null();
      });

      it('when set to false can disallow websql', async () => {
        contents = (webContents as any).create({
          session: sqlSession,
          nodeIntegration: true,
          enableWebSQL: false,
          contextIsolation: false
        });
        contents.loadURL(origin);
        const [, error] = await emittedOnce(ipcMain, 'web-sql-response');
        expect(error).to.equal(securityError);
      });

      it('when set to false does not disable indexedDB', async () => {
        contents = (webContents as any).create({
          session: sqlSession,
          nodeIntegration: true,
          enableWebSQL: false,
          contextIsolation: false
        });
        contents.loadURL(origin);
        const [, error] = await emittedOnce(ipcMain, 'web-sql-response');
        expect(error).to.equal(securityError);
        const dbName = 'random';
        const result = await contents.executeJavaScript(`
          new Promise((resolve, reject) => {
            try {
              let req = window.indexedDB.open('${dbName}');
              req.onsuccess = (event) => {
                let db = req.result;
                resolve(db.name);
              }
              req.onerror = (event) => { resolve(event.target.code); }
            } catch (e) {
              resolve(e.message);
            }
          });
        `);
        expect(result).to.equal(dbName);
      });

      it('child webContents can override when the embedder has allowed websql', async () => {
        w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            webviewTag: true,
            session: sqlSession,
            contextIsolation: false
          }
        });
        w.webContents.loadURL(origin);
        const [, error] = await emittedOnce(ipcMain, 'web-sql-response');
        expect(error).to.be.null();
        const webviewResult = emittedOnce(ipcMain, 'web-sql-response');
        await w.webContents.executeJavaScript(`
          new Promise((resolve, reject) => {
            const webview = new WebView();
            webview.setAttribute('src', '${origin}');
            webview.setAttribute('webpreferences', 'enableWebSQL=0,contextIsolation=no');
            webview.setAttribute('partition', '${sqlPartition}');
            webview.setAttribute('nodeIntegration', 'on');
            document.body.appendChild(webview);
            webview.addEventListener('dom-ready', () => resolve());
          });
        `);
        const [, childError] = await webviewResult;
        expect(childError).to.equal(securityError);
      });

      it('child webContents cannot override when the embedder has disallowed websql', async () => {
        w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            enableWebSQL: false,
            webviewTag: true,
            session: sqlSession,
            contextIsolation: false
          }
        });
        w.webContents.loadURL('data:text/html,<html></html>');
        const webviewResult = emittedOnce(ipcMain, 'web-sql-response');
        await w.webContents.executeJavaScript(`
          new Promise((resolve, reject) => {
            const webview = new WebView();
            webview.setAttribute('src', '${origin}');
            webview.setAttribute('webpreferences', 'enableWebSQL=1,contextIsolation=no');
            webview.setAttribute('partition', '${sqlPartition}');
            webview.setAttribute('nodeIntegration', 'on');
            document.body.appendChild(webview);
            webview.addEventListener('dom-ready', () => resolve());
          });
        `);
        const [, childError] = await webviewResult;
        expect(childError).to.equal(securityError);
      });

      it('child webContents can use websql when the embedder has allowed websql', async () => {
        w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            webviewTag: true,
            session: sqlSession,
            contextIsolation: false
          }
        });
        w.webContents.loadURL(origin);
        const [, error] = await emittedOnce(ipcMain, 'web-sql-response');
        expect(error).to.be.null();
        const webviewResult = emittedOnce(ipcMain, 'web-sql-response');
        await w.webContents.executeJavaScript(`
          new Promise((resolve, reject) => {
            const webview = new WebView();
            webview.setAttribute('src', '${origin}');
            webview.setAttribute('webpreferences', 'enableWebSQL=1,contextIsolation=no');
            webview.setAttribute('partition', '${sqlPartition}');
            webview.setAttribute('nodeIntegration', 'on');
            document.body.appendChild(webview);
            webview.addEventListener('dom-ready', () => resolve());
          });
        `);
        const [, childError] = await webviewResult;
        expect(childError).to.be.null();
      });
    });
  });

  ifdescribe(features.isPDFViewerEnabled())('PDF Viewer', () => {
    const pdfSource = url.format({
      pathname: path.join(__dirname, 'fixtures', 'cat.pdf').replace(/\\/g, '/'),
      protocol: 'file',
      slashes: true
    });

    it('successfully loads a PDF file', async () => {
      const w = new BrowserWindow({ show: false });

      w.loadURL(pdfSource);
      await emittedOnce(w.webContents, 'did-finish-load');
    });

    it('opens when loading a pdf resource as top level navigation', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL(pdfSource);
      const [, contents] = await emittedOnce(app, 'web-contents-created');
      await emittedOnce(contents, 'did-navigate');
      expect(contents.getURL()).to.equal('chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/index.html');
    });

    it('opens when loading a pdf resource in a iframe', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.join(__dirname, 'fixtures', 'pages', 'pdf-in-iframe.html'));
      const [, contents] = await emittedOnce(app, 'web-contents-created');
      await emittedOnce(contents, 'did-navigate');
      expect(contents.getURL()).to.equal('chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/index.html');
    });
  });

  describe('window.history', () => {
    describe('window.history.pushState', () => {
      it('should push state after calling history.pushState() from the same url', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
        // History should have current page by now.
        expect((w.webContents as any).length()).to.equal(1);

        const waitCommit = emittedOnce(w.webContents, 'navigation-entry-committed');
        w.webContents.executeJavaScript('window.history.pushState({}, "")');
        await waitCommit;
        // Initial page + pushed state.
        expect((w.webContents as any).length()).to.equal(2);
      });
    });
  });

  describe('chrome://media-internals', () => {
    it('loads the page successfully', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('chrome://media-internals');
      const pageExists = await w.webContents.executeJavaScript(
        "window.hasOwnProperty('chrome') && window.chrome.hasOwnProperty('send')"
      );
      expect(pageExists).to.be.true();
    });
  });

  describe('chrome://webrtc-internals', () => {
    it('loads the page successfully', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('chrome://webrtc-internals');
      const pageExists = await w.webContents.executeJavaScript(
        "window.hasOwnProperty('chrome') && window.chrome.hasOwnProperty('send')"
      );
      expect(pageExists).to.be.true();
    });
  });

  describe('document.hasFocus', () => {
    it('has correct value when multiple windows are opened', async () => {
      const w1 = new BrowserWindow({ show: true });
      const w2 = new BrowserWindow({ show: true });
      const w3 = new BrowserWindow({ show: false });
      await w1.loadFile(path.join(__dirname, 'fixtures', 'blank.html'));
      await w2.loadFile(path.join(__dirname, 'fixtures', 'blank.html'));
      await w3.loadFile(path.join(__dirname, 'fixtures', 'blank.html'));
      expect(webContents.getFocusedWebContents().id).to.equal(w2.webContents.id);
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
  const fullscreenChildHtml = promisify(fs.readFile)(
    path.join(fixturesPath, 'pages', 'fullscreen-oopif.html')
  );
  let w: BrowserWindow, server: http.Server;

  before(() => {
    server = http.createServer(async (_req, res) => {
      res.writeHead(200, { 'Content-Type': 'text/html' });
      res.write(await fullscreenChildHtml);
      res.end();
    });

    server.listen(8989, '127.0.0.1');
  });

  beforeEach(() => {
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

  it('can fullscreen from out-of-process iframes (OOPIFs)', async () => {
    const fullscreenChange = emittedOnce(ipcMain, 'fullscreenChange');
    const html =
      '<iframe style="width: 0" frameborder=0 src="http://localhost:8989" allowfullscreen></iframe>';
    w.loadURL(`data:text/html,${html}`);
    await fullscreenChange;

    const fullscreenWidth = await w.webContents.executeJavaScript(
      "document.querySelector('iframe').offsetWidth"
    );
    expect(fullscreenWidth > 0).to.be.true();

    await w.webContents.executeJavaScript(
      "document.querySelector('iframe').contentWindow.postMessage('exitFullscreen', '*')"
    );

    await delay(500);

    const width = await w.webContents.executeJavaScript(
      "document.querySelector('iframe').offsetWidth"
    );
    expect(width).to.equal(0);
  });

  // TODO(jkleinsc) fix this flaky test on WOA
  ifit(process.platform !== 'win32' || process.arch !== 'arm64')('can fullscreen from in-process iframes', async () => {
    const fullscreenChange = emittedOnce(ipcMain, 'fullscreenChange');
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

  after(closeAllWindows);
  afterEach(() => {
    session.defaultSession.setPermissionCheckHandler(null);
    session.defaultSession.removeAllListeners('select-serial-port');
  });

  it('does not return a port if select-serial-port event is not defined', async () => {
    w.loadFile(path.join(fixturesPath, 'pages', 'blank.html'));
    const port = await getPorts();
    expect(port).to.equal('NotFoundError: No port selected by the user.');
  });

  it('does not return a port when permission denied', async () => {
    w.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
      callback(portList[0].portId);
    });
    session.defaultSession.setPermissionCheckHandler(() => false);
    const port = await getPorts();
    expect(port).to.equal('NotFoundError: No port selected by the user.');
  });

  it('does not crash when select-serial-port is called with an invalid port', async () => {
    w.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
      callback('i-do-not-exist');
    });
    const port = await getPorts();
    expect(port).to.equal('NotFoundError: No port selected by the user.');
  });

  it('returns a port when select-serial-port event is defined', async () => {
    w.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
      callback(portList[0].portId);
    });
    const port = await getPorts();
    expect(port).to.equal('[object SerialPort]');
  });
});

describe('navigator.clipboard', () => {
  let w: BrowserWindow;
  before(async () => {
    w = new BrowserWindow({
      webPreferences: {
        enableBlinkFeatures: 'Serial'
      }
    });
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
    expect(clipboard).to.not.equal('Read permission denied.');
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
    expect(clipboard).to.equal('Read permission denied.');
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
    expect(clipboard).to.not.equal('Read permission denied.');
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
      await new Promise(resolve => setTimeout(resolve, 10));
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
      w.webContents.on('crashed', () => done(new Error('WebContents crashed.')));
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
      w.webContents.on('crashed', () => done(new Error('WebContents crashed.')));
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
    await new Promise<void>(resolve => server.listen(0, '127.0.0.1', resolve));
    serverUrl = `http://localhost:${(server.address() as any).port}`;
  });

  const getDevices: any = () => {
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
    const device = await getDevices();
    expect(device).to.equal('');
  });

  it('does not return a device when permission denied', async () => {
    let selectFired = false;
    w.webContents.session.on('select-hid-device', (event, details, callback) => {
      selectFired = true;
      callback();
    });
    session.defaultSession.setPermissionCheckHandler(() => false);
    const device = await getDevices();
    expect(selectFired).to.be.false();
    expect(device).to.equal('');
  });

  it('returns a device when select-hid-device event is defined', async () => {
    let haveDevices = false;
    let selectFired = false;
    w.webContents.session.on('select-hid-device', (event, details, callback) => {
      expect(details.frame).to.have.ownProperty('frameTreeNodeId').that.is.a('number');
      selectFired = true;
      if (details.deviceList.length > 0) {
        haveDevices = true;
        callback(details.deviceList[0].deviceId);
      } else {
        callback();
      }
    });
    const device = await getDevices();
    expect(selectFired).to.be.true();
    if (haveDevices) {
      expect(device).to.contain('[object HIDDevice]');
    } else {
      expect(device).to.equal('');
    }
    if (process.arch === 'arm64' || process.arch === 'arm') {
      // arm CI returns HID devices - this block may need to change if CI hardware changes.
      expect(haveDevices).to.be.true();
      // Verify that navigation will clear device permissions
      const grantedDevices = await w.webContents.executeJavaScript('navigator.hid.getDevices()');
      expect(grantedDevices).to.not.be.empty();
      w.loadURL(serverUrl);
      const [,,,,, frameProcessId, frameRoutingId] = await emittedOnce(w.webContents, 'did-frame-navigate');
      const frame = webFrameMain.fromId(frameProcessId, frameRoutingId);
      expect(frame).to.not.be.empty();
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
    const device = await getDevices();
    expect(selectFired).to.be.true();
    if (haveDevices) {
      expect(device).to.contain('[object HIDDevice]');
      expect(gotDevicePerms).to.be.true();
    } else {
      expect(device).to.equal('');
    }
  });

  it('returns excludes a device when a exclusionFilter is specified', async () => {
    const exclusionFilters = <any>[];
    let haveDevices = false;
    let checkForExcludedDevice = false;

    w.webContents.session.on('select-hid-device', (event, details, callback) => {
      if (details.deviceList.length > 0) {
        details.deviceList.find((device) => {
          if (device.name && device.name !== '' && device.serialNumber && device.serialNumber !== '') {
            console.log('device is: ', device);
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

    await getDevices();
    if (haveDevices) {
      // We have devices to exclude, so check if exculsionFilters work
      checkForExcludedDevice = true;
      await w.webContents.executeJavaScript(`
        navigator.hid.requestDevice({filters: [], exclusionFilters: ${JSON.stringify(exclusionFilters)}}).then(device => device.toString()).catch(err => err.toString());
      `, true);
    }
  });
});
