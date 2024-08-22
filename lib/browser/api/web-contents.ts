import { app, ipcMain, session, webFrameMain, dialog } from 'electron/main';
import type { BrowserWindowConstructorOptions, LoadURLOptions, MessageBoxOptions, WebFrameMain } from 'electron/main';

import * as url from 'url';
import * as path from 'path';
import { openGuestWindow, makeWebPreferences, parseContentTypeFormat } from '@electron/internal/browser/guest-window-manager';
import { parseFeatures } from '@electron/internal/browser/parse-features-string';
import { ipcMainInternal } from '@electron/internal/browser/ipc-main-internal';
import * as ipcMainUtils from '@electron/internal/browser/ipc-main-internal-utils';
import { MessagePortMain } from '@electron/internal/browser/message-port-main';
import { IPC_MESSAGES } from '@electron/internal/common/ipc-messages';
import { IpcMainImpl } from '@electron/internal/browser/ipc-main-impl';
import * as deprecate from '@electron/internal/common/deprecate';

// session is not used here, the purpose is to make sure session is initialized
// before the webContents module.
// eslint-disable-next-line no-unused-expressions
session;

const webFrameMainBinding = process._linkedBinding('electron_browser_web_frame_main');

let nextId = 0;
const getNextId = function () {
  return ++nextId;
};

type PostData = LoadURLOptions['postData']

// Stock page sizes
const PDFPageSizes: Record<string, ElectronInternal.MediaSize> = {
  Letter: {
    custom_display_name: 'Letter',
    height_microns: 279400,
    name: 'NA_LETTER',
    width_microns: 215900
  },
  Legal: {
    custom_display_name: 'Legal',
    height_microns: 355600,
    name: 'NA_LEGAL',
    width_microns: 215900
  },
  Tabloid: {
    height_microns: 431800,
    name: 'NA_LEDGER',
    width_microns: 279400,
    custom_display_name: 'Tabloid'
  },
  A0: {
    custom_display_name: 'A0',
    height_microns: 1189000,
    name: 'ISO_A0',
    width_microns: 841000
  },
  A1: {
    custom_display_name: 'A1',
    height_microns: 841000,
    name: 'ISO_A1',
    width_microns: 594000
  },
  A2: {
    custom_display_name: 'A2',
    height_microns: 594000,
    name: 'ISO_A2',
    width_microns: 420000
  },
  A3: {
    custom_display_name: 'A3',
    height_microns: 420000,
    name: 'ISO_A3',
    width_microns: 297000
  },
  A4: {
    custom_display_name: 'A4',
    height_microns: 297000,
    name: 'ISO_A4',
    is_default: 'true',
    width_microns: 210000
  },
  A5: {
    custom_display_name: 'A5',
    height_microns: 210000,
    name: 'ISO_A5',
    width_microns: 148000
  },
  A6: {
    custom_display_name: 'A6',
    height_microns: 148000,
    name: 'ISO_A6',
    width_microns: 105000
  }
} as const;

const paperFormats: Record<string, ElectronInternal.PageSize> = {
  letter: { width: 8.5, height: 11 },
  legal: { width: 8.5, height: 14 },
  tabloid: { width: 11, height: 17 },
  ledger: { width: 17, height: 11 },
  a0: { width: 33.1, height: 46.8 },
  a1: { width: 23.4, height: 33.1 },
  a2: { width: 16.54, height: 23.4 },
  a3: { width: 11.7, height: 16.54 },
  a4: { width: 8.27, height: 11.7 },
  a5: { width: 5.83, height: 8.27 },
  a6: { width: 4.13, height: 5.83 }
} as const;

// The minimum micron size Chromium accepts is that where:
// Per printing/units.h:
//  * kMicronsPerInch - Length of an inch in 0.001mm unit.
//  * kPointsPerInch - Length of an inch in CSS's 1pt unit.
//
// Formula: (kPointsPerInch / kMicronsPerInch) * size >= 1
//
// Practically, this means microns need to be > 352 microns.
// We therefore need to verify this or it will silently fail.
const isValidCustomPageSize = (width: number, height: number) => {
  return [width, height].every(x => x > 352);
};

// JavaScript implementations of WebContents.
const binding = process._linkedBinding('electron_browser_web_contents');
const printing = process._linkedBinding('electron_browser_printing');
const { WebContents } = binding as { WebContents: { prototype: Electron.WebContents } };

WebContents.prototype.postMessage = function (...args) {
  return this.mainFrame.postMessage(...args);
};

WebContents.prototype.send = function (channel, ...args) {
  return this.mainFrame.send(channel, ...args);
};

