/// <reference path="../electron.d.ts" />

/**
 * This file augments the Electron TS namespace with the internal APIs
 * that are not documented but are used by Electron internally
 */

declare namespace Electron {
  enum ProcessType {
    browser = 'browser',
    renderer = 'renderer',
    worker = 'worker',
    utility = 'utility'
  }

  interface App {
    setVersion(version: string): void;
    setDesktopName(name: string): void;
    setAppPath(path: string | null): void;
  }

  interface AutoUpdater {
    isVersionAllowedForUpdate?(currentVersion: string, targetVersion: string): boolean;
  }

  type TouchBarItemType = NonNullable<Electron.TouchBarConstructorOptions['items']>[0];

  interface BaseWindow {
    _init(): void;
  }

  interface BrowserWindow {
    _init(): void;
    _touchBar: Electron.TouchBar | null;
    _setTouchBarItems: (items: TouchBarItemType[]) => void;
    _setEscapeTouchBarItem: (item: TouchBarItemType | {}) => void;
    _refreshTouchBarItem: (itemID: string) => void;
    _getWindowButtonVisibility: () => boolean;
    _getAlwaysOnTopLevel: () => string;
    devToolsWebContents: WebContents;
    frameName: string;
    on(event: '-touch-bar-interaction', listener: (event: Event, itemID: string, details: any) => void): this;
    removeListener(event: '-touch-bar-interaction', listener: (event: Event, itemID: string, details: any) => void): this;

    _browserViews: BrowserView[];
  }

  interface BrowserView {
    ownerWindow: BrowserWindow | null
    webContentsView: WebContentsView
  }

  interface BrowserWindowConstructorOptions {
    webContents?: WebContents;
  }

  interface ContextBridge {
    internalContextBridge?: {
      contextIsolationEnabled: boolean;
      overrideGlobalValueFromIsolatedWorld(keys: string[], value: any): void;
      overrideGlobalValueWithDynamicPropsFromIsolatedWorld(keys: string[], value: any): void;
      overrideGlobalPropertyFromIsolatedWorld(keys: string[], getter: Function, setter?: Function): void;
      isInMainWorld(): boolean;
    }
  }

  interface TouchBar {
    _removeFromWindow: (win: BrowserWindow) => void;
  }

  interface WebContents {
    _loadURL(url: string, options: ElectronInternal.LoadURLOptions): void;
    getOwnerBrowserWindow(): Electron.BrowserWindow | null;
    getLastWebPreferences(): Electron.WebPreferences | null;
    _getProcessMemoryInfo(): Electron.ProcessMemoryInfo;
    _getPreloadPaths(): string[];
    equal(other: WebContents): boolean;
    browserWindowOptions: BrowserWindowConstructorOptions;
    _windowOpenHandler: ((details: Electron.HandlerDetails) => any) | null;
    _callWindowOpenHandler(event: any, details: Electron.HandlerDetails): {browserWindowConstructorOptions: Electron.BrowserWindowConstructorOptions | null, outlivesOpener: boolean, createWindow?: Electron.CreateWindowFunction};
    _setNextChildWebPreferences(prefs: Partial<Electron.BrowserWindowConstructorOptions['webPreferences']> & Pick<Electron.BrowserWindowConstructorOptions, 'backgroundColor'>): void;
    _send(internal: boolean, channel: string, args: any): boolean;
    _sendInternal(channel: string, ...args: any[]): void;
    _printToPDF(options: any): Promise<Buffer>;
    _print(options: any, callback?: (success: boolean, failureReason: string) => void): void;
    _getPrintersAsync(): Promise<Electron.PrinterInfo[]>;
    _init(): void;
    _getNavigationEntryAtIndex(index: number): Electron.EntryAtIndex | null;
    _getActiveIndex(): number;
    _historyLength(): number;
    _canGoBack(): boolean;
    _canGoForward(): boolean;
    _canGoToOffset(): boolean;
    _goBack(): void;
    _goForward(): void;
    _goToOffset(index: number): void;
    _goToIndex(index: number): void;
    _clearHistory():void
    canGoToIndex(index: number): boolean;
    destroy(): void;
    // <webview>
    attachToIframe(embedderWebContents: Electron.WebContents, embedderFrameId: number): void;
    detachFromOuterFrame(): void;
    setEmbedder(embedder: Electron.WebContents): void;
    viewInstanceId: number;
    _setOwnerWindow(w: BaseWindow | null): void;
  }

