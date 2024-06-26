import { expect } from 'chai';
import { AddressInfo } from 'node:net';
import * as path from 'node:path';
import * as fs from 'node:fs';
import * as http from 'node:http';
import { BrowserWindow, ipcMain, webContents, session, app, BrowserView, WebContents } from 'electron/main';
import { closeAllWindows } from './lib/window-helpers';
import { ifdescribe, defer, waitUntil, listen, ifit } from './lib/spec-helpers';
import { once } from 'node:events';
import { setTimeout } from 'node:timers/promises';

const pdfjs = require('pdfjs-dist');
const fixturesPath = path.resolve(__dirname, 'fixtures');
const mainFixturesPath = path.resolve(__dirname, 'fixtures');
const features = process._linkedBinding('electron_common_features');

describe('webContents module', () => {
  describe('getAllWebContents() API', () => {
    afterEach(closeAllWindows);
    it('returns an array of web contents', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: { webviewTag: true }
      });
      w.loadFile(path.join(fixturesPath, 'pages', 'webview-zoom-factor.html'));

      await once(w.webContents, 'did-attach-webview') as [any, WebContents];

      w.webContents.openDevTools();

      await once(w.webContents, 'devtools-opened');

      const all = webContents.getAllWebContents().sort((a, b) => {
        return a.id - b.id;
      });

      expect(all).to.have.length(3);
      expect(all[0].getType()).to.equal('window');
      expect(all[all.length - 2].getType()).to.equal('webview');
      expect(all[all.length - 1].getType()).to.equal('remote');
    });
  });

  describe('webContents properties', () => {
    afterEach(closeAllWindows);

    it('has expected additional enumerable properties', () => {
      const w = new BrowserWindow({ show: false });
      const properties = Object.getOwnPropertyNames(w.webContents);
      expect(properties).to.include('ipc');
      expect(properties).to.include('navigationHistory');
    });
  });

  describe('fromId()', () => {
    it('returns undefined for an unknown id', () => {
      expect(webContents.fromId(12345)).to.be.undefined();
    });
  });

  describe('fromFrame()', () => {
    it('returns WebContents for mainFrame', () => {
      const contents = (webContents as typeof ElectronInternal.WebContents).create();
      expect(webContents.fromFrame(contents.mainFrame)).to.equal(contents);
    });
    it('returns undefined for disposed frame', async () => {
      const contents = (webContents as typeof ElectronInternal.WebContents).create();
      const { mainFrame } = contents;
      contents.destroy();
      await waitUntil(() => typeof webContents.fromFrame(mainFrame) === 'undefined');
    });
    it('throws when passing invalid argument', async () => {
      let errored = false;
      try {
        webContents.fromFrame({} as any);
      } catch {
        errored = true;
      }
      expect(errored).to.be.true();
    });
  });

  describe('fromDevToolsTargetId()', () => {
    it('returns WebContents for attached DevTools target', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');
      try {
        await w.webContents.debugger.attach('1.3');
        const { targetInfo } = await w.webContents.debugger.sendCommand('Target.getTargetInfo');
        expect(webContents.fromDevToolsTargetId(targetInfo.targetId)).to.equal(w.webContents);
      } finally {
        await w.webContents.debugger.detach();
      }
    });

    it('returns undefined for an unknown id', () => {
      expect(webContents.fromDevToolsTargetId('nope')).to.be.undefined();
    });
  });

  describe('will-prevent-unload event', function () {
    afterEach(closeAllWindows);
    it('does not emit if beforeunload returns undefined in a BrowserWindow', async () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.once('will-prevent-unload', () => {
        expect.fail('should not have fired');
      });
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-undefined.html'));
      const wait = once(w, 'closed');
      w.close();
      await wait;
    });

    it('does not emit if beforeunload returns undefined in a BrowserView', async () => {
      const w = new BrowserWindow({ show: false });
      const view = new BrowserView();
      w.setBrowserView(view);
      view.setBounds(w.getBounds());

      view.webContents.once('will-prevent-unload', () => {
        expect.fail('should not have fired');
      });

      await view.webContents.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-undefined.html'));
      const wait = once(w, 'closed');
      w.close();
      await wait;
    });

    it('emits if beforeunload returns false in a BrowserWindow', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false.html'));
      w.close();
      await once(w.webContents, 'will-prevent-unload');
    });

    it('emits if beforeunload returns false in a BrowserView', async () => {
      const w = new BrowserWindow({ show: false });
      const view = new BrowserView();
      w.setBrowserView(view);
      view.setBounds(w.getBounds());

      await view.webContents.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false.html'));
      w.close();
      await once(view.webContents, 'will-prevent-unload');
    });

    it('supports calling preventDefault on will-prevent-unload events in a BrowserWindow', async () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.once('will-prevent-unload', event => event.preventDefault());
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false.html'));
      const wait = once(w, 'closed');
      w.close();
      await wait;
    });
  });

  describe('webContents.send(channel, args...)', () => {
    afterEach(closeAllWindows);
    it('throws an error when the channel is missing', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        (w.webContents.send as any)();
      }).to.throw('Missing required channel argument');

      expect(() => {
        w.webContents.send(null as any);
      }).to.throw('Missing required channel argument');
    });

    it('does not block node async APIs when sent before document is ready', (done) => {
      // Please reference https://github.com/electron/electron/issues/19368 if
      // this test fails.
      ipcMain.once('async-node-api-done', () => {
        done();
      });
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          sandbox: false,
          contextIsolation: false
        }
      });
      w.loadFile(path.join(fixturesPath, 'pages', 'send-after-node.html'));
      setTimeout(50).then(() => {
        w.webContents.send('test');
      });
    });
  });

  ifdescribe(features.isPrintingEnabled())('webContents.print()', () => {
    let w: BrowserWindow;

    beforeEach(() => {
      w = new BrowserWindow({ show: false });
    });

    afterEach(closeAllWindows);

    it('does not throw when options are not passed', () => {
      expect(() => {
        w.webContents.print();
      }).not.to.throw();

      expect(() => {
        w.webContents.print(undefined);
      }).not.to.throw();
    });

    it('does not throw when options object is empty', () => {
      expect(() => {
        w.webContents.print({});
      }).not.to.throw();
    });

    it('throws when invalid settings are passed', () => {
      expect(() => {
        // @ts-ignore this line is intentionally incorrect
        w.webContents.print(true);
      }).to.throw('webContents.print(): Invalid print settings specified.');

      expect(() => {
        // @ts-ignore this line is intentionally incorrect
        w.webContents.print(null);
      }).to.throw('webContents.print(): Invalid print settings specified.');
    });

    it('throws when an invalid pageSize is passed', () => {
      const badSize = 5;
      expect(() => {
        // @ts-ignore this line is intentionally incorrect
        w.webContents.print({ pageSize: badSize });
      }).to.throw(`Unsupported pageSize: ${badSize}`);
    });

    it('throws when an invalid callback is passed', () => {
      expect(() => {
        // @ts-ignore this line is intentionally incorrect
        w.webContents.print({}, true);
      }).to.throw('webContents.print(): Invalid optional callback provided.');
    });

    it('fails when an invalid deviceName is passed', (done) => {
      w.webContents.print({ deviceName: 'i-am-a-nonexistent-printer' }, (success, reason) => {
        expect(success).to.equal(false);
        expect(reason).to.match(/Invalid deviceName provided/);
        done();
      });
    });

    it('throws when an invalid pageSize is passed', () => {
      expect(() => {
        // @ts-ignore this line is intentionally incorrect
        w.webContents.print({ pageSize: 'i-am-a-bad-pagesize' }, () => {});
      }).to.throw('Unsupported pageSize: i-am-a-bad-pagesize');
    });

    it('throws when an invalid custom pageSize is passed', () => {
      expect(() => {
        w.webContents.print({
          pageSize: {
            width: 100,
            height: 200
          }
        });
      }).to.throw('height and width properties must be minimum 352 microns.');
    });

    it('does not crash with custom margins', () => {
      expect(() => {
        w.webContents.print({
          silent: true,
          margins: {
            marginType: 'custom',
            top: 1,
            bottom: 1,
            left: 1,
            right: 1
          }
        });
      }).to.not.throw();
    });
  });

  describe('webContents.executeJavaScript', () => {
    describe('in about:blank', () => {
      const expected = 'hello, world!';
      const expectedErrorMsg = 'woops!';
      const code = `(() => "${expected}")()`;
      const asyncCode = `(() => new Promise(r => setTimeout(() => r("${expected}"), 500)))()`;
      const badAsyncCode = `(() => new Promise((r, e) => setTimeout(() => e("${expectedErrorMsg}"), 500)))()`;
      const errorTypes = new Set([
        Error,
        ReferenceError,
        EvalError,
        RangeError,
        SyntaxError,
        TypeError,
        URIError
      ]);
      let w: BrowserWindow;

      before(async () => {
        w = new BrowserWindow({ show: false, webPreferences: { contextIsolation: false } });
        await w.loadURL('about:blank');
      });
      after(closeAllWindows);

      it('resolves the returned promise with the result', async () => {
        const result = await w.webContents.executeJavaScript(code);
        expect(result).to.equal(expected);
      });
      it('resolves the returned promise with the result if the code returns an asynchronous promise', async () => {
        const result = await w.webContents.executeJavaScript(asyncCode);
        expect(result).to.equal(expected);
      });
      it('rejects the returned promise if an async error is thrown', async () => {
        await expect(w.webContents.executeJavaScript(badAsyncCode)).to.eventually.be.rejectedWith(expectedErrorMsg);
      });
      it('rejects the returned promise with an error if an Error.prototype is thrown', async () => {
        for (const error of errorTypes) {
          await expect(w.webContents.executeJavaScript(`Promise.reject(new ${error.name}("Wamp-wamp"))`))
            .to.eventually.be.rejectedWith(error);
        }
      });
    });

    describe('on a real page', () => {
      let w: BrowserWindow;
      beforeEach(() => {
        w = new BrowserWindow({ show: false });
      });
      afterEach(closeAllWindows);

      let server: http.Server;
      let serverUrl: string;

      before(async () => {
        server = http.createServer((request, response) => {
          response.end();
        });
        serverUrl = (await listen(server)).url;
      });

      after(() => {
        server.close();
      });

      it('works after page load and during subframe load', async () => {
        await w.loadURL(serverUrl);
        // initiate a sub-frame load, then try and execute script during it
        await w.webContents.executeJavaScript(`
          var iframe = document.createElement('iframe')
          iframe.src = '${serverUrl}/slow'
          document.body.appendChild(iframe)
          null // don't return the iframe
        `);
        await w.webContents.executeJavaScript('console.log(\'hello\')');
      });

      it('executes after page load', async () => {
        const executeJavaScript = w.webContents.executeJavaScript('(() => "test")()');
        w.loadURL(serverUrl);
        const result = await executeJavaScript;
        expect(result).to.equal('test');
      });
    });
  });

  describe('webContents.executeJavaScriptInIsolatedWorld', () => {
    let w: BrowserWindow;

    before(async () => {
      w = new BrowserWindow({ show: false, webPreferences: { contextIsolation: true } });
      await w.loadURL('about:blank');
    });

    it('resolves the returned promise with the result', async () => {
      await w.webContents.executeJavaScriptInIsolatedWorld(999, [{ code: 'window.X = 123' }]);
      const isolatedResult = await w.webContents.executeJavaScriptInIsolatedWorld(999, [{ code: 'window.X' }]);
      const mainWorldResult = await w.webContents.executeJavaScript('window.X');
      expect(isolatedResult).to.equal(123);
      expect(mainWorldResult).to.equal(undefined);
    });
  });

  describe('loadURL() promise API', () => {
    let w: BrowserWindow;

    beforeEach(async () => {
      w = new BrowserWindow({ show: false });
    });
    afterEach(closeAllWindows);

    it('resolves when done loading', async () => {
      await expect(w.loadURL('about:blank')).to.eventually.be.fulfilled();
    });

    it('resolves when done loading a file URL', async () => {
      await expect(w.loadFile(path.join(fixturesPath, 'pages', 'base-page.html'))).to.eventually.be.fulfilled();
    });

    it('resolves when navigating within the page', async () => {
      await w.loadFile(path.join(fixturesPath, 'pages', 'base-page.html'));
      await setTimeout();
      await expect(w.loadURL(w.getURL() + '#foo')).to.eventually.be.fulfilled();
    });

    it('resolves after browser initiated navigation', async () => {
      let finishedLoading = false;
      w.webContents.on('did-finish-load', function () {
        finishedLoading = true;
      });

      await w.loadFile(path.join(fixturesPath, 'pages', 'navigate_in_page_and_wait.html'));
      expect(finishedLoading).to.be.true();
    });

    it('rejects when failing to load a file URL', async () => {
      await expect(w.loadURL('file:non-existent')).to.eventually.be.rejected()
        .and.have.property('code', 'ERR_FILE_NOT_FOUND');
    });

    // FIXME: Temporarily disable on WOA until
    // https://github.com/electron/electron/issues/20008 is resolved
    ifit(!(process.platform === 'win32' && process.arch === 'arm64'))('rejects when loading fails due to DNS not resolved', async () => {
      await expect(w.loadURL('https://err.name.not.resolved')).to.eventually.be.rejected()
        .and.have.property('code', 'ERR_NAME_NOT_RESOLVED');
    });

    it('rejects when navigation is cancelled due to a bad scheme', async () => {
      await expect(w.loadURL('bad-scheme://foo')).to.eventually.be.rejected()
        .and.have.property('code', 'ERR_FAILED');
    });

    it('does not crash when loading a new URL with emulation settings set', async () => {
      const setEmulation = async () => {
        if (w.webContents) {
          w.webContents.debugger.attach('1.3');

          const deviceMetrics = {
            width: 700,
            height: 600,
            deviceScaleFactor: 2,
            mobile: true,
            dontSetVisibleSize: true
          };
          await w.webContents.debugger.sendCommand(
            'Emulation.setDeviceMetricsOverride',
            deviceMetrics
          );
        }
      };

      try {
        await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
        await setEmulation();
        await w.loadURL('data:text/html,<h1>HELLO</h1>');
        await setEmulation();
      } catch (e) {
        expect((e as Error).message).to.match(/Debugger is already attached to the target/);
      }
    });

    it('fails if loadURL is called inside a non-reentrant critical section', (done) => {
      w.webContents.once('did-fail-load', (_event, _errorCode, _errorDescription, validatedURL) => {
        expect(validatedURL).to.contain('blank.html');
        done();
      });

      w.webContents.once('did-start-loading', () => {
        w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      });

      w.loadURL('data:text/html,<h1>HELLO</h1>');
    });

    it('sets appropriate error information on rejection', async () => {
      let err: any;
      try {
        await w.loadURL('file:non-existent');
      } catch (e) {
        err = e;
      }
      expect(err).not.to.be.null();
      expect(err.code).to.eql('ERR_FILE_NOT_FOUND');
      expect(err.errno).to.eql(-6);
      expect(err.url).to.eql(process.platform === 'win32' ? 'file://non-existent/' : 'file:///non-existent');
    });

    it('rejects if the load is aborted', async () => {
      const s = http.createServer(() => { /* never complete the request */ });
      const { port } = await listen(s);
      const p = expect(w.loadURL(`http://127.0.0.1:${port}`)).to.eventually.be.rejectedWith(Error, /ERR_ABORTED/);
      // load a different file before the first load completes, causing the
      // first load to be aborted.
      await w.loadFile(path.join(fixturesPath, 'pages', 'base-page.html'));
      await p;
      s.close();
    });

    it("doesn't reject when a subframe fails to load", async () => {
      let resp = null as unknown as http.ServerResponse;
      const s = http.createServer((req, res) => {
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.write('<iframe src="http://err.name.not.resolved"></iframe>');
        resp = res;
        // don't end the response yet
      });
      const { port } = await listen(s);
      const p = new Promise<void>(resolve => {
        w.webContents.on('did-fail-load', (event, errorCode, errorDescription, validatedURL, isMainFrame) => {
          if (!isMainFrame) {
            resolve();
          }
        });
      });
      const main = w.loadURL(`http://127.0.0.1:${port}`);
      await p;
      resp.end();
      await main;
      s.close();
    });

    it("doesn't resolve when a subframe loads", async () => {
      let resp = null as unknown as http.ServerResponse;
      const s = http.createServer((req, res) => {
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.write('<iframe src="about:blank"></iframe>');
        resp = res;
        // don't end the response yet
      });
      const { port } = await listen(s);
      const p = new Promise<void>(resolve => {
        w.webContents.on('did-frame-finish-load', (event, isMainFrame) => {
          if (!isMainFrame) {
            resolve();
          }
        });
      });
      const main = w.loadURL(`http://127.0.0.1:${port}`);
      await p;
      resp.destroy(); // cause the main request to fail
      await expect(main).to.eventually.be.rejected()
        .and.have.property('errno', -355); // ERR_INCOMPLETE_CHUNKED_ENCODING
      s.close();
    });

    it('subsequent load failures reject each time', async () => {
      await expect(w.loadURL('file:non-existent')).to.eventually.be.rejected();
      await expect(w.loadURL('file:non-existent')).to.eventually.be.rejected();
    });

    it('invalid URL load rejects', async () => {
      await expect(w.loadURL('invalidURL')).to.eventually.be.rejected();
    });
  });

  describe('navigationHistory', () => {
    let w: BrowserWindow;
    const urlPage1 = 'data:text/html,<html><head><script>document.title = "Page 1";</script></head><body></body></html>';
    const urlPage2 = 'data:text/html,<html><head><script>document.title = "Page 2";</script></head><body></body></html>';
    const urlPage3 = 'data:text/html,<html><head><script>document.title = "Page 3";</script></head><body></body></html>';

    beforeEach(async () => {
      w = new BrowserWindow({ show: false });
    });
    afterEach(closeAllWindows);
    describe('navigationHistory.canGoBack and navigationHistory.goBack API', () => {
      it('should not be able to go back if history is empty', async () => {
        expect(w.webContents.navigationHistory.canGoBack()).to.be.false();
      });

      it('should be able to go back if history is not empty', async () => {
        await w.loadURL(urlPage1);
        await w.loadURL(urlPage2);
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(1);
        expect(w.webContents.navigationHistory.canGoBack()).to.be.true();
        w.webContents.navigationHistory.goBack();
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(0);
      });
    });

    describe('navigationHistory.canGoForward and navigationHistory.goForward API', () => {
      it('should not be able to go forward if history is empty', async () => {
        expect(w.webContents.navigationHistory.canGoForward()).to.be.false();
      });

      it('should not be able to go forward if current index is same as history length', async () => {
        await w.loadURL(urlPage1);
        await w.loadURL(urlPage2);
        expect(w.webContents.navigationHistory.canGoForward()).to.be.false();
      });

      it('should be able to go forward if history is not empty and active index is less than history length', async () => {
        await w.loadURL(urlPage1);
        await w.loadURL(urlPage2);
        w.webContents.navigationHistory.goBack();
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(0);
        expect(w.webContents.navigationHistory.canGoForward()).to.be.true();
        w.webContents.navigationHistory.goForward();
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(1);
      });
    });

    describe('navigationHistory.canGoToOffset(index) and navigationHistory.goToOffset(index) API', () => {
      it('should not be able to go to invalid offset', async () => {
        expect(w.webContents.navigationHistory.canGoToOffset(-1)).to.be.false();
        expect(w.webContents.navigationHistory.canGoToOffset(10)).to.be.false();
      });

      it('should be able to go to valid negative offset', async () => {
        await w.loadURL(urlPage1);
        await w.loadURL(urlPage2);
        await w.loadURL(urlPage3);
        expect(w.webContents.navigationHistory.canGoToOffset(-2)).to.be.true();
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(2);
        w.webContents.navigationHistory.goToOffset(-2);
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(0);
      });

      it('should be able to go to valid positive offset', async () => {
        await w.loadURL(urlPage1);
        await w.loadURL(urlPage2);
        await w.loadURL(urlPage3);

        w.webContents.navigationHistory.goBack();
        expect(w.webContents.navigationHistory.canGoToOffset(1)).to.be.true();
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(1);
        w.webContents.navigationHistory.goToOffset(1);
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(2);
      });
    });

    describe('navigationHistory.clear API', () => {
      it('should be able clear history', async () => {
        await w.loadURL(urlPage1);
        await w.loadURL(urlPage2);
        await w.loadURL(urlPage3);

        expect(w.webContents.navigationHistory.length()).to.equal(3);
        w.webContents.navigationHistory.clear();
        expect(w.webContents.navigationHistory.length()).to.equal(1);
      });
    });

    describe('navigationHistory.getEntryAtIndex(index) API ', () => {
      it('should fetch default navigation entry when no urls are loaded', async () => {
        const result = w.webContents.navigationHistory.getEntryAtIndex(0);
        expect(result).to.deep.equal({ url: '', title: '' });
      });
      it('should fetch navigation entry given a valid index', async () => {
        await w.loadURL(urlPage1);
        const result = w.webContents.navigationHistory.getEntryAtIndex(0);
        expect(result).to.deep.equal({ url: urlPage1, title: 'Page 1' });
      });
      it('should return null given an invalid index larger than history length', async () => {
        await w.loadURL(urlPage1);
        const result = w.webContents.navigationHistory.getEntryAtIndex(5);
        expect(result).to.be.null();
      });
      it('should return null given an invalid negative index', async () => {
        await w.loadURL(urlPage1);
        const result = w.webContents.navigationHistory.getEntryAtIndex(-1);
        expect(result).to.be.null();
      });
    });

    describe('navigationHistory.getActiveIndex() API', () => {
      it('should return valid active index after a single page visit', async () => {
        await w.loadURL(urlPage1);
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(0);
      });

      it('should return valid active index after a multiple page visits', async () => {
        await w.loadURL(urlPage1);
        await w.loadURL(urlPage2);
        await w.loadURL(urlPage3);

        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(2);
      });

      it('should return valid active index given no page visits', async () => {
        expect(w.webContents.navigationHistory.getActiveIndex()).to.equal(0);
      });
    });

    describe('navigationHistory.length() API', () => {
      it('should return valid history length after a single page visit', async () => {
        await w.loadURL(urlPage1);
        expect(w.webContents.navigationHistory.length()).to.equal(1);
      });

      it('should return valid history length after a multiple page visits', async () => {
        await w.loadURL(urlPage1);
        await w.loadURL(urlPage2);
        await w.loadURL(urlPage3);

        expect(w.webContents.navigationHistory.length()).to.equal(3);
      });

      it('should return valid history length given no page visits', async () => {
        // Note: Even if no navigation has committed, the history list will always start with an initial navigation entry
        // Ref: https://source.chromium.org/chromium/chromium/src/+/main:ceontent/public/browser/navigation_controller.h;l=381
        expect(w.webContents.navigationHistory.length()).to.equal(1);
      });
    });
  });

  describe('getFocusedWebContents() API', () => {
    afterEach(closeAllWindows);

    // FIXME
    ifit(!(process.platform === 'win32' && process.arch === 'arm64'))('returns the focused web contents', async () => {
      const w = new BrowserWindow({ show: true });
      await w.loadFile(path.join(__dirname, 'fixtures', 'blank.html'));
      expect(webContents.getFocusedWebContents()?.id).to.equal(w.webContents.id);

      const devToolsOpened = once(w.webContents, 'devtools-opened');
      w.webContents.openDevTools();
      await devToolsOpened;
      expect(webContents.getFocusedWebContents()?.id).to.equal(w.webContents.devToolsWebContents!.id);
      const devToolsClosed = once(w.webContents, 'devtools-closed');
      w.webContents.closeDevTools();
      await devToolsClosed;
      expect(webContents.getFocusedWebContents()?.id).to.equal(w.webContents.id);
    });

    it('does not crash when called on a detached dev tools window', async () => {
      const w = new BrowserWindow({ show: true });

      w.webContents.openDevTools({ mode: 'detach' });
      w.webContents.inspectElement(100, 100);

      // For some reason we have to wait for two focused events...?
      await once(w.webContents, 'devtools-focused');

      expect(() => { webContents.getFocusedWebContents(); }).to.not.throw();

      // Work around https://github.com/electron/electron/issues/19985
      await setTimeout();

      const devToolsClosed = once(w.webContents, 'devtools-closed');
      w.webContents.closeDevTools();
      await devToolsClosed;
      expect(() => { webContents.getFocusedWebContents(); }).to.not.throw();
    });
  });

  describe('setDevToolsWebContents() API', () => {
    afterEach(closeAllWindows);
    it('sets arbitrary webContents as devtools', async () => {
      const w = new BrowserWindow({ show: false });
      const devtools = new BrowserWindow({ show: false });
      const promise = once(devtools.webContents, 'dom-ready');
      w.webContents.setDevToolsWebContents(devtools.webContents);
      w.webContents.openDevTools();
      await promise;
      expect(devtools.webContents.getURL().startsWith('devtools://devtools')).to.be.true();
      const result = await devtools.webContents.executeJavaScript('InspectorFrontendHost.constructor.name');
      expect(result).to.equal('InspectorFrontendHostImpl');
      devtools.destroy();
    });
  });

  describe('isFocused() API', () => {
    it('returns false when the window is hidden', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');
      expect(w.isVisible()).to.be.false();
      expect(w.webContents.isFocused()).to.be.false();
    });
  });

  describe('isCurrentlyAudible() API', () => {
    afterEach(closeAllWindows);
    it('returns whether audio is playing', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');
      await w.webContents.executeJavaScript(`
        window.context = new AudioContext
        // Start in suspended state, because of the
        // new web audio api policy.
        context.suspend()
        window.oscillator = context.createOscillator()
        oscillator.connect(context.destination)
        oscillator.start()
      `);
      let p = once(w.webContents, 'audio-state-changed');
      w.webContents.executeJavaScript('context.resume()');
      await p;
      expect(w.webContents.isCurrentlyAudible()).to.be.true();
      p = once(w.webContents, 'audio-state-changed');
      w.webContents.executeJavaScript('oscillator.stop()');
      await p;
      expect(w.webContents.isCurrentlyAudible()).to.be.false();
    });
  });

  describe('openDevTools() API', () => {
    afterEach(closeAllWindows);
    it('can show window with activation', async () => {
      const w = new BrowserWindow({ show: false });
      const focused = once(w, 'focus');
      w.show();
      await focused;
      expect(w.isFocused()).to.be.true();
      const blurred = once(w, 'blur');
      w.webContents.openDevTools({ mode: 'detach', activate: true });
      await Promise.all([
        once(w.webContents, 'devtools-opened'),
        once(w.webContents, 'devtools-focused')
      ]);
      await blurred;
      expect(w.isFocused()).to.be.false();
    });

    it('can show window without activation', async () => {
      const w = new BrowserWindow({ show: false });
      const devtoolsOpened = once(w.webContents, 'devtools-opened');
      w.webContents.openDevTools({ mode: 'detach', activate: false });
      await devtoolsOpened;
      expect(w.webContents.isDevToolsOpened()).to.be.true();
    });

    it('can show a DevTools window with custom title', async () => {
      const w = new BrowserWindow({ show: false });
      const devtoolsOpened = once(w.webContents, 'devtools-opened');
      w.webContents.openDevTools({ mode: 'detach', activate: false, title: 'myTitle' });
      await devtoolsOpened;
      expect(w.webContents.getDevToolsTitle()).to.equal('myTitle');
    });

    it('can re-open devtools', async () => {
      const w = new BrowserWindow({ show: false });
      const devtoolsOpened = once(w.webContents, 'devtools-opened');
      w.webContents.openDevTools({ mode: 'detach', activate: true });
      await devtoolsOpened;
      expect(w.webContents.isDevToolsOpened()).to.be.true();

      const devtoolsClosed = once(w.webContents, 'devtools-closed');
      w.webContents.closeDevTools();
      await devtoolsClosed;
      expect(w.webContents.isDevToolsOpened()).to.be.false();

      const devtoolsOpened2 = once(w.webContents, 'devtools-opened');
      w.webContents.openDevTools({ mode: 'detach', activate: true });
      await devtoolsOpened2;
      expect(w.webContents.isDevToolsOpened()).to.be.true();
    });
  });

  describe('setDevToolsTitle() API', () => {
    afterEach(closeAllWindows);
    it('can set devtools title with function', async () => {
      const w = new BrowserWindow({ show: false });
      const devtoolsOpened = once(w.webContents, 'devtools-opened');
      w.webContents.openDevTools({ mode: 'detach', activate: false });
      await devtoolsOpened;
      expect(w.webContents.isDevToolsOpened()).to.be.true();
      w.webContents.setDevToolsTitle('newTitle');
      expect(w.webContents.getDevToolsTitle()).to.equal('newTitle');
    });
  });

  describe('before-input-event event', () => {
    afterEach(closeAllWindows);
    it('can prevent document keyboard events', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      await w.loadFile(path.join(fixturesPath, 'pages', 'key-events.html'));
      const keyDown = new Promise(resolve => {
        ipcMain.once('keydown', (event, key) => resolve(key));
      });
      w.webContents.once('before-input-event', (event, input) => {
        if (input.key === 'a') event.preventDefault();
      });
      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'a' });
      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'b' });
      expect(await keyDown).to.equal('b');
    });

    it('has the correct properties', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'base-page.html'));
      const testBeforeInput = async (opts: any) => {
        const modifiers = [];
        if (opts.shift) modifiers.push('shift');
        if (opts.control) modifiers.push('control');
        if (opts.alt) modifiers.push('alt');
        if (opts.meta) modifiers.push('meta');
        if (opts.isAutoRepeat) modifiers.push('isAutoRepeat');

        const p = once(w.webContents, 'before-input-event') as Promise<[any, Electron.Input]>;
        w.webContents.sendInputEvent({
          type: opts.type,
          keyCode: opts.keyCode,
          modifiers: modifiers as any
        });
        const [, input] = await p;

        expect(input.type).to.equal(opts.type);
        expect(input.key).to.equal(opts.key);
        expect(input.code).to.equal(opts.code);
        expect(input.isAutoRepeat).to.equal(opts.isAutoRepeat);
        expect(input.shift).to.equal(opts.shift);
        expect(input.control).to.equal(opts.control);
        expect(input.alt).to.equal(opts.alt);
        expect(input.meta).to.equal(opts.meta);
      };
      await testBeforeInput({
        type: 'keyDown',
        key: 'A',
        code: 'KeyA',
        keyCode: 'a',
        shift: true,
        control: true,
        alt: true,
        meta: true,
        isAutoRepeat: true
      });
      await testBeforeInput({
        type: 'keyUp',
        key: '.',
        code: 'Period',
        keyCode: '.',
        shift: false,
        control: true,
        alt: true,
        meta: false,
        isAutoRepeat: false
      });
      await testBeforeInput({
        type: 'keyUp',
        key: '!',
        code: 'Digit1',
        keyCode: '1',
        shift: true,
        control: false,
        alt: false,
        meta: true,
        isAutoRepeat: false
      });
      await testBeforeInput({
        type: 'keyUp',
        key: 'Tab',
        code: 'Tab',
        keyCode: 'Tab',
        shift: false,
        control: true,
        alt: false,
        meta: false,
        isAutoRepeat: true
      });
    });
  });

  // On Mac, zooming isn't done with the mouse wheel.
  ifdescribe(process.platform !== 'darwin')('zoom-changed', () => {
    afterEach(closeAllWindows);
    it('is emitted with the correct zoom-in info', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'base-page.html'));

      const testZoomChanged = async () => {
        w.webContents.sendInputEvent({
          type: 'mouseWheel',
          x: 300,
          y: 300,
          deltaX: 0,
          deltaY: 1,
          wheelTicksX: 0,
          wheelTicksY: 1,
          modifiers: ['control', 'meta']
        });

        const [, zoomDirection] = await once(w.webContents, 'zoom-changed') as [any, string];
        expect(zoomDirection).to.equal('in');
      };

      await testZoomChanged();
    });

    it('is emitted with the correct zoom-out info', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'base-page.html'));

      const testZoomChanged = async () => {
        w.webContents.sendInputEvent({
          type: 'mouseWheel',
          x: 300,
          y: 300,
          deltaX: 0,
          deltaY: -1,
          wheelTicksX: 0,
          wheelTicksY: -1,
          modifiers: ['control', 'meta']
        });

        const [, zoomDirection] = await once(w.webContents, 'zoom-changed') as [any, string];
        expect(zoomDirection).to.equal('out');
      };

      await testZoomChanged();
    });
  });

  describe('sendInputEvent(event)', () => {
    let w: BrowserWindow;
    beforeEach(async () => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      await w.loadFile(path.join(fixturesPath, 'pages', 'key-events.html'));
    });
    afterEach(closeAllWindows);

    it('can send keydown events', async () => {
      const keydown = once(ipcMain, 'keydown');
      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'A' });
      const [, key, code, keyCode, shiftKey, ctrlKey, altKey] = await keydown;
      expect(key).to.equal('a');
      expect(code).to.equal('KeyA');
      expect(keyCode).to.equal(65);
      expect(shiftKey).to.be.false();
      expect(ctrlKey).to.be.false();
      expect(altKey).to.be.false();
    });

    it('can send keydown events with modifiers', async () => {
      const keydown = once(ipcMain, 'keydown');
      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Z', modifiers: ['shift', 'ctrl'] });
      const [, key, code, keyCode, shiftKey, ctrlKey, altKey] = await keydown;
      expect(key).to.equal('Z');
      expect(code).to.equal('KeyZ');
      expect(keyCode).to.equal(90);
      expect(shiftKey).to.be.true();
      expect(ctrlKey).to.be.true();
      expect(altKey).to.be.false();
    });

    it('can send keydown events with special keys', async () => {
      const keydown = once(ipcMain, 'keydown');
      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Tab', modifiers: ['alt'] });
      const [, key, code, keyCode, shiftKey, ctrlKey, altKey] = await keydown;
      expect(key).to.equal('Tab');
      expect(code).to.equal('Tab');
      expect(keyCode).to.equal(9);
      expect(shiftKey).to.be.false();
      expect(ctrlKey).to.be.false();
      expect(altKey).to.be.true();
    });

    it('can send char events', async () => {
      const keypress = once(ipcMain, 'keypress');
      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'A' });
      w.webContents.sendInputEvent({ type: 'char', keyCode: 'A' });
      const [, key, code, keyCode, shiftKey, ctrlKey, altKey] = await keypress;
      expect(key).to.equal('a');
      expect(code).to.equal('KeyA');
      expect(keyCode).to.equal(65);
      expect(shiftKey).to.be.false();
      expect(ctrlKey).to.be.false();
      expect(altKey).to.be.false();
    });

    it('can correctly convert accelerators to key codes', async () => {
      const keyup = once(ipcMain, 'keyup');
      w.webContents.sendInputEvent({ keyCode: 'Plus', type: 'char' });
      w.webContents.sendInputEvent({ keyCode: 'Space', type: 'char' });
      w.webContents.sendInputEvent({ keyCode: 'Plus', type: 'char' });
      w.webContents.sendInputEvent({ keyCode: 'Space', type: 'char' });
      w.webContents.sendInputEvent({ keyCode: 'Plus', type: 'char' });
      w.webContents.sendInputEvent({ keyCode: 'Plus', type: 'keyUp' });

      await keyup;
      const inputText = await w.webContents.executeJavaScript('document.getElementById("input").value');
      expect(inputText).to.equal('+ + +');
    });

    it('can send char events with modifiers', async () => {
      const keypress = once(ipcMain, 'keypress');
      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Z' });
      w.webContents.sendInputEvent({ type: 'char', keyCode: 'Z', modifiers: ['shift', 'ctrl'] });
      const [, key, code, keyCode, shiftKey, ctrlKey, altKey] = await keypress;
      expect(key).to.equal('Z');
      expect(code).to.equal('KeyZ');
      expect(keyCode).to.equal(90);
      expect(shiftKey).to.be.true();
      expect(ctrlKey).to.be.true();
      expect(altKey).to.be.false();
    });
  });

  describe('insertCSS', () => {
    afterEach(closeAllWindows);
    it('supports inserting CSS', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      await w.webContents.insertCSS('body { background-repeat: round; }');
      const result = await w.webContents.executeJavaScript('window.getComputedStyle(document.body).getPropertyValue("background-repeat")');
      expect(result).to.equal('round');
    });

    it('supports removing inserted CSS', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      const key = await w.webContents.insertCSS('body { background-repeat: round; }');
      await w.webContents.removeInsertedCSS(key);
      const result = await w.webContents.executeJavaScript('window.getComputedStyle(document.body).getPropertyValue("background-repeat")');
      expect(result).to.equal('repeat');
    });
  });

  describe('inspectElement()', () => {
    afterEach(closeAllWindows);
    it('supports inspecting an element in the devtools', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      const event = once(w.webContents, 'devtools-opened');
      w.webContents.inspectElement(10, 10);
      await event;
    });
  });

  describe('startDrag({file, icon})', () => {
    it('throws errors for a missing file or a missing/empty icon', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        w.webContents.startDrag({ icon: path.join(fixturesPath, 'assets', 'logo.png') } as any);
      }).to.throw('Must specify either \'file\' or \'files\' option');

      expect(() => {
        w.webContents.startDrag({ file: __filename } as any);
      }).to.throw('\'icon\' parameter is required');

      expect(() => {
        w.webContents.startDrag({ file: __filename, icon: path.join(mainFixturesPath, 'blank.png') });
      }).to.throw(/Failed to load image from path (.+)/);
    });
  });

  describe('focus APIs', () => {
    describe('focus()', () => {
      afterEach(closeAllWindows);
      it('does not blur the focused window when the web contents is hidden', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
        w.show();
        await w.loadURL('about:blank');
        w.focus();
        const child = new BrowserWindow({ show: false });
        child.loadURL('about:blank');
        child.webContents.focus();
        const currentFocused = w.isFocused();
        const childFocused = child.isFocused();
        child.close();
        expect(currentFocused).to.be.true();
        expect(childFocused).to.be.false();
      });
    });

    const moveFocusToDevTools = async (win: BrowserWindow) => {
      const devToolsOpened = once(win.webContents, 'devtools-opened');
      win.webContents.openDevTools({ mode: 'right' });
      await devToolsOpened;
      win.webContents.devToolsWebContents!.focus();
    };

    describe('focus event', () => {
      afterEach(closeAllWindows);

      it('is triggered when web contents is focused', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadURL('about:blank');
        await moveFocusToDevTools(w);
        const focusPromise = once(w.webContents, 'focus');
        w.webContents.focus();
        await expect(focusPromise).to.eventually.be.fulfilled();
      });
    });

    describe('blur event', () => {
      afterEach(closeAllWindows);
      it('is triggered when web contents is blurred', async () => {
        const w = new BrowserWindow({ show: true });
        await w.loadURL('about:blank');
        w.webContents.focus();
        const blurPromise = once(w.webContents, 'blur');
        await moveFocusToDevTools(w);
        await expect(blurPromise).to.eventually.be.fulfilled();
      });
    });
  });

  describe('getOSProcessId()', () => {
    afterEach(closeAllWindows);
    it('returns a valid process id', async () => {
      const w = new BrowserWindow({ show: false });
      expect(w.webContents.getOSProcessId()).to.equal(0);

      await w.loadURL('about:blank');
      expect(w.webContents.getOSProcessId()).to.be.above(0);
    });
  });

  describe('getMediaSourceId()', () => {
    afterEach(closeAllWindows);
    it('returns a valid stream id', () => {
      const w = new BrowserWindow({ show: false });
      expect(w.webContents.getMediaSourceId(w.webContents)).to.be.a('string').that.is.not.empty();
    });
  });

  describe('userAgent APIs', () => {
    it('is not empty by default', () => {
      const w = new BrowserWindow({ show: false });
      const userAgent = w.webContents.getUserAgent();
      expect(userAgent).to.be.a('string').that.is.not.empty();
    });

    it('can set the user agent (functions)', () => {
      const w = new BrowserWindow({ show: false });
      const userAgent = w.webContents.getUserAgent();

      w.webContents.setUserAgent('my-user-agent');
      expect(w.webContents.getUserAgent()).to.equal('my-user-agent');

      w.webContents.setUserAgent(userAgent);
      expect(w.webContents.getUserAgent()).to.equal(userAgent);
    });

    it('can set the user agent (properties)', () => {
      const w = new BrowserWindow({ show: false });
      const userAgent = w.webContents.userAgent;

      w.webContents.userAgent = 'my-user-agent';
      expect(w.webContents.userAgent).to.equal('my-user-agent');

      w.webContents.userAgent = userAgent;
      expect(w.webContents.userAgent).to.equal(userAgent);
    });
  });

  describe('audioMuted APIs', () => {
    it('can set the audio mute level (functions)', () => {
      const w = new BrowserWindow({ show: false });

      w.webContents.setAudioMuted(true);
      expect(w.webContents.isAudioMuted()).to.be.true();

      w.webContents.setAudioMuted(false);
      expect(w.webContents.isAudioMuted()).to.be.false();
    });

    it('can set the audio mute level (functions)', () => {
      const w = new BrowserWindow({ show: false });

      w.webContents.audioMuted = true;
      expect(w.webContents.audioMuted).to.be.true();

      w.webContents.audioMuted = false;
      expect(w.webContents.audioMuted).to.be.false();
    });
  });

  describe('zoom api', () => {
    const hostZoomMap: Record<string, number> = {
      host1: 0.3,
      host2: 0.7,
      host3: 0.2
    };

    before(() => {
      const protocol = session.defaultSession.protocol;
      protocol.registerStringProtocol(standardScheme, (request, callback) => {
        const response = `<script>
                            const {ipcRenderer} = require('electron')
                            ipcRenderer.send('set-zoom', window.location.hostname)
                            ipcRenderer.on(window.location.hostname + '-zoom-set', () => {
                              ipcRenderer.send(window.location.hostname + '-zoom-level')
                            })
                          </script>`;
        callback({ data: response, mimeType: 'text/html' });
      });
    });

    after(() => {
      const protocol = session.defaultSession.protocol;
      protocol.unregisterProtocol(standardScheme);
    });

    afterEach(closeAllWindows);

    it('throws on an invalid zoomFactor', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');

      expect(() => {
        w.webContents.setZoomFactor(0.0);
      }).to.throw(/'zoomFactor' must be a double greater than 0.0/);

      expect(() => {
        w.webContents.setZoomFactor(-2.0);
      }).to.throw(/'zoomFactor' must be a double greater than 0.0/);
    });

    it('can set the correct zoom level (functions)', async () => {
      const w = new BrowserWindow({ show: false });
      try {
        await w.loadURL('about:blank');
        const zoomLevel = w.webContents.getZoomLevel();
        expect(zoomLevel).to.eql(0.0);
        w.webContents.setZoomLevel(0.5);
        const newZoomLevel = w.webContents.getZoomLevel();
        expect(newZoomLevel).to.eql(0.5);
      } finally {
        w.webContents.setZoomLevel(0);
      }
    });

    it('can set the correct zoom level (properties)', async () => {
      const w = new BrowserWindow({ show: false });
      try {
        await w.loadURL('about:blank');
        const zoomLevel = w.webContents.zoomLevel;
        expect(zoomLevel).to.eql(0.0);
        w.webContents.zoomLevel = 0.5;
        const newZoomLevel = w.webContents.zoomLevel;
        expect(newZoomLevel).to.eql(0.5);
      } finally {
        w.webContents.zoomLevel = 0;
      }
    });

    it('can set the correct zoom factor (functions)', async () => {
      const w = new BrowserWindow({ show: false });
      try {
        await w.loadURL('about:blank');
        const zoomFactor = w.webContents.getZoomFactor();
        expect(zoomFactor).to.eql(1.0);

        w.webContents.setZoomFactor(0.5);
        const newZoomFactor = w.webContents.getZoomFactor();
        expect(newZoomFactor).to.eql(0.5);
      } finally {
        w.webContents.setZoomFactor(1.0);
      }
    });

    it('can set the correct zoom factor (properties)', async () => {
      const w = new BrowserWindow({ show: false });
      try {
        await w.loadURL('about:blank');
        const zoomFactor = w.webContents.zoomFactor;
        expect(zoomFactor).to.eql(1.0);

        w.webContents.zoomFactor = 0.5;
        const newZoomFactor = w.webContents.zoomFactor;
        expect(newZoomFactor).to.eql(0.5);
      } finally {
        w.webContents.zoomFactor = 1.0;
      }
    });

    it('can persist zoom level across navigation', (done) => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      let finalNavigation = false;
      ipcMain.on('set-zoom', (e, host) => {
        const zoomLevel = hostZoomMap[host];
        if (!finalNavigation) w.webContents.zoomLevel = zoomLevel;
        e.sender.send(`${host}-zoom-set`);
      });
      ipcMain.on('host1-zoom-level', (e) => {
        try {
          const zoomLevel = e.sender.getZoomLevel();
          const expectedZoomLevel = hostZoomMap.host1;
          expect(zoomLevel).to.equal(expectedZoomLevel);
          if (finalNavigation) {
            done();
          } else {
            w.loadURL(`${standardScheme}://host2`);
          }
        } catch (e) {
          done(e);
        }
      });
      ipcMain.once('host2-zoom-level', (e) => {
        try {
          const zoomLevel = e.sender.getZoomLevel();
          const expectedZoomLevel = hostZoomMap.host2;
          expect(zoomLevel).to.equal(expectedZoomLevel);
          finalNavigation = true;
          w.webContents.goBack();
        } catch (e) {
          done(e);
        }
      });
      w.loadURL(`${standardScheme}://host1`);
    });

    it('can propagate zoom level across same session', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
      const w2 = new BrowserWindow({ show: false });

      defer(() => {
        w2.setClosable(true);
        w2.close();
      });

      await w.loadURL(`${standardScheme}://host3`);
      w.webContents.zoomLevel = hostZoomMap.host3;

      await w2.loadURL(`${standardScheme}://host3`);
      const zoomLevel1 = w.webContents.zoomLevel;
      expect(zoomLevel1).to.equal(hostZoomMap.host3);

      const zoomLevel2 = w2.webContents.zoomLevel;
      expect(zoomLevel1).to.equal(zoomLevel2);
    });

    it('cannot propagate zoom level across different session', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
      const w2 = new BrowserWindow({
        show: false,
        webPreferences: {
          partition: 'temp'
        }
      });
      const protocol = w2.webContents.session.protocol;
      protocol.registerStringProtocol(standardScheme, (request, callback) => {
        callback('hello');
      });

      defer(() => {
        w2.setClosable(true);
        w2.close();

        protocol.unregisterProtocol(standardScheme);
      });

      await w.loadURL(`${standardScheme}://host3`);
      w.webContents.zoomLevel = hostZoomMap.host3;

      await w2.loadURL(`${standardScheme}://host3`);
      const zoomLevel1 = w.webContents.zoomLevel;
      expect(zoomLevel1).to.equal(hostZoomMap.host3);

      const zoomLevel2 = w2.webContents.zoomLevel;
      expect(zoomLevel2).to.equal(0);
      expect(zoomLevel1).to.not.equal(zoomLevel2);
    });

    it('can persist when it contains iframe', (done) => {
      const w = new BrowserWindow({ show: false });
      const server = http.createServer((req, res) => {
        setTimeout(200).then(() => {
          res.end();
        });
      });
      listen(server).then(({ url }) => {
        const content = `<iframe src=${url}></iframe>`;
        w.webContents.on('did-frame-finish-load', (e, isMainFrame) => {
          if (!isMainFrame) {
            try {
              const zoomLevel = w.webContents.zoomLevel;
              expect(zoomLevel).to.equal(2.0);

              w.webContents.zoomLevel = 0;
              done();
            } catch (e) {
              done(e);
            } finally {
              server.close();
            }
          }
        });
        w.webContents.on('dom-ready', () => {
          w.webContents.zoomLevel = 2.0;
        });
        w.loadURL(`data:text/html,${content}`);
      });
    });

    it('cannot propagate when used with webframe', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      const w2 = new BrowserWindow({ show: false });

      const temporaryZoomSet = once(ipcMain, 'temporary-zoom-set');
      w.loadFile(path.join(fixturesPath, 'pages', 'webframe-zoom.html'));
      await temporaryZoomSet;

      const finalZoomLevel = w.webContents.getZoomLevel();
      await w2.loadFile(path.join(fixturesPath, 'pages', 'c.html'));
      const zoomLevel1 = w.webContents.zoomLevel;
      const zoomLevel2 = w2.webContents.zoomLevel;

      w2.setClosable(true);
      w2.close();

      expect(zoomLevel1).to.equal(finalZoomLevel);
      expect(zoomLevel2).to.equal(0);
      expect(zoomLevel1).to.not.equal(zoomLevel2);
    });

    describe('with unique domains', () => {
      let server: http.Server;
      let serverUrl: string;
      let crossSiteUrl: string;

      before(async () => {
        server = http.createServer((req, res) => {
          setTimeout().then(() => res.end('hey'));
        });
        serverUrl = (await listen(server)).url;
        crossSiteUrl = serverUrl.replace('127.0.0.1', 'localhost');
      });

      after(() => {
        server.close();
      });

      it('cannot persist zoom level after navigation with webFrame', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
        const source = `
          const {ipcRenderer, webFrame} = require('electron')
          webFrame.setZoomLevel(0.6)
          ipcRenderer.send('zoom-level-set', webFrame.getZoomLevel())
        `;
        const zoomLevelPromise = once(ipcMain, 'zoom-level-set');
        await w.loadURL(serverUrl);
        await w.webContents.executeJavaScript(source);
        let [, zoomLevel] = await zoomLevelPromise;
        expect(zoomLevel).to.equal(0.6);
        const loadPromise = once(w.webContents, 'did-finish-load');
        await w.loadURL(crossSiteUrl);
        await loadPromise;
        zoomLevel = w.webContents.zoomLevel;
        expect(zoomLevel).to.equal(0);
      });
    });
  });

  describe('webrtc ip policy api', () => {
    afterEach(closeAllWindows);
    it('can set and get webrtc ip policies', () => {
      const w = new BrowserWindow({ show: false });
      const policies = [
        'default',
        'default_public_interface_only',
        'default_public_and_private_interfaces',
        'disable_non_proxied_udp'
      ] as const;
      for (const policy of policies) {
        w.webContents.setWebRTCIPHandlingPolicy(policy);
        expect(w.webContents.getWebRTCIPHandlingPolicy()).to.equal(policy);
      }
    });
  });

  describe('webrtc udp port range policy api', () => {
    let w: BrowserWindow;
    beforeEach(() => {
      w = new BrowserWindow({ show: false });
    });

    afterEach(closeAllWindows);

    it('check default webrtc udp port range is { min: 0, max: 0 }', () => {
      const settings = w.webContents.getWebRTCUDPPortRange();
      expect(settings).to.deep.equal({ min: 0, max: 0 });
    });

    it('can set and get webrtc udp port range policy with correct arguments', () => {
      w.webContents.setWebRTCUDPPortRange({ min: 1, max: 65535 });
      const settings = w.webContents.getWebRTCUDPPortRange();
      expect(settings).to.deep.equal({ min: 1, max: 65535 });
    });

    it('can not set webrtc udp port range policy with invalid arguments', () => {
      expect(() => {
        w.webContents.setWebRTCUDPPortRange({ min: 0, max: 65535 });
      }).to.throw("'min' and 'max' must be in the (0, 65535] range or [0, 0]");
      expect(() => {
        w.webContents.setWebRTCUDPPortRange({ min: 1, max: 65536 });
      }).to.throw("'min' and 'max' must be in the (0, 65535] range or [0, 0]");
      expect(() => {
        w.webContents.setWebRTCUDPPortRange({ min: 60000, max: 56789 });
      }).to.throw("'max' must be greater than or equal to 'min'");
    });

    it('can reset webrtc udp port range policy to default with { min: 0, max: 0 }', () => {
      w.webContents.setWebRTCUDPPortRange({ min: 1, max: 65535 });
      const settings = w.webContents.getWebRTCUDPPortRange();
      expect(settings).to.deep.equal({ min: 1, max: 65535 });
      w.webContents.setWebRTCUDPPortRange({ min: 0, max: 0 });
      const defaultSetting = w.webContents.getWebRTCUDPPortRange();
      expect(defaultSetting).to.deep.equal({ min: 0, max: 0 });
    });
  });

  describe('opener api', () => {
    afterEach(closeAllWindows);
    it('can get opener with window.open()', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { sandbox: true } });
      await w.loadURL('about:blank');
      const childPromise = once(w.webContents, 'did-create-window') as Promise<[BrowserWindow, Electron.DidCreateWindowDetails]>;
      w.webContents.executeJavaScript('window.open("about:blank")', true);
      const [childWindow] = await childPromise;
      expect(childWindow.webContents.opener).to.equal(w.webContents.mainFrame);
    });
    it('has no opener when using "noopener"', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { sandbox: true } });
      await w.loadURL('about:blank');
      const childPromise = once(w.webContents, 'did-create-window') as Promise<[BrowserWindow, Electron.DidCreateWindowDetails]>;
      w.webContents.executeJavaScript('window.open("about:blank", undefined, "noopener")', true);
      const [childWindow] = await childPromise;
      expect(childWindow.webContents.opener).to.be.null();
    });
    it('can get opener with a[target=_blank][rel=opener]', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { sandbox: true } });
      await w.loadURL('about:blank');
      const childPromise = once(w.webContents, 'did-create-window') as Promise<[BrowserWindow, Electron.DidCreateWindowDetails]>;
      w.webContents.executeJavaScript(`(function() {
        const a = document.createElement('a');
        a.target = '_blank';
        a.rel = 'opener';
        a.href = 'about:blank';
        a.click();
      }())`, true);
      const [childWindow] = await childPromise;
      expect(childWindow.webContents.opener).to.equal(w.webContents.mainFrame);
    });
    it('has no opener with a[target=_blank][rel=noopener]', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { sandbox: true } });
      await w.loadURL('about:blank');
      const childPromise = once(w.webContents, 'did-create-window') as Promise<[BrowserWindow, Electron.DidCreateWindowDetails]>;
      w.webContents.executeJavaScript(`(function() {
        const a = document.createElement('a');
        a.target = '_blank';
        a.rel = 'noopener';
        a.href = 'about:blank';
        a.click();
      }())`, true);
      const [childWindow] = await childPromise;
      expect(childWindow.webContents.opener).to.be.null();
    });
  });

  describe('render view deleted events', () => {
    let server: http.Server;
    let serverUrl: string;
    let crossSiteUrl: string;

    before(async () => {
      server = http.createServer((req, res) => {
        const respond = () => {
          if (req.url === '/redirect-cross-site') {
            res.setHeader('Location', `${crossSiteUrl}/redirected`);
            res.statusCode = 302;
            res.end();
          } else if (req.url === '/redirected') {
            res.end('<html><script>window.localStorage</script></html>');
          } else if (req.url === '/first-window-open') {
            res.end(`<html><script>window.open('${serverUrl}/second-window-open', 'first child');</script></html>`);
          } else if (req.url === '/second-window-open') {
            res.end('<html><script>window.open(\'wrong://url\', \'second child\');</script></html>');
          } else {
            res.end();
          }
        };
        setTimeout().then(respond);
      });
      serverUrl = (await listen(server)).url;
      crossSiteUrl = serverUrl.replace('127.0.0.1', 'localhost');
    });

    after(() => {
      server.close();
    });

    afterEach(closeAllWindows);

    it('does not emit current-render-view-deleted when speculative RVHs are deleted', async () => {
      const w = new BrowserWindow({ show: false });
      let currentRenderViewDeletedEmitted = false;
      const renderViewDeletedHandler = () => {
        currentRenderViewDeletedEmitted = true;
      };
      w.webContents.on('current-render-view-deleted' as any, renderViewDeletedHandler);
      w.webContents.on('did-finish-load', () => {
        w.webContents.removeListener('current-render-view-deleted' as any, renderViewDeletedHandler);
        w.close();
      });
      const destroyed = once(w.webContents, 'destroyed');
      w.loadURL(`${serverUrl}/redirect-cross-site`);
      await destroyed;
      expect(currentRenderViewDeletedEmitted).to.be.false('current-render-view-deleted was emitted');
    });

    it('does not emit current-render-view-deleted when speculative RVHs are deleted', async () => {
      const parentWindow = new BrowserWindow({ show: false });
      let currentRenderViewDeletedEmitted = false;
      let childWindow: BrowserWindow | null = null;
      const destroyed = once(parentWindow.webContents, 'destroyed');
      const renderViewDeletedHandler = () => {
        currentRenderViewDeletedEmitted = true;
      };
      const childWindowCreated = new Promise<void>((resolve) => {
        app.once('browser-window-created', (event, window) => {
          childWindow = window;
          window.webContents.on('current-render-view-deleted' as any, renderViewDeletedHandler);
          resolve();
        });
      });
      parentWindow.loadURL(`${serverUrl}/first-window-open`);
      await childWindowCreated;
      childWindow!.webContents.removeListener('current-render-view-deleted' as any, renderViewDeletedHandler);
      parentWindow.close();
      await destroyed;
      expect(currentRenderViewDeletedEmitted).to.be.false('child window was destroyed');
    });

    it('emits current-render-view-deleted if the current RVHs are deleted', async () => {
      const w = new BrowserWindow({ show: false });
      let currentRenderViewDeletedEmitted = false;
      w.webContents.on('current-render-view-deleted' as any, () => {
        currentRenderViewDeletedEmitted = true;
      });
      w.webContents.on('did-finish-load', () => {
        w.close();
      });
      const destroyed = once(w.webContents, 'destroyed');
      w.loadURL(`${serverUrl}/redirect-cross-site`);
      await destroyed;
      expect(currentRenderViewDeletedEmitted).to.be.true('current-render-view-deleted wasn\'t emitted');
    });

    it('emits render-view-deleted if any RVHs are deleted', async () => {
      const w = new BrowserWindow({ show: false });
      let rvhDeletedCount = 0;
      w.webContents.on('render-view-deleted' as any, () => {
        rvhDeletedCount++;
      });
      w.webContents.on('did-finish-load', () => {
        w.close();
      });
      const destroyed = once(w.webContents, 'destroyed');
      w.loadURL(`${serverUrl}/redirect-cross-site`);
      await destroyed;
      const expectedRenderViewDeletedEventCount = 1;
      expect(rvhDeletedCount).to.equal(expectedRenderViewDeletedEventCount, 'render-view-deleted wasn\'t emitted the expected nr. of times');
    });
  });

  describe('setIgnoreMenuShortcuts(ignore)', () => {
    afterEach(closeAllWindows);
    it('does not throw', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        w.webContents.setIgnoreMenuShortcuts(true);
        w.webContents.setIgnoreMenuShortcuts(false);
      }).to.not.throw();
    });
  });

  const crashPrefs = [
    {
      nodeIntegration: true
    },
    {
      sandbox: true
    }
  ];

  const nicePrefs = (o: any) => {
    let s = '';
    for (const key of Object.keys(o)) {
      s += `${key}=${o[key]}, `;
    }
    return `(${s.slice(0, s.length - 2)})`;
  };

  for (const prefs of crashPrefs) {
    describe(`crash  with webPreferences ${nicePrefs(prefs)}`, () => {
      let w: BrowserWindow;
      beforeEach(async () => {
        w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
        await w.loadURL('about:blank');
      });
      afterEach(closeAllWindows);

      it('isCrashed() is false by default', () => {
        expect(w.webContents.isCrashed()).to.equal(false);
      });

      it('forcefullyCrashRenderer() crashes the process with reason=killed||crashed', async () => {
        expect(w.webContents.isCrashed()).to.equal(false);
        const crashEvent = once(w.webContents, 'render-process-gone') as Promise<[any, Electron.RenderProcessGoneDetails]>;
        w.webContents.forcefullyCrashRenderer();
        const [, details] = await crashEvent;
        expect(details.reason === 'killed' || details.reason === 'crashed').to.equal(true, 'reason should be killed || crashed');
        expect(w.webContents.isCrashed()).to.equal(true);
      });

      it('a crashed process is recoverable with reload()', async () => {
        expect(w.webContents.isCrashed()).to.equal(false);
        w.webContents.forcefullyCrashRenderer();
        w.webContents.reload();
        expect(w.webContents.isCrashed()).to.equal(false);
      });
    });
  }

  // Destroying webContents in its event listener is going to crash when
  // Electron is built in Debug mode.
  describe('destroy()', () => {
    let server: http.Server;
    let serverUrl: string;

    before((done) => {
      server = http.createServer((request, response) => {
        switch (request.url) {
          case '/net-error':
            response.destroy();
            break;
          case '/200':
            response.end();
            break;
          default:
            done(new Error('unsupported endpoint'));
        }
      });
      listen(server).then(({ url }) => {
        serverUrl = url;
        done();
      });
    });

    after(() => {
      server.close();
    });

    const events = [
      { name: 'did-start-loading', url: '/200' },
      { name: 'dom-ready', url: '/200' },
      { name: 'did-stop-loading', url: '/200' },
      { name: 'did-finish-load', url: '/200' },
      // FIXME: Multiple Emit calls inside an observer assume that object
      // will be alive till end of the observer. Synchronous `destroy` api
      // violates this contract and crashes.
      { name: 'did-frame-finish-load', url: '/200' },
      { name: 'did-fail-load', url: '/net-error' }
    ];
    for (const e of events) {
      it(`should not crash when invoked synchronously inside ${e.name} handler`, async function () {
        // This test is flaky on Windows CI and we don't know why, but the
        // purpose of this test is to make sure Electron does not crash so it
        // is fine to retry this test for a few times.
        this.retries(3);

        const contents = (webContents as typeof ElectronInternal.WebContents).create();
        const originalEmit = contents.emit.bind(contents);
        contents.emit = (...args) => { return originalEmit(...args); };
        contents.once(e.name as any, () => contents.destroy());
        const destroyed = once(contents, 'destroyed');
        contents.loadURL(serverUrl + e.url);
        await destroyed;
      });
    }
  });

  describe('did-change-theme-color event', () => {
    afterEach(closeAllWindows);
    it('is triggered with correct theme color', (done) => {
      const w = new BrowserWindow({ show: true });
      let count = 0;
      w.webContents.on('did-change-theme-color', (e, color) => {
        try {
          if (count === 0) {
            count += 1;
            expect(color).to.equal('#FFEEDD');
            w.loadFile(path.join(fixturesPath, 'pages', 'base-page.html'));
          } else if (count === 1) {
            expect(color).to.be.null();
            done();
          }
        } catch (e) {
          done(e);
        }
      });
      w.loadFile(path.join(fixturesPath, 'pages', 'theme-color.html'));
    });
  });

  describe('console-message event', () => {
    afterEach(closeAllWindows);
    it('is triggered with correct log message', (done) => {
      const w = new BrowserWindow({ show: true });
      w.webContents.on('console-message', (e, level, message) => {
        // Don't just assert as Chromium might emit other logs that we should ignore.
        if (message === 'a') {
          done();
        }
      });
      w.loadFile(path.join(fixturesPath, 'pages', 'a.html'));
    });
  });

  describe('ipc-message event', () => {
    afterEach(closeAllWindows);
    it('emits when the renderer process sends an asynchronous message', async () => {
      const w = new BrowserWindow({ show: true, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      await w.webContents.loadURL('about:blank');
      w.webContents.executeJavaScript(`
        require('electron').ipcRenderer.send('message', 'Hello World!')
      `);

      const [, channel, message] = await once(w.webContents, 'ipc-message');
      expect(channel).to.equal('message');
      expect(message).to.equal('Hello World!');
    });
  });

  describe('ipc-message-sync event', () => {
    afterEach(closeAllWindows);
    it('emits when the renderer process sends a synchronous message', async () => {
      const w = new BrowserWindow({ show: true, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      await w.webContents.loadURL('about:blank');
      const promise: Promise<[string, string]> = new Promise(resolve => {
        w.webContents.once('ipc-message-sync', (event, channel, arg) => {
          event.returnValue = 'foobar';
          resolve([channel, arg]);
        });
      });
      const result = await w.webContents.executeJavaScript(`
        require('electron').ipcRenderer.sendSync('message', 'Hello World!')
      `);

      const [channel, message] = await promise;
      expect(channel).to.equal('message');
      expect(message).to.equal('Hello World!');
      expect(result).to.equal('foobar');
    });
  });

  describe('referrer', () => {
    afterEach(closeAllWindows);
    it('propagates referrer information to new target=_blank windows', (done) => {
      const w = new BrowserWindow({ show: false });
      const server = http.createServer((req, res) => {
        if (req.url === '/should_have_referrer') {
          try {
            expect(req.headers.referer).to.equal(`http://127.0.0.1:${(server.address() as AddressInfo).port}/`);
            return done();
          } catch (e) {
            return done(e);
          } finally {
            server.close();
          }
        }
        res.end('<a id="a" href="/should_have_referrer" target="_blank">link</a>');
      });
      listen(server).then(({ url }) => {
        w.webContents.once('did-finish-load', () => {
          w.webContents.setWindowOpenHandler(details => {
            expect(details.referrer.url).to.equal(url + '/');
            expect(details.referrer.policy).to.equal('strict-origin-when-cross-origin');
            return { action: 'allow' };
          });
          w.webContents.executeJavaScript('a.click()');
        });
        w.loadURL(url);
      });
    });

    it('propagates referrer information to windows opened with window.open', (done) => {
      const w = new BrowserWindow({ show: false });
      const server = http.createServer((req, res) => {
        if (req.url === '/should_have_referrer') {
          try {
            expect(req.headers.referer).to.equal(`http://127.0.0.1:${(server.address() as AddressInfo).port}/`);
            return done();
          } catch (e) {
            return done(e);
          }
        }
        res.end('');
      });
      listen(server).then(({ url }) => {
        w.webContents.once('did-finish-load', () => {
          w.webContents.setWindowOpenHandler(details => {
            expect(details.referrer.url).to.equal(url + '/');
            expect(details.referrer.policy).to.equal('strict-origin-when-cross-origin');
            return { action: 'allow' };
          });
          w.webContents.executeJavaScript('window.open(location.href + "should_have_referrer")');
        });
        w.loadURL(url);
      });
    });
  });

  describe('webframe messages in sandboxed contents', () => {
    afterEach(closeAllWindows);
    it('responds to executeJavaScript', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { sandbox: true } });
      await w.loadURL('about:blank');
      const result = await w.webContents.executeJavaScript('37 + 5');
      expect(result).to.equal(42);
    });
  });

  describe('preload-error event', () => {
    afterEach(closeAllWindows);
    const generateSpecs = (description: string, sandbox: boolean) => {
      describe(description, () => {
        it('is triggered when unhandled exception is thrown', async () => {
          const preload = path.join(fixturesPath, 'module', 'preload-error-exception.js');

          const w = new BrowserWindow({
            show: false,
            webPreferences: {
              sandbox,
              preload
            }
          });

          const promise = once(w.webContents, 'preload-error') as Promise<[any, string, Error]>;
          w.loadURL('about:blank');

          const [, preloadPath, error] = await promise;
          expect(preloadPath).to.equal(preload);
          expect(error.message).to.equal('Hello World!');
        });

        it('is triggered on syntax errors', async () => {
          const preload = path.join(fixturesPath, 'module', 'preload-error-syntax.js');

          const w = new BrowserWindow({
            show: false,
            webPreferences: {
              sandbox,
              preload
            }
          });

          const promise = once(w.webContents, 'preload-error') as Promise<[any, string, Error]>;
          w.loadURL('about:blank');

          const [, preloadPath, error] = await promise;
          expect(preloadPath).to.equal(preload);
          expect(error.message).to.equal('foobar is not defined');
        });

        it('is triggered when preload script loading fails', async () => {
          const preload = path.join(fixturesPath, 'module', 'preload-invalid.js');

          const w = new BrowserWindow({
            show: false,
            webPreferences: {
              sandbox,
              preload
            }
          });

          const promise = once(w.webContents, 'preload-error') as Promise<[any, string, Error]>;
          w.loadURL('about:blank');

          const [, preloadPath, error] = await promise;
          expect(preloadPath).to.equal(preload);
          expect(error.message).to.contain('preload-invalid.js');
        });
      });
    };

    generateSpecs('without sandbox', false);
    generateSpecs('with sandbox', true);
  });

  describe('takeHeapSnapshot()', () => {
    afterEach(closeAllWindows);

    it('works with sandboxed renderers', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          sandbox: true
        }
      });

      await w.loadURL('about:blank');

      const filePath = path.join(app.getPath('temp'), 'test.heapsnapshot');

      const cleanup = () => {
        try {
          fs.unlinkSync(filePath);
        } catch {
          // ignore error
        }
      };

      try {
        await w.webContents.takeHeapSnapshot(filePath);
        const stats = fs.statSync(filePath);
        expect(stats.size).not.to.be.equal(0);
      } finally {
        cleanup();
      }
    });

    it('fails with invalid file path', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          sandbox: true
        }
      });

      await w.loadURL('about:blank');

      const badPath = path.join('i', 'am', 'a', 'super', 'bad', 'path');
      const promise = w.webContents.takeHeapSnapshot(badPath);
      return expect(promise).to.be.eventually.rejectedWith(Error, `Failed to take heap snapshot with invalid file path ${badPath}`);
    });

    it('fails with invalid render process', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          sandbox: true
        }
      });

      const filePath = path.join(app.getPath('temp'), 'test.heapsnapshot');

      w.webContents.destroy();
      const promise = w.webContents.takeHeapSnapshot(filePath);
      return expect(promise).to.be.eventually.rejectedWith(Error, 'Failed to take heap snapshot with nonexistent render frame');
    });
  });

  describe('setBackgroundThrottling()', () => {
    afterEach(closeAllWindows);
    it('does not crash when allowing', () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.setBackgroundThrottling(true);
    });

    it('does not crash when called via BrowserWindow', () => {
      const w = new BrowserWindow({ show: false });

      w.setBackgroundThrottling(true);
    });

    it('does not crash when disallowing', () => {
      const w = new BrowserWindow({ show: false, webPreferences: { backgroundThrottling: true } });

      w.webContents.setBackgroundThrottling(false);
    });
  });

  describe('getBackgroundThrottling()', () => {
    afterEach(closeAllWindows);
    it('works via getter', () => {
      const w = new BrowserWindow({ show: false });

      w.webContents.setBackgroundThrottling(false);
      expect(w.webContents.getBackgroundThrottling()).to.equal(false);

      w.webContents.setBackgroundThrottling(true);
      expect(w.webContents.getBackgroundThrottling()).to.equal(true);
    });

    it('works via property', () => {
      const w = new BrowserWindow({ show: false });

      w.webContents.backgroundThrottling = false;
      expect(w.webContents.backgroundThrottling).to.equal(false);

      w.webContents.backgroundThrottling = true;
      expect(w.webContents.backgroundThrottling).to.equal(true);
    });

    it('works via BrowserWindow', () => {
      const w = new BrowserWindow({ show: false });

      w.setBackgroundThrottling(false);
      expect(w.getBackgroundThrottling()).to.equal(false);

      w.setBackgroundThrottling(true);
      expect(w.getBackgroundThrottling()).to.equal(true);
    });
  });

  ifdescribe(features.isPrintingEnabled())('getPrintersAsync()', () => {
    afterEach(closeAllWindows);
    it('can get printer list', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { sandbox: true } });
      await w.loadURL('about:blank');
      const printers = await w.webContents.getPrintersAsync();
      expect(printers).to.be.an('array');
    });
  });

  ifdescribe(features.isPrintingEnabled())('printToPDF()', () => {
    let w: BrowserWindow;

    beforeEach(() => {
      w = new BrowserWindow({
        show: false,
        webPreferences: {
          sandbox: true
        }
      });
    });

    afterEach(closeAllWindows);

    it('rejects on incorrectly typed parameters', async () => {
      const badTypes = {
        landscape: [],
        displayHeaderFooter: '123',
        printBackground: 2,
        scale: 'not-a-number',
        pageSize: 'IAmAPageSize',
        margins: 'terrible',
        pageRanges: { oops: 'im-not-the-right-key' },
        headerTemplate: [1, 2, 3],
        footerTemplate: [4, 5, 6],
        preferCSSPageSize: 'no',
        generateTaggedPDF: 'wtf',
        generateDocumentOutline: [7, 8, 9]
      };

      await w.loadURL('data:text/html,<h1>Hello, World!</h1>');

      // These will hard crash in Chromium unless we type-check
      for (const [key, value] of Object.entries(badTypes)) {
        const param = { [key]: value };
        await expect(w.webContents.printToPDF(param)).to.eventually.be.rejected();
      }
    });

    it('rejects when margins exceed physical page size', async () => {
      await w.loadURL('data:text/html,<h1>Hello, World!</h1>');

      await expect(w.webContents.printToPDF({
        pageSize: 'Letter',
        margins: {
          top: 100,
          bottom: 100,
          left: 5,
          right: 5
        }
      })).to.eventually.be.rejectedWith('margins must be less than or equal to pageSize');
    });

    it('does not crash when called multiple times in parallel', async () => {
      await w.loadURL('data:text/html,<h1>Hello, World!</h1>');

      const promises = [];
      for (let i = 0; i < 3; i++) {
        promises.push(w.webContents.printToPDF({}));
      }

      const results = await Promise.all(promises);
      for (const data of results) {
        expect(data).to.be.an.instanceof(Buffer).that.is.not.empty();
      }
    });

    it('does not crash when called multiple times in sequence', async () => {
      await w.loadURL('data:text/html,<h1>Hello, World!</h1>');

      const results = [];
      for (let i = 0; i < 3; i++) {
        const result = await w.webContents.printToPDF({});
        results.push(result);
      }

      for (const data of results) {
        expect(data).to.be.an.instanceof(Buffer).that.is.not.empty();
      }
    });

    it('can print a PDF with default settings', async () => {
      await w.loadURL('data:text/html,<h1>Hello, World!</h1>');

      const data = await w.webContents.printToPDF({});
      expect(data).to.be.an.instanceof(Buffer).that.is.not.empty();
    });

    type PageSizeString = Exclude<Required<Electron.PrintToPDFOptions>['pageSize'], Electron.Size>;

    it('with custom page sizes', async () => {
      const paperFormats: Record<PageSizeString, ElectronInternal.PageSize> = {
        Letter: { width: 8.5, height: 11 },
        Legal: { width: 8.5, height: 14 },
        Tabloid: { width: 11, height: 17 },
        Ledger: { width: 17, height: 11 },
        A0: { width: 33.1, height: 46.8 },
        A1: { width: 23.4, height: 33.1 },
        A2: { width: 16.54, height: 23.4 },
        A3: { width: 11.7, height: 16.54 },
        A4: { width: 8.27, height: 11.7 },
        A5: { width: 5.83, height: 8.27 },
        A6: { width: 4.13, height: 5.83 }
      };

      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'print-to-pdf-small.html'));

      for (const format of Object.keys(paperFormats) as PageSizeString[]) {
        const data = await w.webContents.printToPDF({ pageSize: format });

        const doc = await pdfjs.getDocument(data).promise;
        const page = await doc.getPage(1);

        // page.view is [top, left, width, height].
        const width = page.view[2] / 72;
        const height = page.view[3] / 72;

        const approxEq = (a: number, b: number, epsilon = 0.01) => Math.abs(a - b) <= epsilon;

        expect(approxEq(width, paperFormats[format].width)).to.be.true();
        expect(approxEq(height, paperFormats[format].height)).to.be.true();
      }
    });

    it('with custom header and footer', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'print-to-pdf-small.html'));

      const data = await w.webContents.printToPDF({
        displayHeaderFooter: true,
        headerTemplate: '<div>I\'m a PDF header</div>',
        footerTemplate: '<div>I\'m a PDF footer</div>'
      });

      const doc = await pdfjs.getDocument(data).promise;
      const page = await doc.getPage(1);

      const { items } = await page.getTextContent();

      // Check that generated PDF contains a header.
      const containsText = (text: RegExp) => items.some(({ str }: { str: string }) => str.match(text));

      expect(containsText(/I'm a PDF header/)).to.be.true();
      expect(containsText(/I'm a PDF footer/)).to.be.true();
    });

    it('in landscape mode', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'print-to-pdf-small.html'));

      const data = await w.webContents.printToPDF({ landscape: true });
      const doc = await pdfjs.getDocument(data).promise;
      const page = await doc.getPage(1);

      // page.view is [top, left, width, height].
      const width = page.view[2];
      const height = page.view[3];

      expect(width).to.be.greaterThan(height);
    });

    it('with custom page ranges', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'print-to-pdf-large.html'));

      const data = await w.webContents.printToPDF({
        pageRanges: '1-3',
        landscape: true
      });

      const doc = await pdfjs.getDocument(data).promise;

      // Check that correct # of pages are rendered.
      expect(doc.numPages).to.equal(3);
    });

    it('does not tag PDFs by default', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'print-to-pdf-small.html'));

      const data = await w.webContents.printToPDF({});
      const doc = await pdfjs.getDocument(data).promise;
      const markInfo = await doc.getMarkInfo();
      expect(markInfo).to.be.null();
    });

    it('can generate tag data for PDFs', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'print-to-pdf-small.html'));

      const data = await w.webContents.printToPDF({ generateTaggedPDF: true });
      const doc = await pdfjs.getDocument(data).promise;
      const markInfo = await doc.getMarkInfo();
      expect(markInfo).to.deep.equal({
        Marked: true,
        UserProperties: false,
        Suspects: false
      });
    });
  });

  describe('PictureInPicture video', () => {
    afterEach(closeAllWindows);
    it('works as expected', async function () {
      const w = new BrowserWindow({ webPreferences: { sandbox: true } });

      // TODO(codebytere): figure out why this workaround is needed and remove.
      // It is not germane to the actual test.
      await w.loadFile(path.join(fixturesPath, 'blank.html'));

      await w.loadFile(path.join(fixturesPath, 'api', 'picture-in-picture.html'));

      await w.webContents.executeJavaScript('document.createElement(\'video\').canPlayType(\'video/webm; codecs="vp8.0"\')', true);

      const result = await w.webContents.executeJavaScript('runTest(true)', true);
      expect(result).to.be.true();
    });
  });

  describe('Shared Workers', () => {
    afterEach(closeAllWindows);

    it('can get multiple shared workers', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });

      const ready = once(ipcMain, 'ready');
      w.loadFile(path.join(fixturesPath, 'api', 'shared-worker', 'shared-worker.html'));
      await ready;

      const sharedWorkers = w.webContents.getAllSharedWorkers();

      expect(sharedWorkers).to.have.lengthOf(2);
      expect(sharedWorkers[0].url).to.contain('shared-worker');
      expect(sharedWorkers[1].url).to.contain('shared-worker');
    });

    it('can inspect a specific shared worker', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });

      const ready = once(ipcMain, 'ready');
      w.loadFile(path.join(fixturesPath, 'api', 'shared-worker', 'shared-worker.html'));
      await ready;

      const sharedWorkers = w.webContents.getAllSharedWorkers();

      const devtoolsOpened = once(w.webContents, 'devtools-opened');
      w.webContents.inspectSharedWorkerById(sharedWorkers[0].id);
      await devtoolsOpened;

      const devtoolsClosed = once(w.webContents, 'devtools-closed');
      w.webContents.closeDevTools();
      await devtoolsClosed;
    });
  });

  describe('login event', () => {
    afterEach(closeAllWindows);

    let server: http.Server;
    let serverUrl: string;
    let serverPort: number;
    let proxyServer: http.Server;
    let proxyServerPort: number;

    before(async () => {
      server = http.createServer((request, response) => {
        if (request.url === '/no-auth') {
          return response.end('ok');
        }
        if (request.headers.authorization) {
          response.writeHead(200, { 'Content-type': 'text/plain' });
          return response.end(request.headers.authorization);
        }
        response
          .writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' })
          .end('401');
      });
      ({ port: serverPort, url: serverUrl } = await listen(server));
    });

    before(async () => {
      proxyServer = http.createServer((request, response) => {
        if (request.headers['proxy-authorization']) {
          response.writeHead(200, { 'Content-type': 'text/plain' });
          return response.end(request.headers['proxy-authorization']);
        }
        response
          .writeHead(407, { 'Proxy-Authenticate': 'Basic realm="Foo"' })
          .end();
      });
      proxyServerPort = (await listen(proxyServer)).port;
    });

    afterEach(async () => {
      await session.defaultSession.clearAuthCache();
    });

    after(() => {
      server.close();
      proxyServer.close();
    });

    it('is emitted when navigating', async () => {
      const [user, pass] = ['user', 'pass'];
      const w = new BrowserWindow({ show: false });
      let eventRequest: any;
      let eventAuthInfo: any;
      w.webContents.on('login', (event, request, authInfo, cb) => {
        eventRequest = request;
        eventAuthInfo = authInfo;
        event.preventDefault();
        cb(user, pass);
      });
      await w.loadURL(serverUrl);
      const body = await w.webContents.executeJavaScript('document.documentElement.textContent');
      expect(body).to.equal(`Basic ${Buffer.from(`${user}:${pass}`).toString('base64')}`);
      expect(eventRequest.url).to.equal(serverUrl + '/');
      expect(eventAuthInfo.isProxy).to.be.false();
      expect(eventAuthInfo.scheme).to.equal('basic');
      expect(eventAuthInfo.host).to.equal('127.0.0.1');
      expect(eventAuthInfo.port).to.equal(serverPort);
      expect(eventAuthInfo.realm).to.equal('Foo');
    });

    it('is emitted when a proxy requests authorization', async () => {
      const customSession = session.fromPartition(`${Math.random()}`);
      await customSession.setProxy({ proxyRules: `127.0.0.1:${proxyServerPort}`, proxyBypassRules: '<-loopback>' });
      const [user, pass] = ['user', 'pass'];
      const w = new BrowserWindow({ show: false, webPreferences: { session: customSession } });
      let eventRequest: any;
      let eventAuthInfo: any;
      w.webContents.on('login', (event, request, authInfo, cb) => {
        eventRequest = request;
        eventAuthInfo = authInfo;
        event.preventDefault();
        cb(user, pass);
      });
      await w.loadURL(`${serverUrl}/no-auth`);
      const body = await w.webContents.executeJavaScript('document.documentElement.textContent');
      expect(body).to.equal(`Basic ${Buffer.from(`${user}:${pass}`).toString('base64')}`);
      expect(eventRequest.url).to.equal(`${serverUrl}/no-auth`);
      expect(eventAuthInfo.isProxy).to.be.true();
      expect(eventAuthInfo.scheme).to.equal('basic');
      expect(eventAuthInfo.host).to.equal('127.0.0.1');
      expect(eventAuthInfo.port).to.equal(proxyServerPort);
      expect(eventAuthInfo.realm).to.equal('Foo');
    });

    it('cancels authentication when callback is called with no arguments', async () => {
      const w = new BrowserWindow({ show: false });
      w.webContents.on('login', (event, request, authInfo, cb) => {
        event.preventDefault();
        cb();
      });
      await w.loadURL(serverUrl);
      const body = await w.webContents.executeJavaScript('document.documentElement.textContent');
      expect(body).to.equal('401');
    });
  });

  describe('page-title-updated event', () => {
    afterEach(closeAllWindows);
    it('is emitted with a full title for pages with no navigation', async () => {
      const bw = new BrowserWindow({ show: false });
      await bw.loadURL('about:blank');
      bw.webContents.executeJavaScript('child = window.open("", "", "show=no"); null');
      const [, child] = await once(app, 'web-contents-created') as [any, WebContents];
      bw.webContents.executeJavaScript('child.document.title = "new title"');
      const [, title] = await once(child, 'page-title-updated') as [any, string];
      expect(title).to.equal('new title');
    });
  });

  describe('context-menu event', () => {
    afterEach(closeAllWindows);
    it('emits when right-clicked in page', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixturesPath, 'pages', 'base-page.html'));

      const promise = once(w.webContents, 'context-menu') as Promise<[any, Electron.ContextMenuParams]>;

      // Simulate right-click to create context-menu event.
      const opts = { x: 0, y: 0, button: 'right' as const };
      w.webContents.sendInputEvent({ ...opts, type: 'mouseDown' });
      w.webContents.sendInputEvent({ ...opts, type: 'mouseUp' });

      const [, params] = await promise;

      expect(params.pageURL).to.equal(w.webContents.getURL());
      expect(params.frame).to.be.an('object');
      expect(params.x).to.be.a('number');
      expect(params.y).to.be.a('number');
    });
  });

  describe('close() method', () => {
    afterEach(closeAllWindows);

    it('closes when close() is called', async () => {
      const w = (webContents as typeof ElectronInternal.WebContents).create();
      const destroyed = once(w, 'destroyed');
      w.close();
      await destroyed;
      expect(w.isDestroyed()).to.be.true();
    });

    it('closes when close() is called after loading a page', async () => {
      const w = (webContents as typeof ElectronInternal.WebContents).create();
      await w.loadURL('about:blank');
      const destroyed = once(w, 'destroyed');
      w.close();
      await destroyed;
      expect(w.isDestroyed()).to.be.true();
    });

    it('can be GCed before loading a page', async () => {
      const v8Util = process._linkedBinding('electron_common_v8_util');
      let registry: FinalizationRegistry<unknown> | null = null;
      const cleanedUp = new Promise<number>(resolve => {
        registry = new FinalizationRegistry(resolve as any);
      });
      (() => {
        const w = (webContents as typeof ElectronInternal.WebContents).create();
        registry!.register(w, 42);
      })();
      const i = setInterval(() => v8Util.requestGarbageCollectionForTesting(), 100);
      defer(() => clearInterval(i));
      expect(await cleanedUp).to.equal(42);
    });

    it('causes its parent browserwindow to be closed', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');
      const closed = once(w, 'closed');
      w.webContents.close();
      await closed;
      expect(w.isDestroyed()).to.be.true();
    });

    it('ignores beforeunload if waitForBeforeUnload not specified', async () => {
      const w = (webContents as typeof ElectronInternal.WebContents).create();
      await w.loadURL('about:blank');
      await w.executeJavaScript('window.onbeforeunload = () => "hello"; null');
      w.on('will-prevent-unload', () => { throw new Error('unexpected will-prevent-unload'); });
      const destroyed = once(w, 'destroyed');
      w.close();
      await destroyed;
      expect(w.isDestroyed()).to.be.true();
    });

    it('runs beforeunload if waitForBeforeUnload is specified', async () => {
      const w = (webContents as typeof ElectronInternal.WebContents).create();
      await w.loadURL('about:blank');
      await w.executeJavaScript('window.onbeforeunload = () => "hello"; null');
      const willPreventUnload = once(w, 'will-prevent-unload');
      w.close({ waitForBeforeUnload: true });
      await willPreventUnload;
      expect(w.isDestroyed()).to.be.false();
    });

    it('overriding beforeunload prevention results in webcontents close', async () => {
      const w = (webContents as typeof ElectronInternal.WebContents).create();
      await w.loadURL('about:blank');
      await w.executeJavaScript('window.onbeforeunload = () => "hello"; null');
      w.once('will-prevent-unload', e => e.preventDefault());
      const destroyed = once(w, 'destroyed');
      w.close({ waitForBeforeUnload: true });
      await destroyed;
      expect(w.isDestroyed()).to.be.true();
    });
  });

  describe('content-bounds-updated event', () => {
    afterEach(closeAllWindows);
    it('emits when moveTo is called', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('window.moveTo(50, 50)', true);
      const [, rect] = await once(w.webContents, 'content-bounds-updated') as [any, Electron.Rectangle];
      const { width, height } = w.getBounds();
      expect(rect).to.deep.equal({
        x: 50,
        y: 50,
        width,
        height
      });
      await new Promise(setImmediate);
      expect(w.getBounds().x).to.equal(50);
      expect(w.getBounds().y).to.equal(50);
    });

    it('emits when resizeTo is called', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('window.resizeTo(100, 100)', true);
      const [, rect] = await once(w.webContents, 'content-bounds-updated') as [any, Electron.Rectangle];
      const { x, y } = w.getBounds();
      expect(rect).to.deep.equal({
        x,
        y,
        width: 100,
        height: 100
      });
      await new Promise(setImmediate);
      expect({
        width: w.getBounds().width,
        height: w.getBounds().height
      }).to.deep.equal(process.platform === 'win32' ? {
        // The width is reported as being larger on Windows? I'm not sure why
        // this is.
        width: 136,
        height: 100
      } : {
        width: 100,
        height: 100
      });
    });

    it('does not change window bounds if cancelled', async () => {
      const w = new BrowserWindow({ show: false });
      const { width, height } = w.getBounds();
      w.loadURL('about:blank');
      w.webContents.once('content-bounds-updated', e => e.preventDefault());
      await w.webContents.executeJavaScript('window.resizeTo(100, 100)', true);
      await new Promise(setImmediate);
      expect(w.getBounds().width).to.equal(width);
      expect(w.getBounds().height).to.equal(height);
    });
  });
});