WebContents.prototype._sendInternal = function (channel, ...args) {
  return this.mainFrame._sendInternal(channel, ...args);
};

function getWebFrame (contents: Electron.WebContents, frame: number | [number, number]) {
  if (typeof frame === 'number') {
    return webFrameMain.fromId(contents.mainFrame.processId, frame);
  } else if (Array.isArray(frame) && frame.length === 2 && frame.every(value => typeof value === 'number')) {
    return webFrameMain.fromId(frame[0], frame[1]);
  } else {
    throw new Error('Missing required frame argument (must be number or [processId, frameId])');
  }
}

WebContents.prototype.sendToFrame = function (frameId, channel, ...args) {
  const frame = getWebFrame(this, frameId);
  if (!frame) return false;
  frame.send(channel, ...args);
  return true;
};

// Following methods are mapped to webFrame.
const webFrameMethods = [
  'insertCSS',
  'insertText',
  'removeInsertedCSS',
  'setVisualZoomLevelLimits'
] as ('insertCSS' | 'insertText' | 'removeInsertedCSS' | 'setVisualZoomLevelLimits')[];

for (const method of webFrameMethods) {
  WebContents.prototype[method] = function (...args: any[]): Promise<any> {
    return ipcMainUtils.invokeInWebContents(this, IPC_MESSAGES.RENDERER_WEB_FRAME_METHOD, method, ...args);
  };
}

const waitTillCanExecuteJavaScript = async (webContents: Electron.WebContents) => {
  if (webContents.getURL() && !webContents.isLoadingMainFrame()) return;

  return new Promise<void>((resolve) => {
    webContents.once('did-stop-loading', () => {
      resolve();
    });
  });
};

// Make sure WebContents::executeJavaScript would run the code only when the
// WebContents has been loaded.
WebContents.prototype.executeJavaScript = async function (code, hasUserGesture) {
  await waitTillCanExecuteJavaScript(this);
  return ipcMainUtils.invokeInWebContents(this, IPC_MESSAGES.RENDERER_WEB_FRAME_METHOD, 'executeJavaScript', String(code), !!hasUserGesture);
};
WebContents.prototype.executeJavaScriptInIsolatedWorld = async function (worldId, code, hasUserGesture) {
  await waitTillCanExecuteJavaScript(this);
  return ipcMainUtils.invokeInWebContents(this, IPC_MESSAGES.RENDERER_WEB_FRAME_METHOD, 'executeJavaScriptInIsolatedWorld', worldId, code, !!hasUserGesture);
};

function checkType<T> (value: T, type: 'number' | 'boolean' | 'string' | 'object', name: string): T {
  // eslint-disable-next-line valid-typeof
  if (typeof value !== type) {
    throw new TypeError(`${name} must be a ${type}`);
  }

  return value;
}

function parsePageSize (pageSize: string | ElectronInternal.PageSize) {
  if (typeof pageSize === 'string') {
    const format = paperFormats[pageSize.toLowerCase()];
    if (!format) {
      throw new Error(`Invalid pageSize ${pageSize}`);
    }

    return { paperWidth: format.width, paperHeight: format.height };
  } else if (typeof pageSize === 'object') {
    if (typeof pageSize.width !== 'number' || typeof pageSize.height !== 'number') {
      throw new TypeError('width and height properties are required for pageSize');
    }

    return { paperWidth: pageSize.width, paperHeight: pageSize.height };
  } else {
    throw new TypeError('pageSize must be a string or an object');
  }
}

// Translate the options of printToPDF.

let pendingPromise: Promise<any> | undefined;
WebContents.prototype.printToPDF = async function (options) {
  const margins = checkType(options.margins ?? {}, 'object', 'margins');
  const pageSize = parsePageSize(options.pageSize ?? 'letter');

  const { top, bottom, left, right } = margins;
  const validHeight = [top, bottom].every(u => u === undefined || u <= pageSize.paperHeight);
  const validWidth = [left, right].every(u => u === undefined || u <= pageSize.paperWidth);

  if (!validHeight || !validWidth) {
    throw new Error('margins must be less than or equal to pageSize');
  }

  const printSettings = {
    requestID: getNextId(),
    landscape: checkType(options.landscape ?? false, 'boolean', 'landscape'),
    displayHeaderFooter: checkType(options.displayHeaderFooter ?? false, 'boolean', 'displayHeaderFooter'),
    headerTemplate: checkType(options.headerTemplate ?? '', 'string', 'headerTemplate'),
    footerTemplate: checkType(options.footerTemplate ?? '', 'string', 'footerTemplate'),
    printBackground: checkType(options.printBackground ?? false, 'boolean', 'printBackground'),
    scale: checkType(options.scale ?? 1.0, 'number', 'scale'),
    marginTop: checkType(margins.top ?? 0.4, 'number', 'margins.top'),
    marginBottom: checkType(margins.bottom ?? 0.4, 'number', 'margins.bottom'),
    marginLeft: checkType(margins.left ?? 0.4, 'number', 'margins.left'),
    marginRight: checkType(margins.right ?? 0.4, 'number', 'margins.right'),
    pageRanges: checkType(options.pageRanges ?? '', 'string', 'pageRanges'),
    preferCSSPageSize: checkType(options.preferCSSPageSize ?? false, 'boolean', 'preferCSSPageSize'),
    generateTaggedPDF: checkType(options.generateTaggedPDF ?? false, 'boolean', 'generateTaggedPDF'),
    generateDocumentOutline: checkType(options.generateDocumentOutline ?? false, 'boolean', 'generateDocumentOutline'),
    ...pageSize
  };

  if (this._printToPDF) {
    if (pendingPromise) {
      pendingPromise = pendingPromise.then(() => this._printToPDF(printSettings));
    } else {
      pendingPromise = this._printToPDF(printSettings);
    }
    return pendingPromise;
  } else {
    throw new Error('Printing feature is disabled');
  }
};