  interface WebFrameMain {
    _send(internal: boolean, channel: string, args: any): void;
    _sendInternal(channel: string, ...args: any[]): void;
    _postMessage(channel: string, message: any, transfer?: any[]): void;
  }

  interface WebFrame {
    _isEvalAllowed(): boolean;
  }

  interface WebPreferences {
    disablePopups?: boolean;
    embedder?: Electron.WebContents;
    type?: 'backgroundPage' | 'window' | 'browserView' | 'remote' | 'webview' | 'offscreen';
  }

  type CreateWindowFunction = (options: BrowserWindowConstructorOptions) => WebContents;

  interface Menu {
    _init(): void;
    _isCommandIdChecked(id: string): boolean;
    _isCommandIdEnabled(id: string): boolean;
    _shouldCommandIdWorkWhenHidden(id: string): boolean;
    _isCommandIdVisible(id: string): boolean;
    _getAcceleratorForCommandId(id: string, useDefaultAccelerator: boolean): Accelerator | undefined;
    _shouldRegisterAcceleratorForCommandId(id: string): boolean;
    _getSharingItemForCommandId(id: string): SharingItem | null;
    _callMenuWillShow(): void;
    _executeCommand(event: KeyboardEvent, id: number): void;
    _menuWillShow(): void;
    commandsMap: Record<string, MenuItem>;
    groupsMap: Record<string, MenuItem[]>;
    getItemCount(): number;
    popupAt(window: BaseWindow, x: number, y: number, positioning: number, sourceType: Required<Electron.PopupOptions>['sourceType'], callback: () => void): void;
    closePopupAt(id: number): void;
    setSublabel(index: number, label: string): void;
    setToolTip(index: number, tooltip: string): void;
    setIcon(index: number, image: string | NativeImage): void;
    setRole(index: number, role: string): void;
    insertItem(index: number, commandId: number, label: string): void;
    insertCheckItem(index: number, commandId: number, label: string): void;
    insertRadioItem(index: number, commandId: number, label: string, groupId: number): void;
    insertSeparator(index: number): void;
    insertSubMenu(index: number, commandId: number, label: string, submenu?: Menu): void;
    delegate?: any;
    _getAcceleratorTextAt(index: number): string;
  }

  interface MenuItem {
    overrideReadOnlyProperty(property: string, value: any): void;
    groupId: number;
    getDefaultRoleAccelerator(): Accelerator | undefined;
    getCheckStatus(): boolean;
    acceleratorWorksWhenHidden?: boolean;
  }

  interface ReplyChannel {
    sendReply(value: any): void;
  }

  interface IpcMainEvent {
    _replyChannel: ReplyChannel;
  }

  interface IpcMainInvokeEvent {
    _replyChannel: ReplyChannel;
  }

  // Deprecated / undocumented BrowserWindow methods
  interface BrowserWindow {
    getURL(): string;
    send(channel: string, ...args: any[]): void;
    openDevTools(options?: Electron.OpenDevToolsOptions): void;
    closeDevTools(): void;
    isDevToolsOpened(): void;
    isDevToolsFocused(): void;
    toggleDevTools(): void;
    inspectElement(x: number, y: number): void;
    inspectSharedWorker(): void;
    inspectServiceWorker(): void;
    getBackgroundThrottling(): void;
    setBackgroundThrottling(allowed: boolean): void;
  }

  interface Protocol {
    registerProtocol(scheme: string, handler: any): boolean;
    interceptProtocol(scheme: string, handler: any): boolean;
  }
}

