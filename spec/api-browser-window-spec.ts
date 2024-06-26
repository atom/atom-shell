import { expect } from 'chai';
import * as childProcess from 'node:child_process';
import * as path from 'node:path';
import * as fs from 'node:fs';
import * as qs from 'node:querystring';
import * as http from 'node:http';
import * as os from 'node:os';
import { AddressInfo } from 'node:net';
import { app, BrowserWindow, BrowserView, dialog, ipcMain, OnBeforeSendHeadersListenerDetails, protocol, screen, webContents, webFrameMain, session, WebContents, WebFrameMain } from 'electron/main';

import { emittedUntil, emittedNTimes } from './lib/events-helpers';
import { ifit, ifdescribe, defer, listen } from './lib/spec-helpers';
import { closeWindow, closeAllWindows } from './lib/window-helpers';
import { HexColors, hasCapturableScreen, ScreenCapture } from './lib/screen-helpers';
import { once } from 'node:events';
import { setTimeout } from 'node:timers/promises';
import { setTimeout as syncSetTimeout } from 'node:timers';

const fixtures = path.resolve(__dirname, 'fixtures');
const mainFixtures = path.resolve(__dirname, 'fixtures');

// Is the display's scale factor possibly causing rounding of pixel coordinate
// values?
const isScaleFactorRounding = () => {
  const { scaleFactor } = screen.getPrimaryDisplay();
  // Return true if scale factor is non-integer value
  if (Math.round(scaleFactor) !== scaleFactor) return true;
  // Return true if scale factor is odd number above 2
  return scaleFactor > 2 && scaleFactor % 2 === 1;
};

const expectBoundsEqual = (actual: any, expected: any) => {
  if (!isScaleFactorRounding()) {
    expect(actual).to.deep.equal(expected);
  } else if (Array.isArray(actual)) {
    expect(actual[0]).to.be.closeTo(expected[0], 1);
    expect(actual[1]).to.be.closeTo(expected[1], 1);
  } else {
    expect(actual.x).to.be.closeTo(expected.x, 1);
    expect(actual.y).to.be.closeTo(expected.y, 1);
    expect(actual.width).to.be.closeTo(expected.width, 1);
    expect(actual.height).to.be.closeTo(expected.height, 1);
  }
};

const isBeforeUnload = (event: Event, level: number, message: string) => {
  return (message === 'beforeunload');
};