// TODO(codebytere): deduplicate argument sanitization by moving rest of
// print param logic into new file shared between printToPDF and print
WebContents.prototype.print = function (options: ElectronInternal.WebContentsPrintOptions = {}, callback) {
  if (typeof options !== 'object' || options == null) {
    throw new TypeError('webContents.print(): Invalid print settings specified.');
  }

  const pageSize = options.pageSize ?? 'A4';
  if (typeof pageSize === 'object') {
    if (!pageSize.height || !pageSize.width) {
      throw new Error('height and width properties are required for pageSize');
    }

    // Dimensions in Microns - 1 meter = 10^6 microns
    const height = Math.ceil(pageSize.height);
    const width = Math.ceil(pageSize.width);
    if (!isValidCustomPageSize(width, height)) {
      throw new RangeError('height and width properties must be minimum 352 microns.');
    }

    options.mediaSize = {
      name: 'CUSTOM',
      custom_display_name: 'Custom',
      height_microns: height,
      width_microns: width,
      imageable_area_left_microns: 0,
      imageable_area_bottom_microns: 0,
      imageable_area_right_microns: width,
      imageable_area_top_microns: height
    };
  } else if (typeof pageSize === 'string' && PDFPageSizes[pageSize]) {
    const mediaSize = PDFPageSizes[pageSize];
    options.mediaSize = {
      ...mediaSize,
      imageable_area_left_microns: 0,
      imageable_area_bottom_microns: 0,
      imageable_area_right_microns: mediaSize.width_microns,
      imageable_area_top_microns: mediaSize.height_microns
    };
  } else {
    throw new Error(`Unsupported pageSize: ${pageSize}`);
  }

  if (this._print) {
    if (callback) {
      this._print(options, callback);
    } else {
      this._print(options);
    }
  } else {
    console.error('Error: Printing feature is disabled.');
  }
};

WebContents.prototype.getPrintersAsync = async function () {
  // TODO(nornagon): this API has nothing to do with WebContents and should be
  // moved.
  if (printing.getPrinterListAsync) {
    return printing.getPrinterListAsync();
  } else {
    console.error('Error: Printing feature is disabled.');
    return [];
  }
};

WebContents.prototype.loadFile = function (filePath, options = {}) {
  if (typeof filePath !== 'string') {
    throw new TypeError('Must pass filePath as a string');
  }
  const { query, search, hash } = options;

  return this.loadURL(url.format({
    protocol: 'file',
    slashes: true,
    pathname: path.resolve(app.getAppPath(), filePath),
    query,
    search,
    hash
  }));
};

type LoadError = { errorCode: number, errorDescription: string, url: string };

