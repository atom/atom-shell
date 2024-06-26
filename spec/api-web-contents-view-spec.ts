import { closeAllWindows } from './lib/window-helpers';
import { expect } from 'chai';

import { BaseWindow, View, WebContentsView, webContents } from 'electron/main';
import { once } from 'node:events';
import { defer } from './lib/spec-helpers';
import { BrowserWindow } from 'electron';

describe('WebContentsView', () => {
  afterEach(closeAllWindows);

  it('can be instantiated with no arguments', () => {
    // eslint-disable-next-line no-new
    new WebContentsView();
  });

  it('can be instantiated with no webPreferences', () => {
    // eslint-disable-next-line no-new
    new WebContentsView({});
  });

  it('accepts existing webContents object', async () => {
    const currentWebContentsCount = webContents.getAllWebContents().length;

    const wc = (webContents as typeof ElectronInternal.WebContents).create({ sandbox: true });
    defer(() => wc.destroy());
    await wc.loadURL('about:blank');

    const webContentsView = new WebContentsView({
      webContents: wc
    });

    expect(webContentsView.webContents).to.eq(wc);
    expect(webContents.getAllWebContents().length).to.equal(currentWebContentsCount + 1, 'expected only single webcontents to be created');
  });

  it('should throw error when created with already attached webContents to BrowserWindow', () => {
    const browserWindow = new BrowserWindow();
    defer(() => browserWindow.webContents.destroy());

    const webContentsView = new WebContentsView();
    defer(() => webContentsView.webContents.destroy());

    browserWindow.contentView.addChildView(webContentsView);
    defer(() => browserWindow.contentView.removeChildView(webContentsView));

    expect(() => new WebContentsView({
      webContents: webContentsView.webContents
    })).to.throw('options.webContents is already attached to a window');
  });

  it('should throw error when created with already attached webContents to other WebContentsView', () => {
    const browserWindow = new BrowserWindow();

    const webContentsView = new WebContentsView();
    defer(() => webContentsView.webContents.destroy());
    webContentsView.webContents.loadURL('about:blank');

    expect(() => new WebContentsView({
      webContents: browserWindow.webContents
    })).to.throw('options.webContents is already attached to a window');
  });

  it('can be used as content view', () => {
    const w = new BaseWindow({ show: false });
    w.setContentView(new WebContentsView());
  });

  it('can be removed after a close', async () => {
    const w = new BaseWindow({ show: false });
    const v = new View();
    const wcv = new WebContentsView();
    w.setContentView(v);
    v.addChildView(wcv);
    await wcv.webContents.loadURL('about:blank');
    const destroyed = once(wcv.webContents, 'destroyed');
    wcv.webContents.executeJavaScript('window.close()');
    await destroyed;
    expect(wcv.webContents.isDestroyed()).to.be.true();
    v.removeChildView(wcv);
  });

  it('correctly reorders children', () => {
    const w = new BaseWindow({ show: false });
    const cv = new View();
    w.setContentView(cv);

    const wcv1 = new WebContentsView();
    const wcv2 = new WebContentsView();
    const wcv3 = new WebContentsView();
    w.contentView.addChildView(wcv1);
    w.contentView.addChildView(wcv2);
    w.contentView.addChildView(wcv3);

    expect(w.contentView.children).to.deep.equal([wcv1, wcv2, wcv3]);

    w.contentView.addChildView(wcv1);
    w.contentView.addChildView(wcv2);
    expect(w.contentView.children).to.deep.equal([wcv3, wcv1, wcv2]);
  });

  function triggerGCByAllocation () {
    const arr = [];
    for (let i = 0; i < 1000000; i++) {
      arr.push([]);
    }
    return arr;
  }

  it('doesn\'t crash when GCed during allocation', (done) => {
    // eslint-disable-next-line no-new
    new WebContentsView();
    setTimeout(() => {
      // NB. the crash we're testing for is the lack of a current `v8::Context`
      // when emitting an event in WebContents's destructor. V8 is inconsistent
      // about whether or not there's a current context during garbage
      // collection, and it seems that `v8Util.requestGarbageCollectionForTesting`
      // causes a GC in which there _is_ a current context, so the crash isn't
      // triggered. Thus, we force a GC by other means: namely, by allocating a
      // bunch of stuff.
      triggerGCByAllocation();
      done();
    });
  });

  it('can be fullscreened', async () => {
    const w = new BaseWindow();
    const v = new WebContentsView();
    w.setContentView(v);
    await v.webContents.loadURL('data:text/html,<div id="div">This is a simple div.</div>');

    const enterFullScreen = once(w, 'enter-full-screen');
    await v.webContents.executeJavaScript('document.getElementById("div").requestFullscreen()', true);
    await enterFullScreen;
    expect(w.isFullScreen()).to.be.true('isFullScreen');
  });

  describe('visibilityState', () => {
    it('is initially hidden', async () => {
      const v = new WebContentsView();
      await v.webContents.loadURL('data:text/html,<script>initialVisibility = document.visibilityState</script>');
      expect(await v.webContents.executeJavaScript('initialVisibility')).to.equal('hidden');
    });

    it('becomes visible when attached', async () => {
      const v = new WebContentsView();
      await v.webContents.loadURL('about:blank');
      expect(await v.webContents.executeJavaScript('document.visibilityState')).to.equal('hidden');
      const p = v.webContents.executeJavaScript('new Promise(resolve => document.addEventListener("visibilitychange", resolve))');
      // Ensure that the above listener has been registered before we add the
      // view to the window, or else the visibilitychange event might be
      // dispatched before the listener is registered.
      // executeJavaScript calls are sequential so if this one's finished then
      // the previous one must also have been finished :)
      await v.webContents.executeJavaScript('undefined');
      const w = new BaseWindow();
      w.setContentView(v);
      await p;
      expect(await v.webContents.executeJavaScript('document.visibilityState')).to.equal('visible');
    });

    it('is initially visible if load happens after attach', async () => {
      const w = new BaseWindow();
      const v = new WebContentsView();
      w.contentView = v;
      await v.webContents.loadURL('data:text/html,<script>initialVisibility = document.visibilityState</script>');
      expect(await v.webContents.executeJavaScript('initialVisibility')).to.equal('visible');
    });

    it('becomes hidden when parent window is hidden', async () => {
      const w = new BaseWindow();
      const v = new WebContentsView();
      w.setContentView(v);
      await v.webContents.loadURL('about:blank');
      expect(await v.webContents.executeJavaScript('document.visibilityState')).to.equal('visible');
      const p = v.webContents.executeJavaScript('new Promise(resolve => document.addEventListener("visibilitychange", resolve))');
      // We have to wait until the listener above is fully registered before hiding the window.
      // On Windows, the executeJavaScript and the visibilitychange can happen out of order
      // without this.
      await v.webContents.executeJavaScript('0');
      w.hide();
      await p;
      expect(await v.webContents.executeJavaScript('document.visibilityState')).to.equal('hidden');
    });

    it('becomes visible when parent window is shown', async () => {
      const w = new BaseWindow({ show: false });
      const v = new WebContentsView();
      w.setContentView(v);
      await v.webContents.loadURL('about:blank');
      expect(await v.webContents.executeJavaScript('document.visibilityState')).to.equal('hidden');
      const p = v.webContents.executeJavaScript('new Promise(resolve => document.addEventListener("visibilitychange", resolve))');
      // We have to wait until the listener above is fully registered before hiding the window.
      // On Windows, the executeJavaScript and the visibilitychange can happen out of order
      // without this.
      await v.webContents.executeJavaScript('0');
      w.show();
      await p;
      expect(await v.webContents.executeJavaScript('document.visibilityState')).to.equal('visible');
    });

    it('does not change when view is moved between two visible windows', async () => {
      const w = new BaseWindow();
      const v = new WebContentsView();
      w.setContentView(v);
      await v.webContents.loadURL('about:blank');
      expect(await v.webContents.executeJavaScript('document.visibilityState')).to.equal('visible');

      const p = v.webContents.executeJavaScript('new Promise(resolve => document.addEventListener("visibilitychange", () => resolve(document.visibilityState)))');
      // Ensure the listener has been registered.
      await v.webContents.executeJavaScript('undefined');
      const w2 = new BaseWindow();
      w2.setContentView(v);
      // Wait for the visibility state to settle as "visible".
      // On macOS one visibilitychange event is fired but visibilityState
      // remains "visible". On Win/Linux, two visibilitychange events are
      // fired, a "hidden" and a "visible" one. Reconcile these two models
      // by waiting until at least one event has been fired, and then waiting
      // until the visibility state settles as "visible".
      let visibilityState = await p;
      for (let attempts = 0; visibilityState !== 'visible' && attempts < 10; attempts++) {
        visibilityState = await v.webContents.executeJavaScript('new Promise(resolve => document.visibilityState === "visible" ? resolve("visible") : document.addEventListener("visibilitychange", () => resolve(document.visibilityState)))');
      }
      expect(visibilityState).to.equal('visible');
    });
  });
});
