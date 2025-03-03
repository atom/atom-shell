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
    _clientCertRequestPasswordHandler: ((params: ClientCertRequestParams) => Promise<string>) | null;
    on(event: '-client-certificate-request-password', listener: (event: Event<ClientCertRequestParams>, callback: (password: string) => void) => Promise<void>): this;
  }

  interface AutoUpdater {
    isVersionAllowedForUpdate?(currentVersion: string, targetVersion: string): boolean;
  }

  type TouchBarItemType = NonNullable<Electron.TouchBarConstructorOptions['items']>[0];

  interface BaseWindow {
    _init(): void;
    _touchBar: Electron.TouchBar | null;
    _setTouchBarItems: (items: TouchBarItemType[]) => void;
    _setEscapeTouchBarItem: (item: TouchBarItemType | {}) => void;
    _refreshTouchBarItem: (itemID: string) => void;
    on(event: '-touch-bar-interaction', listener: (event: Event, itemID: string, details: any) => void): this;
    removeListener(event: '-touch-bar-interaction', listener: (event: Event, itemID: string, details: any) => void): this;
  }

  interface BrowserWindow extends BaseWindow {
    _init(): void;
    _getWindowButtonVisibility: () => boolean;
    _getAlwaysOnTopLevel: () => string;
    devToolsWebContents: WebContents;
    frameName: string;
    _browserViews: BrowserView[];
    on(event: '-touch-bar-interaction', listener: (event: Event, itemID: string, details: any) => void): this;
    removeListener(event: '-touch-bar-interaction', listener: (event: Event, itemID: string, details: any) => void): this;
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
    }
  }

  interface ServiceWorkers {
    _getWorkerFromVersionIDIfExists(versionId: number): Electron.ServiceWorkerMain | undefined;
    _stopAllWorkers(): Promise<void>;
  }

  interface ServiceWorkerMain {
    _send(internal: boolean, channel: string, args: any): void;
    _startExternalRequest(hasTimeout: boolean): { id: string, ok: boolean };
    _finishExternalRequest(uuid: string): void;
    _countExternalRequests(): number;
  }

  interface Session {
    _init(): void;
  }

  interface TouchBar {
    _removeFromWindow: (win: BaseWindow) => void;
  }

  interface WebContents {
    _awaitNextLoad(expectedUrl: string): Promise<void>;
    _loadURL(url: string, options: ElectronInternal.LoadURLOptions): void;
    getOwnerBrowserWindow(): Electron.BrowserWindow | null;
    getLastWebPreferences(): Electron.WebPreferences | null;
    _getProcessMemoryInfo(): Electron.ProcessMemoryInfo;
    _getPreloadScript(): Electron.PreloadScript | null;
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
    _getNavigationEntryAtIndex(index: number): Electron.NavigationEntry | null;
    _getActiveIndex(): number;
    _historyLength(): number;
    _canGoBack(): boolean;
    _canGoForward(): boolean;
    _canGoToOffset(index: number): boolean;
    _goBack(): void;
    _goForward(): void;
    _goToOffset(index: number): void;
    _goToIndex(index: number): void;
    _removeNavigationEntryAtIndex(index: number): boolean;
    _getHistory(): Electron.NavigationEntry[];
    _restoreHistory(index: number, entries: Electron.NavigationEntry[]): void
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
    _lifecycleStateForTesting: string;
  }

  interface WebFrame {
    _isEvalAllowed(): boolean;
  }

  interface WebPreferences {
    disablePopups?: boolean;
    embedder?: Electron.WebContents;
    type?: 'backgroundPage' | 'window' | 'browserView' | 'remote' | 'webview' | 'offscreen';
  }

  interface Session {
    _setDisplayMediaRequestHandler: Electron.Session['setDisplayMediaRequestHandler'];
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
    popupAt(window: BaseWindow, frame: WebFrameMain | undefined, x: number, y: number, positioning: number, sourceType: Required<Electron.PopupOptions>['sourceType'], callback: () => void): void;
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
    frameTreeNodeId?: number;
  }

  interface IpcMainInvokeEvent {
    _replyChannel: ReplyChannel;
    frameTreeNodeId?: number;
  }

  interface IpcMainServiceWorkerEvent {
    _replyChannel: ReplyChannel;
  }

  interface IpcMainServiceWorkerInvokeEvent {
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

  interface WebContents {
    on(event: '-new-window', listener: (event: Electron.Event, url: string, frameName: string, disposition: Electron.HandlerDetails['disposition'],
      rawFeatures: string, referrer: Electron.Referrer, postData: LoadURLOptions['postData']) => void): this;
    on(event: '-add-new-contents', listener: (event: Event, webContents: Electron.WebContents, disposition: string,
      _userGesture: boolean, _left: number, _top: number, _width: number, _height: number, url: string, frameName: string,
      referrer: Electron.Referrer, rawFeatures: string, postData: LoadURLOptions['postData']) => void): this;
    on(event: '-will-add-new-contents', listener: (event: Electron.Event, url: string, frameName: string, rawFeatures: string, disposition: Electron.HandlerDetails['disposition'], referrer: Electron.Referrer, postData: LoadURLOptions['postData']) => void): this;
    on(event: '-ipc-message', listener: (event: Electron.IpcMainEvent, internal: boolean, channel: string, args: any[]) => void): this;
    on(event: '-ipc-message-sync', listener: (event: Electron.IpcMainEvent, internal: boolean, channel: string, args: any[]) => void): this;
    on(event: '-ipc-invoke', listener: (event: Electron.IpcMainInvokeEvent, internal: boolean, channel: string, args: any[]) => void): this;
    on(event: '-ipc-ports', listener: (event: Electron.IpcMainEvent, internal: boolean, channel: string, message: any, ports: any[]) => void): this;
    on(event: '-run-dialog', listener: (info: {frame: WebFrameMain, dialogType: 'prompt' | 'confirm' | 'alert', messageText: string, defaultPromptText: string}, callback: (success: boolean, user_input: string) => void) => void): this;
    on(event: '-cancel-dialogs', listener: () => void): this;
    on(event: 'ready-to-show', listener: () => void): this;
    on(event: '-before-unload-fired', listener: (event: Electron.Event, proceed: boolean) => void): this;

    on(event: '-window-visibility-change', listener: (visibilityState: 'hidden' | 'visible') => void): this;
    removeListener(event: '-window-visibility-change', listener: (visibilityState: 'hidden' | 'visible') => void): this;

    once(event: 'destroyed', listener: (event: Electron.Event) => void): this;
  }

  interface WebContentsWillFrameNavigateEventParams {
    processId: number;
    routingId: number;
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

  type IpcMainInternalEvent = Omit<Electron.IpcMainEvent, 'reply'> | Omit<Electron.IpcMainServiceWorkerEvent, 'reply'>;
  type IpcMainInternalInvokeEvent = Electron.IpcMainInvokeEvent | Electron.IpcMainServiceWorkerInvokeEvent;

  interface IpcMainInternal extends NodeJS.EventEmitter {
    handle(channel: string, listener: (event: IpcMainInternalInvokeEvent, ...args: any[]) => Promise<any> | any): void;
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

  interface PreloadScript extends Electron.PreloadScript {
    contents?: string;
    error?: Error;
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