WebContents.prototype.loadURL = function (url, options) {
  const p = new Promise<void>((resolve, reject) => {
    const resolveAndCleanup = () => {
      removeListeners();
      resolve();
    };
    let error: LoadError | undefined;
    const rejectAndCleanup = ({ errorCode, errorDescription, url }: LoadError) => {
      const err = new Error(`${errorDescription} (${errorCode}) loading '${typeof url === 'string' ? url.substr(0, 2048) : url}'`);
      Object.assign(err, { errno: errorCode, code: errorDescription, url });
      removeListeners();
      reject(err);
    };
    const finishListener = () => {
      if (error) {
        rejectAndCleanup(error);
      } else {
        resolveAndCleanup();
      }
    };

    let navigationStarted = false;
    let browserInitiatedInPageNavigation = false;
    const navigationListener = (event: Electron.Event, url: string, isSameDocument: boolean, isMainFrame: boolean) => {
      if (isMainFrame) {
        if (navigationStarted && !isSameDocument) {
          // the webcontents has started another unrelated navigation in the
          // main frame (probably from the app calling `loadURL` again); reject
          // the promise
          // We should only consider the request aborted if the "navigation" is
          // actually navigating and not simply transitioning URL state in the
          // current context.  E.g. pushState and `location.hash` changes are
          // considered navigation events but are triggered with isSameDocument.
          // We can ignore these to allow virtual routing on page load as long
          // as the routing does not leave the document
          return rejectAndCleanup({ errorCode: -3, errorDescription: 'ERR_ABORTED', url });
        }
        browserInitiatedInPageNavigation = navigationStarted && isSameDocument;
        navigationStarted = true;
      }
    };
    const failListener = (event: Electron.Event, errorCode: number, errorDescription: string, validatedURL: string, isMainFrame: boolean) => {
      if (!error && isMainFrame) {
        error = { errorCode, errorDescription, url: validatedURL };
      }
      if (!navigationStarted && isMainFrame) {
        finishListener();
      }
    };
    const stopLoadingListener = () => {
      // By the time we get here, either 'finish' or 'fail' should have fired
      // if the navigation occurred. However, in some situations (e.g. when
      // attempting to load a page with a bad scheme), loading will stop
      // without emitting finish or fail. In this case, we reject the promise
      // with a generic failure.
      // TODO(jeremy): enumerate all the cases in which this can happen. If
      // the only one is with a bad scheme, perhaps ERR_INVALID_ARGUMENT
      // would be more appropriate.
      if (!error) {
        error = { errorCode: -2, errorDescription: 'ERR_FAILED', url: url };
      }
      finishListener();
    };
    const finishListenerWhenUserInitiatedNavigation = () => {
      if (!browserInitiatedInPageNavigation) {
        finishListener();
      }
    };
    const removeListeners = () => {
      this.removeListener('did-finish-load', finishListener);
      this.removeListener('did-fail-load', failListener);
      this.removeListener('did-navigate-in-page', finishListenerWhenUserInitiatedNavigation);
      this.removeListener('did-start-navigation', navigationListener);
      this.removeListener('did-stop-loading', stopLoadingListener);
      this.removeListener('destroyed', stopLoadingListener);
    };
    this.on('did-finish-load', finishListener);
    this.on('did-fail-load', failListener);
    this.on('did-navigate-in-page', finishListenerWhenUserInitiatedNavigation);
    this.on('did-start-navigation', navigationListener);
    this.on('did-stop-loading', stopLoadingListener);
    this.on('destroyed', stopLoadingListener);
  });
  // Add a no-op rejection handler to silence the unhandled rejection error.
  p.catch(() => {});
  this._loadURL(url, options ?? {});
  return p;
};

WebContents.prototype.setWindowOpenHandler = function (handler: (details: Electron.HandlerDetails) => Electron.WindowOpenHandlerResponse) {
  this._windowOpenHandler = handler;
};

WebContents.prototype._callWindowOpenHandler = function (event: Electron.Event, details: Electron.HandlerDetails): {browserWindowConstructorOptions: BrowserWindowConstructorOptions | null, outlivesOpener: boolean, createWindow?: Electron.CreateWindowFunction} {
  const defaultResponse = {
    browserWindowConstructorOptions: null,
    outlivesOpener: false,
    createWindow: undefined
  };
  if (!this._windowOpenHandler) {
    return defaultResponse;
  }

  const response = this._windowOpenHandler(details);

  if (typeof response !== 'object') {
    event.preventDefault();
    console.error(`The window open handler response must be an object, but was instead of type '${typeof response}'.`);
    return defaultResponse;
  }

  if (response === null) {
    event.preventDefault();
    console.error('The window open handler response must be an object, but was instead null.');
    return defaultResponse;
  }

  if (response.action === 'deny') {
    event.preventDefault();
    return defaultResponse;
  } else if (response.action === 'allow') {
    return {
      browserWindowConstructorOptions: typeof response.overrideBrowserWindowOptions === 'object' ? response.overrideBrowserWindowOptions : null,
      outlivesOpener: typeof response.outlivesOpener === 'boolean' ? response.outlivesOpener : false,
      createWindow: typeof response.createWindow === 'function' ? response.createWindow : undefined
    };
  } else {
    event.preventDefault();
    console.error('The window open handler response must be an object with an \'action\' property of \'allow\' or \'deny\'.');
    return defaultResponse;
  }
};