declare namespace ElectronInternal {
  interface DesktopCapturer {
    startHandling(captureWindow: boolean, captureScreen: boolean, thumbnailSize: Electron.Size, fetchWindowIcons: boolean): void;
    _onerror?: (error: string) => void;
    _onfinished?: (sources: Electron.DesktopCapturerSource[], fetchWindowIcons: boolean) => void;
  }

  interface GetSourcesOptions {
    captureWindow: boolean;
    captureScreen: boolean;
    thumbnailSize: Electron.Size;
    fetchWindowIcons: boolean;
  }

  interface GetSourcesResult {
    id: string;
    name: string;
    thumbnail: Electron.NativeImage;
    display_id: string;
    appIcon: Electron.NativeImage | null;
  }

  interface IpcRendererInternal extends NodeJS.EventEmitter, Pick<Electron.IpcRenderer, 'send' | 'sendSync' | 'invoke'> {
    invoke<T>(channel: string, ...args: any[]): Promise<T>;
  }

  interface IpcMainInternalEvent extends Omit<Electron.IpcMainEvent, 'reply'> {
  }

  interface IpcMainInternal extends NodeJS.EventEmitter {
    handle(channel: string, listener: (event: Electron.IpcMainInvokeEvent, ...args: any[]) => Promise<any> | any): void;
    on(channel: string, listener: (event: IpcMainInternalEvent, ...args: any[]) => void): this;
    once(channel: string, listener: (event: IpcMainInternalEvent, ...args: any[]) => void): this;
  }

  interface LoadURLOptions extends Electron.LoadURLOptions {
    reloadIgnoringCache?: boolean;
  }

  interface WebContentsPrintOptions extends Electron.WebContentsPrintOptions {
    mediaSize?: MediaSize;
  }

  type MediaSize = {
    name: string,
    custom_display_name: string,
    height_microns: number,
    width_microns: number,
    imageable_area_left_microns?: number,
    imageable_area_bottom_microns?: number,
    imageable_area_right_microns?: number,
    imageable_area_top_microns?: number,
    is_default?: 'true',
  }

  type PageSize = {
    width: number,
    height: number,
  }

  type ModuleLoader = () => any;

  interface ModuleEntry {
    name: string;
    loader: ModuleLoader;
  }

  interface UtilityProcessWrapper extends NodeJS.EventEmitter {
    readonly pid: (number) | (undefined);
    kill(): boolean;
    postMessage(message: any, transfer?: any[]): void;
  }

  interface ParentPort extends NodeJS.EventEmitter {
    start(): void;
    pause(): void;
    postMessage(message: any): void;
  }

  class WebViewElement extends HTMLElement {
    static observedAttributes: Array<string>;

    public contentWindow: Window;

    public connectedCallback?(): void;
    public attributeChangedCallback?(): void;
    public disconnectedCallback?(): void;

    // Created in web-view-impl
    public getWebContentsId(): number;
    public capturePage(rect?: Electron.Rectangle): Promise<Electron.NativeImage>;
  }

  class WebContents extends Electron.WebContents {
    static create(opts?: Electron.WebPreferences): Electron.WebContents;
  }
}

declare namespace Chrome {
  namespace Tabs {
    // https://developer.chrome.com/docs/extensions/tabs#method-executeScript
    interface ExecuteScriptDetails {
      code?: string;
      file?: string;
      allFrames?: boolean;
      frameId?: number;
      matchAboutBlank?: boolean;
      runAt?: 'document-start' | 'document-end' | 'document_idle';
      cssOrigin: 'author' | 'user';
    }

    type ExecuteScriptCallback = (result: Array<any>) => void;

    // https://developer.chrome.com/docs/extensions/tabs#method-sendMessage
    interface SendMessageDetails {
      frameId?: number;
    }

    type SendMessageCallback = (result: any) => void;
  }
}

interface Global extends NodeJS.Global {
  require: NodeRequire;
  module: NodeModule;
  __filename: string;
  __dirname: string;
}
