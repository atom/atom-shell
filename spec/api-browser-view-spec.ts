import { expect } from 'chai';
import * as path from 'node:path';
import { BrowserView, BrowserWindow, screen, webContents } from 'electron/main';
import { closeWindow } from './lib/window-helpers';
import { defer, ifit, startRemoteControlApp } from './lib/spec-helpers';
import { ScreenCapture } from './lib/screen-helpers';
import { once } from 'node:events';

describe('BrowserView module', () => {
  const fixtures = path.resolve(__dirname, 'fixtures');

  let w: BrowserWindow;
  let view: BrowserView;

  beforeEach(() => {
    expect(webContents.getAllWebContents().length).to.equal(0, 'expected no webContents to exist');
    w = new BrowserWindow({
      show: false,
      width: 400,
      height: 400,
      webPreferences: {
        backgroundThrottling: false
      }
    });
  });

  afterEach(async () => {
    if (!w.isDestroyed()) {
      const p = once(w.webContents, 'destroyed');
      await closeWindow(w);
      w = null as any;
      await p;
    }

    if (view && view.webContents) {
      const p = once(view.webContents, 'destroyed');
      view.webContents.destroy();
      view = null as any;
      await p;
    }

    expect(webContents.getAllWebContents().length).to.equal(0, 'expected no webContents to exist');
  });

  it('sets the correct class name on the prototype', () => {
    expect(BrowserView.prototype.constructor.name).to.equal('BrowserView');
  });

  it('can be created with an existing webContents', async () => {
    const wc = (webContents as typeof ElectronInternal.WebContents).create({ sandbox: true });
    await wc.loadURL('about:blank');

    view = new BrowserView({ webContents: wc } as any);
    expect(view.webContents === wc).to.be.true('view.webContents === wc');

    expect(view.webContents.getURL()).to.equal('about:blank');
  });

  it('has type browserView', () => {
    view = new BrowserView();
    expect(view.webContents.getType()).to.equal('browserView');
  });

  describe('BrowserView.setBackgroundColor()', () => {
    it('does not throw for valid args', () => {
      view = new BrowserView();
      view.setBackgroundColor('#000');
    });

    // We now treat invalid args as "no background".
    it('does not throw for invalid args', () => {
      view = new BrowserView();
      expect(() => {
        view.setBackgroundColor({} as any);
      }).not.to.throw();
    });

    // Linux and arm64 platforms (WOA and macOS) do not return any capture sources
    ifit(process.platform === 'darwin' && process.arch === 'x64')('sets the background color to transparent if none is set', async () => {
      const display = screen.getPrimaryDisplay();
      const WINDOW_BACKGROUND_COLOR = '#55ccbb';

      w.show();
      w.setBounds(display.bounds);
      w.setBackgroundColor(WINDOW_BACKGROUND_COLOR);
      await w.loadURL('about:blank');

      view = new BrowserView();
      view.setBounds(display.bounds);
      w.setBrowserView(view);
      await view.webContents.loadURL('data:text/html,hello there');

      const screenCapture = await ScreenCapture.createForDisplay(display);
      await screenCapture.expectColorAtCenterMatches(WINDOW_BACKGROUND_COLOR);
    });

    // Linux and arm64 platforms (WOA and macOS) do not return any capture sources
    ifit(process.platform === 'darwin' && process.arch === 'x64')('successfully applies the background color', async () => {
      const WINDOW_BACKGROUND_COLOR = '#55ccbb';
      const VIEW_BACKGROUND_COLOR = '#ff00ff';
      const display = screen.getPrimaryDisplay();

      w.show();
      w.setBounds(display.bounds);
      w.setBackgroundColor(WINDOW_BACKGROUND_COLOR);
      await w.loadURL('about:blank');

      view = new BrowserView();
      view.setBounds(display.bounds);
      w.setBrowserView(view);
      w.setBackgroundColor(VIEW_BACKGROUND_COLOR);
      await view.webContents.loadURL('data:text/html,hello there');

      const screenCapture = await ScreenCapture.createForDisplay(display);
      await screenCapture.expectColorAtCenterMatches(VIEW_BACKGROUND_COLOR);
    });
  });

  describe('BrowserView.setAutoResize()', () => {
    it('does not throw for valid args', () => {
      view = new BrowserView();
      view.setAutoResize({});
      view.setAutoResize({ width: true, height: false });
    });

    it('throws for invalid args', () => {
      view = new BrowserView();
      expect(() => {
        view.setAutoResize(null as any);
      }).to.throw(/Invalid auto resize options/);
    });

    it('does not resize when the BrowserView has no AutoResize', () => {
      view = new BrowserView();
      w.addBrowserView(view);
      view.setBounds({ x: 0, y: 0, width: 400, height: 200 });
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 400,
        height: 200
      });
      w.setSize(800, 400);
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 400,
        height: 200
      });
    });

    it('resizes horizontally when the window is resized horizontally', () => {
      view = new BrowserView();
      view.setAutoResize({ width: true, height: false });
      w.addBrowserView(view);
      view.setBounds({ x: 0, y: 0, width: 400, height: 200 });
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 400,
        height: 200
      });
      w.setSize(800, 400);
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 800,
        height: 200
      });
    });

    it('resizes vertically when the window is resized vertically', () => {
      view = new BrowserView();
      view.setAutoResize({ width: false, height: true });
      w.addBrowserView(view);
      view.setBounds({ x: 0, y: 0, width: 200, height: 400 });
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 200,
        height: 400
      });
      w.setSize(400, 800);
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 200,
        height: 800
      });
    });

    it('resizes both vertically and horizontally when the window is resized', () => {
      view = new BrowserView();
      view.setAutoResize({ width: true, height: true });
      w.addBrowserView(view);
      view.setBounds({ x: 0, y: 0, width: 400, height: 400 });
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 400,
        height: 400
      });
      w.setSize(800, 800);
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 800,
        height: 800
      });
    });

    it('resizes proportionally', () => {
      view = new BrowserView();
      view.setAutoResize({ width: true, height: false });
      w.addBrowserView(view);
      view.setBounds({ x: 0, y: 0, width: 200, height: 100 });
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 200,
        height: 100
      });
      w.setSize(800, 400);
      expect(view.getBounds()).to.deep.equal({
        x: 0,
        y: 0,
        width: 600,
        height: 100
      });
    });

    it('does not move x if horizontal: false', () => {
      view = new BrowserView();
      view.setAutoResize({ width: true });
      w.addBrowserView(view);
      view.setBounds({ x: 200, y: 0, width: 200, height: 100 });
      w.setSize(800, 400);
      expect(view.getBounds()).to.deep.equal({
        x: 200,
        y: 0,
        width: 600,
        height: 100
      });
    });

    it('moves x if horizontal: true', () => {
      view = new BrowserView();
      view.setAutoResize({ horizontal: true });
      w.addBrowserView(view);
      view.setBounds({ x: 200, y: 0, width: 200, height: 100 });
      w.setSize(800, 400);
      expect(view.getBounds()).to.deep.equal({
        x: 400,
        y: 0,
        width: 400,
        height: 100
      });
    });

    it('moves x if horizontal: true width: true', () => {
      view = new BrowserView();
      view.setAutoResize({ horizontal: true, width: true });
      w.addBrowserView(view);
      view.setBounds({ x: 200, y: 0, width: 200, height: 100 });
      w.setSize(800, 400);
      expect(view.getBounds()).to.deep.equal({
        x: 400,
        y: 0,
        width: 400,
        height: 100
      });
    });
  });

  describe('BrowserView.setBounds()', () => {
    it('does not throw for valid args', () => {
      view = new BrowserView();
      view.setBounds({ x: 0, y: 0, width: 1, height: 1 });
    });

    it('throws for invalid args', () => {
      view = new BrowserView();
      expect(() => {
        view.setBounds(null as any);
      }).to.throw(/conversion failure/);
      expect(() => {
        view.setBounds({} as any);
      }).to.throw(/conversion failure/);
    });

    it('can set bounds after view is added to window', () => {
      view = new BrowserView();

      const bounds = { x: 0, y: 0, width: 50, height: 50 };

      w.addBrowserView(view);
      view.setBounds(bounds);

      expect(view.getBounds()).to.deep.equal(bounds);
    });

    it('can set bounds before view is added to window', () => {
      view = new BrowserView();

      const bounds = { x: 0, y: 0, width: 50, height: 50 };

      view.setBounds(bounds);
      w.addBrowserView(view);

      expect(view.getBounds()).to.deep.equal(bounds);
    });

    it('can update bounds', () => {
      view = new BrowserView();
      w.addBrowserView(view);

      const bounds1 = { x: 0, y: 0, width: 50, height: 50 };
      view.setBounds(bounds1);
      expect(view.getBounds()).to.deep.equal(bounds1);

      const bounds2 = { x: 0, y: 150, width: 50, height: 50 };
      view.setBounds(bounds2);
      expect(view.getBounds()).to.deep.equal(bounds2);
    });
  });

  describe('BrowserView.getBounds()', () => {
    it('returns the current bounds', () => {
      view = new BrowserView();
      const bounds = { x: 10, y: 20, width: 30, height: 40 };
      view.setBounds(bounds);
      expect(view.getBounds()).to.deep.equal(bounds);
    });

    it('does not changer after being added to a window', () => {
      view = new BrowserView();
      const bounds = { x: 10, y: 20, width: 30, height: 40 };
      view.setBounds(bounds);
      expect(view.getBounds()).to.deep.equal(bounds);

      w.addBrowserView(view);
      expect(view.getBounds()).to.deep.equal(bounds);
    });
  });

  describe('BrowserWindow.setBrowserView()', () => {
    it('does not throw for valid args', () => {
      view = new BrowserView();
      w.setBrowserView(view);
    });

    it('does not throw if called multiple times with same view', () => {
      view = new BrowserView();
      w.setBrowserView(view);
      w.setBrowserView(view);
      w.setBrowserView(view);
    });
  });

  describe('BrowserWindow.getBrowserView()', () => {
    it('returns the set view', () => {
      view = new BrowserView();
      w.setBrowserView(view);

      const view2 = w.getBrowserView();
      expect(view2!.webContents.id).to.equal(view.webContents.id);
    });

    it('returns null if none is set', () => {
      const view = w.getBrowserView();
      expect(view).to.be.null('view');
    });
  });

  describe('BrowserWindow.addBrowserView()', () => {
    it('does not throw for valid args', () => {
      const view1 = new BrowserView();
      defer(() => view1.webContents.destroy());
      w.addBrowserView(view1);
      defer(() => w.removeBrowserView(view1));
      const view2 = new BrowserView();
      defer(() => view2.webContents.destroy());
      w.addBrowserView(view2);
      defer(() => w.removeBrowserView(view2));
    });

    it('does not throw if called multiple times with same view', () => {
      view = new BrowserView();
      w.addBrowserView(view);
      w.addBrowserView(view);
      w.addBrowserView(view);
    });

    it('does not crash if the BrowserView webContents are destroyed prior to window addition', () => {
      expect(() => {
        const view1 = new BrowserView();
        view1.webContents.destroy();
        w.addBrowserView(view1);
      }).to.not.throw();
    });

    it('does not crash if the webContents is destroyed after a URL is loaded', () => {
      view = new BrowserView();
      expect(async () => {
        view.setBounds({ x: 0, y: 0, width: 400, height: 300 });
        await view.webContents.loadURL('data:text/html,hello there');
        view.webContents.destroy();
      }).to.not.throw();
    });

    it('can handle BrowserView reparenting', async () => {
      view = new BrowserView();

      w.addBrowserView(view);
      view.webContents.loadURL('about:blank');
      await once(view.webContents, 'did-finish-load');

      const w2 = new BrowserWindow({ show: false });
      w2.addBrowserView(view);

      w.close();

      view.webContents.loadURL(`file://${fixtures}/pages/blank.html`);
      await once(view.webContents, 'did-finish-load');

      // Clean up - the afterEach hook assumes the webContents on w is still alive.
      w = new BrowserWindow({ show: false });
      w2.close();
      w2.destroy();
    });

    it('does not cause a crash when used for view with destroyed web contents', async () => {
      const w2 = new BrowserWindow({ show: false });
      const view = new BrowserView();
      view.webContents.close();
      w2.addBrowserView(view);
      w2.webContents.loadURL('about:blank');
      await once(w2.webContents, 'did-finish-load');
      w2.close();
    });
  });

  describe('BrowserWindow.removeBrowserView()', () => {
    it('does not throw if called multiple times with same view', () => {
      expect(() => {
        view = new BrowserView();
        w.addBrowserView(view);
        w.removeBrowserView(view);
        w.removeBrowserView(view);
      }).to.not.throw();
    });

    it('can be called on a BrowserView with a destroyed webContents', async () => {
      view = new BrowserView();
      w.addBrowserView(view);
      await view.webContents.loadURL('data:text/html,hello there');
      const destroyed = once(view.webContents, 'destroyed');
      view.webContents.close();
      await destroyed;
      w.removeBrowserView(view);
    });
  });

  describe('BrowserWindow.getBrowserViews()', () => {
    it('returns same views as was added', () => {
      const view1 = new BrowserView();
      defer(() => view1.webContents.destroy());
      w.addBrowserView(view1);
      defer(() => w.removeBrowserView(view1));
      const view2 = new BrowserView();
      defer(() => view2.webContents.destroy());
      w.addBrowserView(view2);
      defer(() => w.removeBrowserView(view2));

      const views = w.getBrowserViews();
      expect(views).to.have.lengthOf(2);
      expect(views[0].webContents.id).to.equal(view1.webContents.id);
      expect(views[1].webContents.id).to.equal(view2.webContents.id);
    });

    it('persists ordering by z-index', () => {
      const view1 = new BrowserView();
      defer(() => view1.webContents.destroy());
      w.addBrowserView(view1);
      defer(() => w.removeBrowserView(view1));
      const view2 = new BrowserView();
      defer(() => view2.webContents.destroy());
      w.addBrowserView(view2);
      defer(() => w.removeBrowserView(view2));
      w.setTopBrowserView(view1);

      const views = w.getBrowserViews();
      expect(views).to.have.lengthOf(2);
      expect(views[0].webContents.id).to.equal(view2.webContents.id);
      expect(views[1].webContents.id).to.equal(view1.webContents.id);
    });
  });

  describe('BrowserWindow.setTopBrowserView()', () => {
    it('should throw an error when a BrowserView is not attached to the window', () => {
      view = new BrowserView();
      expect(() => {
        w.setTopBrowserView(view);
      }).to.throw(/is not attached/);
    });

    it('should throw an error when a BrowserView is attached to some other window', () => {
      view = new BrowserView();

      const win2 = new BrowserWindow();

      w.addBrowserView(view);
      view.setBounds({ x: 0, y: 0, width: 100, height: 100 });
      win2.addBrowserView(view);

      expect(() => {
        w.setTopBrowserView(view);
      }).to.throw(/is not attached/);

      win2.close();
      win2.destroy();
    });
  });

  describe('BrowserView.webContents.getOwnerBrowserWindow()', () => {
    it('points to owning window', () => {
      view = new BrowserView();
      expect(view.webContents.getOwnerBrowserWindow()).to.be.null('owner browser window');

      w.setBrowserView(view);
      expect(view.webContents.getOwnerBrowserWindow()).to.equal(w);

      w.setBrowserView(null);
      expect(view.webContents.getOwnerBrowserWindow()).to.be.null('owner browser window');
    });
  });

  describe('shutdown behavior', () => {
    it('emits the destroyed event when the host BrowserWindow is closed', async () => {
      view = new BrowserView();
      w.addBrowserView(view);
      await view.webContents.loadURL(`data:text/html,
        <html>
          <body>
            <div id="bv_id">HELLO BROWSERVIEW</div>
          </body>
        </html>
      `);

      const query = 'document.getElementById("bv_id").textContent';
      const contentBefore = await view.webContents.executeJavaScript(query);
      expect(contentBefore).to.equal('HELLO BROWSERVIEW');

      w.close();

      const destroyed = once(view.webContents, 'destroyed');
      const closed = once(w, 'closed');
      await Promise.all([destroyed, closed]);
    });

    it('does not destroy its webContents if an owner BrowserWindow close event is prevented', async () => {
      view = new BrowserView();
      w.addBrowserView(view);
      await view.webContents.loadURL(`data:text/html,
        <html>
          <body>
            <div id="bv_id">HELLO BROWSERVIEW</div>
          </body>
        </html>
      `);

      const query = 'document.getElementById("bv_id").textContent';
      const contentBefore = await view.webContents.executeJavaScript(query);
      expect(contentBefore).to.equal('HELLO BROWSERVIEW');

      w.once('close', (e) => {
        e.preventDefault();
      });

      w.close();

      const contentAfter = await view.webContents.executeJavaScript(query);
      expect(contentAfter).to.equal('HELLO BROWSERVIEW');
    });

    it('does not crash on exit', async () => {
      const rc = await startRemoteControlApp();
      await rc.remotely(() => {
        const { BrowserView, app } = require('electron');
        // eslint-disable-next-line no-new
        new BrowserView({});
        setTimeout(() => {
          app.quit();
        });
      });
      const [code] = await once(rc.process, 'exit');
      expect(code).to.equal(0);
    });

    it('does not crash on exit if added to a browser window', async () => {
      const rc = await startRemoteControlApp();
      await rc.remotely(() => {
        const { app, BrowserView, BrowserWindow } = require('electron');
        const bv = new BrowserView();
        bv.webContents.loadURL('about:blank');
        const bw = new BrowserWindow({ show: false });
        bw.addBrowserView(bv);
        setTimeout(() => {
          app.quit();
        });
      });
      const [code] = await once(rc.process, 'exit');
      expect(code).to.equal(0);
    });

    it('emits the destroyed event when webContents.close() is called', async () => {
      view = new BrowserView();
      w.setBrowserView(view);
      await view.webContents.loadFile(path.join(fixtures, 'pages', 'a.html'));

      view.webContents.close();
      await once(view.webContents, 'destroyed');
    });

    it('emits the destroyed event when window.close() is called', async () => {
      view = new BrowserView();
      w.setBrowserView(view);
      await view.webContents.loadFile(path.join(fixtures, 'pages', 'a.html'));

      view.webContents.executeJavaScript('window.close()');
      await once(view.webContents, 'destroyed');
    });
  });

  describe('window.open()', () => {
    it('works in BrowserView', (done) => {
      view = new BrowserView();
      w.setBrowserView(view);
      view.webContents.setWindowOpenHandler(({ url, frameName }) => {
        expect(url).to.equal('http://host/');
        expect(frameName).to.equal('host');
        done();
        return { action: 'deny' };
      });
      view.webContents.loadFile(path.join(fixtures, 'pages', 'window-open.html'));
    });
  });

  describe('BrowserView.capturePage(rect)', () => {
    it('returns a Promise with a Buffer', async () => {
      view = new BrowserView({
        webPreferences: {
          backgroundThrottling: false
        }
      });
      w.addBrowserView(view);
      view.setBounds({
        ...w.getBounds(),
        x: 0,
        y: 0
      });
      const image = await view.webContents.capturePage({
        x: 0,
        y: 0,
        width: 100,
        height: 100
      });

      expect(image.isEmpty()).to.equal(true);
    });

    describe('when `outputSize is provided`', () => {
      describe('when both dimensions of `outputSize` are smaller than captured area', () => {
        it('returns image with requested size', async () => {
          const w = new BrowserWindow({ show: true });
          view = new BrowserView({
            webPreferences: {
              backgroundThrottling: false
            }
          });
          w.setBrowserView(view);
          view.setBounds({
            ...w.getBounds(),
            x: 0,
            y: 0
          });
          await view.webContents.loadFile(path.join(fixtures, 'pages', 'a.html'));
          const image = await view.webContents.capturePage({
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
          const w = new BrowserWindow({ show: true });
          w.setBounds({ width: 200, height: 250 });
          view = new BrowserView({
            webPreferences: {
              backgroundThrottling: false
            }
          });
          w.setBrowserView(view);
          view.setBounds({
            ...w.getBounds(),
            x: 0,
            y: 0
          });
          await view.webContents.loadFile(path.join(fixtures, 'pages', 'a.html'));
          const image = await view.webContents.capturePage({
            x: 0,
            y: 0,
            width: 100,
            height: 100
          }, {
            outputSize: {
              width: 1500,
              height: 150
            }
          });

          expect(image.getSize()).to.deep.equal({ width: 1500, height: 150 });
        });
      });

      describe('when both dimensions of `outputSize` are bigger than both dimensions of the captured area', () => {
        it('returns image with requested size', async () => {
          const w = new BrowserWindow({ show: true });
          w.setBounds({ width: 200, height: 200 });
          view = new BrowserView({
            webPreferences: {
              backgroundThrottling: false
            }
          });
          w.setBrowserView(view);
          view.setBounds({
            ...w.getBounds(),
            x: 0,
            y: 0
          });
          await view.webContents.loadFile(path.join(fixtures, 'pages', 'a.html'));
          const image = await view.webContents.capturePage({
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

    xit('resolves after the window is hidden and capturer count is non-zero', async () => {
      view = new BrowserView({
        webPreferences: {
          backgroundThrottling: false
        }
      });
      w.setBrowserView(view);
      view.setBounds({
        ...w.getBounds(),
        x: 0,
        y: 0
      });
      await view.webContents.loadFile(path.join(fixtures, 'pages', 'a.html'));

      const image = await view.webContents.capturePage();
      expect(image.isEmpty()).to.equal(false);
    });
  });
});