const addReplyToEvent = (event: Electron.IpcMainEvent) => {
  const { processId, frameId } = event;
  event.reply = (channel: string, ...args: any[]) => {
    event.sender.sendToFrame([processId, frameId], channel, ...args);
  };
};

const addSenderToEvent = (event: Electron.IpcMainEvent | Electron.IpcMainInvokeEvent, sender: Electron.WebContents) => {
  event.sender = sender;
  const { processId, frameId } = event;
  Object.defineProperty(event, 'senderFrame', {
    get: () => webFrameMain.fromId(processId, frameId)
  });
};

const addReturnValueToEvent = (event: Electron.IpcMainEvent) => {
  Object.defineProperty(event, 'returnValue', {
    set: (value) => event._replyChannel.sendReply(value),
    get: () => {}
  });
};

const getWebFrameForEvent = (event: Electron.IpcMainEvent | Electron.IpcMainInvokeEvent) => {
  if (!event.processId || !event.frameId) return null;
  return webFrameMainBinding.fromIdOrNull(event.processId, event.frameId);
};

const commandLine = process._linkedBinding('electron_common_command_line');
const environment = process._linkedBinding('electron_common_environment');

const loggingEnabled = () => {
  return environment.hasVar('ELECTRON_ENABLE_LOGGING') || commandLine.hasSwitch('enable-logging');
};
// Deprecation warnings for navigation related APIs.
const canGoBackDeprecated = deprecate.warnOnce('webContents.canGoBack', 'webContents.navigationHistory.canGoBack');
WebContents.prototype.canGoBack = function () {
  canGoBackDeprecated();
  return this._canGoBack();
};

const canGoForwardDeprecated = deprecate.warnOnce('webContents.canGoForward', 'webContents.navigationHistory.canGoForward');
WebContents.prototype.canGoForward = function () {
  canGoForwardDeprecated();
  return this._canGoForward();
};

const canGoToOffsetDeprecated = deprecate.warnOnce('webContents.canGoToOffset', 'webContents.navigationHistory.canGoToOffset');
WebContents.prototype.canGoToOffset = function () {
  canGoToOffsetDeprecated();
  return this._canGoToOffset();
};

const clearHistoryDeprecated = deprecate.warnOnce('webContents.clearHistory', 'webContents.navigationHistory.clear');
WebContents.prototype.clearHistory = function () {
  clearHistoryDeprecated();
  return this._clearHistory();
};

const goBackDeprecated = deprecate.warnOnce('webContents.goBack', 'webContents.navigationHistory.goBack');
WebContents.prototype.goBack = function () {
  goBackDeprecated();
  return this._goBack();
};

const goForwardDeprecated = deprecate.warnOnce('webContents.goForward', 'webContents.navigationHistory.goForward');
WebContents.prototype.goForward = function () {
  goForwardDeprecated();
  return this._goForward();
};

const goToIndexDeprecated = deprecate.warnOnce('webContents.goToIndex', 'webContents.navigationHistory.goToIndex');
WebContents.prototype.goToIndex = function (index: number) {
  goToIndexDeprecated();
  return this._goToIndex(index);
};

const goToOffsetDeprecated = deprecate.warnOnce('webContents.goToOffset', 'webContents.navigationHistory.goToOffset');
WebContents.prototype.goToOffset = function (index: number) {
  goToOffsetDeprecated();
  return this._goToOffset(index);
};