describe('BrowserWindow module', () => {
  it('sets the correct class name on the prototype', () => {
    expect(BrowserWindow.prototype.constructor.name).to.equal('BrowserWindow');
  });

  describe('BrowserWindow constructor', () => {
    it('allows passing void 0 as the webContents', async () => {
      expect(() => {
        const w = new BrowserWindow({
          show: false,
          // apparently void 0 had different behaviour from undefined in the
          // issue that this test is supposed to catch.
          webContents: void 0 // eslint-disable-line no-void
        } as any);
        w.destroy();
      }).not.to.throw();
    });

    ifit(process.platform === 'linux')('does not crash when setting large window icons', async () => {
      const appPath = path.join(fixtures, 'apps', 'xwindow-icon');
      const appProcess = childProcess.spawn(process.execPath, [appPath]);
      await once(appProcess, 'exit');
    });

    it('does not crash or throw when passed an invalid icon', async () => {
      expect(() => {
        const w = new BrowserWindow({
          icon: undefined
        } as any);
        w.destroy();
      }).not.to.throw();
    });
  });

  describe('garbage collection', () => {
    const v8Util = process._linkedBinding('electron_common_v8_util');
    afterEach(closeAllWindows);

    it('window does not get garbage collected when opened', async () => {
      const w = new BrowserWindow({ show: false });
      // Keep a weak reference to the window.
      const wr = new WeakRef(w);
      await setTimeout();
      // Do garbage collection, since |w| is not referenced in this closure
      // it would be gone after next call if there is no other reference.
      v8Util.requestGarbageCollectionForTesting();

      await setTimeout();
      expect(wr.deref()).to.not.be.undefined();
    });
  });

  describe('BrowserWindow.close()', () => {
    let w: BrowserWindow;
    beforeEach(() => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
    });
    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });

    it('should work if called when a messageBox is showing', async () => {
      const closed = once(w, 'closed');
      dialog.showMessageBox(w, { message: 'Hello Error' });
      w.close();
      await closed;
    });

    it('closes window without rounded corners', async () => {
      await closeWindow(w);
      w = new BrowserWindow({ show: false, frame: false, roundedCorners: false });
      const closed = once(w, 'closed');
      w.close();
      await closed;
    });

    it('should not crash if called after webContents is destroyed', () => {
      w.webContents.destroy();
      w.webContents.on('destroyed', () => w.close());
    });

    it('should allow access to id after destruction', async () => {
      const closed = once(w, 'closed');
      w.destroy();
      await closed;
      expect(w.id).to.be.a('number');
    });

    it('should emit unload handler', async () => {
      await w.loadFile(path.join(fixtures, 'api', 'unload.html'));
      const closed = once(w, 'closed');
      w.close();
      await closed;
      const test = path.join(fixtures, 'api', 'unload');
      const content = fs.readFileSync(test);
      fs.unlinkSync(test);
      expect(String(content)).to.equal('unload');
    });

    it('should emit beforeunload handler', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false.html'));
      w.close();
      await once(w.webContents, '-before-unload-fired');
    });

    it('should not crash when keyboard event is sent before closing', async () => {
      await w.loadURL('data:text/html,pls no crash');
      const closed = once(w, 'closed');
      w.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Escape' });
      w.close();
      await closed;
    });

    describe('when invoked synchronously inside navigation observer', () => {
      let server: http.Server;
      let url: string;

      before(async () => {
        server = http.createServer((request, response) => {
          switch (request.url) {
            case '/net-error':
              response.destroy();
              break;
            case '/301':
              response.statusCode = 301;
              response.setHeader('Location', '/200');
              response.end();
              break;
            case '/200':
              response.statusCode = 200;
              response.end('hello');
              break;
            case '/title':
              response.statusCode = 200;
              response.end('<title>Hello</title>');
              break;
            default:
              throw new Error(`unsupported endpoint: ${request.url}`);
          }
        });

        url = (await listen(server)).url;
      });

      after(() => {
        server.close();
      });

      const events = [
        { name: 'did-start-loading', path: '/200' },
        { name: 'dom-ready', path: '/200' },
        { name: 'page-title-updated', path: '/title' },
        { name: 'did-stop-loading', path: '/200' },
        { name: 'did-finish-load', path: '/200' },
        { name: 'did-frame-finish-load', path: '/200' },
        { name: 'did-fail-load', path: '/net-error' }
      ];

      for (const { name, path } of events) {
        it(`should not crash when closed during ${name}`, async () => {
          const w = new BrowserWindow({ show: false });
          w.webContents.once((name as any), () => {
            w.close();
          });
          const destroyed = once(w.webContents, 'destroyed');
          w.webContents.loadURL(url + path);
          await destroyed;
        });
      }
    });
  });

  describe('window.close()', () => {
    let w: BrowserWindow;
    beforeEach(() => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
    });
    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });

    it('should emit unload event', async () => {
      w.loadFile(path.join(fixtures, 'api', 'close.html'));
      await once(w, 'closed');
      const test = path.join(fixtures, 'api', 'close');
      const content = fs.readFileSync(test).toString();
      fs.unlinkSync(test);
      expect(content).to.equal('close');
    });

    it('should emit beforeunload event', async function () {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false.html'));
      w.webContents.executeJavaScript('window.close()', true);
      await once(w.webContents, '-before-unload-fired');
    });
  });

  describe('BrowserWindow.destroy()', () => {
    let w: BrowserWindow;
    beforeEach(() => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
    });
    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });

    it('prevents users to access methods of webContents', async () => {
      const contents = w.webContents;
      w.destroy();
      await new Promise(setImmediate);
      expect(() => {
        contents.getProcessId();
      }).to.throw('Object has been destroyed');
    });
    it('should not crash when destroying windows with pending events', () => {
      const focusListener = () => { };
      app.on('browser-window-focus', focusListener);
      const windowCount = 3;
      const windowOptions = {
        show: false,
        width: 400,
        height: 400,
        webPreferences: {
          backgroundThrottling: false
        }
      };
      const windows = Array.from(Array(windowCount)).map(() => new BrowserWindow(windowOptions));
      for (const win of windows) win.show();
      for (const win of windows) win.focus();
      for (const win of windows) win.destroy();
      app.removeListener('browser-window-focus', focusListener);
    });
  });

  describe('BrowserWindow.loadURL(url)', () => {
    let w: BrowserWindow;
    const scheme = 'other';
    const srcPath = path.join(fixtures, 'api', 'loaded-from-dataurl.js');
    before(() => {
      protocol.registerFileProtocol(scheme, (request, callback) => {
        callback(srcPath);
      });
    });

    after(() => {
      protocol.unregisterProtocol(scheme);
    });

    beforeEach(() => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
    });
    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });
    let server: http.Server;
    let url: string;
    let postData = null as any;
    before(async () => {
      const filePath = path.join(fixtures, 'pages', 'a.html');
      const fileStats = fs.statSync(filePath);
      postData = [
        {
          type: 'rawData',
          bytes: Buffer.from('username=test&file=')
        },
        {
          type: 'file',
          filePath: filePath,
          offset: 0,
          length: fileStats.size,
          modificationTime: fileStats.mtime.getTime() / 1000
        }
      ];
      server = http.createServer((req, res) => {
        function respond () {
          if (req.method === 'POST') {
            let body = '';
            req.on('data', (data) => {
              if (data) body += data;
            });
            req.on('end', () => {
              const parsedData = qs.parse(body);
              fs.readFile(filePath, (err, data) => {
                if (err) return;
                if (parsedData.username === 'test' &&
                  parsedData.file === data.toString()) {
                  res.end();
                }
              });
            });
          } else if (req.url === '/302') {
            res.setHeader('Location', '/200');
            res.statusCode = 302;
            res.end();
          } else {
            res.end();
          }
        }
        setTimeout(req.url && req.url.includes('slow') ? 200 : 0).then(respond);
      });

      url = (await listen(server)).url;
    });

    after(() => {
      server.close();
    });

    it('should emit did-start-loading event', async () => {
      const didStartLoading = once(w.webContents, 'did-start-loading');
      w.loadURL('about:blank');
      await didStartLoading;
    });
    it('should emit ready-to-show event', async () => {
      const readyToShow = once(w, 'ready-to-show');
      w.loadURL('about:blank');
      await readyToShow;
    });
    // DISABLED-FIXME(deepak1556): The error code now seems to be `ERR_FAILED`, verify what
    // changed and adjust the test.
    it('should emit did-fail-load event for files that do not exist', async () => {
      const didFailLoad = once(w.webContents, 'did-fail-load');
      w.loadURL('file://a.txt');
      const [, code, desc,, isMainFrame] = await didFailLoad;
      expect(code).to.equal(-6);
      expect(desc).to.equal('ERR_FILE_NOT_FOUND');
      expect(isMainFrame).to.equal(true);
    });
    it('should emit did-fail-load event for invalid URL', async () => {
      const didFailLoad = once(w.webContents, 'did-fail-load');
      w.loadURL('http://example:port');
      const [, code, desc,, isMainFrame] = await didFailLoad;
      expect(desc).to.equal('ERR_INVALID_URL');
      expect(code).to.equal(-300);
      expect(isMainFrame).to.equal(true);
    });
    it('should not emit did-fail-load for a successfully loaded media file', async () => {
      w.webContents.on('did-fail-load', () => {
        expect.fail('did-fail-load should not emit on media file loads');
      });

      const mediaStarted = once(w.webContents, 'media-started-playing');
      w.loadFile(path.join(fixtures, 'cat-spin.mp4'));
      await mediaStarted;
    });
    it('should set `mainFrame = false` on did-fail-load events in iframes', async () => {
      const didFailLoad = once(w.webContents, 'did-fail-load');
      w.loadFile(path.join(fixtures, 'api', 'did-fail-load-iframe.html'));
      const [,,,, isMainFrame] = await didFailLoad;
      expect(isMainFrame).to.equal(false);
    });
    it('does not crash in did-fail-provisional-load handler', (done) => {
      w.webContents.once('did-fail-provisional-load', () => {
        w.loadURL('http://127.0.0.1:11111');
        done();
      });
      w.loadURL('http://127.0.0.1:11111');
    });
    it('should emit did-fail-load event for URL exceeding character limit', async () => {
      const data = Buffer.alloc(2 * 1024 * 1024).toString('base64');
      const didFailLoad = once(w.webContents, 'did-fail-load');
      w.loadURL(`data:image/png;base64,${data}`);
      const [, code, desc,, isMainFrame] = await didFailLoad;
      expect(desc).to.equal('ERR_INVALID_URL');
      expect(code).to.equal(-300);
      expect(isMainFrame).to.equal(true);
    });

    it('should return a promise', () => {
      const p = w.loadURL('about:blank');
      expect(p).to.have.property('then');
    });

    it('should return a promise that resolves', async () => {
      await expect(w.loadURL('about:blank')).to.eventually.be.fulfilled();
    });

    it('should return a promise that rejects on a load failure', async () => {
      const data = Buffer.alloc(2 * 1024 * 1024).toString('base64');
      const p = w.loadURL(`data:image/png;base64,${data}`);
      await expect(p).to.eventually.be.rejected;
    });

    it('should return a promise that resolves even if pushState occurs during navigation', async () => {
      const p = w.loadURL('data:text/html,<script>window.history.pushState({}, "/foo")</script>');
      await expect(p).to.eventually.be.fulfilled;
    });

    describe('POST navigations', () => {
      afterEach(() => { w.webContents.session.webRequest.onBeforeSendHeaders(null); });

      it('supports specifying POST data', async () => {
        await w.loadURL(url, { postData });
      });
      it('sets the content type header on URL encoded forms', async () => {
        await w.loadURL(url);
        const requestDetails: Promise<OnBeforeSendHeadersListenerDetails> = new Promise(resolve => {
          w.webContents.session.webRequest.onBeforeSendHeaders((details) => {
            resolve(details);
          });
        });
        w.webContents.executeJavaScript(`
          form = document.createElement('form')
          document.body.appendChild(form)
          form.method = 'POST'
          form.submit()
        `);
        const details = await requestDetails;
        expect(details.requestHeaders['Content-Type']).to.equal('application/x-www-form-urlencoded');
      });
      it('sets the content type header on multi part forms', async () => {
        await w.loadURL(url);
        const requestDetails: Promise<OnBeforeSendHeadersListenerDetails> = new Promise(resolve => {
          w.webContents.session.webRequest.onBeforeSendHeaders((details) => {
            resolve(details);
          });
        });
        w.webContents.executeJavaScript(`
          form = document.createElement('form')
          document.body.appendChild(form)
          form.method = 'POST'
          form.enctype = 'multipart/form-data'
          file = document.createElement('input')
          file.type = 'file'
          file.name = 'file'
          form.appendChild(file)
          form.submit()
        `);
        const details = await requestDetails;
        expect(details.requestHeaders['Content-Type'].startsWith('multipart/form-data; boundary=----WebKitFormBoundary')).to.equal(true);
      });
    });

    it('should support base url for data urls', async () => {
      await w.loadURL('data:text/html,<script src="loaded-from-dataurl.js"></script>', { baseURLForDataURL: `other://${path.join(fixtures, 'api')}${path.sep}` });
      expect(await w.webContents.executeJavaScript('window.ping')).to.equal('pong');
    });
  });

  for (const sandbox of [false, true]) {
    describe(`navigation events${sandbox ? ' with sandbox' : ''}`, () => {
      let w: BrowserWindow;
      beforeEach(() => {
        w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: false, sandbox } });
      });
      afterEach(async () => {
        await closeWindow(w);
        w = null as unknown as BrowserWindow;
      });

      describe('will-navigate event', () => {
        let server: http.Server;
        let url: string;
        before(async () => {
          server = http.createServer((req, res) => {
            if (req.url === '/navigate-top') {
              res.end('<a target=_top href="/">navigate _top</a>');
            } else {
              res.end('');
            }
          });
          url = (await listen(server)).url;
        });

        after(() => {
          server.close();
        });

        it('allows the window to be closed from the event listener', async () => {
          const event = once(w.webContents, 'will-navigate');
          w.loadFile(path.join(fixtures, 'pages', 'will-navigate.html'));
          await event;
          w.close();
        });

        it('can be prevented', (done) => {
          let willNavigate = false;
          w.webContents.once('will-navigate', (e) => {
            willNavigate = true;
            e.preventDefault();
          });
          w.webContents.on('did-stop-loading', () => {
            if (willNavigate) {
              // i.e. it shouldn't have had '?navigated' appended to it.
              try {
                expect(w.webContents.getURL().endsWith('will-navigate.html')).to.be.true();
                done();
              } catch (e) {
                done(e);
              }
            }
          });
          w.loadFile(path.join(fixtures, 'pages', 'will-navigate.html'));
        });

        it('is triggered when navigating from file: to http:', async () => {
          await w.loadFile(path.join(fixtures, 'api', 'blank.html'));
          w.webContents.executeJavaScript(`location.href = ${JSON.stringify(url)}`);
          const navigatedTo = await new Promise(resolve => {
            w.webContents.once('will-navigate', (e, url) => {
              e.preventDefault();
              resolve(url);
            });
          });
          expect(navigatedTo).to.equal(url + '/');
          expect(w.webContents.getURL()).to.match(/^file:/);
        });

        it('is triggered when navigating from about:blank to http:', async () => {
          await w.loadURL('about:blank');
          w.webContents.executeJavaScript(`location.href = ${JSON.stringify(url)}`);
          const navigatedTo = await new Promise(resolve => {
            w.webContents.once('will-navigate', (e, url) => {
              e.preventDefault();
              resolve(url);
            });
          });
          expect(navigatedTo).to.equal(url + '/');
          expect(w.webContents.getURL()).to.equal('about:blank');
        });

        it('is triggered when a cross-origin iframe navigates _top', async () => {
          w.loadURL(`data:text/html,<iframe src="http://127.0.0.1:${(server.address() as AddressInfo).port}/navigate-top"></iframe>`);
          await emittedUntil(w.webContents, 'did-frame-finish-load', (e: any, isMainFrame: boolean) => !isMainFrame);
          let initiator: WebFrameMain | undefined;
          w.webContents.on('will-navigate', (e) => {
            initiator = e.initiator;
          });
          const subframe = w.webContents.mainFrame.frames[0];
          subframe.executeJavaScript('document.getElementsByTagName("a")[0].click()', true);
          await once(w.webContents, 'did-navigate');
          expect(initiator).not.to.be.undefined();
          expect(initiator).to.equal(subframe);
        });

        it('is triggered when navigating from chrome: to http:', async () => {
          let hasEmittedWillNavigate = false;
          const willNavigatePromise = new Promise((resolve) => {
            w.webContents.once('will-navigate', e => {
              e.preventDefault();
              hasEmittedWillNavigate = true;
              resolve(e.url);
            });
          });
          await w.loadURL('chrome://gpu');

          // shouldn't emit for browser-initiated request via loadURL
          expect(hasEmittedWillNavigate).to.equal(false);

          w.webContents.executeJavaScript(`location.href = ${JSON.stringify(url)}`);
          const navigatedTo = await willNavigatePromise;
          expect(navigatedTo).to.equal(url + '/');
          expect(w.webContents.getURL()).to.equal('chrome://gpu/');
        });
      });

      describe('will-frame-navigate event', () => {
        let server = null as unknown as http.Server;
        let url = null as unknown as string;
        before(async () => {
          server = http.createServer((req, res) => {
            if (req.url === '/navigate-top') {
              res.end('<a target=_top href="/">navigate _top</a>');
            } else if (req.url === '/navigate-iframe') {
              res.end('<a href="/test">navigate iframe</a>');
            } else if (req.url === '/navigate-iframe?navigated') {
              res.end('Successfully navigated');
            } else if (req.url === '/navigate-iframe-immediately') {
              res.end(`
                <script type="text/javascript" charset="utf-8">
                  location.href += '?navigated'
                </script>
              `);
            } else if (req.url === '/navigate-iframe-immediately?navigated') {
              res.end('Successfully navigated');
            } else {
              res.end('');
            }
          });
          url = (await listen(server)).url;
        });

        after(() => {
          server.close();
        });

        it('allows the window to be closed from the event listener', (done) => {
          w.webContents.once('will-frame-navigate', () => {
            w.close();
            done();
          });
          w.loadFile(path.join(fixtures, 'pages', 'will-navigate.html'));
        });

        it('can be prevented', (done) => {
          let willNavigate = false;
          w.webContents.once('will-frame-navigate', (e) => {
            willNavigate = true;
            e.preventDefault();
          });
          w.webContents.on('did-stop-loading', () => {
            if (willNavigate) {
              // i.e. it shouldn't have had '?navigated' appended to it.
              try {
                expect(w.webContents.getURL().endsWith('will-navigate.html')).to.be.true();
                done();
              } catch (e) {
                done(e);
              }
            }
          });
          w.loadFile(path.join(fixtures, 'pages', 'will-navigate.html'));
        });

        it('can be prevented when navigating subframe', (done) => {
          let willNavigate = false;
          w.webContents.on('did-frame-navigate', (_event, _url, _httpResponseCode, _httpStatusText, isMainFrame, frameProcessId, frameRoutingId) => {
            if (isMainFrame) return;

            w.webContents.once('will-frame-navigate', (e) => {
              willNavigate = true;
              e.preventDefault();
            });

            w.webContents.on('did-stop-loading', () => {
              const frame = webFrameMain.fromId(frameProcessId, frameRoutingId);
              expect(frame).to.not.be.undefined();
              if (willNavigate) {
                // i.e. it shouldn't have had '?navigated' appended to it.
                try {
                  expect(frame!.url.endsWith('/navigate-iframe-immediately')).to.be.true();
                  done();
                } catch (e) {
                  done(e);
                }
              }
            });
          });
          w.loadURL(`data:text/html,<iframe src="http://127.0.0.1:${(server.address() as AddressInfo).port}/navigate-iframe-immediately"></iframe>`);
        });

        it('is triggered when navigating from file: to http:', async () => {
          await w.loadFile(path.join(fixtures, 'api', 'blank.html'));
          w.webContents.executeJavaScript(`location.href = ${JSON.stringify(url)}`);
          const navigatedTo = await new Promise(resolve => {
            w.webContents.once('will-frame-navigate', (e) => {
              e.preventDefault();
              resolve(e.url);
            });
          });
          expect(navigatedTo).to.equal(url + '/');
          expect(w.webContents.getURL()).to.match(/^file:/);
        });

        it('is triggered when navigating from about:blank to http:', async () => {
          await w.loadURL('about:blank');
          w.webContents.executeJavaScript(`location.href = ${JSON.stringify(url)}`);
          const navigatedTo = await new Promise(resolve => {
            w.webContents.once('will-frame-navigate', (e) => {
              e.preventDefault();
              resolve(e.url);
            });
          });
          expect(navigatedTo).to.equal(url + '/');
          expect(w.webContents.getURL()).to.equal('about:blank');
        });

        it('is triggered when a cross-origin iframe navigates _top', async () => {
          await w.loadURL(`data:text/html,<iframe src="http://127.0.0.1:${(server.address() as AddressInfo).port}/navigate-top"></iframe>`);
          await setTimeout(1000);

          let willFrameNavigateEmitted = false;
          let isMainFrameValue;
          w.webContents.on('will-frame-navigate', (event) => {
            willFrameNavigateEmitted = true;
            isMainFrameValue = event.isMainFrame;
          });
          const didNavigatePromise = once(w.webContents, 'did-navigate');

          w.webContents.debugger.attach('1.1');
          const targets = await w.webContents.debugger.sendCommand('Target.getTargets');
          const iframeTarget = targets.targetInfos.find((t: any) => t.type === 'iframe');
          const { sessionId } = await w.webContents.debugger.sendCommand('Target.attachToTarget', {
            targetId: iframeTarget.targetId,
            flatten: true
          });
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mousePressed',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mouseReleased',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);

          await didNavigatePromise;

          expect(willFrameNavigateEmitted).to.be.true();
          expect(isMainFrameValue).to.be.true();
        });

        it('is triggered when a cross-origin iframe navigates itself', async () => {
          await w.loadURL(`data:text/html,<iframe src="http://127.0.0.1:${(server.address() as AddressInfo).port}/navigate-iframe"></iframe>`);
          await setTimeout(1000);

          let willNavigateEmitted = false;
          let isMainFrameValue;
          w.webContents.on('will-frame-navigate', (event) => {
            willNavigateEmitted = true;
            isMainFrameValue = event.isMainFrame;
          });
          const didNavigatePromise = once(w.webContents, 'did-frame-navigate');

          w.webContents.debugger.attach('1.1');
          const targets = await w.webContents.debugger.sendCommand('Target.getTargets');
          const iframeTarget = targets.targetInfos.find((t: any) => t.type === 'iframe');
          const { sessionId } = await w.webContents.debugger.sendCommand('Target.attachToTarget', {
            targetId: iframeTarget.targetId,
            flatten: true
          });
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mousePressed',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mouseReleased',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);

          await didNavigatePromise;

          expect(willNavigateEmitted).to.be.true();
          expect(isMainFrameValue).to.be.false();
        });

        it('can cancel when a cross-origin iframe navigates itself', async () => {

        });
      });

      describe('will-redirect event', () => {
        let server: http.Server;
        let url: string;
        before(async () => {
          server = http.createServer((req, res) => {
            if (req.url === '/302') {
              res.setHeader('Location', '/200');
              res.statusCode = 302;
              res.end();
            } else if (req.url === '/navigate-302') {
              res.end(`<html><body><script>window.location='${url}/302'</script></body></html>`);
            } else {
              res.end();
            }
          });
          url = (await listen(server)).url;
        });

        after(() => {
          server.close();
        });
        it('is emitted on redirects', async () => {
          const willRedirect = once(w.webContents, 'will-redirect');
          w.loadURL(`${url}/302`);
          await willRedirect;
        });

        it('is emitted after will-navigate on redirects', async () => {
          let navigateCalled = false;
          w.webContents.on('will-navigate', () => {
            navigateCalled = true;
          });
          const willRedirect = once(w.webContents, 'will-redirect');
          w.loadURL(`${url}/navigate-302`);
          await willRedirect;
          expect(navigateCalled).to.equal(true, 'should have called will-navigate first');
        });

        it('is emitted before did-stop-loading on redirects', async () => {
          let stopCalled = false;
          w.webContents.on('did-stop-loading', () => {
            stopCalled = true;
          });
          const willRedirect = once(w.webContents, 'will-redirect');
          w.loadURL(`${url}/302`);
          await willRedirect;
          expect(stopCalled).to.equal(false, 'should not have called did-stop-loading first');
        });

        it('allows the window to be closed from the event listener', async () => {
          const event = once(w.webContents, 'will-redirect');
          w.loadURL(`${url}/302`);
          await event;
          w.close();
        });

        it('can be prevented', (done) => {
          w.webContents.once('will-redirect', (event) => {
            event.preventDefault();
          });
          w.webContents.on('will-navigate', (e, u) => {
            expect(u).to.equal(`${url}/302`);
          });
          w.webContents.on('did-stop-loading', () => {
            try {
              expect(w.webContents.getURL()).to.equal(
                `${url}/navigate-302`,
                'url should not have changed after navigation event'
              );
              done();
            } catch (e) {
              done(e);
            }
          });
          w.webContents.on('will-redirect', (e, u) => {
            try {
              expect(u).to.equal(`${url}/200`);
            } catch (e) {
              done(e);
            }
          });
          w.loadURL(`${url}/navigate-302`);
        });
      });

      describe('ordering', () => {
        let server = null as unknown as http.Server;
        let url = null as unknown as string;
        const navigationEvents = [
          'did-start-navigation',
          'did-navigate-in-page',
          'will-frame-navigate',
          'will-navigate',
          'will-redirect',
          'did-redirect-navigation',
          'did-frame-navigate',
          'did-navigate'
        ];
        before(async () => {
          server = http.createServer((req, res) => {
            if (req.url === '/navigate') {
              res.end('<a href="/">navigate</a>');
            } else if (req.url === '/redirect') {
              res.end('<a href="/redirect2">redirect</a>');
            } else if (req.url === '/redirect2') {
              res.statusCode = 302;
              res.setHeader('location', url);
              res.end();
            } else if (req.url === '/in-page') {
              res.end('<a href="#in-page">redirect</a><div id="in-page"></div>');
            } else {
              res.end('');
            }
          });
          url = (await listen(server)).url;
        });
        it('for initial navigation, event order is consistent', async () => {
          const firedEvents: string[] = [];
          const expectedEventOrder = [
            'did-start-navigation',
            'did-frame-navigate',
            'did-navigate'
          ];
          const allEvents = Promise.all(navigationEvents.map(event =>
            once(w.webContents, event).then(() => firedEvents.push(event))
          ));
          const timeout = setTimeout(1000);
          w.loadURL(url);
          await Promise.race([allEvents, timeout]);
          expect(firedEvents).to.deep.equal(expectedEventOrder);
        });

        it('for second navigation, event order is consistent', async () => {
          const firedEvents: string[] = [];
          const expectedEventOrder = [
            'did-start-navigation',
            'will-frame-navigate',
            'will-navigate',
            'did-frame-navigate',
            'did-navigate'
          ];
          w.loadURL(url + '/navigate');
          await once(w.webContents, 'did-navigate');
          await setTimeout(2000);
          Promise.all(navigationEvents.map(event =>
            once(w.webContents, event).then(() => firedEvents.push(event))
          ));
          const navigationFinished = once(w.webContents, 'did-navigate');
          w.webContents.debugger.attach('1.1');
          const targets = await w.webContents.debugger.sendCommand('Target.getTargets');
          const pageTarget = targets.targetInfos.find((t: any) => t.type === 'page');
          const { sessionId } = await w.webContents.debugger.sendCommand('Target.attachToTarget', {
            targetId: pageTarget.targetId,
            flatten: true
          });
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mousePressed',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mouseReleased',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);
          await navigationFinished;
          expect(firedEvents).to.deep.equal(expectedEventOrder);
        });

        it('when navigating with redirection, event order is consistent', async () => {
          const firedEvents: string[] = [];
          const expectedEventOrder = [
            'did-start-navigation',
            'will-frame-navigate',
            'will-navigate',
            'will-redirect',
            'did-redirect-navigation',
            'did-frame-navigate',
            'did-navigate'
          ];
          w.loadURL(url + '/redirect');
          await once(w.webContents, 'did-navigate');
          await setTimeout(2000);
          Promise.all(navigationEvents.map(event =>
            once(w.webContents, event).then(() => firedEvents.push(event))
          ));
          const navigationFinished = once(w.webContents, 'did-navigate');
          w.webContents.debugger.attach('1.1');
          const targets = await w.webContents.debugger.sendCommand('Target.getTargets');
          const pageTarget = targets.targetInfos.find((t: any) => t.type === 'page');
          const { sessionId } = await w.webContents.debugger.sendCommand('Target.attachToTarget', {
            targetId: pageTarget.targetId,
            flatten: true
          });
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mousePressed',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mouseReleased',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);
          await navigationFinished;
          expect(firedEvents).to.deep.equal(expectedEventOrder);
        });

        it('when navigating in-page, event order is consistent', async () => {
          const firedEvents: string[] = [];
          const expectedEventOrder = [
            'did-start-navigation',
            'did-navigate-in-page'
          ];
          w.loadURL(url + '/in-page');
          await once(w.webContents, 'did-navigate');
          await setTimeout(2000);
          Promise.all(navigationEvents.map(event =>
            once(w.webContents, event).then(() => firedEvents.push(event))
          ));
          const navigationFinished = once(w.webContents, 'did-navigate-in-page');
          w.webContents.debugger.attach('1.1');
          const targets = await w.webContents.debugger.sendCommand('Target.getTargets');
          const pageTarget = targets.targetInfos.find((t: any) => t.type === 'page');
          const { sessionId } = await w.webContents.debugger.sendCommand('Target.attachToTarget', {
            targetId: pageTarget.targetId,
            flatten: true
          });
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mousePressed',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);
          await w.webContents.debugger.sendCommand('Input.dispatchMouseEvent', {
            type: 'mouseReleased',
            x: 10,
            y: 10,
            clickCount: 1,
            button: 'left'
          }, sessionId);
          await navigationFinished;
          expect(firedEvents).to.deep.equal(expectedEventOrder);
        });
      });
    });
  }

  describe('focus and visibility', () => {
    let w: BrowserWindow;
    beforeEach(() => {
      w = new BrowserWindow({ show: false });
    });
    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });

    describe('BrowserWindow.show()', () => {
      it('should focus on window', async () => {
        const p = once(w, 'focus');
        w.show();
        await p;
        expect(w.isFocused()).to.equal(true);
      });
      it('should make the window visible', async () => {
        const p = once(w, 'focus');
        w.show();
        await p;
        expect(w.isVisible()).to.equal(true);
      });
      it('emits when window is shown', async () => {
        const show = once(w, 'show');
        w.show();
        await show;
        expect(w.isVisible()).to.equal(true);
      });
    });

    describe('BrowserWindow.hide()', () => {
      it('should defocus on window', () => {
        w.hide();
        expect(w.isFocused()).to.equal(false);
      });
      it('should make the window not visible', () => {
        w.show();
        w.hide();
        expect(w.isVisible()).to.equal(false);
      });
      it('emits when window is hidden', async () => {
        const shown = once(w, 'show');
        w.show();
        await shown;
        const hidden = once(w, 'hide');
        w.hide();
        await hidden;
        expect(w.isVisible()).to.equal(false);
      });
    });

    describe('BrowserWindow.minimize()', () => {
      // TODO(codebytere): Enable for Linux once maximize/minimize events work in CI.
      ifit(process.platform !== 'linux')('should not be visible when the window is minimized', async () => {
        const minimize = once(w, 'minimize');
        w.minimize();
        await minimize;

        expect(w.isMinimized()).to.equal(true);
        expect(w.isVisible()).to.equal(false);
      });
    });

    describe('BrowserWindow.showInactive()', () => {
      it('should not focus on window', () => {
        w.showInactive();
        expect(w.isFocused()).to.equal(false);
      });

      // TODO(dsanders11): Enable for Linux once CI plays nice with these kinds of tests
      ifit(process.platform !== 'linux')('should not restore maximized windows', async () => {
        const maximize = once(w, 'maximize');
        const shown = once(w, 'show');
        w.maximize();
        // TODO(dsanders11): The maximize event isn't firing on macOS for a window initially hidden
        if (process.platform !== 'darwin') {
          await maximize;
        } else {
          await setTimeout(1000);
        }
        w.showInactive();
        await shown;
        expect(w.isMaximized()).to.equal(true);
      });

      ifit(process.platform === 'darwin')('should attach child window to parent', async () => {
        const wShow = once(w, 'show');
        w.show();
        await wShow;

        const c = new BrowserWindow({ show: false, parent: w });
        const cShow = once(c, 'show');
        c.showInactive();
        await cShow;

        // verifying by checking that the child tracks the parent's visibility
        const minimized = once(w, 'minimize');
        w.minimize();
        await minimized;

        expect(w.isVisible()).to.be.false('parent is visible');
        expect(c.isVisible()).to.be.false('child is visible');

        const restored = once(w, 'restore');
        w.restore();
        await restored;

        expect(w.isVisible()).to.be.true('parent is visible');
        expect(c.isVisible()).to.be.true('child is visible');

        closeWindow(c);
      });
    });

    describe('BrowserWindow.focus()', () => {
      it('does not make the window become visible', () => {
        expect(w.isVisible()).to.equal(false);
        w.focus();
        expect(w.isVisible()).to.equal(false);
      });

      ifit(process.platform !== 'win32')('focuses a blurred window', async () => {
        {
          const isBlurred = once(w, 'blur');
          const isShown = once(w, 'show');
          w.show();
          w.blur();
          await isShown;
          await isBlurred;
        }
        expect(w.isFocused()).to.equal(false);
        w.focus();
        expect(w.isFocused()).to.equal(true);
      });

      ifit(process.platform !== 'linux')('acquires focus status from the other windows', async () => {
        const w1 = new BrowserWindow({ show: false });
        const w2 = new BrowserWindow({ show: false });
        const w3 = new BrowserWindow({ show: false });
        {
          const isFocused3 = once(w3, 'focus');
          const isShown1 = once(w1, 'show');
          const isShown2 = once(w2, 'show');
          const isShown3 = once(w3, 'show');
          w1.show();
          w2.show();
          w3.show();
          await isShown1;
          await isShown2;
          await isShown3;
          await isFocused3;
        }
        // TODO(RaisinTen): Investigate why this assertion fails only on Linux.
        expect(w1.isFocused()).to.equal(false);
        expect(w2.isFocused()).to.equal(false);
        expect(w3.isFocused()).to.equal(true);

        w1.focus();
        expect(w1.isFocused()).to.equal(true);
        expect(w2.isFocused()).to.equal(false);
        expect(w3.isFocused()).to.equal(false);

        w2.focus();
        expect(w1.isFocused()).to.equal(false);
        expect(w2.isFocused()).to.equal(true);
        expect(w3.isFocused()).to.equal(false);

        w3.focus();
        expect(w1.isFocused()).to.equal(false);
        expect(w2.isFocused()).to.equal(false);
        expect(w3.isFocused()).to.equal(true);

        {
          const isClosed1 = once(w1, 'closed');
          const isClosed2 = once(w2, 'closed');
          const isClosed3 = once(w3, 'closed');
          w1.destroy();
          w2.destroy();
          w3.destroy();
          await isClosed1;
          await isClosed2;
          await isClosed3;
        }
      });

      ifit(process.platform === 'darwin')('it does not activate the app if focusing an inactive panel', async () => {
        // Show to focus app, then remove existing window
        w.show();
        w.destroy();

        // We first need to resign app focus for this test to work
        const isInactive = once(app, 'did-resign-active');
        childProcess.execSync('osascript -e \'tell application "Finder" to activate\'');
        await isInactive;

        // Create new window
        w = new BrowserWindow({
          type: 'panel',
          height: 200,
          width: 200,
          center: true,
          show: false
        });

        const isShow = once(w, 'show');
        const isFocus = once(w, 'focus');

        w.show();
        w.focus();

        await isShow;
        await isFocus;

        const getActiveAppOsa = 'tell application "System Events" to get the name of the first process whose frontmost is true';
        const activeApp = childProcess.execSync(`osascript -e '${getActiveAppOsa}'`).toString().trim();

        expect(activeApp).to.equal('Finder');
      });
    });

    // TODO(RaisinTen): Make this work on Windows too.
    // Refs: https://github.com/electron/electron/issues/20464.
    ifdescribe(process.platform !== 'win32')('BrowserWindow.blur()', () => {
      it('removes focus from window', async () => {
        {
          const isFocused = once(w, 'focus');
          const isShown = once(w, 'show');
          w.show();
          await isShown;
          await isFocused;
        }
        expect(w.isFocused()).to.equal(true);
        w.blur();
        expect(w.isFocused()).to.equal(false);
      });

      ifit(process.platform !== 'linux')('transfers focus status to the next window', async () => {
        const w1 = new BrowserWindow({ show: false });
        const w2 = new BrowserWindow({ show: false });
        const w3 = new BrowserWindow({ show: false });
        {
          const isFocused3 = once(w3, 'focus');
          const isShown1 = once(w1, 'show');
          const isShown2 = once(w2, 'show');
          const isShown3 = once(w3, 'show');
          w1.show();
          w2.show();
          w3.show();
          await isShown1;
          await isShown2;
          await isShown3;
          await isFocused3;
        }
        // TODO(RaisinTen): Investigate why this assertion fails only on Linux.
        expect(w1.isFocused()).to.equal(false);
        expect(w2.isFocused()).to.equal(false);
        expect(w3.isFocused()).to.equal(true);

        w3.blur();
        expect(w1.isFocused()).to.equal(false);
        expect(w2.isFocused()).to.equal(true);
        expect(w3.isFocused()).to.equal(false);

        w2.blur();
        expect(w1.isFocused()).to.equal(true);
        expect(w2.isFocused()).to.equal(false);
        expect(w3.isFocused()).to.equal(false);

        w1.blur();
        expect(w1.isFocused()).to.equal(false);
        expect(w2.isFocused()).to.equal(false);
        expect(w3.isFocused()).to.equal(true);

        {
          const isClosed1 = once(w1, 'closed');
          const isClosed2 = once(w2, 'closed');
          const isClosed3 = once(w3, 'closed');
          w1.destroy();
          w2.destroy();
          w3.destroy();
          await isClosed1;
          await isClosed2;
          await isClosed3;
        }
      });
    });

    describe('BrowserWindow.getFocusedWindow()', () => {
      it('returns the opener window when dev tools window is focused', async () => {
        const p = once(w, 'focus');
        w.show();
        await p;
        w.webContents.openDevTools({ mode: 'undocked' });
        await once(w.webContents, 'devtools-focused');
        expect(BrowserWindow.getFocusedWindow()).to.equal(w);
      });
    });

    describe('BrowserWindow.moveTop()', () => {
      afterEach(closeAllWindows);

      it('should not steal focus', async () => {
        const posDelta = 50;
        const wShownInactive = once(w, 'show');
        w.showInactive();
        await wShownInactive;
        expect(w.isFocused()).to.equal(false);

        const otherWindow = new BrowserWindow({ show: false, title: 'otherWindow' });
        const otherWindowShown = once(otherWindow, 'show');
        const otherWindowFocused = once(otherWindow, 'focus');
        otherWindow.show();
        await otherWindowShown;
        await otherWindowFocused;
        expect(otherWindow.isFocused()).to.equal(true);

        w.moveTop();
        const wPos = w.getPosition();
        const wMoving = once(w, 'move');
        w.setPosition(wPos[0] + posDelta, wPos[1] + posDelta);
        await wMoving;
        expect(w.isFocused()).to.equal(false);
        expect(otherWindow.isFocused()).to.equal(true);

        const wFocused = once(w, 'focus');
        const otherWindowBlurred = once(otherWindow, 'blur');
        w.focus();
        await wFocused;
        await otherWindowBlurred;
        expect(w.isFocused()).to.equal(true);

        otherWindow.moveTop();
        const otherWindowPos = otherWindow.getPosition();
        const otherWindowMoving = once(otherWindow, 'move');
        otherWindow.setPosition(otherWindowPos[0] + posDelta, otherWindowPos[1] + posDelta);
        await otherWindowMoving;
        expect(otherWindow.isFocused()).to.equal(false);
        expect(w.isFocused()).to.equal(true);

        await closeWindow(otherWindow, { assertNotWindows: false });
        expect(BrowserWindow.getAllWindows()).to.have.lengthOf(1);
      });

      it('should not crash when called on a modal child window', async () => {
        const shown = once(w, 'show');
        w.show();
        await shown;

        const child = new BrowserWindow({ modal: true, parent: w });
        expect(() => { child.moveTop(); }).to.not.throw();
      });
    });

    describe('BrowserWindow.moveAbove(mediaSourceId)', () => {
      it('should throw an exception if wrong formatting', async () => {
        const fakeSourceIds = [
          'none', 'screen:0', 'window:fake', 'window:1234', 'foobar:1:2'
        ];
        for (const sourceId of fakeSourceIds) {
          expect(() => {
            w.moveAbove(sourceId);
          }).to.throw(/Invalid media source id/);
        }
      });

      it('should throw an exception if wrong type', async () => {
        const fakeSourceIds = [null as any, 123 as any];
        for (const sourceId of fakeSourceIds) {
          expect(() => {
            w.moveAbove(sourceId);
          }).to.throw(/Error processing argument at index 0 */);
        }
      });

      it('should throw an exception if invalid window', async () => {
        // It is very unlikely that these window id exist.
        const fakeSourceIds = ['window:99999999:0', 'window:123456:1',
          'window:123456:9'];
        for (const sourceId of fakeSourceIds) {
          expect(() => {
            w.moveAbove(sourceId);
          }).to.throw(/Invalid media source id/);
        }
      });

      it('should not throw an exception', async () => {
        const w2 = new BrowserWindow({ show: false, title: 'window2' });
        const w2Shown = once(w2, 'show');
        w2.show();
        await w2Shown;

        expect(() => {
          w.moveAbove(w2.getMediaSourceId());
        }).to.not.throw();

        await closeWindow(w2, { assertNotWindows: false });
      });
    });

    describe('BrowserWindow.setFocusable()', () => {
      it('can set unfocusable window to focusable', async () => {
        const w2 = new BrowserWindow({ focusable: false });
        const w2Focused = once(w2, 'focus');
        w2.setFocusable(true);
        w2.focus();
        await w2Focused;
        await closeWindow(w2, { assertNotWindows: false });
      });
    });

    describe('BrowserWindow.isFocusable()', () => {
      it('correctly returns whether a window is focusable', async () => {
        const w2 = new BrowserWindow({ focusable: false });
        expect(w2.isFocusable()).to.be.false();

        w2.setFocusable(true);
        expect(w2.isFocusable()).to.be.true();
        await closeWindow(w2, { assertNotWindows: false });
      });
    });
  });

  describe('sizing', () => {
    let w: BrowserWindow;

    beforeEach(() => {
      w = new BrowserWindow({ show: false, width: 400, height: 400 });
    });

    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });

    describe('BrowserWindow.setBounds(bounds[, animate])', () => {
      it('sets the window bounds with full bounds', () => {
        const fullBounds = { x: 440, y: 225, width: 500, height: 400 };
        w.setBounds(fullBounds);

        expectBoundsEqual(w.getBounds(), fullBounds);
      });

      it('rounds non-integer bounds', () => {
        w.setBounds({ x: 440.5, y: 225.1, width: 500.4, height: 400.9 });

        const bounds = w.getBounds();
        expect(bounds).to.deep.equal({ x: 441, y: 225, width: 500, height: 401 });
      });

      it('sets the window bounds with partial bounds', () => {
        const fullBounds = { x: 440, y: 225, width: 500, height: 400 };
        w.setBounds(fullBounds);

        const boundsUpdate = { width: 200 };
        w.setBounds(boundsUpdate as any);

        const expectedBounds = { ...fullBounds, ...boundsUpdate };
        expectBoundsEqual(w.getBounds(), expectedBounds);
      });

      ifit(process.platform === 'darwin')('on macOS', () => {
        it('emits \'resized\' event after animating', async () => {
          const fullBounds = { x: 440, y: 225, width: 500, height: 400 };
          w.setBounds(fullBounds, true);

          await expect(once(w, 'resized')).to.eventually.be.fulfilled();
        });
      });
    });

    describe('BrowserWindow.setSize(width, height)', () => {
      it('sets the window size', async () => {
        const size = [300, 400];

        const resized = once(w, 'resize');
        w.setSize(size[0], size[1]);
        await resized;

        expectBoundsEqual(w.getSize(), size);
      });

      ifit(process.platform === 'darwin')('on macOS', () => {
        it('emits \'resized\' event after animating', async () => {
          const size = [300, 400];
          w.setSize(size[0], size[1], true);

          await expect(once(w, 'resized')).to.eventually.be.fulfilled();
        });
      });
    });

    describe('BrowserWindow.setMinimum/MaximumSize(width, height)', () => {
      it('sets the maximum and minimum size of the window', () => {
        expect(w.getMinimumSize()).to.deep.equal([0, 0]);
        expect(w.getMaximumSize()).to.deep.equal([0, 0]);

        w.setMinimumSize(100, 100);
        expectBoundsEqual(w.getMinimumSize(), [100, 100]);
        expectBoundsEqual(w.getMaximumSize(), [0, 0]);

        w.setMaximumSize(900, 600);
        expectBoundsEqual(w.getMinimumSize(), [100, 100]);
        expectBoundsEqual(w.getMaximumSize(), [900, 600]);
      });
    });

    describe('BrowserWindow.setAspectRatio(ratio)', () => {
      it('resets the behaviour when passing in 0', async () => {
        const size = [300, 400];
        w.setAspectRatio(1 / 2);
        w.setAspectRatio(0);
        const resize = once(w, 'resize');
        w.setSize(size[0], size[1]);
        await resize;
        expectBoundsEqual(w.getSize(), size);
      });

      it('doesn\'t change bounds when maximum size is set', () => {
        w.setMenu(null);
        w.setMaximumSize(400, 400);
        // Without https://github.com/electron/electron/pull/29101
        // following call would shrink the window to 384x361.
        // There would be also DCHECK in resize_utils.cc on
        // debug build.
        w.setAspectRatio(1.0);
        expectBoundsEqual(w.getSize(), [400, 400]);
      });
    });

    describe('BrowserWindow.setPosition(x, y)', () => {
      it('sets the window position', async () => {
        const pos = [10, 10];
        const move = once(w, 'move');
        w.setPosition(pos[0], pos[1]);
        await move;
        expect(w.getPosition()).to.deep.equal(pos);
      });
    });

    describe('BrowserWindow.setContentSize(width, height)', () => {
      it('sets the content size', async () => {
        // NB. The CI server has a very small screen. Attempting to size the window
        // larger than the screen will limit the window's size to the screen and
        // cause the test to fail.
        const size = [456, 567];
        w.setContentSize(size[0], size[1]);
        await new Promise(setImmediate);
        const after = w.getContentSize();
        expect(after).to.deep.equal(size);
      });

      it('works for a frameless window', async () => {
        w.destroy();
        w = new BrowserWindow({
          show: false,
          frame: false,
          width: 400,
          height: 400
        });
        const size = [456, 567];
        w.setContentSize(size[0], size[1]);
        await new Promise(setImmediate);
        const after = w.getContentSize();
        expect(after).to.deep.equal(size);
      });
    });

    describe('BrowserWindow.setContentBounds(bounds)', () => {
      it('sets the content size and position', async () => {
        const bounds = { x: 10, y: 10, width: 250, height: 250 };
        const resize = once(w, 'resize');
        w.setContentBounds(bounds);
        await resize;
        await setTimeout();
        expectBoundsEqual(w.getContentBounds(), bounds);
      });
      it('works for a frameless window', async () => {
        w.destroy();
        w = new BrowserWindow({
          show: false,
          frame: false,
          width: 300,
          height: 300
        });
        const bounds = { x: 10, y: 10, width: 250, height: 250 };
        const resize = once(w, 'resize');
        w.setContentBounds(bounds);
        await resize;
        await setTimeout();
        expectBoundsEqual(w.getContentBounds(), bounds);
      });
    });

    describe('BrowserWindow.getBackgroundColor()', () => {
      it('returns default value if no backgroundColor is set', () => {
        w.destroy();
        w = new BrowserWindow({});
        expect(w.getBackgroundColor()).to.equal('#FFFFFF');
      });
      it('returns correct value if backgroundColor is set', () => {
        const backgroundColor = '#BBAAFF';
        w.destroy();
        w = new BrowserWindow({
          backgroundColor: backgroundColor
        });
        expect(w.getBackgroundColor()).to.equal(backgroundColor);
      });
      it('returns correct value from setBackgroundColor()', () => {
        const backgroundColor = '#AABBFF';
        w.destroy();
        w = new BrowserWindow({});
        w.setBackgroundColor(backgroundColor);
        expect(w.getBackgroundColor()).to.equal(backgroundColor);
      });
      it('returns correct color with multiple passed formats', () => {
        w.destroy();
        w = new BrowserWindow({});

        w.setBackgroundColor('#AABBFF');
        expect(w.getBackgroundColor()).to.equal('#AABBFF');

        w.setBackgroundColor('blueviolet');
        expect(w.getBackgroundColor()).to.equal('#8A2BE2');

        w.setBackgroundColor('rgb(255, 0, 185)');
        expect(w.getBackgroundColor()).to.equal('#FF00B9');

        w.setBackgroundColor('rgba(245, 40, 145, 0.8)');
        expect(w.getBackgroundColor()).to.equal('#F52891');

        w.setBackgroundColor('hsl(155, 100%, 50%)');
        expect(w.getBackgroundColor()).to.equal('#00FF95');
      });
    });

    describe('BrowserWindow.getNormalBounds()', () => {
      describe('Normal state', () => {
        it('checks normal bounds after resize', async () => {
          const size = [300, 400];
          const resize = once(w, 'resize');
          w.setSize(size[0], size[1]);
          await resize;
          expectBoundsEqual(w.getNormalBounds(), w.getBounds());
        });

        it('checks normal bounds after move', async () => {
          const pos = [10, 10];
          const move = once(w, 'move');
          w.setPosition(pos[0], pos[1]);
          await move;
          expectBoundsEqual(w.getNormalBounds(), w.getBounds());
        });
      });

      ifdescribe(process.platform !== 'linux')('Maximized state', () => {
        it('checks normal bounds when maximized', async () => {
          const bounds = w.getBounds();
          const maximize = once(w, 'maximize');
          w.show();
          w.maximize();
          await maximize;
          expectBoundsEqual(w.getNormalBounds(), bounds);
        });

        it('updates normal bounds after resize and maximize', async () => {
          const size = [300, 400];
          const resize = once(w, 'resize');
          w.setSize(size[0], size[1]);
          await resize;
          const original = w.getBounds();

          const maximize = once(w, 'maximize');
          w.maximize();
          await maximize;

          const normal = w.getNormalBounds();
          const bounds = w.getBounds();

          expect(normal).to.deep.equal(original);
          expect(normal).to.not.deep.equal(bounds);

          const close = once(w, 'close');
          w.close();
          await close;
        });

        it('updates normal bounds after move and maximize', async () => {
          const pos = [10, 10];
          const move = once(w, 'move');
          w.setPosition(pos[0], pos[1]);
          await move;
          const original = w.getBounds();

          const maximize = once(w, 'maximize');
          w.maximize();
          await maximize;

          const normal = w.getNormalBounds();
          const bounds = w.getBounds();

          expect(normal).to.deep.equal(original);
          expect(normal).to.not.deep.equal(bounds);

          const close = once(w, 'close');
          w.close();
          await close;
        });

        it('checks normal bounds when unmaximized', async () => {
          const bounds = w.getBounds();
          w.once('maximize', () => {
            w.unmaximize();
          });
          const unmaximize = once(w, 'unmaximize');
          w.show();
          w.maximize();
          await unmaximize;
          expectBoundsEqual(w.getNormalBounds(), bounds);
        });

        it('correctly reports maximized state after maximizing then minimizing', async () => {
          w.destroy();
          w = new BrowserWindow({ show: false });

          w.show();

          const maximize = once(w, 'maximize');
          w.maximize();
          await maximize;

          const minimize = once(w, 'minimize');
          w.minimize();
          await minimize;

          expect(w.isMaximized()).to.equal(false);
          expect(w.isMinimized()).to.equal(true);
        });

        it('correctly reports maximized state after maximizing then fullscreening', async () => {
          w.destroy();
          w = new BrowserWindow({ show: false });

          w.show();

          const maximize = once(w, 'maximize');
          w.maximize();
          await maximize;

          const enterFS = once(w, 'enter-full-screen');
          w.setFullScreen(true);
          await enterFS;

          expect(w.isMaximized()).to.equal(false);
          expect(w.isFullScreen()).to.equal(true);
        });

        it('does not crash if maximized, minimized, then restored to maximized state', (done) => {
          w.destroy();
          w = new BrowserWindow({ show: false });

          w.show();

          let count = 0;

          w.on('maximize', () => {
            if (count === 0) syncSetTimeout(() => { w.minimize(); });
            count++;
          });

          w.on('minimize', () => {
            if (count === 1) syncSetTimeout(() => { w.restore(); });
            count++;
          });

          w.on('restore', () => {
            try {
              throw new Error('hey!');
            } catch (e: any) {
              expect(e.message).to.equal('hey!');
              done();
            }
          });

          w.maximize();
        });

        it('checks normal bounds for maximized transparent window', async () => {
          w.destroy();
          w = new BrowserWindow({
            transparent: true,
            show: false
          });
          w.show();

          const bounds = w.getNormalBounds();

          const maximize = once(w, 'maximize');
          w.maximize();
          await maximize;

          expectBoundsEqual(w.getNormalBounds(), bounds);
        });

        it('does not change size for a frameless window with min size', async () => {
          w.destroy();
          w = new BrowserWindow({
            show: false,
            frame: false,
            width: 300,
            height: 300,
            minWidth: 300,
            minHeight: 300
          });
          const bounds = w.getBounds();
          w.once('maximize', () => {
            w.unmaximize();
          });
          const unmaximize = once(w, 'unmaximize');
          w.show();
          w.maximize();
          await unmaximize;
          expectBoundsEqual(w.getNormalBounds(), bounds);
        });

        it('correctly checks transparent window maximization state', async () => {
          w.destroy();
          w = new BrowserWindow({
            show: false,
            width: 300,
            height: 300,
            transparent: true
          });

          const maximize = once(w, 'maximize');
          w.show();
          w.maximize();
          await maximize;
          expect(w.isMaximized()).to.equal(true);
          const unmaximize = once(w, 'unmaximize');
          w.unmaximize();
          await unmaximize;
          expect(w.isMaximized()).to.equal(false);
        });

        it('returns the correct value for windows with an aspect ratio', async () => {
          w.destroy();
          w = new BrowserWindow({
            show: false,
            fullscreenable: false
          });

          w.setAspectRatio(16 / 11);

          const maximize = once(w, 'maximize');
          w.show();
          w.maximize();
          await maximize;

          expect(w.isMaximized()).to.equal(true);
          w.resizable = false;
          expect(w.isMaximized()).to.equal(true);
        });
      });

      ifdescribe(process.platform !== 'linux')('Minimized state', () => {
        it('checks normal bounds when minimized', async () => {
          const bounds = w.getBounds();
          const minimize = once(w, 'minimize');
          w.show();
          w.minimize();
          await minimize;
          expectBoundsEqual(w.getNormalBounds(), bounds);
        });

        it('updates normal bounds after move and minimize', async () => {
          const pos = [10, 10];
          const move = once(w, 'move');
          w.setPosition(pos[0], pos[1]);
          await move;
          const original = w.getBounds();

          const minimize = once(w, 'minimize');
          w.minimize();
          await minimize;

          const normal = w.getNormalBounds();

          expect(original).to.deep.equal(normal);
          expectBoundsEqual(normal, w.getBounds());
        });

        it('updates normal bounds after resize and minimize', async () => {
          const size = [300, 400];
          const resize = once(w, 'resize');
          w.setSize(size[0], size[1]);
          await resize;
          const original = w.getBounds();

          const minimize = once(w, 'minimize');
          w.minimize();
          await minimize;

          const normal = w.getNormalBounds();

          expect(original).to.deep.equal(normal);
          expectBoundsEqual(normal, w.getBounds());
        });

        it('checks normal bounds when restored', async () => {
          const bounds = w.getBounds();
          w.once('minimize', () => {
            w.restore();
          });
          const restore = once(w, 'restore');
          w.show();
          w.minimize();
          await restore;
          expectBoundsEqual(w.getNormalBounds(), bounds);
        });

        it('does not change size for a frameless window with min size', async () => {
          w.destroy();
          w = new BrowserWindow({
            show: false,
            frame: false,
            width: 300,
            height: 300,
            minWidth: 300,
            minHeight: 300
          });
          const bounds = w.getBounds();
          w.once('minimize', () => {
            w.restore();
          });
          const restore = once(w, 'restore');
          w.show();
          w.minimize();
          await restore;
          expectBoundsEqual(w.getNormalBounds(), bounds);
        });
      });

      ifdescribe(process.platform === 'win32')('Fullscreen state', () => {
        it('with properties', () => {
          it('can be set with the fullscreen constructor option', () => {
            w = new BrowserWindow({ fullscreen: true });
            expect(w.fullScreen).to.be.true();
          });

          it('does not go fullscreen if roundedCorners are enabled', async () => {
            w = new BrowserWindow({ frame: false, roundedCorners: false, fullscreen: true });
            expect(w.fullScreen).to.be.false();
          });

          it('can be changed', () => {
            w.fullScreen = false;
            expect(w.fullScreen).to.be.false();
            w.fullScreen = true;
            expect(w.fullScreen).to.be.true();
          });

          it('checks normal bounds when fullscreen\'ed', async () => {
            const bounds = w.getBounds();
            const enterFullScreen = once(w, 'enter-full-screen');
            w.show();
            w.fullScreen = true;
            await enterFullScreen;
            expectBoundsEqual(w.getNormalBounds(), bounds);
          });

          it('updates normal bounds after resize and fullscreen', async () => {
            const size = [300, 400];
            const resize = once(w, 'resize');
            w.setSize(size[0], size[1]);
            await resize;
            const original = w.getBounds();

            const fsc = once(w, 'enter-full-screen');
            w.fullScreen = true;
            await fsc;

            const normal = w.getNormalBounds();
            const bounds = w.getBounds();

            expect(normal).to.deep.equal(original);
            expect(normal).to.not.deep.equal(bounds);

            const close = once(w, 'close');
            w.close();
            await close;
          });

          it('updates normal bounds after move and fullscreen', async () => {
            const pos = [10, 10];
            const move = once(w, 'move');
            w.setPosition(pos[0], pos[1]);
            await move;
            const original = w.getBounds();

            const fsc = once(w, 'enter-full-screen');
            w.fullScreen = true;
            await fsc;

            const normal = w.getNormalBounds();
            const bounds = w.getBounds();

            expect(normal).to.deep.equal(original);
            expect(normal).to.not.deep.equal(bounds);

            const close = once(w, 'close');
            w.close();
            await close;
          });

          it('checks normal bounds when unfullscreen\'ed', async () => {
            const bounds = w.getBounds();
            w.once('enter-full-screen', () => {
              w.fullScreen = false;
            });
            const leaveFullScreen = once(w, 'leave-full-screen');
            w.show();
            w.fullScreen = true;
            await leaveFullScreen;
            expectBoundsEqual(w.getNormalBounds(), bounds);
          });
        });

        it('with functions', () => {
          it('can be set with the fullscreen constructor option', () => {
            w = new BrowserWindow({ fullscreen: true });
            expect(w.isFullScreen()).to.be.true();
          });

          it('can be changed', () => {
            w.setFullScreen(false);
            expect(w.isFullScreen()).to.be.false();
            w.setFullScreen(true);
            expect(w.isFullScreen()).to.be.true();
          });

          it('checks normal bounds when fullscreen\'ed', async () => {
            const bounds = w.getBounds();
            w.show();

            const enterFullScreen = once(w, 'enter-full-screen');
            w.setFullScreen(true);
            await enterFullScreen;

            expectBoundsEqual(w.getNormalBounds(), bounds);
          });

          it('updates normal bounds after resize and fullscreen', async () => {
            const size = [300, 400];
            const resize = once(w, 'resize');
            w.setSize(size[0], size[1]);
            await resize;
            const original = w.getBounds();

            const fsc = once(w, 'enter-full-screen');
            w.setFullScreen(true);
            await fsc;

            const normal = w.getNormalBounds();
            const bounds = w.getBounds();

            expect(normal).to.deep.equal(original);
            expect(normal).to.not.deep.equal(bounds);

            const close = once(w, 'close');
            w.close();
            await close;
          });

          it('updates normal bounds after move and fullscreen', async () => {
            const pos = [10, 10];
            const move = once(w, 'move');
            w.setPosition(pos[0], pos[1]);
            await move;
            const original = w.getBounds();

            const fsc = once(w, 'enter-full-screen');
            w.setFullScreen(true);
            await fsc;

            const normal = w.getNormalBounds();
            const bounds = w.getBounds();

            expect(normal).to.deep.equal(original);
            expect(normal).to.not.deep.equal(bounds);

            const close = once(w, 'close');
            w.close();
            await close;
          });

          it('checks normal bounds when unfullscreen\'ed', async () => {
            const bounds = w.getBounds();
            w.show();

            const enterFullScreen = once(w, 'enter-full-screen');
            w.setFullScreen(true);
            await enterFullScreen;

            const leaveFullScreen = once(w, 'leave-full-screen');
            w.setFullScreen(false);
            await leaveFullScreen;

            expectBoundsEqual(w.getNormalBounds(), bounds);
          });
        });
      });
    });
  });

  ifdescribe(process.platform === 'darwin')('tabbed windows', () => {
    let w: BrowserWindow;
    beforeEach(() => {
      w = new BrowserWindow({ show: false });
    });
    afterEach(closeAllWindows);

    describe('BrowserWindow.selectPreviousTab()', () => {
      it('does not throw', () => {
        expect(() => {
          w.selectPreviousTab();
        }).to.not.throw();
      });
    });

    describe('BrowserWindow.selectNextTab()', () => {
      it('does not throw', () => {
        expect(() => {
          w.selectNextTab();
        }).to.not.throw();
      });
    });

    describe('BrowserWindow.showAllTabs()', () => {
      it('does not throw', () => {
        expect(() => {
          w.showAllTabs();
        }).to.not.throw();
      });
    });

    describe('BrowserWindow.mergeAllWindows()', () => {
      it('does not throw', () => {
        expect(() => {
          w.mergeAllWindows();
        }).to.not.throw();
      });
    });

    describe('BrowserWindow.moveTabToNewWindow()', () => {
      it('does not throw', () => {
        expect(() => {
          w.moveTabToNewWindow();
        }).to.not.throw();
      });
    });

    describe('BrowserWindow.toggleTabBar()', () => {
      it('does not throw', () => {
        expect(() => {
          w.toggleTabBar();
        }).to.not.throw();
      });
    });

    describe('BrowserWindow.addTabbedWindow()', () => {
      it('does not throw', async () => {
        const tabbedWindow = new BrowserWindow({});
        expect(() => {
          w.addTabbedWindow(tabbedWindow);
        }).to.not.throw();

        expect(BrowserWindow.getAllWindows()).to.have.lengthOf(2); // w + tabbedWindow

        await closeWindow(tabbedWindow, { assertNotWindows: false });
        expect(BrowserWindow.getAllWindows()).to.have.lengthOf(1); // w
      });

      it('throws when called on itself', () => {
        expect(() => {
          w.addTabbedWindow(w);
        }).to.throw('AddTabbedWindow cannot be called by a window on itself.');
      });
    });

    describe('BrowserWindow.tabbingIdentifier', () => {
      it('is undefined if no tabbingIdentifier was set', () => {
        const w = new BrowserWindow({ show: false });
        expect(w.tabbingIdentifier).to.be.undefined('tabbingIdentifier');
      });

      it('returns the window tabbingIdentifier', () => {
        const w = new BrowserWindow({ show: false, tabbingIdentifier: 'group1' });
        expect(w.tabbingIdentifier).to.equal('group1');
      });
    });
  });

  describe('autoHideMenuBar state', () => {
    afterEach(closeAllWindows);

    it('for properties', () => {
      it('can be set with autoHideMenuBar constructor option', () => {
        const w = new BrowserWindow({ show: false, autoHideMenuBar: true });
        expect(w.autoHideMenuBar).to.be.true('autoHideMenuBar');
      });

      it('can be changed', () => {
        const w = new BrowserWindow({ show: false });
        expect(w.autoHideMenuBar).to.be.false('autoHideMenuBar');
        w.autoHideMenuBar = true;
        expect(w.autoHideMenuBar).to.be.true('autoHideMenuBar');
        w.autoHideMenuBar = false;
        expect(w.autoHideMenuBar).to.be.false('autoHideMenuBar');
      });
    });

    it('for functions', () => {
      it('can be set with autoHideMenuBar constructor option', () => {
        const w = new BrowserWindow({ show: false, autoHideMenuBar: true });
        expect(w.isMenuBarAutoHide()).to.be.true('autoHideMenuBar');
      });

      it('can be changed', () => {
        const w = new BrowserWindow({ show: false });
        expect(w.isMenuBarAutoHide()).to.be.false('autoHideMenuBar');
        w.setAutoHideMenuBar(true);
        expect(w.isMenuBarAutoHide()).to.be.true('autoHideMenuBar');
        w.setAutoHideMenuBar(false);
        expect(w.isMenuBarAutoHide()).to.be.false('autoHideMenuBar');
      });
    });
  });

  describe('BrowserWindow.capturePage(rect)', () => {
    afterEach(closeAllWindows);

    it('returns a Promise with a Buffer', async () => {
      const w = new BrowserWindow({ show: false });
      const image = await w.capturePage({
        x: 0,
        y: 0,
        width: 100,
        height: 100
      });

      expect(image.isEmpty()).to.equal(true);
    });

    describe('when `outputSize` is provided', () => {
      describe('when both dimensions of`outputSize` are smaller than dimensions of the captured area', () => {
        it('returns image with requested size', async () => {
          const w = new BrowserWindow({ show: false });
          w.loadFile(path.join(fixtures, 'pages', 'a.html'));
          await once(w, 'ready-to-show');
          w.show();
          const image = await w.capturePage({
            x: 0,
            y: 0,
            width: 100,
            height: 100
          }, {
            outputSize: {
              width: 20,
              height: 20
            }
          });

          expect(image.getSize()).to.deep.equal({ width: 20, height: 20 });
        });
      });

      describe('when one dimension of `outputSize` is bigger than one dimension of the captured area', () => {
        it('returns image with requested size', async () => {
          const w = new BrowserWindow({ show: false });
          w.loadFile(path.join(fixtures, 'pages', 'a.html'));
          await once(w, 'ready-to-show');
          w.show();
          w.setBounds({ width: 200, height: 200 });
          const image = await w.capturePage({
            x: 0,
            y: 0,
            width: 100,
            height: 100
          }, {
            outputSize: {
              width: 150,
              height: 1500
            }
          });

          expect(image.getSize()).to.deep.equal({ width: 150, height: 1500 });
        });
      });

      describe('when both dimensions of `outputSize` are bigger than both dimensions of the captured area', () => {
        it('returns image with requested size', async () => {
          const w = new BrowserWindow({ show: false });
          w.loadFile(path.join(fixtures, 'pages', 'a.html'));
          await once(w, 'ready-to-show');
          w.show();
          w.setBounds({ width: 200, height: 200 });
          const image = await w.capturePage({
            x: 0,
            y: 0,
            width: 100,
            height: 100
          }, {
            outputSize: {
              width: 1500,
              height: 1500
            }
          });

          expect(image.getSize()).to.deep.equal({ width: 1500, height: 1500 });
        });
      });
    });

    ifit(process.platform === 'darwin')('honors the stayHidden argument', async () => {
      const w = new BrowserWindow({
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });

      w.loadFile(path.join(fixtures, 'pages', 'visibilitychange.html'));

      {
        const [, visibilityState, hidden] = await once(ipcMain, 'pong');
        expect(visibilityState).to.equal('visible');
        expect(hidden).to.be.false('hidden');
      }

      w.hide();

      {
        const [, visibilityState, hidden] = await once(ipcMain, 'pong');
        expect(visibilityState).to.equal('hidden');
        expect(hidden).to.be.true('hidden');
      }

      await w.capturePage({ x: 0, y: 0, width: 0, height: 0 }, { stayHidden: true });

      const visible = await w.webContents.executeJavaScript('document.visibilityState');
      expect(visible).to.equal('hidden');
    });

    it('resolves when the window is occluded', async () => {
      const w1 = new BrowserWindow({ show: false });
      w1.loadFile(path.join(fixtures, 'pages', 'a.html'));
      await once(w1, 'ready-to-show');
      w1.show();

      const w2 = new BrowserWindow({ show: false });
      w2.loadFile(path.join(fixtures, 'pages', 'a.html'));
      await once(w2, 'ready-to-show');
      w2.show();

      const visibleImage = await w1.capturePage();
      expect(visibleImage.isEmpty()).to.equal(false);
    });

    it('resolves when the window is not visible', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadFile(path.join(fixtures, 'pages', 'a.html'));
      await once(w, 'ready-to-show');
      w.show();

      const visibleImage = await w.capturePage();
      expect(visibleImage.isEmpty()).to.equal(false);

      w.minimize();

      const hiddenImage = await w.capturePage();
      expect(hiddenImage.isEmpty()).to.equal(false);
    });

    it('preserves transparency', async () => {
      const w = new BrowserWindow({ show: false, transparent: true });
      w.loadFile(path.join(fixtures, 'pages', 'theme-color.html'));
      await once(w, 'ready-to-show');
      w.show();

      const image = await w.capturePage();
      const imgBuffer = image.toPNG();

      // Check the 25th byte in the PNG.
      // Values can be 0,2,3,4, or 6. We want 6, which is RGB + Alpha
      expect(imgBuffer[25]).to.equal(6);
    });
  });

  describe('BrowserWindow.setProgressBar(progress)', () => {
    let w: BrowserWindow;
    before(() => {
      w = new BrowserWindow({ show: false });
    });
    after(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });
    it('sets the progress', () => {
      expect(() => {
        if (process.platform === 'darwin') {
          app.dock.setIcon(path.join(fixtures, 'assets', 'logo.png'));
        }
        w.setProgressBar(0.5);

        if (process.platform === 'darwin') {
          app.dock.setIcon(null as any);
        }
        w.setProgressBar(-1);
      }).to.not.throw();
    });
    it('sets the progress using "paused" mode', () => {
      expect(() => {
        w.setProgressBar(0.5, { mode: 'paused' });
      }).to.not.throw();
    });
    it('sets the progress using "error" mode', () => {
      expect(() => {
        w.setProgressBar(0.5, { mode: 'error' });
      }).to.not.throw();
    });
    it('sets the progress using "normal" mode', () => {
      expect(() => {
        w.setProgressBar(0.5, { mode: 'normal' });
      }).to.not.throw();
    });
  });

  describe('BrowserWindow.setAlwaysOnTop(flag, level)', () => {
    let w: BrowserWindow;

    afterEach(closeAllWindows);

    beforeEach(() => {
      w = new BrowserWindow({ show: true });
    });

    it('sets the window as always on top', () => {
      expect(w.isAlwaysOnTop()).to.be.false('is alwaysOnTop');
      w.setAlwaysOnTop(true, 'screen-saver');
      expect(w.isAlwaysOnTop()).to.be.true('is not alwaysOnTop');
      w.setAlwaysOnTop(false);
      expect(w.isAlwaysOnTop()).to.be.false('is alwaysOnTop');
      w.setAlwaysOnTop(true);
      expect(w.isAlwaysOnTop()).to.be.true('is not alwaysOnTop');
    });

    ifit(process.platform === 'darwin')('resets the windows level on minimize', async () => {
      expect(w.isAlwaysOnTop()).to.be.false('is alwaysOnTop');
      w.setAlwaysOnTop(true, 'screen-saver');
      expect(w.isAlwaysOnTop()).to.be.true('is not alwaysOnTop');
      const minimized = once(w, 'minimize');
      w.minimize();
      await minimized;
      expect(w.isAlwaysOnTop()).to.be.false('is alwaysOnTop');
      const restored = once(w, 'restore');
      w.restore();
      await restored;
      expect(w.isAlwaysOnTop()).to.be.true('is not alwaysOnTop');
    });

    it('causes the right value to be emitted on `always-on-top-changed`', async () => {
      const alwaysOnTopChanged = once(w, 'always-on-top-changed') as Promise<[any, boolean]>;
      expect(w.isAlwaysOnTop()).to.be.false('is alwaysOnTop');
      w.setAlwaysOnTop(true);
      const [, alwaysOnTop] = await alwaysOnTopChanged;
      expect(alwaysOnTop).to.be.true('is not alwaysOnTop');
    });

    ifit(process.platform === 'darwin')('honors the alwaysOnTop level of a child window', () => {
      w = new BrowserWindow({ show: false });
      const c = new BrowserWindow({ parent: w });
      c.setAlwaysOnTop(true, 'screen-saver');

      expect(w.isAlwaysOnTop()).to.be.false();
      expect(c.isAlwaysOnTop()).to.be.true('child is not always on top');
      expect(c._getAlwaysOnTopLevel()).to.equal('screen-saver');
    });
  });

  describe('preconnect feature', () => {
    let w: BrowserWindow;

    let server: http.Server;
    let url: string;
    let connections = 0;

    beforeEach(async () => {
      connections = 0;
      server = http.createServer((req, res) => {
        if (req.url === '/link') {
          res.setHeader('Content-type', 'text/html');
          res.end('<head><link rel="preconnect" href="//example.com" /></head><body>foo</body>');
          return;
        }
        res.end();
      });
      server.on('connection', () => { connections++; });
      url = (await listen(server)).url;
    });
    afterEach(async () => {
      server.close();
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
      server = null as unknown as http.Server;
    });

    it('calling preconnect() connects to the server', async () => {
      w = new BrowserWindow({ show: false });
      w.webContents.on('did-start-navigation', (event, url) => {
        w.webContents.session.preconnect({ url, numSockets: 4 });
      });
      await w.loadURL(url);
      expect(connections).to.equal(4);
    });

    it('does not preconnect unless requested', async () => {
      w = new BrowserWindow({ show: false });
      await w.loadURL(url);
      expect(connections).to.equal(1);
    });

    it('parses <link rel=preconnect>', async () => {
      w = new BrowserWindow({ show: true });
      const p = once(w.webContents.session, 'preconnect');
      w.loadURL(url + '/link');
      const [, preconnectUrl, allowCredentials] = await p;
      expect(preconnectUrl).to.equal('http://example.com/');
      expect(allowCredentials).to.be.true('allowCredentials');
    });
  });

  describe('BrowserWindow.setAutoHideCursor(autoHide)', () => {
    let w: BrowserWindow;
    beforeEach(() => {
      w = new BrowserWindow({ show: false });
    });
    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });

    ifit(process.platform === 'darwin')('on macOS', () => {
      it('allows changing cursor auto-hiding', () => {
        expect(() => {
          w.setAutoHideCursor(false);
          w.setAutoHideCursor(true);
        }).to.not.throw();
      });
    });

    ifit(process.platform !== 'darwin')('on non-macOS platforms', () => {
      it('is not available', () => {
        expect(w.setAutoHideCursor).to.be.undefined('setAutoHideCursor function');
      });
    });
  });

  ifdescribe(process.platform === 'darwin')('BrowserWindow.setWindowButtonVisibility()', () => {
    afterEach(closeAllWindows);

    it('does not throw', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        w.setWindowButtonVisibility(true);
        w.setWindowButtonVisibility(false);
      }).to.not.throw();
    });

    it('changes window button visibility for normal window', () => {
      const w = new BrowserWindow({ show: false });
      expect(w._getWindowButtonVisibility()).to.equal(true);
      w.setWindowButtonVisibility(false);
      expect(w._getWindowButtonVisibility()).to.equal(false);
      w.setWindowButtonVisibility(true);
      expect(w._getWindowButtonVisibility()).to.equal(true);
    });

    it('changes window button visibility for frameless window', () => {
      const w = new BrowserWindow({ show: false, frame: false });
      expect(w._getWindowButtonVisibility()).to.equal(false);
      w.setWindowButtonVisibility(true);
      expect(w._getWindowButtonVisibility()).to.equal(true);
      w.setWindowButtonVisibility(false);
      expect(w._getWindowButtonVisibility()).to.equal(false);
    });

    it('changes window button visibility for hiddenInset window', () => {
      const w = new BrowserWindow({ show: false, frame: false, titleBarStyle: 'hiddenInset' });
      expect(w._getWindowButtonVisibility()).to.equal(true);
      w.setWindowButtonVisibility(false);
      expect(w._getWindowButtonVisibility()).to.equal(false);
      w.setWindowButtonVisibility(true);
      expect(w._getWindowButtonVisibility()).to.equal(true);
    });

    // Buttons of customButtonsOnHover are always hidden unless hovered.
    it('does not change window button visibility for customButtonsOnHover window', () => {
      const w = new BrowserWindow({ show: false, frame: false, titleBarStyle: 'customButtonsOnHover' });
      expect(w._getWindowButtonVisibility()).to.equal(false);
      w.setWindowButtonVisibility(true);
      expect(w._getWindowButtonVisibility()).to.equal(false);
      w.setWindowButtonVisibility(false);
      expect(w._getWindowButtonVisibility()).to.equal(false);
    });

    it('correctly updates when entering/exiting fullscreen for hidden style', async () => {
      const w = new BrowserWindow({ show: false, frame: false, titleBarStyle: 'hidden' });
      expect(w._getWindowButtonVisibility()).to.equal(true);
      w.setWindowButtonVisibility(false);
      expect(w._getWindowButtonVisibility()).to.equal(false);

      const enterFS = once(w, 'enter-full-screen');
      w.setFullScreen(true);
      await enterFS;

      const leaveFS = once(w, 'leave-full-screen');
      w.setFullScreen(false);
      await leaveFS;

      w.setWindowButtonVisibility(true);
      expect(w._getWindowButtonVisibility()).to.equal(true);
    });

    it('correctly updates when entering/exiting fullscreen for hiddenInset style', async () => {
      const w = new BrowserWindow({ show: false, frame: false, titleBarStyle: 'hiddenInset' });
      expect(w._getWindowButtonVisibility()).to.equal(true);
      w.setWindowButtonVisibility(false);
      expect(w._getWindowButtonVisibility()).to.equal(false);

      const enterFS = once(w, 'enter-full-screen');
      w.setFullScreen(true);
      await enterFS;

      const leaveFS = once(w, 'leave-full-screen');
      w.setFullScreen(false);
      await leaveFS;

      w.setWindowButtonVisibility(true);
      expect(w._getWindowButtonVisibility()).to.equal(true);
    });
  });

  ifdescribe(process.platform === 'darwin')('BrowserWindow.setVibrancy(type)', () => {
    afterEach(closeAllWindows);

    it('allows setting, changing, and removing the vibrancy', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        w.setVibrancy('titlebar');
        w.setVibrancy('selection');
        w.setVibrancy(null);
        w.setVibrancy('menu');
        w.setVibrancy('' as any);
      }).to.not.throw();
    });

    it('does not crash if vibrancy is set to an invalid value', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        w.setVibrancy('i-am-not-a-valid-vibrancy-type' as any);
      }).to.not.throw();
    });
  });

  ifdescribe(process.platform === 'darwin')('trafficLightPosition', () => {
    const pos = { x: 10, y: 10 };
    afterEach(closeAllWindows);

    describe('BrowserWindow.getWindowButtonPosition(pos)', () => {
      it('returns null when there is no custom position', () => {
        const w = new BrowserWindow({ show: false });
        expect(w.getWindowButtonPosition()).to.be.null('getWindowButtonPosition');
      });

      it('gets position property for "hidden" titleBarStyle', () => {
        const w = new BrowserWindow({ show: false, titleBarStyle: 'hidden', trafficLightPosition: pos });
        expect(w.getWindowButtonPosition()).to.deep.equal(pos);
      });

      it('gets position property for "customButtonsOnHover" titleBarStyle', () => {
        const w = new BrowserWindow({ show: false, titleBarStyle: 'customButtonsOnHover', trafficLightPosition: pos });
        expect(w.getWindowButtonPosition()).to.deep.equal(pos);
      });
    });

    describe('BrowserWindow.setWindowButtonPosition(pos)', () => {
      it('resets the position when null is passed', () => {
        const w = new BrowserWindow({ show: false, titleBarStyle: 'hidden', trafficLightPosition: pos });
        w.setWindowButtonPosition(null);
        expect(w.getWindowButtonPosition()).to.be.null('setWindowButtonPosition');
      });

      it('sets position property for "hidden" titleBarStyle', () => {
        const w = new BrowserWindow({ show: false, titleBarStyle: 'hidden', trafficLightPosition: pos });
        const newPos = { x: 20, y: 20 };
        w.setWindowButtonPosition(newPos);
        expect(w.getWindowButtonPosition()).to.deep.equal(newPos);
      });

      it('sets position property for "customButtonsOnHover" titleBarStyle', () => {
        const w = new BrowserWindow({ show: false, titleBarStyle: 'customButtonsOnHover', trafficLightPosition: pos });
        const newPos = { x: 20, y: 20 };
        w.setWindowButtonPosition(newPos);
        expect(w.getWindowButtonPosition()).to.deep.equal(newPos);
      });
    });
  });

  ifdescribe(process.platform === 'win32')('BrowserWindow.setAppDetails(options)', () => {
    afterEach(closeAllWindows);

    it('supports setting the app details', () => {
      const w = new BrowserWindow({ show: false });
      const iconPath = path.join(fixtures, 'assets', 'icon.ico');

      expect(() => {
        w.setAppDetails({ appId: 'my.app.id' });
        w.setAppDetails({ appIconPath: iconPath, appIconIndex: 0 });
        w.setAppDetails({ appIconPath: iconPath });
        w.setAppDetails({ relaunchCommand: 'my-app.exe arg1 arg2', relaunchDisplayName: 'My app name' });
        w.setAppDetails({ relaunchCommand: 'my-app.exe arg1 arg2' });
        w.setAppDetails({ relaunchDisplayName: 'My app name' });
        w.setAppDetails({
          appId: 'my.app.id',
          appIconPath: iconPath,
          appIconIndex: 0,
          relaunchCommand: 'my-app.exe arg1 arg2',
          relaunchDisplayName: 'My app name'
        });
        w.setAppDetails({});
      }).to.not.throw();

      expect(() => {
        (w.setAppDetails as any)();
      }).to.throw('Insufficient number of arguments.');
    });
  });

  describe('BrowserWindow.fromId(id)', () => {
    afterEach(closeAllWindows);
    it('returns the window with id', () => {
      const w = new BrowserWindow({ show: false });
      expect(BrowserWindow.fromId(w.id)!.id).to.equal(w.id);
    });
  });

  describe('Opening a BrowserWindow from a link', () => {
    let appProcess: childProcess.ChildProcessWithoutNullStreams | undefined;

    afterEach(() => {
      if (appProcess && !appProcess.killed) {
        appProcess.kill();
        appProcess = undefined;
      }
    });

    it('can properly open and load a new window from a link', async () => {
      const appPath = path.join(__dirname, 'fixtures', 'apps', 'open-new-window-from-link');

      appProcess = childProcess.spawn(process.execPath, [appPath]);

      const [code] = await once(appProcess, 'exit');
      expect(code).to.equal(0);
    });
  });

  describe('BrowserWindow.fromWebContents(webContents)', () => {
    afterEach(closeAllWindows);

    it('returns the window with the webContents', () => {
      const w = new BrowserWindow({ show: false });
      const found = BrowserWindow.fromWebContents(w.webContents);
      expect(found!.id).to.equal(w.id);
    });

    it('returns null for webContents without a BrowserWindow', () => {
      const contents = (webContents as typeof ElectronInternal.WebContents).create();
      try {
        expect(BrowserWindow.fromWebContents(contents)).to.be.null('BrowserWindow.fromWebContents(contents)');
      } finally {
        contents.destroy();
      }
    });

    it('returns the correct window for a BrowserView webcontents', async () => {
      const w = new BrowserWindow({ show: false });
      const bv = new BrowserView();
      w.setBrowserView(bv);
      defer(() => {
        w.removeBrowserView(bv);
        bv.webContents.destroy();
      });
      await bv.webContents.loadURL('about:blank');
      expect(BrowserWindow.fromWebContents(bv.webContents)!.id).to.equal(w.id);
    });

    it('returns the correct window for a WebView webcontents', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { webviewTag: true } });
      w.loadURL('data:text/html,<webview src="data:text/html,hi"></webview>');
      // NOTE(nornagon): Waiting for 'did-attach-webview' is a workaround for
      // https://github.com/electron/electron/issues/25413, and is not integral
      // to the test.
      const p = once(w.webContents, 'did-attach-webview');
      const [, webviewContents] = await once(app, 'web-contents-created') as [any, WebContents];
      expect(BrowserWindow.fromWebContents(webviewContents)!.id).to.equal(w.id);
      await p;
    });

    it('is usable immediately on browser-window-created', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('window.open(""); null');
      const [win, winFromWebContents] = await new Promise<any>((resolve) => {
        app.once('browser-window-created', (e, win) => {
          resolve([win, BrowserWindow.fromWebContents(win.webContents)]);
        });
      });
      expect(winFromWebContents).to.equal(win);
    });
  });

  describe('BrowserWindow.openDevTools()', () => {
    afterEach(closeAllWindows);
    it('does not crash for frameless window', () => {
      const w = new BrowserWindow({ show: false, frame: false });
      w.webContents.openDevTools();
    });
  });

  describe('BrowserWindow.fromBrowserView(browserView)', () => {
    afterEach(closeAllWindows);

    it('returns the window with the BrowserView', () => {
      const w = new BrowserWindow({ show: false });
      const bv = new BrowserView();
      w.setBrowserView(bv);
      defer(() => {
        w.removeBrowserView(bv);
        bv.webContents.destroy();
      });
      expect(BrowserWindow.fromBrowserView(bv)!.id).to.equal(w.id);
    });

    it('returns the window when there are multiple BrowserViews', () => {
      const w = new BrowserWindow({ show: false });
      const bv1 = new BrowserView();
      w.addBrowserView(bv1);
      const bv2 = new BrowserView();
      w.addBrowserView(bv2);
      defer(() => {
        w.removeBrowserView(bv1);
        w.removeBrowserView(bv2);
        bv1.webContents.destroy();
        bv2.webContents.destroy();
      });
      expect(BrowserWindow.fromBrowserView(bv1)!.id).to.equal(w.id);
      expect(BrowserWindow.fromBrowserView(bv2)!.id).to.equal(w.id);
    });

    it('returns undefined if not attached', () => {
      const bv = new BrowserView();
      defer(() => {
        bv.webContents.destroy();
      });
      expect(BrowserWindow.fromBrowserView(bv)).to.be.null('BrowserWindow associated with bv');
    });
  });

  describe('BrowserWindow.setOpacity(opacity)', () => {
    afterEach(closeAllWindows);

    ifdescribe(process.platform !== 'linux')(('Windows and Mac'), () => {
      it('make window with initial opacity', () => {
        const w = new BrowserWindow({ show: false, opacity: 0.5 });
        expect(w.getOpacity()).to.equal(0.5);
      });
      it('allows setting the opacity', () => {
        const w = new BrowserWindow({ show: false });
        expect(() => {
          w.setOpacity(0.0);
          expect(w.getOpacity()).to.equal(0.0);
          w.setOpacity(0.5);
          expect(w.getOpacity()).to.equal(0.5);
          w.setOpacity(1.0);
          expect(w.getOpacity()).to.equal(1.0);
        }).to.not.throw();
      });

      it('clamps opacity to [0.0...1.0]', () => {
        const w = new BrowserWindow({ show: false, opacity: 0.5 });
        w.setOpacity(100);
        expect(w.getOpacity()).to.equal(1.0);
        w.setOpacity(-100);
        expect(w.getOpacity()).to.equal(0.0);
      });
    });

    ifdescribe(process.platform === 'linux')(('Linux'), () => {
      it('sets 1 regardless of parameter', () => {
        const w = new BrowserWindow({ show: false });
        w.setOpacity(0);
        expect(w.getOpacity()).to.equal(1.0);
        w.setOpacity(0.5);
        expect(w.getOpacity()).to.equal(1.0);
      });
    });
  });

  describe('BrowserWindow.setShape(rects)', () => {
    afterEach(closeAllWindows);
    it('allows setting shape', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        w.setShape([]);
        w.setShape([{ x: 0, y: 0, width: 100, height: 100 }]);
        w.setShape([{ x: 0, y: 0, width: 100, height: 100 }, { x: 0, y: 200, width: 1000, height: 100 }]);
        w.setShape([]);
      }).to.not.throw();
    });
  });

  describe('"useContentSize" option', () => {
    afterEach(closeAllWindows);
    it('make window created with content size when used', () => {
      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        useContentSize: true
      });
      const contentSize = w.getContentSize();
      expect(contentSize).to.deep.equal([400, 400]);
    });
    it('make window created with window size when not used', () => {
      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400
      });
      const size = w.getSize();
      expect(size).to.deep.equal([400, 400]);
    });
    it('works for a frameless window', () => {
      const w = new BrowserWindow({
        show: false,
        frame: false,
        width: 400,
        height: 400,
        useContentSize: true
      });
      const contentSize = w.getContentSize();
      expect(contentSize).to.deep.equal([400, 400]);
      const size = w.getSize();
      expect(size).to.deep.equal([400, 400]);
    });
  });

  ifdescribe(['win32', 'darwin'].includes(process.platform))('"titleBarStyle" option', () => {
    const testWindowsOverlay = async (style: any) => {
      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        titleBarStyle: style,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        },
        titleBarOverlay: true
      });
      const overlayHTML = path.join(__dirname, 'fixtures', 'pages', 'overlay.html');
      if (process.platform === 'darwin') {
        await w.loadFile(overlayHTML);
      } else {
        const overlayReady = once(ipcMain, 'geometrychange');
        await w.loadFile(overlayHTML);
        await overlayReady;
      }
      const overlayEnabled = await w.webContents.executeJavaScript('navigator.windowControlsOverlay.visible');
      expect(overlayEnabled).to.be.true('overlayEnabled');
      const overlayRect = await w.webContents.executeJavaScript('getJSOverlayProperties()');
      expect(overlayRect.y).to.equal(0);
      if (process.platform === 'darwin') {
        expect(overlayRect.x).to.be.greaterThan(0);
      } else {
        expect(overlayRect.x).to.equal(0);
      }
      expect(overlayRect.width).to.be.greaterThan(0);
      expect(overlayRect.height).to.be.greaterThan(0);
      const cssOverlayRect = await w.webContents.executeJavaScript('getCssOverlayProperties();');
      expect(cssOverlayRect).to.deep.equal(overlayRect);
      const geometryChange = once(ipcMain, 'geometrychange');
      w.setBounds({ width: 800 });
      const [, newOverlayRect] = await geometryChange;
      expect(newOverlayRect.width).to.equal(overlayRect.width + 400);
    };
    afterEach(async () => {
      await closeAllWindows();
      ipcMain.removeAllListeners('geometrychange');
    });
    it('creates browser window with hidden title bar', () => {
      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        titleBarStyle: 'hidden'
      });
      const contentSize = w.getContentSize();
      expect(contentSize).to.deep.equal([400, 400]);
    });
    ifit(process.platform === 'darwin')('creates browser window with hidden inset title bar', () => {
      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        titleBarStyle: 'hiddenInset'
      });
      const contentSize = w.getContentSize();
      expect(contentSize).to.deep.equal([400, 400]);
    });
    it('sets Window Control Overlay with hidden title bar', async () => {
      await testWindowsOverlay('hidden');
    });
    ifit(process.platform === 'darwin')('sets Window Control Overlay with hidden inset title bar', async () => {
      await testWindowsOverlay('hiddenInset');
    });

    ifdescribe(process.platform === 'win32')('when an invalid titleBarStyle is initially set', () => {
      let w: BrowserWindow;

      beforeEach(() => {
        w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            contextIsolation: false
          },
          titleBarOverlay: {
            color: '#0000f0',
            symbolColor: '#ffffff'
          },
          titleBarStyle: 'hiddenInset'
        });
      });

      afterEach(async () => {
        await closeAllWindows();
      });

      it('does not crash changing minimizability ', () => {
        expect(() => {
          w.setMinimizable(false);
        }).to.not.throw();
      });

      it('does not crash changing maximizability', () => {
        expect(() => {
          w.setMaximizable(false);
        }).to.not.throw();
      });
    });
  });

  ifdescribe(['win32', 'darwin'].includes(process.platform))('"titleBarOverlay" option', () => {
    const testWindowsOverlayHeight = async (size: any) => {
      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        titleBarStyle: 'hidden',
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        },
        titleBarOverlay: {
          height: size
        }
      });
      const overlayHTML = path.join(__dirname, 'fixtures', 'pages', 'overlay.html');
      if (process.platform === 'darwin') {
        await w.loadFile(overlayHTML);
      } else {
        const overlayReady = once(ipcMain, 'geometrychange');
        await w.loadFile(overlayHTML);
        await overlayReady;
      }
      const overlayEnabled = await w.webContents.executeJavaScript('navigator.windowControlsOverlay.visible');
      expect(overlayEnabled).to.be.true('overlayEnabled');
      const overlayRectPreMax = await w.webContents.executeJavaScript('getJSOverlayProperties()');

      if (!w.isMaximized()) {
        const maximize = once(w, 'maximize');
        w.show();
        w.maximize();
        await maximize;
      }

      expect(w.isMaximized()).to.be.true('not maximized');
      const overlayRectPostMax = await w.webContents.executeJavaScript('getJSOverlayProperties()');

      expect(overlayRectPreMax.y).to.equal(0);
      if (process.platform === 'darwin') {
        expect(overlayRectPreMax.x).to.be.greaterThan(0);
      } else {
        expect(overlayRectPreMax.x).to.equal(0);
      }
      expect(overlayRectPreMax.width).to.be.greaterThan(0);

      expect(overlayRectPreMax.height).to.equal(size);
      // Confirm that maximization only affected the height of the buttons and not the title bar
      expect(overlayRectPostMax.height).to.equal(size);
    };
    afterEach(async () => {
      await closeAllWindows();
      ipcMain.removeAllListeners('geometrychange');
    });
    it('sets Window Control Overlay with title bar height of 40', async () => {
      await testWindowsOverlayHeight(40);
    });
  });

  ifdescribe(process.platform === 'win32')('BrowserWindow.setTitlebarOverlay', () => {
    afterEach(async () => {
      await closeAllWindows();
      ipcMain.removeAllListeners('geometrychange');
    });

    it('does not crash when an invalid titleBarStyle was initially set', () => {
      const win = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        },
        titleBarOverlay: {
          color: '#0000f0',
          symbolColor: '#ffffff'
        },
        titleBarStyle: 'hiddenInset'
      });

      expect(() => {
        win.setTitleBarOverlay({
          color: '#000000'
        });
      }).to.not.throw();
    });

    it('correctly updates the height of the overlay', async () => {
      const testOverlay = async (w: BrowserWindow, size: Number, firstRun: boolean) => {
        const overlayHTML = path.join(__dirname, 'fixtures', 'pages', 'overlay.html');
        const overlayReady = once(ipcMain, 'geometrychange');
        await w.loadFile(overlayHTML);
        if (firstRun) {
          await overlayReady;
        }

        const overlayEnabled = await w.webContents.executeJavaScript('navigator.windowControlsOverlay.visible');
        expect(overlayEnabled).to.be.true('overlayEnabled');
        const { height: preMaxHeight } = await w.webContents.executeJavaScript('getJSOverlayProperties()');

        if (!w.isMaximized()) {
          const maximize = once(w, 'maximize');
          w.show();
          w.maximize();
          await maximize;
        }

        expect(w.isMaximized()).to.be.true('not maximized');
        const { x, y, width, height } = await w.webContents.executeJavaScript('getJSOverlayProperties()');
        expect(x).to.equal(0);
        expect(y).to.equal(0);
        expect(width).to.be.greaterThan(0);
        expect(height).to.equal(size);
        expect(preMaxHeight).to.equal(size);
      };

      const INITIAL_SIZE = 40;
      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        titleBarStyle: 'hidden',
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        },
        titleBarOverlay: {
          height: INITIAL_SIZE
        }
      });

      await testOverlay(w, INITIAL_SIZE, true);

      w.setTitleBarOverlay({
        height: INITIAL_SIZE + 10
      });

      await testOverlay(w, INITIAL_SIZE + 10, false);
    });
  });

  ifdescribe(process.platform === 'darwin')('"enableLargerThanScreen" option', () => {
    afterEach(closeAllWindows);
    it('can move the window out of screen', () => {
      const w = new BrowserWindow({ show: true, enableLargerThanScreen: true });
      w.setPosition(-10, 50);
      const after = w.getPosition();
      expect(after).to.deep.equal([-10, 50]);
    });
    it('cannot move the window behind menu bar', () => {
      const w = new BrowserWindow({ show: true, enableLargerThanScreen: true });
      w.setPosition(-10, -10);
      const after = w.getPosition();
      expect(after[1]).to.be.at.least(0);
    });
    it('can move the window behind menu bar if it has no frame', () => {
      const w = new BrowserWindow({ show: true, enableLargerThanScreen: true, frame: false });
      w.setPosition(-10, -10);
      const after = w.getPosition();
      expect(after[0]).to.be.equal(-10);
      expect(after[1]).to.be.equal(-10);
    });
    it('without it, cannot move the window out of screen', () => {
      const w = new BrowserWindow({ show: true, enableLargerThanScreen: false });
      w.setPosition(-10, -10);
      const after = w.getPosition();
      expect(after[1]).to.be.at.least(0);
    });
    it('can set the window larger than screen', () => {
      const w = new BrowserWindow({ show: true, enableLargerThanScreen: true });
      const size = screen.getPrimaryDisplay().size;
      size.width += 100;
      size.height += 100;
      w.setSize(size.width, size.height);
      expectBoundsEqual(w.getSize(), [size.width, size.height]);
    });
    it('without it, cannot set the window larger than screen', () => {
      const w = new BrowserWindow({ show: true, enableLargerThanScreen: false });
      const size = screen.getPrimaryDisplay().size;
      size.width += 100;
      size.height += 100;
      w.setSize(size.width, size.height);
      expect(w.getSize()[1]).to.at.most(screen.getPrimaryDisplay().size.height);
    });
  });

  ifdescribe(process.platform === 'darwin')('"zoomToPageWidth" option', () => {
    afterEach(closeAllWindows);
    it('sets the window width to the page width when used', () => {
      const w = new BrowserWindow({
        show: false,
        width: 500,
        height: 400,
        zoomToPageWidth: true
      });
      w.maximize();
      expect(w.getSize()[0]).to.equal(500);
    });
  });

  describe('"tabbingIdentifier" option', () => {
    afterEach(closeAllWindows);
    it('can be set on a window', () => {
      expect(() => {
        /* eslint-disable-next-line no-new */
        new BrowserWindow({
          tabbingIdentifier: 'group1'
        });
        /* eslint-disable-next-line no-new */
        new BrowserWindow({
          tabbingIdentifier: 'group2',
          frame: false
        });
      }).not.to.throw();
    });
  });

  describe('"webPreferences" option', () => {
    afterEach(() => { ipcMain.removeAllListeners('answer'); });
    afterEach(closeAllWindows);

    describe('"preload" option', () => {
      const doesNotLeakSpec = (name: string, webPrefs: { nodeIntegration: boolean, sandbox: boolean, contextIsolation: boolean }) => {
        it(name, async () => {
          const w = new BrowserWindow({
            webPreferences: {
              ...webPrefs,
              preload: path.resolve(fixtures, 'module', 'empty.js')
            },
            show: false
          });
          w.loadFile(path.join(fixtures, 'api', 'no-leak.html'));
          const [, result] = await once(ipcMain, 'leak-result');
          expect(result).to.have.property('require', 'undefined');
          expect(result).to.have.property('exports', 'undefined');
          expect(result).to.have.property('windowExports', 'undefined');
          expect(result).to.have.property('windowPreload', 'undefined');
          expect(result).to.have.property('windowRequire', 'undefined');
        });
      };
      doesNotLeakSpec('does not leak require', {
        nodeIntegration: false,
        sandbox: false,
        contextIsolation: false
      });
      doesNotLeakSpec('does not leak require when sandbox is enabled', {
        nodeIntegration: false,
        sandbox: true,
        contextIsolation: false
      });
      doesNotLeakSpec('does not leak require when context isolation is enabled', {
        nodeIntegration: false,
        sandbox: false,
        contextIsolation: true
      });
      doesNotLeakSpec('does not leak require when context isolation and sandbox are enabled', {
        nodeIntegration: false,
        sandbox: true,
        contextIsolation: true
      });
      it('does not leak any node globals on the window object with nodeIntegration is disabled', async () => {
        let w = new BrowserWindow({
          webPreferences: {
            contextIsolation: false,
            nodeIntegration: false,
            preload: path.resolve(fixtures, 'module', 'empty.js')
          },
          show: false
        });
        w.loadFile(path.join(fixtures, 'api', 'globals.html'));
        const [, notIsolated] = await once(ipcMain, 'leak-result');
        expect(notIsolated).to.have.property('globals');

        w.destroy();
        w = new BrowserWindow({
          webPreferences: {
            contextIsolation: true,
            nodeIntegration: false,
            preload: path.resolve(fixtures, 'module', 'empty.js')
          },
          show: false
        });
        w.loadFile(path.join(fixtures, 'api', 'globals.html'));
        const [, isolated] = await once(ipcMain, 'leak-result');
        expect(isolated).to.have.property('globals');
        const notIsolatedGlobals = new Set(notIsolated.globals);
        for (const isolatedGlobal of isolated.globals) {
          notIsolatedGlobals.delete(isolatedGlobal);
        }
        expect([...notIsolatedGlobals]).to.deep.equal([], 'non-isolated renderer should have no additional globals');
      });

      it('loads the script before other scripts in window', async () => {
        const preload = path.join(fixtures, 'module', 'set-global.js');
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            contextIsolation: false,
            preload
          }
        });
        w.loadFile(path.join(fixtures, 'api', 'preload.html'));
        const [, test] = await once(ipcMain, 'answer');
        expect(test).to.eql('preload');
      });
      it('has synchronous access to all eventual window APIs', async () => {
        const preload = path.join(fixtures, 'module', 'access-blink-apis.js');
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            contextIsolation: false,
            preload
          }
        });
        w.loadFile(path.join(fixtures, 'api', 'preload.html'));
        const [, test] = await once(ipcMain, 'answer');
        expect(test).to.be.an('object');
        expect(test.atPreload).to.be.an('array');
        expect(test.atLoad).to.be.an('array');
        expect(test.atPreload).to.deep.equal(test.atLoad, 'should have access to the same window APIs');
      });
    });

    describe('session preload scripts', function () {
      const preloads = [
        path.join(fixtures, 'module', 'set-global-preload-1.js'),
        path.join(fixtures, 'module', 'set-global-preload-2.js'),
        path.relative(process.cwd(), path.join(fixtures, 'module', 'set-global-preload-3.js'))
      ];
      const defaultSession = session.defaultSession;

      beforeEach(() => {
        expect(defaultSession.getPreloads()).to.deep.equal([]);
        defaultSession.setPreloads(preloads);
      });
      afterEach(() => {
        defaultSession.setPreloads([]);
      });

      it('can set multiple session preload script', () => {
        expect(defaultSession.getPreloads()).to.deep.equal(preloads);
      });

      const generateSpecs = (description: string, sandbox: boolean) => {
        describe(description, () => {
          it('loads the script before other scripts in window including normal preloads', async () => {
            const w = new BrowserWindow({
              show: false,
              webPreferences: {
                sandbox,
                preload: path.join(fixtures, 'module', 'get-global-preload.js'),
                contextIsolation: false
              }
            });
            w.loadURL('about:blank');
            const [, preload1, preload2, preload3] = await once(ipcMain, 'vars');
            expect(preload1).to.equal('preload-1');
            expect(preload2).to.equal('preload-1-2');
            expect(preload3).to.be.undefined('preload 3');
          });
        });
      };

      generateSpecs('without sandbox', false);
      generateSpecs('with sandbox', true);
    });

    describe('"additionalArguments" option', () => {
      it('adds extra args to process.argv in the renderer process', async () => {
        const preload = path.join(fixtures, 'module', 'check-arguments.js');
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            preload,
            additionalArguments: ['--my-magic-arg']
          }
        });
        w.loadFile(path.join(fixtures, 'api', 'blank.html'));
        const [, argv] = await once(ipcMain, 'answer');
        expect(argv).to.include('--my-magic-arg');
      });

      it('adds extra value args to process.argv in the renderer process', async () => {
        const preload = path.join(fixtures, 'module', 'check-arguments.js');
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            preload,
            additionalArguments: ['--my-magic-arg=foo']
          }
        });
        w.loadFile(path.join(fixtures, 'api', 'blank.html'));
        const [, argv] = await once(ipcMain, 'answer');
        expect(argv).to.include('--my-magic-arg=foo');
      });
    });

    describe('"node-integration" option', () => {
      it('disables node integration by default', async () => {
        const preload = path.join(fixtures, 'module', 'send-later.js');
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            preload,
            contextIsolation: false
          }
        });
        w.loadFile(path.join(fixtures, 'api', 'blank.html'));
        const [, typeofProcess, typeofBuffer] = await once(ipcMain, 'answer');
        expect(typeofProcess).to.equal('undefined');
        expect(typeofBuffer).to.equal('undefined');
      });
    });

    describe('"sandbox" option', () => {
      const preload = path.join(path.resolve(__dirname, 'fixtures'), 'module', 'preload-sandbox.js');

      let server: http.Server;
      let serverUrl: string;

      before(async () => {
        server = http.createServer((request, response) => {
          switch (request.url) {
            case '/cross-site':
              response.end(`<html><body><h1>${request.url}</h1></body></html>`);
              break;
            default:
              throw new Error(`unsupported endpoint: ${request.url}`);
          }
        });
        serverUrl = (await listen(server)).url;
      });

      after(() => {
        server.close();
      });

      it('exposes ipcRenderer to preload script', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true,
            preload,
            contextIsolation: false
          }
        });
        w.loadFile(path.join(fixtures, 'api', 'preload.html'));
        const [, test] = await once(ipcMain, 'answer');
        expect(test).to.equal('preload');
      });

      it('exposes ipcRenderer to preload script (path has special chars)', async () => {
        const preloadSpecialChars = path.join(fixtures, 'module', 'preload-sandboxæø åü.js');
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true,
            preload: preloadSpecialChars,
            contextIsolation: false
          }
        });
        w.loadFile(path.join(fixtures, 'api', 'preload.html'));
        const [, test] = await once(ipcMain, 'answer');
        expect(test).to.equal('preload');
      });

      it('exposes "loaded" event to preload script', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true,
            preload
          }
        });
        w.loadURL('about:blank');
        await once(ipcMain, 'process-loaded');
      });

      it('exposes "exit" event to preload script', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true,
            preload,
            contextIsolation: false
          }
        });
        const htmlPath = path.join(__dirname, 'fixtures', 'api', 'sandbox.html?exit-event');
        const pageUrl = 'file://' + htmlPath;
        w.loadURL(pageUrl);
        const [, url] = await once(ipcMain, 'answer');
        const expectedUrl = process.platform === 'win32'
          ? 'file:///' + htmlPath.replaceAll('\\', '/')
          : pageUrl;
        expect(url).to.equal(expectedUrl);
      });

      it('exposes full EventEmitter object to preload script', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true,
            preload: path.join(fixtures, 'module', 'preload-eventemitter.js')
          }
        });
        w.loadURL('about:blank');
        const [, rendererEventEmitterProperties] = await once(ipcMain, 'answer');
        const { EventEmitter } = require('node:events');
        const emitter = new EventEmitter();
        const browserEventEmitterProperties = [];
        let currentObj = emitter;
        do {
          browserEventEmitterProperties.push(...Object.getOwnPropertyNames(currentObj));
        } while ((currentObj = Object.getPrototypeOf(currentObj)));
        expect(rendererEventEmitterProperties).to.deep.equal(browserEventEmitterProperties);
      });

      it('should open windows in same domain with cross-scripting enabled', async () => {
        const w = new BrowserWindow({
          show: true,
          webPreferences: {
            sandbox: true,
            preload,
            contextIsolation: false
          }
        });

        w.webContents.setWindowOpenHandler(() => ({
          action: 'allow',
          overrideBrowserWindowOptions: {
            webPreferences: {
              preload
            }
          }
        }));

        const htmlPath = path.join(__dirname, 'fixtures', 'api', 'sandbox.html?window-open');
        const pageUrl = 'file://' + htmlPath;
        const answer = once(ipcMain, 'answer');
        w.loadURL(pageUrl);
        const [, { url, frameName, options }] = await once(w.webContents, 'did-create-window') as [BrowserWindow, Electron.DidCreateWindowDetails];
        const expectedUrl = process.platform === 'win32'
          ? 'file:///' + htmlPath.replaceAll('\\', '/')
          : pageUrl;
        expect(url).to.equal(expectedUrl);
        expect(frameName).to.equal('popup!');
        expect(options.width).to.equal(500);
        expect(options.height).to.equal(600);
        const [, html] = await answer;
        expect(html).to.equal('<h1>scripting from opener</h1>');
      });

      it('should open windows in another domain with cross-scripting disabled', async () => {
        const w = new BrowserWindow({
          show: true,
          webPreferences: {
            sandbox: true,
            preload,
            contextIsolation: false
          }
        });

        w.webContents.setWindowOpenHandler(() => ({
          action: 'allow',
          overrideBrowserWindowOptions: {
            webPreferences: {
              preload
            }
          }
        }));

        w.loadFile(
          path.join(__dirname, 'fixtures', 'api', 'sandbox.html'),
          { search: 'window-open-external' }
        );

        // Wait for a message from the main window saying that it's ready.
        await once(ipcMain, 'opener-loaded');

        // Ask the opener to open a popup with window.opener.
        const expectedPopupUrl = `${serverUrl}/cross-site`; // Set in "sandbox.html".

        w.webContents.send('open-the-popup', expectedPopupUrl);

        // The page is going to open a popup that it won't be able to close.
        // We have to close it from here later.
        const [, popupWindow] = await once(app, 'browser-window-created') as [any, BrowserWindow];

        // Ask the popup window for details.
        const detailsAnswer = once(ipcMain, 'child-loaded');
        popupWindow.webContents.send('provide-details');
        const [, openerIsNull, , locationHref] = await detailsAnswer;
        expect(openerIsNull).to.be.false('window.opener is null');
        expect(locationHref).to.equal(expectedPopupUrl);

        // Ask the page to access the popup.
        const touchPopupResult = once(ipcMain, 'answer');
        w.webContents.send('touch-the-popup');
        const [, popupAccessMessage] = await touchPopupResult;

        // Ask the popup to access the opener.
        const touchOpenerResult = once(ipcMain, 'answer');
        popupWindow.webContents.send('touch-the-opener');
        const [, openerAccessMessage] = await touchOpenerResult;

        // We don't need the popup anymore, and its parent page can't close it,
        // so let's close it from here before we run any checks.
        await closeWindow(popupWindow, { assertNotWindows: false });

        const errorPattern = /Failed to read a named property 'document' from 'Window': Blocked a frame with origin "(.*?)" from accessing a cross-origin frame./;
        expect(popupAccessMessage).to.be.a('string',
          'child\'s .document is accessible from its parent window');
        expect(popupAccessMessage).to.match(errorPattern);
        expect(openerAccessMessage).to.be.a('string',
          'opener .document is accessible from a popup window');
        expect(openerAccessMessage).to.match(errorPattern);
      });

      it('should inherit the sandbox setting in opened windows', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true
          }
        });

        const preloadPath = path.join(mainFixtures, 'api', 'new-window-preload.js');
        w.webContents.setWindowOpenHandler(() => ({ action: 'allow', overrideBrowserWindowOptions: { webPreferences: { preload: preloadPath } } }));
        w.loadFile(path.join(fixtures, 'api', 'new-window.html'));
        const [, { argv }] = await once(ipcMain, 'answer');
        expect(argv).to.include('--enable-sandbox');
      });

      it('should open windows with the options configured via setWindowOpenHandler handlers', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true
          }
        });

        const preloadPath = path.join(mainFixtures, 'api', 'new-window-preload.js');
        w.webContents.setWindowOpenHandler(() => ({ action: 'allow', overrideBrowserWindowOptions: { webPreferences: { preload: preloadPath, contextIsolation: false } } }));
        w.loadFile(path.join(fixtures, 'api', 'new-window.html'));
        const [[, childWebContents]] = await Promise.all([
          once(app, 'web-contents-created') as Promise<[any, WebContents]>,
          once(ipcMain, 'answer')
        ]);
        const webPreferences = childWebContents.getLastWebPreferences();
        expect(webPreferences!.contextIsolation).to.equal(false);
      });

      it('should set ipc event sender correctly', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true,
            preload,
            contextIsolation: false
          }
        });
        let childWc: WebContents | null = null;
        w.webContents.setWindowOpenHandler(() => ({ action: 'allow', overrideBrowserWindowOptions: { webPreferences: { preload, contextIsolation: false } } }));

        w.webContents.on('did-create-window', (win) => {
          childWc = win.webContents;
          expect(w.webContents).to.not.equal(childWc);
        });

        ipcMain.once('parent-ready', function (event) {
          expect(event.sender).to.equal(w.webContents, 'sender should be the parent');
          event.sender.send('verified');
        });
        ipcMain.once('child-ready', function (event) {
          expect(childWc).to.not.be.null('child webcontents should be available');
          expect(event.sender).to.equal(childWc, 'sender should be the child');
          event.sender.send('verified');
        });

        const done = Promise.all([
          'parent-answer',
          'child-answer'
        ].map(name => once(ipcMain, name)));
        w.loadFile(path.join(__dirname, 'fixtures', 'api', 'sandbox.html'), { search: 'verify-ipc-sender' });
        await done;
      });

      describe('event handling', () => {
        let w: BrowserWindow;
        beforeEach(() => {
          w = new BrowserWindow({ show: false, webPreferences: { sandbox: true } });
        });
        it('works for window events', async () => {
          const pageTitleUpdated = once(w, 'page-title-updated');
          w.loadURL('data:text/html,<script>document.title = \'changed\'</script>');
          await pageTitleUpdated;
        });

        it('works for stop events', async () => {
          const done = Promise.all([
            'did-navigate',
            'did-fail-load',
            'did-stop-loading'
          ].map(name => once(w.webContents, name)));
          w.loadURL('data:text/html,<script>stop()</script>');
          await done;
        });

        it('works for web contents events', async () => {
          const done = Promise.all([
            'did-finish-load',
            'did-frame-finish-load',
            'did-navigate-in-page',
            'will-navigate',
            'did-start-loading',
            'did-stop-loading',
            'did-frame-finish-load',
            'dom-ready'
          ].map(name => once(w.webContents, name)));
          w.loadFile(path.join(__dirname, 'fixtures', 'api', 'sandbox.html'), { search: 'webcontents-events' });
          await done;
        });
      });

      it('validates process APIs access in sandboxed renderer', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true,
            preload,
            contextIsolation: false
          }
        });
        w.webContents.once('preload-error', (event, preloadPath, error) => {
          throw error;
        });
        process.env.sandboxmain = 'foo';
        w.loadFile(path.join(fixtures, 'api', 'preload.html'));
        const [, test] = await once(ipcMain, 'answer');
        expect(test.hasCrash).to.be.true('has crash');
        expect(test.hasHang).to.be.true('has hang');
        expect(test.heapStatistics).to.be.an('object');
        expect(test.blinkMemoryInfo).to.be.an('object');
        expect(test.processMemoryInfo).to.be.an('object');
        expect(test.systemVersion).to.be.a('string');
        expect(test.cpuUsage).to.be.an('object');
        expect(test.uptime).to.be.a('number');
        expect(test.arch).to.equal(process.arch);
        expect(test.platform).to.equal(process.platform);
        expect(test.env).to.deep.equal(process.env);
        expect(test.execPath).to.equal(process.helperExecPath);
        expect(test.sandboxed).to.be.true('sandboxed');
        expect(test.contextIsolated).to.be.false('contextIsolated');
        expect(test.type).to.equal('renderer');
        expect(test.version).to.equal(process.version);
        expect(test.versions).to.deep.equal(process.versions);
        expect(test.contextId).to.be.a('string');
        expect(test.nodeEvents).to.equal(true);
        expect(test.nodeTimers).to.equal(true);
        expect(test.nodeUrl).to.equal(true);

        if (process.platform === 'linux' && test.osSandbox) {
          expect(test.creationTime).to.be.null('creation time');
          expect(test.systemMemoryInfo).to.be.null('system memory info');
        } else {
          expect(test.creationTime).to.be.a('number');
          expect(test.systemMemoryInfo).to.be.an('object');
        }
      });

      it('webview in sandbox renderer', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            sandbox: true,
            preload,
            webviewTag: true,
            contextIsolation: false
          }
        });
        const didAttachWebview = once(w.webContents, 'did-attach-webview') as Promise<[any, WebContents]>;
        const webviewDomReady = once(ipcMain, 'webview-dom-ready');
        w.loadFile(path.join(fixtures, 'pages', 'webview-did-attach-event.html'));

        const [, webContents] = await didAttachWebview;
        const [, id] = await webviewDomReady;
        expect(webContents.id).to.equal(id);
      });
    });

    describe('child windows', () => {
      let w: BrowserWindow;

      beforeEach(() => {
        w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            // tests relies on preloads in opened windows
            nodeIntegrationInSubFrames: true,
            contextIsolation: false
          }
        });
      });

      it('opens window of about:blank with cross-scripting enabled', async () => {
        const answer = once(ipcMain, 'answer');
        w.loadFile(path.join(fixtures, 'api', 'native-window-open-blank.html'));
        const [, content] = await answer;
        expect(content).to.equal('Hello');
      });
      it('opens window of same domain with cross-scripting enabled', async () => {
        const answer = once(ipcMain, 'answer');
        w.loadFile(path.join(fixtures, 'api', 'native-window-open-file.html'));
        const [, content] = await answer;
        expect(content).to.equal('Hello');
      });
      it('blocks accessing cross-origin frames', async () => {
        const answer = once(ipcMain, 'answer');
        w.loadFile(path.join(fixtures, 'api', 'native-window-open-cross-origin.html'));
        const [, content] = await answer;
        expect(content).to.equal('Failed to read a named property \'toString\' from \'Location\': Blocked a frame with origin "file://" from accessing a cross-origin frame.');
      });
      it('opens window from <iframe> tags', async () => {
        const answer = once(ipcMain, 'answer');
        w.loadFile(path.join(fixtures, 'api', 'native-window-open-iframe.html'));
        const [, content] = await answer;
        expect(content).to.equal('Hello');
      });
      it('opens window with cross-scripting enabled from isolated context', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            preload: path.join(fixtures, 'api', 'native-window-open-isolated-preload.js')
          }
        });
        w.loadFile(path.join(fixtures, 'api', 'native-window-open-isolated.html'));
        const [, content] = await once(ipcMain, 'answer');
        expect(content).to.equal('Hello');
      });
      ifit(!process.env.ELECTRON_SKIP_NATIVE_MODULE_TESTS)('loads native addons correctly after reload', async () => {
        w.loadFile(path.join(__dirname, 'fixtures', 'api', 'native-window-open-native-addon.html'));
        {
          const [, content] = await once(ipcMain, 'answer');
          expect(content).to.equal('function');
        }
        w.reload();
        {
          const [, content] = await once(ipcMain, 'answer');
          expect(content).to.equal('function');
        }
      });
      it('<webview> works in a scriptable popup', async () => {
        const preload = path.join(fixtures, 'api', 'new-window-webview-preload.js');

        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegrationInSubFrames: true,
            webviewTag: true,
            contextIsolation: false,
            preload
          }
        });
        w.webContents.setWindowOpenHandler(() => ({
          action: 'allow',
          overrideBrowserWindowOptions: {
            show: false,
            webPreferences: {
              contextIsolation: false,
              webviewTag: true,
              nodeIntegrationInSubFrames: true,
              preload
            }
          }
        }));

        const webviewLoaded = once(ipcMain, 'webview-loaded');
        w.loadFile(path.join(fixtures, 'api', 'new-window-webview.html'));
        await webviewLoaded;
      });
      it('should open windows with the options configured via setWindowOpenHandler handlers', async () => {
        const preloadPath = path.join(mainFixtures, 'api', 'new-window-preload.js');
        w.webContents.setWindowOpenHandler(() => ({
          action: 'allow',
          overrideBrowserWindowOptions: {
            webPreferences: {
              preload: preloadPath,
              contextIsolation: false
            }
          }
        }));
        w.loadFile(path.join(fixtures, 'api', 'new-window.html'));
        const [[, childWebContents]] = await Promise.all([
          once(app, 'web-contents-created') as Promise<[any, WebContents]>,
          once(ipcMain, 'answer')
        ]);
        const webPreferences = childWebContents.getLastWebPreferences();
        expect(webPreferences!.contextIsolation).to.equal(false);
      });

      describe('window.location', () => {
        const protocols = [
          ['foo', path.join(fixtures, 'api', 'window-open-location-change.html')],
          ['bar', path.join(fixtures, 'api', 'window-open-location-final.html')]
        ];
        beforeEach(() => {
          for (const [scheme, path] of protocols) {
            protocol.registerBufferProtocol(scheme, (request, callback) => {
              callback({
                mimeType: 'text/html',
                data: fs.readFileSync(path)
              });
            });
          }
        });
        afterEach(() => {
          for (const [scheme] of protocols) {
            protocol.unregisterProtocol(scheme);
          }
        });
        it('retains the original web preferences when window.location is changed to a new origin', async () => {
          const w = new BrowserWindow({
            show: false,
            webPreferences: {
              // test relies on preloads in opened window
              nodeIntegrationInSubFrames: true,
              contextIsolation: false
            }
          });

          w.webContents.setWindowOpenHandler(() => ({
            action: 'allow',
            overrideBrowserWindowOptions: {
              webPreferences: {
                preload: path.join(mainFixtures, 'api', 'window-open-preload.js'),
                contextIsolation: false,
                nodeIntegrationInSubFrames: true
              }
            }
          }));

          w.loadFile(path.join(fixtures, 'api', 'window-open-location-open.html'));
          const [, { nodeIntegration, typeofProcess }] = await once(ipcMain, 'answer');
          expect(nodeIntegration).to.be.false();
          expect(typeofProcess).to.eql('undefined');
        });

        it('window.opener is not null when window.location is changed to a new origin', async () => {
          const w = new BrowserWindow({
            show: false,
            webPreferences: {
              // test relies on preloads in opened window
              nodeIntegrationInSubFrames: true
            }
          });

          w.webContents.setWindowOpenHandler(() => ({
            action: 'allow',
            overrideBrowserWindowOptions: {
              webPreferences: {
                preload: path.join(mainFixtures, 'api', 'window-open-preload.js')
              }
            }
          }));
          w.loadFile(path.join(fixtures, 'api', 'window-open-location-open.html'));
          const [, { windowOpenerIsNull }] = await once(ipcMain, 'answer');
          expect(windowOpenerIsNull).to.be.false('window.opener is null');
        });
      });
    });

    describe('"disableHtmlFullscreenWindowResize" option', () => {
      it('prevents window from resizing when set', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            disableHtmlFullscreenWindowResize: true
          }
        });
        await w.loadURL('about:blank');
        const size = w.getSize();
        const enterHtmlFullScreen = once(w.webContents, 'enter-html-full-screen');
        w.webContents.executeJavaScript('document.body.webkitRequestFullscreen()', true);
        await enterHtmlFullScreen;
        expect(w.getSize()).to.deep.equal(size);
      });
    });

    describe('"defaultFontFamily" option', () => {
      it('can change the standard font family', async () => {
        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            defaultFontFamily: {
              standard: 'Impact'
            }
          }
        });
        await w.loadFile(path.join(fixtures, 'pages', 'content.html'));
        const fontFamily = await w.webContents.executeJavaScript("window.getComputedStyle(document.getElementsByTagName('p')[0])['font-family']", true);
        expect(fontFamily).to.equal('Impact');
      });
    });
  });

  describe('beforeunload handler', function () {
    let w: BrowserWindow;
    beforeEach(() => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
    });
    afterEach(closeAllWindows);

    it('returning undefined would not prevent close', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-undefined.html'));
      const wait = once(w, 'closed');
      w.close();
      await wait;
    });

    it('returning false would prevent close', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false.html'));
      w.close();
      const [, proceed] = await once(w.webContents, '-before-unload-fired');
      expect(proceed).to.equal(false);
    });

    it('returning empty string would prevent close', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-empty-string.html'));
      w.close();
      const [, proceed] = await once(w.webContents, '-before-unload-fired');
      expect(proceed).to.equal(false);
    });

    it('emits for each close attempt', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false-prevent3.html'));

      const destroyListener = () => { expect.fail('Close was not prevented'); };
      w.webContents.once('destroyed', destroyListener);

      w.webContents.executeJavaScript('installBeforeUnload(2)', true);
      // The renderer needs to report the status of beforeunload handler
      // back to main process, so wait for next console message, which means
      // the SuddenTerminationStatus message have been flushed.
      await once(w.webContents, 'console-message');
      w.close();
      await once(w.webContents, '-before-unload-fired');
      w.close();
      await once(w.webContents, '-before-unload-fired');

      w.webContents.removeListener('destroyed', destroyListener);
      const wait = once(w, 'closed');
      w.close();
      await wait;
    });

    it('emits for each reload attempt', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false-prevent3.html'));

      const navigationListener = () => { expect.fail('Reload was not prevented'); };
      w.webContents.once('did-start-navigation', navigationListener);

      w.webContents.executeJavaScript('installBeforeUnload(2)', true);
      // The renderer needs to report the status of beforeunload handler
      // back to main process, so wait for next console message, which means
      // the SuddenTerminationStatus message have been flushed.
      await once(w.webContents, 'console-message');
      w.reload();
      // Chromium does not emit '-before-unload-fired' on WebContents for
      // navigations, so we have to use other ways to know if beforeunload
      // is fired.
      await emittedUntil(w.webContents, 'console-message', isBeforeUnload);
      w.reload();
      await emittedUntil(w.webContents, 'console-message', isBeforeUnload);

      w.webContents.removeListener('did-start-navigation', navigationListener);
      w.reload();
      await once(w.webContents, 'did-finish-load');
    });

    it('emits for each navigation attempt', async () => {
      await w.loadFile(path.join(__dirname, 'fixtures', 'api', 'beforeunload-false-prevent3.html'));

      const navigationListener = () => { expect.fail('Reload was not prevented'); };
      w.webContents.once('did-start-navigation', navigationListener);

      w.webContents.executeJavaScript('installBeforeUnload(2)', true);
      // The renderer needs to report the status of beforeunload handler
      // back to main process, so wait for next console message, which means
      // the SuddenTerminationStatus message have been flushed.
      await once(w.webContents, 'console-message');
      w.loadURL('about:blank');
      // Chromium does not emit '-before-unload-fired' on WebContents for
      // navigations, so we have to use other ways to know if beforeunload
      // is fired.
      await emittedUntil(w.webContents, 'console-message', isBeforeUnload);
      w.loadURL('about:blank');
      await emittedUntil(w.webContents, 'console-message', isBeforeUnload);

      w.webContents.removeListener('did-start-navigation', navigationListener);
      await w.loadURL('about:blank');
    });
  });

  // TODO(codebytere): figure out how to make these pass in CI on Windows.
  ifdescribe(process.platform !== 'win32')('document.visibilityState/hidden', () => {
    afterEach(closeAllWindows);

    it('visibilityState is initially visible despite window being hidden', async () => {
      const w = new BrowserWindow({
        show: false,
        width: 100,
        height: 100,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });

      let readyToShow = false;
      w.once('ready-to-show', () => {
        readyToShow = true;
      });

      w.loadFile(path.join(fixtures, 'pages', 'visibilitychange.html'));

      const [, visibilityState, hidden] = await once(ipcMain, 'pong');

      expect(readyToShow).to.be.false('ready to show');
      expect(visibilityState).to.equal('visible');
      expect(hidden).to.be.false('hidden');
    });

    it('visibilityState changes when window is hidden', async () => {
      const w = new BrowserWindow({
        width: 100,
        height: 100,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });

      w.loadFile(path.join(fixtures, 'pages', 'visibilitychange.html'));

      {
        const [, visibilityState, hidden] = await once(ipcMain, 'pong');
        expect(visibilityState).to.equal('visible');
        expect(hidden).to.be.false('hidden');
      }

      w.hide();

      {
        const [, visibilityState, hidden] = await once(ipcMain, 'pong');
        expect(visibilityState).to.equal('hidden');
        expect(hidden).to.be.true('hidden');
      }
    });

    it('visibilityState changes when window is shown', async () => {
      const w = new BrowserWindow({
        width: 100,
        height: 100,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });

      w.loadFile(path.join(fixtures, 'pages', 'visibilitychange.html'));
      if (process.platform === 'darwin') {
        // See https://github.com/electron/electron/issues/8664
        await once(w, 'show');
      }
      w.hide();
      w.show();
      const [, visibilityState] = await once(ipcMain, 'pong');
      expect(visibilityState).to.equal('visible');
    });

    it('visibilityState changes when window is shown inactive', async () => {
      const w = new BrowserWindow({
        width: 100,
        height: 100,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      w.loadFile(path.join(fixtures, 'pages', 'visibilitychange.html'));
      if (process.platform === 'darwin') {
        // See https://github.com/electron/electron/issues/8664
        await once(w, 'show');
      }
      w.hide();
      w.showInactive();
      const [, visibilityState] = await once(ipcMain, 'pong');
      expect(visibilityState).to.equal('visible');
    });

    ifit(process.platform === 'darwin')('visibilityState changes when window is minimized', async () => {
      const w = new BrowserWindow({
        width: 100,
        height: 100,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      w.loadFile(path.join(fixtures, 'pages', 'visibilitychange.html'));

      {
        const [, visibilityState, hidden] = await once(ipcMain, 'pong');
        expect(visibilityState).to.equal('visible');
        expect(hidden).to.be.false('hidden');
      }

      w.minimize();

      {
        const [, visibilityState, hidden] = await once(ipcMain, 'pong');
        expect(visibilityState).to.equal('hidden');
        expect(hidden).to.be.true('hidden');
      }
    });

    it('visibilityState remains visible if backgroundThrottling is disabled', async () => {
      const w = new BrowserWindow({
        show: false,
        width: 100,
        height: 100,
        webPreferences: {
          backgroundThrottling: false,
          nodeIntegration: true,
          contextIsolation: false
        }
      });

      w.loadFile(path.join(fixtures, 'pages', 'visibilitychange.html'));

      {
        const [, visibilityState, hidden] = await once(ipcMain, 'pong');
        expect(visibilityState).to.equal('visible');
        expect(hidden).to.be.false('hidden');
      }

      ipcMain.once('pong', (event, visibilityState, hidden) => {
        throw new Error(`Unexpected visibility change event. visibilityState: ${visibilityState} hidden: ${hidden}`);
      });
      try {
        const shown1 = once(w, 'show');
        w.show();
        await shown1;
        const hidden = once(w, 'hide');
        w.hide();
        await hidden;
        const shown2 = once(w, 'show');
        w.show();
        await shown2;
      } finally {
        ipcMain.removeAllListeners('pong');
      }
    });
  });

  ifdescribe(process.platform !== 'linux')('max/minimize events', () => {
    afterEach(closeAllWindows);
    it('emits an event when window is maximized', async () => {
      const w = new BrowserWindow({ show: false });
      const maximize = once(w, 'maximize');
      w.show();
      w.maximize();
      await maximize;
    });

    it('emits an event when a transparent window is maximized', async () => {
      const w = new BrowserWindow({
        show: false,
        frame: false,
        transparent: true
      });

      const maximize = once(w, 'maximize');
      w.show();
      w.maximize();
      await maximize;
    });

    it('emits only one event when frameless window is maximized', () => {
      const w = new BrowserWindow({ show: false, frame: false });
      let emitted = 0;
      w.on('maximize', () => emitted++);
      w.show();
      w.maximize();
      expect(emitted).to.equal(1);
    });

    it('emits an event when window is unmaximized', async () => {
      const w = new BrowserWindow({ show: false });
      const unmaximize = once(w, 'unmaximize');
      w.show();
      w.maximize();
      w.unmaximize();
      await unmaximize;
    });

    it('emits an event when a transparent window is unmaximized', async () => {
      const w = new BrowserWindow({
        show: false,
        frame: false,
        transparent: true
      });

      const maximize = once(w, 'maximize');
      const unmaximize = once(w, 'unmaximize');
      w.show();
      w.maximize();
      await maximize;
      w.unmaximize();
      await unmaximize;
    });

    it('emits an event when window is minimized', async () => {
      const w = new BrowserWindow({ show: false });
      const minimize = once(w, 'minimize');
      w.show();
      w.minimize();
      await minimize;
    });
  });

  describe('beginFrameSubscription method', () => {
    it('does not crash when callback returns nothing', (done) => {
      const w = new BrowserWindow({ show: false });
      let called = false;
      w.loadFile(path.join(fixtures, 'api', 'frame-subscriber.html'));
      w.webContents.on('dom-ready', () => {
        w.webContents.beginFrameSubscription(function () {
          // This callback might be called twice.
          if (called) return;
          called = true;

          // Pending endFrameSubscription to next tick can reliably reproduce
          // a crash which happens when nothing is returned in the callback.
          setTimeout().then(() => {
            w.webContents.endFrameSubscription();
            done();
          });
        });
      });
    });

    it('subscribes to frame updates', (done) => {
      const w = new BrowserWindow({ show: false });
      let called = false;
      w.loadFile(path.join(fixtures, 'api', 'frame-subscriber.html'));
      w.webContents.on('dom-ready', () => {
        w.webContents.beginFrameSubscription(function (data) {
          // This callback might be called twice.
          if (called) return;
          called = true;

          try {
            expect(data.constructor.name).to.equal('NativeImage');
            expect(data.isEmpty()).to.be.false('data is empty');
            done();
          } catch (e) {
            done(e);
          } finally {
            w.webContents.endFrameSubscription();
          }
        });
      });
    });

    it('subscribes to frame updates (only dirty rectangle)', (done) => {
      const w = new BrowserWindow({ show: false });
      let called = false;
      let gotInitialFullSizeFrame = false;
      const [contentWidth, contentHeight] = w.getContentSize();
      w.webContents.on('did-finish-load', () => {
        w.webContents.beginFrameSubscription(true, (image, rect) => {
          if (image.isEmpty()) {
            // Chromium sometimes sends a 0x0 frame at the beginning of the
            // page load.
            return;
          }
          if (rect.height === contentHeight && rect.width === contentWidth &&
            !gotInitialFullSizeFrame) {
            // The initial frame is full-size, but we're looking for a call
            // with just the dirty-rect. The next frame should be a smaller
            // rect.
            gotInitialFullSizeFrame = true;
            return;
          }
          // This callback might be called twice.
          if (called) return;
          // We asked for just the dirty rectangle, so we expect to receive a
          // rect smaller than the full size.
          // TODO(jeremy): this is failing on windows currently; investigate.
          // assert(rect.width < contentWidth || rect.height < contentHeight)
          called = true;

          try {
            const expectedSize = rect.width * rect.height * 4;
            expect(image.getBitmap()).to.be.an.instanceOf(Buffer).with.lengthOf(expectedSize);
            done();
          } catch (e) {
            done(e);
          } finally {
            w.webContents.endFrameSubscription();
          }
        });
      });
      w.loadFile(path.join(fixtures, 'api', 'frame-subscriber.html'));
    });

    it('throws error when subscriber is not well defined', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        w.webContents.beginFrameSubscription(true, true as any);
        // TODO(zcbenz): gin is weak at guessing parameter types, we should
        // upstream native_mate's implementation to gin.
      }).to.throw('Error processing argument at index 1, conversion failure from ');
    });
  });

  describe('savePage method', () => {
    const savePageDir = path.join(fixtures, 'save_page');
    const savePageHtmlPath = path.join(savePageDir, 'save_page.html');
    const savePageJsPath = path.join(savePageDir, 'save_page_files', 'test.js');
    const savePageCssPath = path.join(savePageDir, 'save_page_files', 'test.css');

    afterEach(() => {
      closeAllWindows();

      try {
        fs.unlinkSync(savePageCssPath);
        fs.unlinkSync(savePageJsPath);
        fs.unlinkSync(savePageHtmlPath);
        fs.rmdirSync(path.join(savePageDir, 'save_page_files'));
        fs.rmdirSync(savePageDir);
      } catch {}
    });

    it('should throw when passing relative paths', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixtures, 'pages', 'save_page', 'index.html'));

      await expect(
        w.webContents.savePage('save_page.html', 'HTMLComplete')
      ).to.eventually.be.rejectedWith('Path must be absolute');

      await expect(
        w.webContents.savePage('save_page.html', 'HTMLOnly')
      ).to.eventually.be.rejectedWith('Path must be absolute');

      await expect(
        w.webContents.savePage('save_page.html', 'MHTML')
      ).to.eventually.be.rejectedWith('Path must be absolute');
    });

    it('should save page to disk with HTMLOnly', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixtures, 'pages', 'save_page', 'index.html'));
      await w.webContents.savePage(savePageHtmlPath, 'HTMLOnly');

      expect(fs.existsSync(savePageHtmlPath)).to.be.true('html path');
      expect(fs.existsSync(savePageJsPath)).to.be.false('js path');
      expect(fs.existsSync(savePageCssPath)).to.be.false('css path');
    });

    it('should save page to disk with MHTML', async () => {
      /* Use temp directory for saving MHTML file since the write handle
       * gets passed to untrusted process and chromium will deny exec access to
       * the path. To perform this task, chromium requires that the path is one
       * of the browser controlled paths, refs https://chromium-review.googlesource.com/c/chromium/src/+/3774416
       */
      const tmpDir = await fs.promises.mkdtemp(path.resolve(os.tmpdir(), 'electron-mhtml-save-'));
      const savePageMHTMLPath = path.join(tmpDir, 'save_page.html');
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixtures, 'pages', 'save_page', 'index.html'));
      await w.webContents.savePage(savePageMHTMLPath, 'MHTML');

      expect(fs.existsSync(savePageMHTMLPath)).to.be.true('html path');
      expect(fs.existsSync(savePageJsPath)).to.be.false('js path');
      expect(fs.existsSync(savePageCssPath)).to.be.false('css path');
      try {
        await fs.promises.unlink(savePageMHTMLPath);
        await fs.promises.rmdir(tmpDir);
      } catch {}
    });

    it('should save page to disk with HTMLComplete', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixtures, 'pages', 'save_page', 'index.html'));
      await w.webContents.savePage(savePageHtmlPath, 'HTMLComplete');

      expect(fs.existsSync(savePageHtmlPath)).to.be.true('html path');
      expect(fs.existsSync(savePageJsPath)).to.be.true('js path');
      expect(fs.existsSync(savePageCssPath)).to.be.true('css path');
    });
  });

  describe('BrowserWindow options argument is optional', () => {
    afterEach(closeAllWindows);
    it('should create a window with default size (800x600)', () => {
      const w = new BrowserWindow();
      expect(w.getSize()).to.deep.equal([800, 600]);
    });
  });

  describe('BrowserWindow.restore()', () => {
    afterEach(closeAllWindows);
    it('should restore the previous window size', () => {
      const w = new BrowserWindow({
        minWidth: 800,
        width: 800
      });

      const initialSize = w.getSize();
      w.minimize();
      w.restore();
      expectBoundsEqual(w.getSize(), initialSize);
    });

    it('does not crash when restoring hidden minimized window', () => {
      const w = new BrowserWindow({});
      w.minimize();
      w.hide();
      w.show();
    });

    // TODO(zcbenz):
    // This test does not run on Linux CI. See:
    // https://github.com/electron/electron/issues/28699
    ifit(process.platform === 'linux' && !process.env.CI)('should bring a minimized maximized window back to maximized state', async () => {
      const w = new BrowserWindow({});
      const maximize = once(w, 'maximize');
      w.maximize();
      await maximize;
      const minimize = once(w, 'minimize');
      w.minimize();
      await minimize;
      expect(w.isMaximized()).to.equal(false);
      const restore = once(w, 'restore');
      w.restore();
      await restore;
      expect(w.isMaximized()).to.equal(true);
    });
  });

  // TODO(dsanders11): Enable once maximize event works on Linux again on CI
  ifdescribe(process.platform !== 'linux')('BrowserWindow.maximize()', () => {
    afterEach(closeAllWindows);
    it('should show the window if it is not currently shown', async () => {
      const w = new BrowserWindow({ show: false });
      const hidden = once(w, 'hide');
      let shown = once(w, 'show');
      const maximize = once(w, 'maximize');
      expect(w.isVisible()).to.be.false('visible');
      w.maximize();
      await maximize;
      await shown;
      expect(w.isMaximized()).to.be.true('maximized');
      expect(w.isVisible()).to.be.true('visible');
      // Even if the window is already maximized
      w.hide();
      await hidden;
      expect(w.isVisible()).to.be.false('visible');
      shown = once(w, 'show');
      w.maximize();
      await shown;
      expect(w.isVisible()).to.be.true('visible');
    });
  });

  describe('BrowserWindow.unmaximize()', () => {
    afterEach(closeAllWindows);
    it('should restore the previous window position', () => {
      const w = new BrowserWindow();

      const initialPosition = w.getPosition();
      w.maximize();
      w.unmaximize();
      expectBoundsEqual(w.getPosition(), initialPosition);
    });

    // TODO(dsanders11): Enable once minimize event works on Linux again.
    //                   See https://github.com/electron/electron/issues/28699
    ifit(process.platform !== 'linux')('should not restore a minimized window', async () => {
      const w = new BrowserWindow();
      const minimize = once(w, 'minimize');
      w.minimize();
      await minimize;
      w.unmaximize();
      await setTimeout(1000);
      expect(w.isMinimized()).to.be.true();
    });

    it('should not change the size or position of a normal window', async () => {
      const w = new BrowserWindow();

      const initialSize = w.getSize();
      const initialPosition = w.getPosition();
      w.unmaximize();
      await setTimeout(1000);
      expectBoundsEqual(w.getSize(), initialSize);
      expectBoundsEqual(w.getPosition(), initialPosition);
    });

    ifit(process.platform === 'darwin')('should not change size or position of a window which is functionally maximized', async () => {
      const { workArea } = screen.getPrimaryDisplay();

      const bounds = {
        x: workArea.x,
        y: workArea.y,
        width: workArea.width,
        height: workArea.height
      };

      const w = new BrowserWindow(bounds);
      w.unmaximize();
      await setTimeout(1000);
      expectBoundsEqual(w.getBounds(), bounds);
    });
  });

  describe('setFullScreen(false)', () => {
    afterEach(closeAllWindows);

    // only applicable to windows: https://github.com/electron/electron/issues/6036
    ifdescribe(process.platform === 'win32')('on windows', () => {
      it('should restore a normal visible window from a fullscreen startup state', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadURL('about:blank');
        const shown = once(w, 'show');
        // start fullscreen and hidden
        w.setFullScreen(true);
        w.show();
        await shown;
        const leftFullScreen = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await leftFullScreen;
        expect(w.isVisible()).to.be.true('visible');
        expect(w.isFullScreen()).to.be.false('fullscreen');
      });
      it('should keep window hidden if already in hidden state', async () => {
        const w = new BrowserWindow({ show: false });
        await w.loadURL('about:blank');
        const leftFullScreen = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await leftFullScreen;
        expect(w.isVisible()).to.be.false('visible');
        expect(w.isFullScreen()).to.be.false('fullscreen');
      });
    });

    ifdescribe(process.platform === 'darwin')('BrowserWindow.setFullScreen(false) when HTML fullscreen', () => {
      it('exits HTML fullscreen when window leaves fullscreen', async () => {
        const w = new BrowserWindow();
        await w.loadURL('about:blank');
        await w.webContents.executeJavaScript('document.body.webkitRequestFullscreen()', true);
        await once(w, 'enter-full-screen');
        // Wait a tick for the full-screen state to 'stick'
        await setTimeout();
        w.setFullScreen(false);
        await once(w, 'leave-html-full-screen');
      });
    });
  });

  describe('parent window', () => {
    afterEach(closeAllWindows);

    ifit(process.platform === 'darwin')('sheet-begin event emits when window opens a sheet', async () => {
      const w = new BrowserWindow();
      const sheetBegin = once(w, 'sheet-begin');
      // eslint-disable-next-line no-new
      new BrowserWindow({
        modal: true,
        parent: w
      });
      await sheetBegin;
    });

    ifit(process.platform === 'darwin')('sheet-end event emits when window has closed a sheet', async () => {
      const w = new BrowserWindow();
      const sheet = new BrowserWindow({
        modal: true,
        parent: w
      });
      const sheetEnd = once(w, 'sheet-end');
      sheet.close();
      await sheetEnd;
    });

    describe('parent option', () => {
      it('sets parent window', () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w });
        expect(c.getParentWindow()).to.equal(w);
      });

      it('adds window to child windows of parent', () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w });
        expect(w.getChildWindows()).to.deep.equal([c]);
      });

      it('removes from child windows of parent when window is closed', async () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w });
        const closed = once(c, 'closed');
        c.close();
        await closed;
        // The child window list is not immediately cleared, so wait a tick until it's ready.
        await setTimeout();
        expect(w.getChildWindows().length).to.equal(0);
      });

      it('can handle child window close and reparent multiple times', async () => {
        const w = new BrowserWindow({ show: false });
        let c: BrowserWindow | null;

        for (let i = 0; i < 5; i++) {
          c = new BrowserWindow({ show: false, parent: w });
          const closed = once(c, 'closed');
          c.close();
          await closed;
        }

        await setTimeout();
        expect(w.getChildWindows().length).to.equal(0);
      });

      ifit(process.platform === 'darwin')('only shows the intended window when a child with siblings is shown', async () => {
        const w = new BrowserWindow({ show: false });
        const childOne = new BrowserWindow({ show: false, parent: w });
        const childTwo = new BrowserWindow({ show: false, parent: w });

        const parentShown = once(w, 'show');
        w.show();
        await parentShown;

        expect(childOne.isVisible()).to.be.false('childOne is visible');
        expect(childTwo.isVisible()).to.be.false('childTwo is visible');

        const childOneShown = once(childOne, 'show');
        childOne.show();
        await childOneShown;

        expect(childOne.isVisible()).to.be.true('childOne is not visible');
        expect(childTwo.isVisible()).to.be.false('childTwo is visible');
      });

      ifit(process.platform === 'darwin')('child matches parent visibility when parent visibility changes', async () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w });

        const wShow = once(w, 'show');
        const cShow = once(c, 'show');

        w.show();
        c.show();

        await Promise.all([wShow, cShow]);

        const minimized = once(w, 'minimize');
        w.minimize();
        await minimized;

        expect(w.isVisible()).to.be.false('parent is visible');
        expect(c.isVisible()).to.be.false('child is visible');

        const restored = once(w, 'restore');
        w.restore();
        await restored;

        expect(w.isVisible()).to.be.true('parent is visible');
        expect(c.isVisible()).to.be.true('child is visible');
      });

      ifit(process.platform === 'darwin')('parent matches child visibility when child visibility changes', async () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w });

        const wShow = once(w, 'show');
        const cShow = once(c, 'show');

        w.show();
        c.show();

        await Promise.all([wShow, cShow]);

        const minimized = once(c, 'minimize');
        c.minimize();
        await minimized;

        expect(c.isVisible()).to.be.false('child is visible');

        const restored = once(c, 'restore');
        c.restore();
        await restored;

        expect(w.isVisible()).to.be.true('parent is visible');
        expect(c.isVisible()).to.be.true('child is visible');
      });

      it('closes a grandchild window when a middle child window is destroyed', async () => {
        const w = new BrowserWindow();

        w.loadFile(path.join(fixtures, 'pages', 'base-page.html'));
        w.webContents.executeJavaScript('window.open("")');

        w.webContents.on('did-create-window', async (window) => {
          const childWindow = new BrowserWindow({ parent: window });

          await setTimeout();

          const closed = once(childWindow, 'closed');
          window.close();
          await closed;

          expect(() => { BrowserWindow.getFocusedWindow(); }).to.not.throw();
        });
      });

      it('should not affect the show option', () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w });
        expect(c.isVisible()).to.be.false('child is visible');
        expect(c.getParentWindow()!.isVisible()).to.be.false('parent is visible');
      });
    });

    describe('win.setParentWindow(parent)', () => {
      it('sets parent window', () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false });
        expect(w.getParentWindow()).to.be.null('w.parent');
        expect(c.getParentWindow()).to.be.null('c.parent');
        c.setParentWindow(w);
        expect(c.getParentWindow()).to.equal(w);
        c.setParentWindow(null);
        expect(c.getParentWindow()).to.be.null('c.parent');
      });

      it('adds window to child windows of parent', () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false });
        expect(w.getChildWindows()).to.deep.equal([]);
        c.setParentWindow(w);
        expect(w.getChildWindows()).to.deep.equal([c]);
        c.setParentWindow(null);
        expect(w.getChildWindows()).to.deep.equal([]);
      });

      it('removes from child windows of parent when window is closed', async () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false });
        const closed = once(c, 'closed');
        c.setParentWindow(w);
        c.close();
        await closed;
        // The child window list is not immediately cleared, so wait a tick until it's ready.
        await setTimeout();
        expect(w.getChildWindows().length).to.equal(0);
      });

      ifit(process.platform === 'darwin')('can reparent when the first parent is destroyed', async () => {
        const w1 = new BrowserWindow({ show: false });
        const w2 = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false });

        c.setParentWindow(w1);
        expect(w1.getChildWindows().length).to.equal(1);

        const closed = once(w1, 'closed');
        w1.destroy();
        await closed;

        c.setParentWindow(w2);
        await setTimeout();

        const children = w2.getChildWindows();
        expect(children[0]).to.equal(c);
      });
    });

    describe('modal option', () => {
      it('does not freeze or crash', async () => {
        const parentWindow = new BrowserWindow();

        const createTwo = async () => {
          const two = new BrowserWindow({
            width: 300,
            height: 200,
            parent: parentWindow,
            modal: true,
            show: false
          });

          const twoShown = once(two, 'show');
          two.show();
          await twoShown;
          setTimeout(500).then(() => two.close());

          await once(two, 'closed');
        };

        const one = new BrowserWindow({
          width: 600,
          height: 400,
          parent: parentWindow,
          modal: true,
          show: false
        });

        const oneShown = once(one, 'show');
        one.show();
        await oneShown;
        setTimeout(500).then(() => one.destroy());

        await once(one, 'closed');
        await createTwo();
      });

      ifit(process.platform !== 'darwin')('can disable and enable a window', () => {
        const w = new BrowserWindow({ show: false });
        w.setEnabled(false);
        expect(w.isEnabled()).to.be.false('w.isEnabled()');
        w.setEnabled(true);
        expect(w.isEnabled()).to.be.true('!w.isEnabled()');
      });

      ifit(process.platform !== 'darwin')('disables parent window', () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w, modal: true });
        expect(w.isEnabled()).to.be.true('w.isEnabled');
        c.show();
        expect(w.isEnabled()).to.be.false('w.isEnabled');
      });

      ifit(process.platform !== 'darwin')('re-enables an enabled parent window when closed', async () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w, modal: true });
        const closed = once(c, 'closed');
        c.show();
        c.close();
        await closed;
        expect(w.isEnabled()).to.be.true('w.isEnabled');
      });

      ifit(process.platform !== 'darwin')('does not re-enable a disabled parent window when closed', async () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w, modal: true });
        const closed = once(c, 'closed');
        w.setEnabled(false);
        c.show();
        c.close();
        await closed;
        expect(w.isEnabled()).to.be.false('w.isEnabled');
      });

      ifit(process.platform !== 'darwin')('disables parent window recursively', () => {
        const w = new BrowserWindow({ show: false });
        const c = new BrowserWindow({ show: false, parent: w, modal: true });
        const c2 = new BrowserWindow({ show: false, parent: w, modal: true });
        c.show();
        expect(w.isEnabled()).to.be.false('w.isEnabled');
        c2.show();
        expect(w.isEnabled()).to.be.false('w.isEnabled');
        c.destroy();
        expect(w.isEnabled()).to.be.false('w.isEnabled');
        c2.destroy();
        expect(w.isEnabled()).to.be.true('w.isEnabled');
      });
    });
  });

  describe('window states', () => {
    afterEach(closeAllWindows);
    it('does not resize frameless windows when states change', () => {
      const w = new BrowserWindow({
        frame: false,
        width: 300,
        height: 200,
        show: false
      });

      w.minimizable = false;
      w.minimizable = true;
      expect(w.getSize()).to.deep.equal([300, 200]);

      w.resizable = false;
      w.resizable = true;
      expect(w.getSize()).to.deep.equal([300, 200]);

      w.maximizable = false;
      w.maximizable = true;
      expect(w.getSize()).to.deep.equal([300, 200]);

      w.fullScreenable = false;
      w.fullScreenable = true;
      expect(w.getSize()).to.deep.equal([300, 200]);

      w.closable = false;
      w.closable = true;
      expect(w.getSize()).to.deep.equal([300, 200]);
    });

    describe('resizable state', () => {
      it('with properties', () => {
        it('can be set with resizable constructor option', () => {
          const w = new BrowserWindow({ show: false, resizable: false });
          expect(w.resizable).to.be.false('resizable');

          if (process.platform === 'darwin') {
            expect(w.maximizable).to.to.true('maximizable');
          }
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.resizable).to.be.true('resizable');
          w.resizable = false;
          expect(w.resizable).to.be.false('resizable');
          w.resizable = true;
          expect(w.resizable).to.be.true('resizable');
        });
      });

      it('with functions', () => {
        it('can be set with resizable constructor option', () => {
          const w = new BrowserWindow({ show: false, resizable: false });
          expect(w.isResizable()).to.be.false('resizable');

          if (process.platform === 'darwin') {
            expect(w.isMaximizable()).to.to.true('maximizable');
          }
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isResizable()).to.be.true('resizable');
          w.setResizable(false);
          expect(w.isResizable()).to.be.false('resizable');
          w.setResizable(true);
          expect(w.isResizable()).to.be.true('resizable');
        });
      });

      it('works for a frameless window', () => {
        const w = new BrowserWindow({ show: false, frame: false });
        expect(w.resizable).to.be.true('resizable');

        if (process.platform === 'win32') {
          const w = new BrowserWindow({ show: false, thickFrame: false });
          expect(w.resizable).to.be.false('resizable');
        }
      });

      // On Linux there is no "resizable" property of a window.
      ifit(process.platform !== 'linux')('does affect maximizability when disabled and enabled', () => {
        const w = new BrowserWindow({ show: false });
        expect(w.resizable).to.be.true('resizable');

        expect(w.maximizable).to.be.true('maximizable');
        w.resizable = false;
        expect(w.maximizable).to.be.false('not maximizable');
        w.resizable = true;
        expect(w.maximizable).to.be.true('maximizable');
      });

      ifit(process.platform === 'win32')('works for a window smaller than 64x64', () => {
        const w = new BrowserWindow({
          show: false,
          frame: false,
          resizable: false,
          transparent: true
        });
        w.setContentSize(60, 60);
        expectBoundsEqual(w.getContentSize(), [60, 60]);
        w.setContentSize(30, 30);
        expectBoundsEqual(w.getContentSize(), [30, 30]);
        w.setContentSize(10, 10);
        expectBoundsEqual(w.getContentSize(), [10, 10]);
      });

      ifit(process.platform === 'win32')('do not change window with frame bounds when maximized', () => {
        const w = new BrowserWindow({
          show: true,
          frame: true,
          thickFrame: true
        });
        expect(w.isResizable()).to.be.true('resizable');
        w.maximize();
        expect(w.isMaximized()).to.be.true('maximized');
        const bounds = w.getBounds();
        w.setResizable(false);
        expectBoundsEqual(w.getBounds(), bounds);
        w.setResizable(true);
        expectBoundsEqual(w.getBounds(), bounds);
      });

      ifit(process.platform === 'win32')('do not change window without frame bounds when maximized', () => {
        const w = new BrowserWindow({
          show: true,
          frame: false,
          thickFrame: true
        });
        expect(w.isResizable()).to.be.true('resizable');
        w.maximize();
        expect(w.isMaximized()).to.be.true('maximized');
        const bounds = w.getBounds();
        w.setResizable(false);
        expectBoundsEqual(w.getBounds(), bounds);
        w.setResizable(true);
        expectBoundsEqual(w.getBounds(), bounds);
      });

      ifit(process.platform === 'win32')('do not change window transparent without frame bounds when maximized', () => {
        const w = new BrowserWindow({
          show: true,
          frame: false,
          thickFrame: true,
          transparent: true
        });
        expect(w.isResizable()).to.be.true('resizable');
        w.maximize();
        expect(w.isMaximized()).to.be.true('maximized');
        const bounds = w.getBounds();
        w.setResizable(false);
        expectBoundsEqual(w.getBounds(), bounds);
        w.setResizable(true);
        expectBoundsEqual(w.getBounds(), bounds);
      });
    });

    describe('loading main frame state', () => {
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

      it('is true when the main frame is loading', async () => {
        const w = new BrowserWindow({ show: false });

        const didStartLoading = once(w.webContents, 'did-start-loading');
        w.webContents.loadURL(serverUrl);
        await didStartLoading;

        expect(w.webContents.isLoadingMainFrame()).to.be.true('isLoadingMainFrame');
      });

      it('is false when only a subframe is loading', async () => {
        const w = new BrowserWindow({ show: false });

        const didStopLoading = once(w.webContents, 'did-stop-loading');
        w.webContents.loadURL(serverUrl);
        await didStopLoading;

        expect(w.webContents.isLoadingMainFrame()).to.be.false('isLoadingMainFrame');

        const didStartLoading = once(w.webContents, 'did-start-loading');
        w.webContents.executeJavaScript(`
          var iframe = document.createElement('iframe')
          iframe.src = '${serverUrl}/page2'
          document.body.appendChild(iframe)
        `);
        await didStartLoading;

        expect(w.webContents.isLoadingMainFrame()).to.be.false('isLoadingMainFrame');
      });

      it('is true when navigating to pages from the same origin', async () => {
        const w = new BrowserWindow({ show: false });

        const didStopLoading = once(w.webContents, 'did-stop-loading');
        w.webContents.loadURL(serverUrl);
        await didStopLoading;

        expect(w.webContents.isLoadingMainFrame()).to.be.false('isLoadingMainFrame');

        const didStartLoading = once(w.webContents, 'did-start-loading');
        w.webContents.loadURL(`${serverUrl}/page2`);
        await didStartLoading;

        expect(w.webContents.isLoadingMainFrame()).to.be.true('isLoadingMainFrame');
      });
    });
  });

  ifdescribe(process.platform !== 'linux')('window states (excluding Linux)', () => {
    // Not implemented on Linux.
    afterEach(closeAllWindows);

    describe('movable state', () => {
      it('with properties', () => {
        it('can be set with movable constructor option', () => {
          const w = new BrowserWindow({ show: false, movable: false });
          expect(w.movable).to.be.false('movable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.movable).to.be.true('movable');
          w.movable = false;
          expect(w.movable).to.be.false('movable');
          w.movable = true;
          expect(w.movable).to.be.true('movable');
        });
      });

      it('with functions', () => {
        it('can be set with movable constructor option', () => {
          const w = new BrowserWindow({ show: false, movable: false });
          expect(w.isMovable()).to.be.false('movable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isMovable()).to.be.true('movable');
          w.setMovable(false);
          expect(w.isMovable()).to.be.false('movable');
          w.setMovable(true);
          expect(w.isMovable()).to.be.true('movable');
        });
      });
    });

    describe('visibleOnAllWorkspaces state', () => {
      it('with properties', () => {
        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.visibleOnAllWorkspaces).to.be.false();
          w.visibleOnAllWorkspaces = true;
          expect(w.visibleOnAllWorkspaces).to.be.true();
        });
      });

      it('with functions', () => {
        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isVisibleOnAllWorkspaces()).to.be.false();
          w.setVisibleOnAllWorkspaces(true);
          expect(w.isVisibleOnAllWorkspaces()).to.be.true();
        });
      });
    });

    ifdescribe(process.platform === 'darwin')('documentEdited state', () => {
      it('with properties', () => {
        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.documentEdited).to.be.false();
          w.documentEdited = true;
          expect(w.documentEdited).to.be.true();
        });
      });

      it('with functions', () => {
        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isDocumentEdited()).to.be.false();
          w.setDocumentEdited(true);
          expect(w.isDocumentEdited()).to.be.true();
        });
      });
    });

    ifdescribe(process.platform === 'darwin')('representedFilename', () => {
      it('with properties', () => {
        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.representedFilename).to.eql('');
          w.representedFilename = 'a name';
          expect(w.representedFilename).to.eql('a name');
        });
      });

      it('with functions', () => {
        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.getRepresentedFilename()).to.eql('');
          w.setRepresentedFilename('a name');
          expect(w.getRepresentedFilename()).to.eql('a name');
        });
      });
    });

    describe('native window title', () => {
      it('with properties', () => {
        it('can be set with title constructor option', () => {
          const w = new BrowserWindow({ show: false, title: 'mYtItLe' });
          expect(w.title).to.eql('mYtItLe');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.title).to.eql('Electron Test Main');
          w.title = 'NEW TITLE';
          expect(w.title).to.eql('NEW TITLE');
        });
      });

      it('with functions', () => {
        it('can be set with minimizable constructor option', () => {
          const w = new BrowserWindow({ show: false, title: 'mYtItLe' });
          expect(w.getTitle()).to.eql('mYtItLe');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.getTitle()).to.eql('Electron Test Main');
          w.setTitle('NEW TITLE');
          expect(w.getTitle()).to.eql('NEW TITLE');
        });
      });
    });

    describe('minimizable state', () => {
      it('with properties', () => {
        it('can be set with minimizable constructor option', () => {
          const w = new BrowserWindow({ show: false, minimizable: false });
          expect(w.minimizable).to.be.false('minimizable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.minimizable).to.be.true('minimizable');
          w.minimizable = false;
          expect(w.minimizable).to.be.false('minimizable');
          w.minimizable = true;
          expect(w.minimizable).to.be.true('minimizable');
        });
      });

      it('with functions', () => {
        it('can be set with minimizable constructor option', () => {
          const w = new BrowserWindow({ show: false, minimizable: false });
          expect(w.isMinimizable()).to.be.false('movable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isMinimizable()).to.be.true('isMinimizable');
          w.setMinimizable(false);
          expect(w.isMinimizable()).to.be.false('isMinimizable');
          w.setMinimizable(true);
          expect(w.isMinimizable()).to.be.true('isMinimizable');
        });
      });
    });

    describe('maximizable state (property)', () => {
      it('with properties', () => {
        it('can be set with maximizable constructor option', () => {
          const w = new BrowserWindow({ show: false, maximizable: false });
          expect(w.maximizable).to.be.false('maximizable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.maximizable).to.be.true('maximizable');
          w.maximizable = false;
          expect(w.maximizable).to.be.false('maximizable');
          w.maximizable = true;
          expect(w.maximizable).to.be.true('maximizable');
        });

        it('is not affected when changing other states', () => {
          const w = new BrowserWindow({ show: false });
          w.maximizable = false;
          expect(w.maximizable).to.be.false('maximizable');
          w.minimizable = false;
          expect(w.maximizable).to.be.false('maximizable');
          w.closable = false;
          expect(w.maximizable).to.be.false('maximizable');

          w.maximizable = true;
          expect(w.maximizable).to.be.true('maximizable');
          w.closable = true;
          expect(w.maximizable).to.be.true('maximizable');
          w.fullScreenable = false;
          expect(w.maximizable).to.be.true('maximizable');
        });
      });

      it('with functions', () => {
        it('can be set with maximizable constructor option', () => {
          const w = new BrowserWindow({ show: false, maximizable: false });
          expect(w.isMaximizable()).to.be.false('isMaximizable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isMaximizable()).to.be.true('isMaximizable');
          w.setMaximizable(false);
          expect(w.isMaximizable()).to.be.false('isMaximizable');
          w.setMaximizable(true);
          expect(w.isMaximizable()).to.be.true('isMaximizable');
        });

        it('is not affected when changing other states', () => {
          const w = new BrowserWindow({ show: false });
          w.setMaximizable(false);
          expect(w.isMaximizable()).to.be.false('isMaximizable');
          w.setMinimizable(false);
          expect(w.isMaximizable()).to.be.false('isMaximizable');
          w.setClosable(false);
          expect(w.isMaximizable()).to.be.false('isMaximizable');

          w.setMaximizable(true);
          expect(w.isMaximizable()).to.be.true('isMaximizable');
          w.setClosable(true);
          expect(w.isMaximizable()).to.be.true('isMaximizable');
          w.setFullScreenable(false);
          expect(w.isMaximizable()).to.be.true('isMaximizable');
        });
      });
    });

    ifdescribe(process.platform === 'win32')('maximizable state', () => {
      it('with properties', () => {
        it('is reset to its former state', () => {
          const w = new BrowserWindow({ show: false });
          w.maximizable = false;
          w.resizable = false;
          w.resizable = true;
          expect(w.maximizable).to.be.false('maximizable');
          w.maximizable = true;
          w.resizable = false;
          w.resizable = true;
          expect(w.maximizable).to.be.true('maximizable');
        });
      });

      it('with functions', () => {
        it('is reset to its former state', () => {
          const w = new BrowserWindow({ show: false });
          w.setMaximizable(false);
          w.setResizable(false);
          w.setResizable(true);
          expect(w.isMaximizable()).to.be.false('isMaximizable');
          w.setMaximizable(true);
          w.setResizable(false);
          w.setResizable(true);
          expect(w.isMaximizable()).to.be.true('isMaximizable');
        });
      });
    });

    ifdescribe(process.platform !== 'darwin')('menuBarVisible state', () => {
      describe('with properties', () => {
        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.menuBarVisible).to.be.true();
          w.menuBarVisible = false;
          expect(w.menuBarVisible).to.be.false();
          w.menuBarVisible = true;
          expect(w.menuBarVisible).to.be.true();
        });
      });

      describe('with functions', () => {
        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isMenuBarVisible()).to.be.true('isMenuBarVisible');
          w.setMenuBarVisibility(false);
          expect(w.isMenuBarVisible()).to.be.false('isMenuBarVisible');
          w.setMenuBarVisibility(true);
          expect(w.isMenuBarVisible()).to.be.true('isMenuBarVisible');
        });
      });
    });

    ifdescribe(process.platform !== 'darwin')('when fullscreen state is changed', () => {
      it('correctly remembers state prior to fullscreen change', async () => {
        const w = new BrowserWindow({ show: false });
        expect(w.isMenuBarVisible()).to.be.true('isMenuBarVisible');
        w.setMenuBarVisibility(false);
        expect(w.isMenuBarVisible()).to.be.false('isMenuBarVisible');

        const enterFS = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFS;
        expect(w.fullScreen).to.be.true('not fullscreen');

        const exitFS = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await exitFS;
        expect(w.fullScreen).to.be.false('not fullscreen');

        expect(w.isMenuBarVisible()).to.be.false('isMenuBarVisible');
      });

      it('correctly remembers state prior to fullscreen change with autoHide', async () => {
        const w = new BrowserWindow({ show: false });
        expect(w.autoHideMenuBar).to.be.false('autoHideMenuBar');
        w.autoHideMenuBar = true;
        expect(w.autoHideMenuBar).to.be.true('autoHideMenuBar');
        w.setMenuBarVisibility(false);
        expect(w.isMenuBarVisible()).to.be.false('isMenuBarVisible');

        const enterFS = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFS;
        expect(w.fullScreen).to.be.true('not fullscreen');

        const exitFS = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await exitFS;
        expect(w.fullScreen).to.be.false('not fullscreen');

        expect(w.isMenuBarVisible()).to.be.false('isMenuBarVisible');
      });
    });

    ifdescribe(process.platform === 'darwin')('fullscreenable state', () => {
      it('with functions', () => {
        it('can be set with fullscreenable constructor option', () => {
          const w = new BrowserWindow({ show: false, fullscreenable: false });
          expect(w.isFullScreenable()).to.be.false('isFullScreenable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isFullScreenable()).to.be.true('isFullScreenable');
          w.setFullScreenable(false);
          expect(w.isFullScreenable()).to.be.false('isFullScreenable');
          w.setFullScreenable(true);
          expect(w.isFullScreenable()).to.be.true('isFullScreenable');
        });
      });

      it('does not open non-fullscreenable child windows in fullscreen if parent is fullscreen', async () => {
        const w = new BrowserWindow();

        const enterFS = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFS;

        const child = new BrowserWindow({ parent: w, resizable: false, fullscreenable: false });
        const shown = once(child, 'show');
        await shown;

        expect(child.resizable).to.be.false('resizable');
        expect(child.fullScreen).to.be.false('fullscreen');
        expect(child.fullScreenable).to.be.false('fullscreenable');
      });

      it('is set correctly with different resizable values', async () => {
        const w1 = new BrowserWindow({
          resizable: false,
          fullscreenable: false
        });

        const w2 = new BrowserWindow({
          resizable: true,
          fullscreenable: false
        });

        const w3 = new BrowserWindow({
          fullscreenable: false
        });

        expect(w1.isFullScreenable()).to.be.false('isFullScreenable');
        expect(w2.isFullScreenable()).to.be.false('isFullScreenable');
        expect(w3.isFullScreenable()).to.be.false('isFullScreenable');
      });

      it('does not disable maximize button if window is resizable', () => {
        const w = new BrowserWindow({
          resizable: true,
          fullscreenable: false
        });

        expect(w.isMaximizable()).to.be.true('isMaximizable');

        w.setResizable(false);

        expect(w.isMaximizable()).to.be.false('isMaximizable');
      });
    });

    ifdescribe(process.platform === 'darwin')('isHiddenInMissionControl state', () => {
      it('with functions', () => {
        it('can be set with ignoreMissionControl constructor option', () => {
          const w = new BrowserWindow({ show: false, hiddenInMissionControl: true });
          expect(w.isHiddenInMissionControl()).to.be.true('isHiddenInMissionControl');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isHiddenInMissionControl()).to.be.false('isHiddenInMissionControl');
          w.setHiddenInMissionControl(true);
          expect(w.isHiddenInMissionControl()).to.be.true('isHiddenInMissionControl');
          w.setHiddenInMissionControl(false);
          expect(w.isHiddenInMissionControl()).to.be.false('isHiddenInMissionControl');
        });
      });
    });

    // fullscreen events are dispatched eagerly and twiddling things too fast can confuse poor Electron

    ifdescribe(process.platform === 'darwin')('kiosk state', () => {
      it('with properties', () => {
        it('can be set with a constructor property', () => {
          const w = new BrowserWindow({ kiosk: true });
          expect(w.kiosk).to.be.true();
        });

        it('can be changed ', async () => {
          const w = new BrowserWindow();
          const enterFullScreen = once(w, 'enter-full-screen');
          w.kiosk = true;
          expect(w.isKiosk()).to.be.true('isKiosk');
          await enterFullScreen;

          await setTimeout();
          const leaveFullScreen = once(w, 'leave-full-screen');
          w.kiosk = false;
          expect(w.isKiosk()).to.be.false('isKiosk');
          await leaveFullScreen;
        });
      });

      it('with functions', () => {
        it('can be set with a constructor property', () => {
          const w = new BrowserWindow({ kiosk: true });
          expect(w.isKiosk()).to.be.true();
        });

        it('can be changed ', async () => {
          const w = new BrowserWindow();
          const enterFullScreen = once(w, 'enter-full-screen');
          w.setKiosk(true);
          expect(w.isKiosk()).to.be.true('isKiosk');
          await enterFullScreen;

          await setTimeout();
          const leaveFullScreen = once(w, 'leave-full-screen');
          w.setKiosk(false);
          expect(w.isKiosk()).to.be.false('isKiosk');
          await leaveFullScreen;
        });
      });
    });

    ifdescribe(process.platform === 'darwin')('fullscreen state with resizable set', () => {
      it('resizable flag should be set to false and restored', async () => {
        const w = new BrowserWindow({ resizable: false });

        const enterFullScreen = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFullScreen;
        expect(w.resizable).to.be.false('resizable');

        await setTimeout();
        const leaveFullScreen = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await leaveFullScreen;
        expect(w.resizable).to.be.false('resizable');
      });

      it('default resizable flag should be restored after entering/exiting fullscreen', async () => {
        const w = new BrowserWindow();

        const enterFullScreen = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFullScreen;
        expect(w.resizable).to.be.false('resizable');

        await setTimeout();
        const leaveFullScreen = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await leaveFullScreen;
        expect(w.resizable).to.be.true('resizable');
      });
    });

    ifdescribe(process.platform === 'darwin')('fullscreen state', () => {
      it('should not cause a crash if called when exiting fullscreen', async () => {
        const w = new BrowserWindow();

        const enterFullScreen = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFullScreen;

        await setTimeout();

        const leaveFullScreen = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await leaveFullScreen;
      });

      it('should be able to load a URL while transitioning to fullscreen', async () => {
        const w = new BrowserWindow({ fullscreen: true });
        w.loadFile(path.join(fixtures, 'pages', 'c.html'));

        const load = once(w.webContents, 'did-finish-load');
        const enterFS = once(w, 'enter-full-screen');

        await Promise.all([enterFS, load]);
        expect(w.fullScreen).to.be.true();

        await setTimeout();

        const leaveFullScreen = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await leaveFullScreen;
      });

      it('can be changed with setFullScreen method', async () => {
        const w = new BrowserWindow();
        const enterFullScreen = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFullScreen;
        expect(w.isFullScreen()).to.be.true('isFullScreen');

        await setTimeout();
        const leaveFullScreen = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await leaveFullScreen;
        expect(w.isFullScreen()).to.be.false('isFullScreen');
      });

      it('handles several transitions starting with fullscreen', async () => {
        const w = new BrowserWindow({ fullscreen: true, show: true });

        expect(w.isFullScreen()).to.be.true('not fullscreen');

        w.setFullScreen(false);
        w.setFullScreen(true);

        const enterFullScreen = emittedNTimes(w, 'enter-full-screen', 2);
        await enterFullScreen;

        expect(w.isFullScreen()).to.be.true('not fullscreen');

        await setTimeout();
        const leaveFullScreen = once(w, 'leave-full-screen');
        w.setFullScreen(false);
        await leaveFullScreen;

        expect(w.isFullScreen()).to.be.false('is fullscreen');
      });

      it('handles several HTML fullscreen transitions', async () => {
        const w = new BrowserWindow();
        await w.loadFile(path.join(fixtures, 'pages', 'a.html'));

        expect(w.isFullScreen()).to.be.false('is fullscreen');

        const enterFullScreen = once(w, 'enter-full-screen');
        const leaveFullScreen = once(w, 'leave-full-screen');

        await w.webContents.executeJavaScript('document.getElementById("div").requestFullscreen()', true);
        await enterFullScreen;
        await w.webContents.executeJavaScript('document.exitFullscreen()', true);
        await leaveFullScreen;

        expect(w.isFullScreen()).to.be.false('is fullscreen');

        await setTimeout();

        await w.webContents.executeJavaScript('document.getElementById("div").requestFullscreen()', true);
        await enterFullScreen;
        await w.webContents.executeJavaScript('document.exitFullscreen()', true);
        await leaveFullScreen;

        expect(w.isFullScreen()).to.be.false('is fullscreen');
      });

      it('handles several transitions in close proximity', async () => {
        const w = new BrowserWindow();

        expect(w.isFullScreen()).to.be.false('is fullscreen');

        const enterFS = emittedNTimes(w, 'enter-full-screen', 2);
        const leaveFS = emittedNTimes(w, 'leave-full-screen', 2);

        w.setFullScreen(true);
        w.setFullScreen(false);
        w.setFullScreen(true);
        w.setFullScreen(false);

        await Promise.all([enterFS, leaveFS]);

        expect(w.isFullScreen()).to.be.false('not fullscreen');
      });

      it('handles several chromium-initiated transitions in close proximity', async () => {
        const w = new BrowserWindow();
        await w.loadFile(path.join(fixtures, 'pages', 'a.html'));

        expect(w.isFullScreen()).to.be.false('is fullscreen');

        let enterCount = 0;
        let exitCount = 0;

        const done = new Promise<void>(resolve => {
          const checkDone = () => {
            if (enterCount === 2 && exitCount === 2) resolve();
          };

          w.webContents.on('enter-html-full-screen', () => {
            enterCount++;
            checkDone();
          });

          w.webContents.on('leave-html-full-screen', () => {
            exitCount++;
            checkDone();
          });
        });

        await w.webContents.executeJavaScript('document.getElementById("div").requestFullscreen()', true);
        await w.webContents.executeJavaScript('document.exitFullscreen()');
        await w.webContents.executeJavaScript('document.getElementById("div").requestFullscreen()', true);
        await w.webContents.executeJavaScript('document.exitFullscreen()');
        await done;
      });

      it('handles HTML fullscreen transitions when fullscreenable is false', async () => {
        const w = new BrowserWindow({ fullscreenable: false });
        await w.loadFile(path.join(fixtures, 'pages', 'a.html'));

        expect(w.isFullScreen()).to.be.false('is fullscreen');

        let enterCount = 0;
        let exitCount = 0;

        const done = new Promise<void>((resolve, reject) => {
          const checkDone = () => {
            if (enterCount === 2 && exitCount === 2) resolve();
          };

          w.webContents.on('enter-html-full-screen', async () => {
            enterCount++;
            if (w.isFullScreen()) reject(new Error('w.isFullScreen should be false'));
            const isFS = await w.webContents.executeJavaScript('!!document.fullscreenElement');
            if (!isFS) reject(new Error('Document should have fullscreen element'));
            checkDone();
          });

          w.webContents.on('leave-html-full-screen', () => {
            exitCount++;
            if (w.isFullScreen()) reject(new Error('w.isFullScreen should be false'));
            checkDone();
          });
        });

        await w.webContents.executeJavaScript('document.getElementById("div").requestFullscreen()', true);
        await w.webContents.executeJavaScript('document.exitFullscreen()');
        await w.webContents.executeJavaScript('document.getElementById("div").requestFullscreen()', true);
        await w.webContents.executeJavaScript('document.exitFullscreen()');
        await expect(done).to.eventually.be.fulfilled();
      });

      it('does not crash when exiting simpleFullScreen (properties)', async () => {
        const w = new BrowserWindow();
        w.setSimpleFullScreen(true);

        await setTimeout(1000);

        w.setFullScreen(!w.isFullScreen());
      });

      it('does not crash when exiting simpleFullScreen (functions)', async () => {
        const w = new BrowserWindow();
        w.simpleFullScreen = true;

        await setTimeout(1000);

        w.setFullScreen(!w.isFullScreen());
      });

      it('should not be changed by setKiosk method', async () => {
        const w = new BrowserWindow();

        const enterFullScreen = once(w, 'enter-full-screen');
        w.setKiosk(true);
        await enterFullScreen;
        expect(w.isFullScreen()).to.be.true('isFullScreen');

        const leaveFullScreen = once(w, 'leave-full-screen');
        w.setKiosk(false);
        await leaveFullScreen;
        expect(w.isFullScreen()).to.be.false('isFullScreen');
      });

      it('should stay fullscreen if fullscreen before kiosk', async () => {
        const w = new BrowserWindow();

        const enterFullScreen = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFullScreen;
        expect(w.isFullScreen()).to.be.true('isFullScreen');

        w.setKiosk(true);

        w.setKiosk(false);
        // Wait enough time for a fullscreen change to take effect.
        await setTimeout(2000);
        expect(w.isFullScreen()).to.be.true('isFullScreen');
      });

      it('multiple windows inherit correct fullscreen state', async () => {
        const w = new BrowserWindow();
        const enterFullScreen = once(w, 'enter-full-screen');
        w.setFullScreen(true);
        await enterFullScreen;
        expect(w.isFullScreen()).to.be.true('isFullScreen');
        await setTimeout(1000);
        const w2 = new BrowserWindow({ show: false });
        const enterFullScreen2 = once(w2, 'enter-full-screen');
        w2.show();
        await enterFullScreen2;
        expect(w2.isFullScreen()).to.be.true('isFullScreen');
      });
    });

    describe('closable state', () => {
      it('with properties', () => {
        it('can be set with closable constructor option', () => {
          const w = new BrowserWindow({ show: false, closable: false });
          expect(w.closable).to.be.false('closable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.closable).to.be.true('closable');
          w.closable = false;
          expect(w.closable).to.be.false('closable');
          w.closable = true;
          expect(w.closable).to.be.true('closable');
        });
      });

      it('with functions', () => {
        it('can be set with closable constructor option', () => {
          const w = new BrowserWindow({ show: false, closable: false });
          expect(w.isClosable()).to.be.false('isClosable');
        });

        it('can be changed', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.isClosable()).to.be.true('isClosable');
          w.setClosable(false);
          expect(w.isClosable()).to.be.false('isClosable');
          w.setClosable(true);
          expect(w.isClosable()).to.be.true('isClosable');
        });
      });
    });

    describe('hasShadow state', () => {
      it('with properties', () => {
        it('returns a boolean on all platforms', () => {
          const w = new BrowserWindow({ show: false });
          expect(w.shadow).to.be.a('boolean');
        });

        // On Windows there's no shadow by default & it can't be changed dynamically.
        it('can be changed with hasShadow option', () => {
          const hasShadow = process.platform !== 'darwin';
          const w = new BrowserWindow({ show: false, hasShadow });
          expect(w.shadow).to.equal(hasShadow);
        });

        it('can be changed with setHasShadow method', () => {
          const w = new BrowserWindow({ show: false });
          w.shadow = false;
          expect(w.shadow).to.be.false('hasShadow');
          w.shadow = true;
          expect(w.shadow).to.be.true('hasShadow');
          w.shadow = false;
          expect(w.shadow).to.be.false('hasShadow');
        });
      });

      describe('with functions', () => {
        it('returns a boolean on all platforms', () => {
          const w = new BrowserWindow({ show: false });
          const hasShadow = w.hasShadow();
          expect(hasShadow).to.be.a('boolean');
        });

        // On Windows there's no shadow by default & it can't be changed dynamically.
        it('can be changed with hasShadow option', () => {
          const hasShadow = process.platform !== 'darwin';
          const w = new BrowserWindow({ show: false, hasShadow });
          expect(w.hasShadow()).to.equal(hasShadow);
        });

        it('can be changed with setHasShadow method', () => {
          const w = new BrowserWindow({ show: false });
          w.setHasShadow(false);
          expect(w.hasShadow()).to.be.false('hasShadow');
          w.setHasShadow(true);
          expect(w.hasShadow()).to.be.true('hasShadow');
          w.setHasShadow(false);
          expect(w.hasShadow()).to.be.false('hasShadow');
        });
      });
    });
  });

  describe('window.getMediaSourceId()', () => {
    afterEach(closeAllWindows);
    it('returns valid source id', async () => {
      const w = new BrowserWindow({ show: false });
      const shown = once(w, 'show');
      w.show();
      await shown;

      // Check format 'window:1234:0'.
      const sourceId = w.getMediaSourceId();
      expect(sourceId).to.match(/^window:\d+:\d+$/);
    });
  });

  ifdescribe(!process.env.ELECTRON_SKIP_NATIVE_MODULE_TESTS)('window.getNativeWindowHandle()', () => {
    afterEach(closeAllWindows);
    it('returns valid handle', () => {
      const w = new BrowserWindow({ show: false });
      const isValidWindow = require('@electron-ci/is-valid-window');
      expect(isValidWindow(w.getNativeWindowHandle())).to.be.true('is valid window');
    });
  });

  ifdescribe(process.platform === 'darwin')('previewFile', () => {
    afterEach(closeAllWindows);
    it('opens the path in Quick Look on macOS', () => {
      const w = new BrowserWindow({ show: false });
      expect(() => {
        w.previewFile(__filename);
        w.closeFilePreview();
      }).to.not.throw();
    });

    it('should not call BrowserWindow show event', async () => {
      const w = new BrowserWindow({ show: false });
      const shown = once(w, 'show');
      w.show();
      await shown;

      let showCalled = false;
      w.on('show', () => {
        showCalled = true;
      });

      w.previewFile(__filename);
      await setTimeout(500);
      expect(showCalled).to.equal(false, 'should not have called show twice');
    });
  });

  // TODO (jkleinsc) renable these tests on mas arm64
  ifdescribe(!process.mas || process.arch !== 'arm64')('contextIsolation option with and without sandbox option', () => {
    const expectedContextData = {
      preloadContext: {
        preloadProperty: 'number',
        pageProperty: 'undefined',
        typeofRequire: 'function',
        typeofProcess: 'object',
        typeofArrayPush: 'function',
        typeofFunctionApply: 'function',
        typeofPreloadExecuteJavaScriptProperty: 'undefined'
      },
      pageContext: {
        preloadProperty: 'undefined',
        pageProperty: 'string',
        typeofRequire: 'undefined',
        typeofProcess: 'undefined',
        typeofArrayPush: 'number',
        typeofFunctionApply: 'boolean',
        typeofPreloadExecuteJavaScriptProperty: 'number',
        typeofOpenedWindow: 'object'
      }
    };

    afterEach(closeAllWindows);

    it('separates the page context from the Electron/preload context', async () => {
      const iw = new BrowserWindow({
        show: false,
        webPreferences: {
          contextIsolation: true,
          preload: path.join(fixtures, 'api', 'isolated-preload.js')
        }
      });
      const p = once(ipcMain, 'isolated-world');
      iw.loadFile(path.join(fixtures, 'api', 'isolated.html'));
      const [, data] = await p;
      expect(data).to.deep.equal(expectedContextData);
    });
    it('recreates the contexts on reload', async () => {
      const iw = new BrowserWindow({
        show: false,
        webPreferences: {
          contextIsolation: true,
          preload: path.join(fixtures, 'api', 'isolated-preload.js')
        }
      });
      await iw.loadFile(path.join(fixtures, 'api', 'isolated.html'));
      const isolatedWorld = once(ipcMain, 'isolated-world');
      iw.webContents.reload();
      const [, data] = await isolatedWorld;
      expect(data).to.deep.equal(expectedContextData);
    });
    it('enables context isolation on child windows', async () => {
      const iw = new BrowserWindow({
        show: false,
        webPreferences: {
          contextIsolation: true,
          preload: path.join(fixtures, 'api', 'isolated-preload.js')
        }
      });
      const browserWindowCreated = once(app, 'browser-window-created') as Promise<[any, BrowserWindow]>;
      iw.loadFile(path.join(fixtures, 'pages', 'window-open.html'));
      const [, window] = await browserWindowCreated;
      expect(window.webContents.getLastWebPreferences()!.contextIsolation).to.be.true('contextIsolation');
    });
    it('separates the page context from the Electron/preload context with sandbox on', async () => {
      const ws = new BrowserWindow({
        show: false,
        webPreferences: {
          sandbox: true,
          contextIsolation: true,
          preload: path.join(fixtures, 'api', 'isolated-preload.js')
        }
      });
      const p = once(ipcMain, 'isolated-world');
      ws.loadFile(path.join(fixtures, 'api', 'isolated.html'));
      const [, data] = await p;
      expect(data).to.deep.equal(expectedContextData);
    });
    it('recreates the contexts on reload with sandbox on', async () => {
      const ws = new BrowserWindow({
        show: false,
        webPreferences: {
          sandbox: true,
          contextIsolation: true,
          preload: path.join(fixtures, 'api', 'isolated-preload.js')
        }
      });
      await ws.loadFile(path.join(fixtures, 'api', 'isolated.html'));
      const isolatedWorld = once(ipcMain, 'isolated-world');
      ws.webContents.reload();
      const [, data] = await isolatedWorld;
      expect(data).to.deep.equal(expectedContextData);
    });
    it('supports fetch api', async () => {
      const fetchWindow = new BrowserWindow({
        show: false,
        webPreferences: {
          contextIsolation: true,
          preload: path.join(fixtures, 'api', 'isolated-fetch-preload.js')
        }
      });
      const p = once(ipcMain, 'isolated-fetch-error');
      fetchWindow.loadURL('about:blank');
      const [, error] = await p;
      expect(error).to.equal('Failed to fetch');
    });
    it('doesn\'t break ipc serialization', async () => {
      const iw = new BrowserWindow({
        show: false,
        webPreferences: {
          contextIsolation: true,
          preload: path.join(fixtures, 'api', 'isolated-preload.js')
        }
      });
      const p = once(ipcMain, 'isolated-world');
      iw.loadURL('about:blank');
      iw.webContents.executeJavaScript(`
        const opened = window.open()
        openedLocation = opened.location.href
        opened.close()
        window.postMessage({openedLocation}, '*')
      `);
      const [, data] = await p;
      expect(data.pageContext.openedLocation).to.equal('about:blank');
    });
    it('reports process.contextIsolated', async () => {
      const iw = new BrowserWindow({
        show: false,
        webPreferences: {
          contextIsolation: true,
          preload: path.join(fixtures, 'api', 'isolated-process.js')
        }
      });
      const p = once(ipcMain, 'context-isolation');
      iw.loadURL('about:blank');
      const [, contextIsolation] = await p;
      expect(contextIsolation).to.be.true('contextIsolation');
    });
  });

  it('reloading does not cause Node.js module API hangs after reload', (done) => {
    const w = new BrowserWindow({
      show: false,
      webPreferences: {
        nodeIntegration: true,
        contextIsolation: false
      }
    });

    let count = 0;
    ipcMain.on('async-node-api-done', () => {
      if (count === 3) {
        ipcMain.removeAllListeners('async-node-api-done');
        done();
      } else {
        count++;
        w.reload();
      }
    });

    w.loadFile(path.join(fixtures, 'pages', 'send-after-node.html'));
  });

  describe('window.webContents.focus()', () => {
    afterEach(closeAllWindows);
    it('focuses window', async () => {
      const w1 = new BrowserWindow({ x: 100, y: 300, width: 300, height: 200 });
      w1.loadURL('about:blank');
      const w2 = new BrowserWindow({ x: 300, y: 300, width: 300, height: 200 });
      w2.loadURL('about:blank');
      const w1Focused = once(w1, 'focus');
      w1.webContents.focus();
      await w1Focused;
      expect(w1.webContents.isFocused()).to.be.true('focuses window');
    });
  });

  describe('offscreen rendering', () => {
    let w: BrowserWindow;
    beforeEach(function () {
      w = new BrowserWindow({
        width: 100,
        height: 100,
        show: false,
        webPreferences: {
          backgroundThrottling: false,
          offscreen: true
        }
      });
    });
    afterEach(closeAllWindows);

    it('creates offscreen window with correct size', async () => {
      const paint = once(w.webContents, 'paint') as Promise<[any, Electron.Rectangle, Electron.NativeImage]>;
      w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
      const [,, data] = await paint;
      expect(data.constructor.name).to.equal('NativeImage');
      expect(data.isEmpty()).to.be.false('data is empty');
      const size = data.getSize();
      const { scaleFactor } = screen.getPrimaryDisplay();
      expect(size.width).to.be.closeTo(100 * scaleFactor, 2);
      expect(size.height).to.be.closeTo(100 * scaleFactor, 2);
    });

    it('does not crash after navigation', () => {
      w.webContents.loadURL('about:blank');
      w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
    });

    describe('window.webContents.isOffscreen()', () => {
      it('is true for offscreen type', () => {
        w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
        expect(w.webContents.isOffscreen()).to.be.true('isOffscreen');
      });

      it('is false for regular window', () => {
        const c = new BrowserWindow({ show: false });
        expect(c.webContents.isOffscreen()).to.be.false('isOffscreen');
        c.destroy();
      });
    });

    describe('window.webContents.isPainting()', () => {
      it('returns whether is currently painting', async () => {
        const paint = once(w.webContents, 'paint') as Promise<[any, Electron.Rectangle, Electron.NativeImage]>;
        w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
        await paint;
        expect(w.webContents.isPainting()).to.be.true('isPainting');
      });
    });

    describe('window.webContents.stopPainting()', () => {
      it('stops painting', async () => {
        const domReady = once(w.webContents, 'dom-ready');
        w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
        await domReady;

        w.webContents.stopPainting();
        expect(w.webContents.isPainting()).to.be.false('isPainting');
      });
    });

    describe('window.webContents.startPainting()', () => {
      it('starts painting', async () => {
        const domReady = once(w.webContents, 'dom-ready');
        w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
        await domReady;

        w.webContents.stopPainting();
        w.webContents.startPainting();

        await once(w.webContents, 'paint') as [any, Electron.Rectangle, Electron.NativeImage];
        expect(w.webContents.isPainting()).to.be.true('isPainting');
      });
    });

    describe('frameRate APIs', () => {
      it('has default frame rate (function)', async () => {
        w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
        await once(w.webContents, 'paint') as [any, Electron.Rectangle, Electron.NativeImage];
        expect(w.webContents.getFrameRate()).to.equal(60);
      });

      it('has default frame rate (property)', async () => {
        w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
        await once(w.webContents, 'paint') as [any, Electron.Rectangle, Electron.NativeImage];
        expect(w.webContents.frameRate).to.equal(60);
      });

      it('sets custom frame rate (function)', async () => {
        const domReady = once(w.webContents, 'dom-ready');
        w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
        await domReady;

        w.webContents.setFrameRate(30);

        await once(w.webContents, 'paint') as [any, Electron.Rectangle, Electron.NativeImage];
        expect(w.webContents.getFrameRate()).to.equal(30);
      });

      it('sets custom frame rate (property)', async () => {
        const domReady = once(w.webContents, 'dom-ready');
        w.loadFile(path.join(fixtures, 'api', 'offscreen-rendering.html'));
        await domReady;

        w.webContents.frameRate = 30;

        await once(w.webContents, 'paint') as [any, Electron.Rectangle, Electron.NativeImage];
        expect(w.webContents.frameRate).to.equal(30);
      });
    });
  });

  describe('"transparent" option', () => {
    afterEach(closeAllWindows);

    ifit(process.platform !== 'linux')('correctly returns isMaximized() when the window is maximized then minimized', async () => {
      const w = new BrowserWindow({
        frame: false,
        transparent: true
      });

      const maximize = once(w, 'maximize');
      w.maximize();
      await maximize;

      const minimize = once(w, 'minimize');
      w.minimize();
      await minimize;

      expect(w.isMaximized()).to.be.false();
      expect(w.isMinimized()).to.be.true();
    });

    // Only applicable on Windows where transparent windows can't be maximized.
    ifit(process.platform === 'win32')('can show maximized frameless window', async () => {
      const display = screen.getPrimaryDisplay();

      const w = new BrowserWindow({
        ...display.bounds,
        frame: false,
        transparent: true,
        show: true
      });

      w.loadURL('about:blank');
      await once(w, 'ready-to-show');

      expect(w.isMaximized()).to.be.true();

      // Fails when the transparent HWND is in an invalid maximized state.
      expect(w.getBounds()).to.deep.equal(display.workArea);

      const newBounds = { width: 256, height: 256, x: 0, y: 0 };
      w.setBounds(newBounds);
      expect(w.getBounds()).to.deep.equal(newBounds);
    });

    // FIXME(codebytere): figure out why these are failing on macOS arm64.
    ifit(process.platform === 'darwin' && process.arch !== 'arm64')('should not display a visible background', async () => {
      const display = screen.getPrimaryDisplay();

      const backgroundWindow = new BrowserWindow({
        ...display.bounds,
        frame: false,
        backgroundColor: HexColors.GREEN,
        hasShadow: false
      });

      await backgroundWindow.loadURL('about:blank');

      const foregroundWindow = new BrowserWindow({
        ...display.bounds,
        show: true,
        transparent: true,
        frame: false,
        hasShadow: false
      });

      const colorFile = path.join(__dirname, 'fixtures', 'pages', 'half-background-color.html');
      await foregroundWindow.loadFile(colorFile);

      await setTimeout(1000);

      const screenCapture = await ScreenCapture.createForDisplay(display);
      await screenCapture.expectColorAtPointOnDisplayMatches(
        HexColors.GREEN,
        (size) => ({
          x: size.width / 4,
          y: size.height / 2
        })
      );
      await screenCapture.expectColorAtPointOnDisplayMatches(
        HexColors.RED,
        (size) => ({
          x: size.width * 3 / 4,
          y: size.height / 2
        })
      );
    });

    // FIXME(codebytere): figure out why these are failing on macOS arm64.
    ifit(process.platform === 'darwin' && process.arch !== 'arm64')('Allows setting a transparent window via CSS', async () => {
      const display = screen.getPrimaryDisplay();

      const backgroundWindow = new BrowserWindow({
        ...display.bounds,
        frame: false,
        backgroundColor: HexColors.PURPLE,
        hasShadow: false
      });

      await backgroundWindow.loadURL('about:blank');

      const foregroundWindow = new BrowserWindow({
        ...display.bounds,
        frame: false,
        transparent: true,
        hasShadow: false,
        webPreferences: {
          contextIsolation: false,
          nodeIntegration: true
        }
      });

      foregroundWindow.loadFile(path.join(__dirname, 'fixtures', 'pages', 'css-transparent.html'));
      await once(ipcMain, 'set-transparent');

      await setTimeout(1000);

      const screenCapture = await ScreenCapture.createForDisplay(display);
      await screenCapture.expectColorAtCenterMatches(HexColors.PURPLE);
    });

    // Linux and arm64 platforms (WOA and macOS) do not return any capture sources
    ifit(process.platform === 'darwin' && process.arch === 'x64')('should not make background transparent if falsy', async () => {
      const display = screen.getPrimaryDisplay();

      for (const transparent of [false, undefined]) {
        const window = new BrowserWindow({
          ...display.bounds,
          transparent
        });

        await once(window, 'show');
        await window.webContents.loadURL('data:text/html,<head><meta name="color-scheme" content="dark"></head>');

        await setTimeout(1000);
        const screenCapture = await ScreenCapture.createForDisplay(display);
        // color-scheme is set to dark so background should not be white
        await screenCapture.expectColorAtCenterDoesNotMatch(HexColors.WHITE);

        window.close();
      }
    });
  });

  describe('"backgroundColor" option', () => {
    afterEach(closeAllWindows);

    // Linux/WOA doesn't return any capture sources.
    ifit(process.platform === 'darwin')('should display the set color', async () => {
      const display = screen.getPrimaryDisplay();

      const w = new BrowserWindow({
        ...display.bounds,
        show: true,
        backgroundColor: HexColors.BLUE
      });

      w.loadURL('about:blank');
      await once(w, 'ready-to-show');

      await setTimeout(1000);

      const screenCapture = await ScreenCapture.createForDisplay(display);
      await screenCapture.expectColorAtCenterMatches(HexColors.BLUE);
    });
  });

  describe('draggable regions', () => {
    afterEach(closeAllWindows);

    ifit(hasCapturableScreen())('should allow the window to be dragged when enabled', async () => {
      // FIXME: nut-js has been removed from npm; we need to find a replacement
      // WOA fails to load libnut so we're using require to defer loading only
      // on supported platforms.
      // "@nut-tree\libnut-win32\build\Release\libnut.node is not a valid Win32 application."
      // @ts-ignore: nut-js is an optional dependency so it may not be installed
      const { mouse, straightTo, centerOf, Region, Button } = require('@nut-tree/nut-js') as typeof import('@nut-tree/nut-js');

      const display = screen.getPrimaryDisplay();

      const w = new BrowserWindow({
        x: 0,
        y: 0,
        width: display.bounds.width / 2,
        height: display.bounds.height / 2,
        frame: false,
        titleBarStyle: 'hidden'
      });

      const overlayHTML = path.join(__dirname, 'fixtures', 'pages', 'overlay.html');
      w.loadFile(overlayHTML);
      await once(w, 'ready-to-show');

      const winBounds = w.getBounds();
      const titleBarHeight = 30;
      const titleBarRegion = new Region(winBounds.x, winBounds.y, winBounds.width, titleBarHeight);
      const screenRegion = new Region(display.bounds.x, display.bounds.y, display.bounds.width, display.bounds.height);

      const startPos = w.getPosition();

      await mouse.setPosition(await centerOf(titleBarRegion));
      await mouse.pressButton(Button.LEFT);
      await mouse.drag(straightTo(centerOf(screenRegion)));

      // Wait for move to complete
      await Promise.race([
        once(w, 'move'),
        setTimeout(100) // fallback for possible race condition
      ]);

      const endPos = w.getPosition();

      expect(startPos).to.not.deep.equal(endPos);
    });

    ifit(hasCapturableScreen())('should allow the window to be dragged when no WCO and --webkit-app-region: drag enabled', async () => {
      // FIXME: nut-js has been removed from npm; we need to find a replacement
      // @ts-ignore: nut-js is an optional dependency so it may not be installed
      const { mouse, straightTo, centerOf, Region, Button } = require('@nut-tree/nut-js') as typeof import('@nut-tree/nut-js');

      const display = screen.getPrimaryDisplay();
      const w = new BrowserWindow({
        x: 0,
        y: 0,
        width: display.bounds.width / 2,
        height: display.bounds.height / 2,
        frame: false
      });

      const basePageHTML = path.join(__dirname, 'fixtures', 'pages', 'base-page.html');
      w.loadFile(basePageHTML);
      await once(w, 'ready-to-show');

      await w.webContents.executeJavaScript(`
        const style = document.createElement('style');
        style.innerHTML = \`
        #titlebar {
            
          background-color: red;
          height: 30px;
          width: 100%;
          -webkit-user-select: none;
          -webkit-app-region: drag;
          position: fixed;
          top: 0;
          left: 0;
          z-index: 1000000000000;
        }
        \`;
        
        const titleBar = document.createElement('title-bar');
        titleBar.id = 'titlebar';
        titleBar.textContent = 'test-titlebar';
        
        document.body.append(style);
        document.body.append(titleBar);
      `);
      // allow time for titlebar to finish loading
      await setTimeout(2000);

      const winBounds = w.getBounds();
      const titleBarHeight = 30;
      const titleBarRegion = new Region(winBounds.x, winBounds.y, winBounds.width, titleBarHeight);
      const screenRegion = new Region(display.bounds.x, display.bounds.y, display.bounds.width, display.bounds.height);

      const startPos = w.getPosition();
      await mouse.setPosition(await centerOf(titleBarRegion));
      await mouse.pressButton(Button.LEFT);
      await mouse.drag(straightTo(centerOf(screenRegion)));

      // Wait for move to complete
      await Promise.race([
        once(w, 'move'),
        setTimeout(1000) // fallback for possible race condition
      ]);

      const endPos = w.getPosition();

      expect(startPos).to.not.deep.equal(endPos);
    });
  });
});
