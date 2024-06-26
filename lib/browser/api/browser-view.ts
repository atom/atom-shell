import { BrowserWindow, AutoResizeOptions, Rectangle, WebContentsView, WebPreferences, WebContents } from 'electron/main';

const v8Util = process._linkedBinding('electron_common_v8_util');

export default class BrowserView {
  #webContentsView: WebContentsView;

  // AutoResize state
  #resizeListener: ((...args: any[]) => void) | null = null;
  #lastWindowSize: {width: number, height: number} = { width: 0, height: 0 };
  #autoResizeFlags: AutoResizeOptions = {};

  constructor (options: {webPreferences: WebPreferences, webContents?: WebContents} = { webPreferences: {} }) {
    const { webPreferences = {}, webContents } = options;
    if (webContents) {
      v8Util.setHiddenValue(webPreferences, 'webContents', webContents);
    }
    webPreferences.type = 'browserView';
    this.#webContentsView = new WebContentsView({ webPreferences });
  }

  get webContents () {
    return this.#webContentsView.webContents;
  }

  setBounds (bounds: Rectangle) {
    this.#webContentsView.setBounds(bounds);
    this.#autoHorizontalProportion = null;
    this.#autoVerticalProportion = null;
  }

  getBounds () {
    return this.#webContentsView.getBounds();
  }

  setAutoResize (options: AutoResizeOptions) {
    if (options == null || typeof options !== 'object') {
      throw new Error('Invalid auto resize options');
    }

    this.#autoResizeFlags = {
      width: !!options.width,
      height: !!options.height,
      horizontal: !!options.horizontal,
      vertical: !!options.vertical
    };

    this.#autoHorizontalProportion = null;
    this.#autoVerticalProportion = null;
  }

  setBackgroundColor (color: string) {
    this.#webContentsView.setBackgroundColor(color);
  }

  // Internal methods
  get ownerWindow (): BrowserWindow | null {
    return !this.webContents.isDestroyed() ? this.webContents.getOwnerBrowserWindow() : null;
  }

  set ownerWindow (w: BrowserWindow | null) {
    if (this.webContents.isDestroyed()) return;
    const oldWindow = this.webContents.getOwnerBrowserWindow();
    if (oldWindow && this.#resizeListener) {
      oldWindow.off('resize', this.#resizeListener);
      this.#resizeListener = null;
    }
    this.webContents._setOwnerWindow(w);
    if (w) {
      this.#lastWindowSize = w.getBounds();
      w.on('resize', this.#resizeListener = this.#autoResize.bind(this));
    }
  }

  #autoHorizontalProportion: {width: number, left: number} | null = null;
  #autoVerticalProportion: {height: number, top: number} | null = null;
  #autoResize () {
    if (!this.ownerWindow) {
      throw new Error('Electron bug: #autoResize called without owner window');
    };

    if (this.#autoResizeFlags.horizontal && this.#autoHorizontalProportion == null) {
      const viewBounds = this.#webContentsView.getBounds();
      this.#autoHorizontalProportion = {
        width: this.#lastWindowSize.width / viewBounds.width,
        left: this.#lastWindowSize.width / viewBounds.x
      };
    }

    if (this.#autoResizeFlags.vertical && this.#autoVerticalProportion == null) {
      const viewBounds = this.#webContentsView.getBounds();
      this.#autoVerticalProportion = {
        height: this.#lastWindowSize.height / viewBounds.height,
        top: this.#lastWindowSize.height / viewBounds.y
      };
    }

    const newBounds = this.ownerWindow.getBounds();
    let widthDelta = newBounds.width - this.#lastWindowSize.width;
    let heightDelta = newBounds.height - this.#lastWindowSize.height;
    if (!this.#autoResizeFlags.width) widthDelta = 0;
    if (!this.#autoResizeFlags.height) heightDelta = 0;

    const newViewBounds = this.#webContentsView.getBounds();
    if (widthDelta || heightDelta) {
      this.#webContentsView.setBounds({
        ...newViewBounds,
        width: newViewBounds.width + widthDelta,
        height: newViewBounds.height + heightDelta
      });
    }

    if (this.#autoHorizontalProportion) {
      newViewBounds.width = newBounds.width / this.#autoHorizontalProportion.width;
      newViewBounds.x = newBounds.width / this.#autoHorizontalProportion.left;
    }

    if (this.#autoVerticalProportion) {
      newViewBounds.height = newBounds.height / this.#autoVerticalProportion.height;
      newViewBounds.y = newBounds.y / this.#autoVerticalProportion.top;
    }

    if (this.#autoHorizontalProportion || this.#autoVerticalProportion) {
      this.#webContentsView.setBounds(newViewBounds);
    }
  }

  get webContentsView () {
    return this.#webContentsView;
  }
}