// Add JavaScript wrappers for WebContents class.
WebContents.prototype._init = function () {
  const prefs = this.getLastWebPreferences() || {};
  if (!prefs.nodeIntegration && prefs.preload != null && prefs.sandbox == null) {
    deprecate.log('The default sandbox option for windows without nodeIntegration is changing. Presently, by default, when a window has a preload script, it defaults to being unsandboxed. In Electron 20, this default will be changing, and all windows that have nodeIntegration: false (which is the default) will be sandboxed by default. If your preload script doesn\'t use Node, no action is needed. If your preload script does use Node, either refactor it to move Node usage to the main process, or specify sandbox: false in your WebPreferences.');
  }
  // Read off the ID at construction time, so that it's accessible even after
  // the underlying C++ WebContents is destroyed.
  const id = this.id;
  Object.defineProperty(this, 'id', {
    value: id,
    writable: false
  });

  this._windowOpenHandler = null;

  const ipc = new IpcMainImpl();
  Object.defineProperty(this, 'ipc', {
    get () { return ipc; },
    enumerable: true
  });

  // Add navigationHistory property which handles session history,
  // maintaining a list of navigation entries for backward and forward navigation.
  Object.defineProperty(this, 'navigationHistory', {
    value: {
      canGoBack: this._canGoBack.bind(this),
      canGoForward: this._canGoForward.bind(this),
      canGoToOffset: this._canGoToOffset.bind(this),
      clear: this._clearHistory.bind(this),
      goBack: this._goBack.bind(this),
      goForward: this._goForward.bind(this),
      goToIndex: this._goToIndex.bind(this),
      goToOffset: this._goToOffset.bind(this),
      getActiveIndex: this._getActiveIndex.bind(this),
      length: this._historyLength.bind(this),
      getEntryAtIndex: this._getNavigationEntryAtIndex.bind(this),
      removeEntryAtIndex: this._removeNavigationEntryAtIndex.bind(this),
      getAllEntries: this._getHistory.bind(this)
    },
    writable: false,
    enumerable: true
  });

  // Dispatch IPC messages to the ipc module.
  this.on('-ipc-message' as any, function (this: Electron.WebContents, event: Electron.IpcMainEvent, internal: boolean, channel: string, args: any[]) {
    addSenderToEvent(event, this);
    if (internal) {
      ipcMainInternal.emit(channel, event, ...args);
    } else {
      addReplyToEvent(event);
      this.emit('ipc-message', event, channel, ...args);
      const maybeWebFrame = getWebFrameForEvent(event);
      maybeWebFrame && maybeWebFrame.ipc.emit(channel, event, ...args);
      ipc.emit(channel, event, ...args);
      ipcMain.emit(channel, event, ...args);
    }
  });

  this.on('-ipc-invoke' as any, async function (this: Electron.WebContents, event: Electron.IpcMainInvokeEvent, internal: boolean, channel: string, args: any[]) {
    addSenderToEvent(event, this);
    const replyWithResult = (result: any) => event._replyChannel.sendReply({ result });
    const replyWithError = (error: Error) => {
      console.error(`Error occurred in handler for '${channel}':`, error);
      event._replyChannel.sendReply({ error: error.toString() });
    };
    const maybeWebFrame = getWebFrameForEvent(event);
    const targets: (ElectronInternal.IpcMainInternal| undefined)[] = internal ? [ipcMainInternal] : [maybeWebFrame?.ipc, ipc, ipcMain];
    const target = targets.find(target => target && (target as any)._invokeHandlers.has(channel));
    if (target) {
      const handler = (target as any)._invokeHandlers.get(channel);
      try {
        replyWithResult(await Promise.resolve(handler(event, ...args)));
      } catch (err) {
        replyWithError(err as Error);
      }
    } else {
      replyWithError(new Error(`No handler registered for '${channel}'`));
    }
  });

  this.on('-ipc-message-sync' as any, function (this: Electron.WebContents, event: Electron.IpcMainEvent, internal: boolean, channel: string, args: any[]) {
    addSenderToEvent(event, this);
    addReturnValueToEvent(event);
    if (internal) {
      ipcMainInternal.emit(channel, event, ...args);
    } else {
      addReplyToEvent(event);
      const maybeWebFrame = getWebFrameForEvent(event);
      if (this.listenerCount('ipc-message-sync') === 0 && ipc.listenerCount(channel) === 0 && ipcMain.listenerCount(channel) === 0 && (!maybeWebFrame || maybeWebFrame.ipc.listenerCount(channel) === 0)) {
        console.warn(`WebContents #${this.id} called ipcRenderer.sendSync() with '${channel}' channel without listeners.`);
      }
      this.emit('ipc-message-sync', event, channel, ...args);
      maybeWebFrame && maybeWebFrame.ipc.emit(channel, event, ...args);
      ipc.emit(channel, event, ...args);
      ipcMain.emit(channel, event, ...args);
    }
  });

  this.on('-ipc-ports' as any, function (this: Electron.WebContents, event: Electron.IpcMainEvent, internal: boolean, channel: string, message: any, ports: any[]) {
    addSenderToEvent(event, this);
    event.ports = ports.map(p => new MessagePortMain(p));
    const maybeWebFrame = getWebFrameForEvent(event);
    maybeWebFrame && maybeWebFrame.ipc.emit(channel, event, message);
    ipc.emit(channel, event, message);
    ipcMain.emit(channel, event, message);
  });

  this.on('render-process-gone', (event, details) => {
    app.emit('render-process-gone', event, this, details);

    // Log out a hint to help users better debug renderer crashes.
    if (loggingEnabled()) {
      console.info(`Renderer process ${details.reason} - see https://www.electronjs.org/docs/tutorial/application-debugging for potential debugging information.`);
    }
  });

  this.on('-before-unload-fired' as any, function (this: Electron.WebContents, event: Electron.Event, proceed: boolean) {
    const type = this.getType();
    // These are the "interactive" types, i.e. ones a user might be looking at.
    // All other types should ignore the "proceed" signal and unload
    // regardless.
    if (type === 'window' || type === 'offscreen' || type === 'browserView') {
      if (!proceed) { return event.preventDefault(); }
    }
  });

  // The devtools requests the webContents to reload.
  this.on('devtools-reload-page', function (this: Electron.WebContents) {
    this.reload();
  });

  if (this.getType() !== 'remote') {
    // Make new windows requested by links behave like "window.open".
    this.on('-new-window' as any, (event: Electron.Event, url: string, frameName: string, disposition: Electron.HandlerDetails['disposition'],
      rawFeatures: string, referrer: Electron.Referrer, postData: PostData) => {
      const postBody = postData ? {
        data: postData,
        ...parseContentTypeFormat(postData)
      } : undefined;
      const details: Electron.HandlerDetails = {
        url,
        frameName,
        features: rawFeatures,
        referrer,
        postBody,
        disposition
      };

      let result: ReturnType<typeof this._callWindowOpenHandler>;
      try {
        result = this._callWindowOpenHandler(event, details);
      } catch (err) {
        event.preventDefault();
        throw err;
      }

      const options = result.browserWindowConstructorOptions;
      if (!event.defaultPrevented) {
        openGuestWindow({
          embedder: this,
          disposition,
          referrer,
          postData,
          overrideBrowserWindowOptions: options || {},
          windowOpenArgs: details,
          outlivesOpener: result.outlivesOpener,
          createWindow: result.createWindow
        });
      }
    });

    let windowOpenOverriddenOptions: BrowserWindowConstructorOptions | null = null;
    let windowOpenOutlivesOpenerOption: boolean = false;
    let createWindow: Electron.CreateWindowFunction | undefined;

    this.on('-will-add-new-contents' as any, (event: Electron.Event, url: string, frameName: string, rawFeatures: string, disposition: Electron.HandlerDetails['disposition'], referrer: Electron.Referrer, postData: PostData) => {
      const postBody = postData ? {
        data: postData,
        ...parseContentTypeFormat(postData)
      } : undefined;
      const details: Electron.HandlerDetails = {
        url,
        frameName,
        features: rawFeatures,
        disposition,
        referrer,
        postBody
      };

      let result: ReturnType<typeof this._callWindowOpenHandler>;
      try {
        result = this._callWindowOpenHandler(event, details);
      } catch (err) {
        event.preventDefault();
        throw err;
      }

      windowOpenOutlivesOpenerOption = result.outlivesOpener;
      windowOpenOverriddenOptions = result.browserWindowConstructorOptions;
      createWindow = result.createWindow;
      if (!event.defaultPrevented) {
        const secureOverrideWebPreferences = windowOpenOverriddenOptions ? {
          // Allow setting of backgroundColor as a webPreference even though
          // it's technically a BrowserWindowConstructorOptions option because
          // we need to access it in the renderer at init time.
          backgroundColor: windowOpenOverriddenOptions.backgroundColor,
          transparent: windowOpenOverriddenOptions.transparent,
          ...windowOpenOverriddenOptions.webPreferences
        } : undefined;
        const { webPreferences: parsedWebPreferences } = parseFeatures(rawFeatures);
        const webPreferences = makeWebPreferences({
          embedder: this,
          insecureParsedWebPreferences: parsedWebPreferences,
          secureOverrideWebPreferences
        });
        windowOpenOverriddenOptions = {
          ...windowOpenOverriddenOptions,
          webPreferences
        };
        this._setNextChildWebPreferences(webPreferences);
      }
    });

    // Create a new browser window for "window.open"
    this.on('-add-new-contents' as any, (event: Electron.Event, webContents: Electron.WebContents, disposition: string,
      _userGesture: boolean, _left: number, _top: number, _width: number, _height: number, url: string, frameName: string,
      referrer: Electron.Referrer, rawFeatures: string, postData: PostData) => {
      const overriddenOptions = windowOpenOverriddenOptions || undefined;
      const outlivesOpener = windowOpenOutlivesOpenerOption;
      const windowOpenFunction = createWindow;

      createWindow = undefined;
      windowOpenOverriddenOptions = null;
      // false is the default
      windowOpenOutlivesOpenerOption = false;

      if ((disposition !== 'foreground-tab' && disposition !== 'new-window' &&
           disposition !== 'background-tab')) {
        event.preventDefault();
        return;
      }

      openGuestWindow({
        embedder: this,
        guest: webContents,
        overrideBrowserWindowOptions: overriddenOptions,
        disposition,
        referrer,
        postData,
        windowOpenArgs: {
          url,
          frameName,
          features: rawFeatures
        },
        outlivesOpener,
        createWindow: windowOpenFunction
      });
    });
  }

  this.on('login', (event, ...args) => {
    app.emit('login', event, this, ...args);
  });

  this.on('ready-to-show' as any, () => {
    const owner = this.getOwnerBrowserWindow();
    if (owner && !owner.isDestroyed()) {
      process.nextTick(() => {
        owner.emit('ready-to-show');
      });
    }
  });

  this.on('select-bluetooth-device', (event, devices, callback) => {
    if (this.listenerCount('select-bluetooth-device') === 1) {
      // Cancel it if there are no handlers
      event.preventDefault();
      callback('');
    }
  });

  const originCounts = new Map<string, number>();
  const openDialogs = new Set<AbortController>();
  this.on('-run-dialog' as any, async (info: {frame: WebFrameMain, dialogType: 'prompt' | 'confirm' | 'alert', messageText: string, defaultPromptText: string}, callback: (success: boolean, user_input: string) => void) => {
    const originUrl = new URL(info.frame.url);
    const origin = originUrl.protocol === 'file:' ? originUrl.href : originUrl.origin;
    if ((originCounts.get(origin) ?? 0) < 0) return callback(false, '');

    const prefs = this.getLastWebPreferences();
    if (!prefs || prefs.disableDialogs) return callback(false, '');

    // We don't support prompt() for some reason :)
    if (info.dialogType === 'prompt') return callback(false, '');

    originCounts.set(origin, (originCounts.get(origin) ?? 0) + 1);

    // TODO: translate?
    const checkbox = originCounts.get(origin)! > 1 && prefs.safeDialogs ? prefs.safeDialogsMessage || 'Prevent this app from creating additional dialogs' : '';
    const parent = this.getOwnerBrowserWindow();
    const abortController = new AbortController();
    const options: MessageBoxOptions = {
      message: info.messageText,
      checkboxLabel: checkbox,
      signal: abortController.signal,
      ...(info.dialogType === 'confirm') ? {
        buttons: ['OK', 'Cancel'],
        defaultId: 0,
        cancelId: 1
      } : {
        buttons: ['OK'],
        defaultId: -1, // No default button
        cancelId: 0
      }
    };
    openDialogs.add(abortController);
    const promise = parent && !prefs.offscreen ? dialog.showMessageBox(parent, options) : dialog.showMessageBox(options);
    try {
      const result = await promise;
      if (abortController.signal.aborted || this.isDestroyed()) return;
      if (result.checkboxChecked) originCounts.set(origin, -1);
      return callback(result.response === 0, '');
    } finally {
      openDialogs.delete(abortController);
    }
  });

  this.on('-cancel-dialogs' as any, () => {
    for (const controller of openDialogs) { controller.abort(); }
    openDialogs.clear();
  });

  app.emit('web-contents-created', { sender: this, preventDefault () {}, get defaultPrevented () { return false; } }, this);

  // Properties

  Object.defineProperty(this, 'audioMuted', {
    get: () => this.isAudioMuted(),
    set: (muted) => this.setAudioMuted(muted)
  });

  Object.defineProperty(this, 'userAgent', {
    get: () => this.getUserAgent(),
    set: (agent) => this.setUserAgent(agent)
  });

  Object.defineProperty(this, 'zoomLevel', {
    get: () => this.getZoomLevel(),
    set: (level) => this.setZoomLevel(level)
  });

  Object.defineProperty(this, 'zoomFactor', {
    get: () => this.getZoomFactor(),
    set: (factor) => this.setZoomFactor(factor)
  });

  Object.defineProperty(this, 'frameRate', {
    get: () => this.getFrameRate(),
    set: (rate) => this.setFrameRate(rate)
  });

  Object.defineProperty(this, 'backgroundThrottling', {
    get: () => this.getBackgroundThrottling(),
    set: (allowed) => this.setBackgroundThrottling(allowed)
  });
};

// Public APIs.
export function create (options = {}): Electron.WebContents {
  return new (WebContents as any)(options);
}

export function fromId (id: string) {
  return binding.fromId(id);
}

export function fromFrame (frame: Electron.WebFrameMain) {
  return binding.fromFrame(frame);
}

export function fromDevToolsTargetId (targetId: string) {
  return binding.fromDevToolsTargetId(targetId);
}

export function getFocusedWebContents () {
  let focused = null;
  for (const contents of binding.getAllWebContents()) {
    if (!contents.isFocused()) continue;
    if (focused == null) focused = contents;
    // Return webview web contents which may be embedded inside another
    // web contents that is also reporting as focused
    if (contents.getType() === 'webview') return contents;
  }
  return focused;
}
export function getAllWebContents () {
  return binding.getAllWebContents();
}
