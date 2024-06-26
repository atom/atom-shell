import {
  app,
  autoUpdater,
  BrowserWindow,
  contentTracing,
  dialog,
  desktopCapturer,
  globalShortcut,
  ipcMain,
  Menu,
  MenuItem,
  net,
  powerMonitor,
  powerSaveBlocker,
  protocol,
  Tray,
  screen,
  session,
  systemPreferences,
  webContents,
  TouchBar
} from 'electron/main';

import { clipboard, crashReporter, nativeImage, shell } from 'electron/common';
import * as path from 'node:path';

// Quick start
// https://github.com/electron/electron/blob/main/docs/tutorial/quick-start.md

// Keep a global reference of the window object, if you don't, the window will
// be closed automatically when the javascript object is GCed.
let mainWindow: Electron.BrowserWindow = null;

// Quit when all windows are closed.
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

// Check single instance app
const gotLock = app.requestSingleInstanceLock();

if (!gotLock) {
  app.quit();
  process.exit(0);
}

// This method will be called when Electron has done everything
// initialization and ready for creating browser windows.
app.whenReady().then(() => {
  // Create the browser window.
  mainWindow = new BrowserWindow({ width: 800, height: 600 });

  // and load the index.html of the app.
  mainWindow.loadURL(`file://${__dirname}/index.html`);
  mainWindow.loadURL('file://foo/bar', { userAgent: 'cool-agent', httpReferrer: 'greatReferrer' });
  mainWindow.webContents.loadURL('file://foo/bar', { userAgent: 'cool-agent', httpReferrer: 'greatReferrer' });
  mainWindow.webContents.loadURL('file://foo/bar', { userAgent: 'cool-agent', httpReferrer: 'greatReferrer', postData: [{ type: 'rawData', bytes: Buffer.from([123]) }] });

  mainWindow.webContents.openDevTools();
  mainWindow.webContents.toggleDevTools();
  mainWindow.webContents.openDevTools({ mode: 'detach' });
  mainWindow.webContents.closeDevTools();
  mainWindow.webContents.addWorkSpace('/path/to/workspace');
  mainWindow.webContents.removeWorkSpace('/path/to/workspace');

  const opened = mainWindow.webContents.isDevToolsOpened();
  console.log('isDevToolsOpened', opened);

  const focused = mainWindow.webContents.isDevToolsFocused();
  console.log('isDevToolsFocused', focused);

  // Emitted when the window is closed.
  mainWindow.on('closed', () => {
    // Dereference the window object, usually you would store windows
    // in an array if your app supports multi windows, this is the time
    // when you should delete the corresponding element.
    mainWindow = null;
  });

  mainWindow.webContents.setVisualZoomLevelLimits(50, 200);

  mainWindow.webContents.print({ silent: true, printBackground: false });
  mainWindow.webContents.print();

  mainWindow.webContents.printToPDF({
    margins: {
      top: 1
    },
    printBackground: true,
    pageRanges: '1-3',
    landscape: true,
    pageSize: {
      width: 100,
      height: 100
    }
  }).then((data: Buffer) => console.log(data));

  mainWindow.webContents.printToPDF({}).then(data => console.log(data));

  mainWindow.webContents.executeJavaScript('return true;').then((v: boolean) => console.log(v));
  mainWindow.webContents.executeJavaScript('return true;', true).then((v: boolean) => console.log(v));
  mainWindow.webContents.executeJavaScript('return true;', true);
  mainWindow.webContents.executeJavaScript('return true;', true).then((result: boolean) => console.log(result));
  mainWindow.webContents.insertText('blah, blah, blah');
  mainWindow.webContents.startDrag({ file: '/path/to/img.png', icon: nativeImage.createFromPath('/path/to/icon.png') });
  mainWindow.webContents.findInPage('blah');
  mainWindow.webContents.findInPage('blah', {
    forward: true,
    matchCase: false
  });
  mainWindow.webContents.stopFindInPage('clearSelection');
  mainWindow.webContents.stopFindInPage('keepSelection');
  mainWindow.webContents.stopFindInPage('activateSelection');

  mainWindow.loadURL('https://github.com');

  mainWindow.webContents.on('did-finish-load', function () {
    mainWindow.webContents.savePage('/tmp/test.html', 'HTMLComplete').then(() => {
      console.log('Page saved successfully');
    });
  });

  try {
    mainWindow.webContents.debugger.attach('1.1');
  } catch (err) {
    console.log('Debugger attach failed : ', err);
  }

  mainWindow.webContents.debugger.on('detach', function (event, reason) {
    console.log('Debugger detached due to : ', reason);
  });

  mainWindow.webContents.debugger.on('message', function (event, method, params: any) {
    if (method === 'Network.requestWillBeSent') {
      if (params.request.url === 'https://www.github.com') {
        mainWindow.webContents.debugger.detach();
      }
    }
  });

  mainWindow.webContents.debugger.sendCommand('Network.enable');
  mainWindow.webContents.capturePage().then(image => {
    console.log(image.toDataURL());
  });
  mainWindow.webContents.capturePage({ x: 0, y: 0, width: 100, height: 200 }).then(image => {
    console.log(image.toPNG());
  });
});

app.commandLine.appendSwitch('enable-web-bluetooth');

app.whenReady().then(() => {
  mainWindow.webContents.on('select-bluetooth-device', (event, deviceList, callback) => {
    event.preventDefault();

    const result = (() => {
      for (const device of deviceList) {
        if (device.deviceName === 'test') {
          return device;
        }
      }
      return null;
    })();

    if (!result) {
      callback('');
    } else {
      callback(result.deviceId);
    }
  });
});

// Locale
app.getLocale();

// Desktop environment integration

app.addRecentDocument('/Users/USERNAME/Desktop/work.type');
app.clearRecentDocuments();
const dockMenu = Menu.buildFromTemplate([
  <Electron.MenuItemConstructorOptions> {
    label: 'New Window',
    click: () => {
      console.log('New Window');
    }
  },
  <Electron.MenuItemConstructorOptions> {
    label: 'New Window with Settings',
    submenu: [
      <Electron.MenuItemConstructorOptions> { label: 'Basic' },
      <Electron.MenuItemConstructorOptions> { label: 'Pro' }
    ]
  },
  <Electron.MenuItemConstructorOptions> { label: 'New Command...' },
  <Electron.MenuItemConstructorOptions> {
    label: 'Edit',
    submenu: [
      {
        label: 'Undo',
        accelerator: 'CmdOrCtrl+Z',
        role: 'undo'
      },
      {
        label: 'Redo',
        accelerator: 'Shift+CmdOrCtrl+Z',
        role: 'redo'
      },
      {
        type: 'separator'
      },
      {
        label: 'Cut',
        accelerator: 'CmdOrCtrl+X',
        role: 'cut'
      },
      {
        label: 'Copy',
        accelerator: 'CmdOrCtrl+C',
        role: 'copy'
      },
      {
        label: 'Paste',
        accelerator: 'CmdOrCtrl+V',
        role: 'paste'
      }
    ]
  }
]);
app.dock.setMenu(dockMenu);
app.dock.setBadge('foo');
const dockid = app.dock.bounce('informational');
app.dock.cancelBounce(dockid);
app.dock.setIcon('/path/to/icon.png');

app.setBadgeCount(app.getBadgeCount() + 1);

app.setUserTasks([
  <Electron.Task> {
    program: process.execPath,
    arguments: '--new-window',
    iconPath: process.execPath,
    iconIndex: 0,
    title: 'New Window',
    description: 'Create a new window',
    workingDirectory: path.dirname(process.execPath)
  }
]);
app.setUserTasks([]);

app.setJumpList([
  {
    type: 'custom',
    name: 'Recent Projects',
    items: [
      { type: 'file', path: 'C:\\Projects\\project1.proj' },
      { type: 'file', path: 'C:\\Projects\\project2.proj' }
    ]
  },
  { // has a name so type is assumed to be "custom"
    name: 'Tools',
    items: [
      {
        type: 'task',
        title: 'Tool A',
        program: process.execPath,
        args: '--run-tool-a',
        iconPath: process.execPath,
        iconIndex: 0,
        description: 'Runs Tool A',
        workingDirectory: path.dirname(process.execPath)
      },
      {
        type: 'task',
        title: 'Tool B',
        program: process.execPath,
        args: '--run-tool-b',
        iconPath: process.execPath,
        iconIndex: 0,
        description: 'Runs Tool B',
        workingDirectory: path.dirname(process.execPath)
      }]
  },
  {
    type: 'frequent'
  },
  { // has no name and no type so type is assumed to be "tasks"
    items: [
      {
        type: 'task',
        title: 'New Project',
        program: process.execPath,
        args: '--new-project',
        description: 'Create a new project.'
      },
      {
        type: 'separator'
      },
      {
        type: 'task',
        title: 'Recover Project',
        program: process.execPath,
        args: '--recover-project',
        description: 'Recover Project'
      }]
  }
]);

if (app.isUnityRunning()) {
  console.log('unity running');
}
if (app.isAccessibilitySupportEnabled()) {
  console.log('a11y running');
}
app.setLoginItemSettings({ openAtLogin: true, openAsHidden: false });
console.log(app.getLoginItemSettings().wasOpenedAtLogin);
app.setAboutPanelOptions({
  applicationName: 'Test',
  version: '1.2.3'
});

// Online/Offline Event Detection
// https://github.com/electron/electron/blob/main/docs/tutorial/online-offline-events.md

let onlineStatusWindow: Electron.BrowserWindow;

app.whenReady().then(() => {
  onlineStatusWindow = new BrowserWindow({ width: 0, height: 0, show: false, vibrancy: 'sidebar' });
  onlineStatusWindow.loadURL(`file://${__dirname}/online-status.html`);
});
app.on('accessibility-support-changed', (_, enabled) => console.log('accessibility: ' + enabled));

ipcMain.on('online-status-changed', (event, status: any) => {
  console.log(status);
});

app.whenReady().then(() => {
  window = new BrowserWindow({
    width: 800,
    height: 600,
    titleBarStyle: 'hiddenInset'
  });
  window.loadURL('https://github.com');
});

// Supported command line switches
// https://github.com/electron/electron/blob/main/docs/api/command-line-switches.md

app.commandLine.appendSwitch('remote-debugging-port', '8315');
app.commandLine.appendSwitch('host-rules', 'MAP * 127.0.0.1');
app.commandLine.appendSwitch('vmodule', 'console=0');

// systemPreferences
// https://github.com/electron/electron/blob/main/docs/api/system-preferences.md

const browserOptions = {
  width: 1000,
  height: 800,
  transparent: false,
  frame: true
};

// Make the window transparent only if the platform supports it.
if (process.platform !== 'win32' || systemPreferences.isAeroGlassEnabled()) {
  browserOptions.transparent = true;
  browserOptions.frame = false;
}

if (process.platform === 'win32') {
  systemPreferences.on('color-changed', () => { console.log('color changed'); });
  // @ts-expect-error Removed API
  systemPreferences.on('inverted-color-scheme-changed', (_, inverted) => console.log(inverted ? 'inverted' : 'not inverted'));
  // @ts-expect-error Removed API
  systemPreferences.on('high-contrast-color-scheme-changed', (_, highContrast) => console.log(highContrast ? 'high contrast' : 'not high contrast'));
  console.log('Color for menu is', systemPreferences.getColor('menu'));
}

if (process.platform === 'darwin') {
  const value = systemPreferences.getUserDefault('Foo', 'string');
  console.log(value);
  const value2 = systemPreferences.getUserDefault('Foo', 'boolean');
  console.log(value2);
  // @ts-expect-error Removed API
  console.log(systemPreferences.getAppLevelAppearance());
  // @ts-expect-error Removed API
  systemPreferences.setAppLevelAppearance('dark');
  // @ts-expect-error Removed API
  console.log(systemPreferences.appLevelAppearance);
  // @ts-expect-error Removed API
  console.log(systemPreferences.getColor('alternate-selected-control-text'));
}

// Create the window.
const win1 = new BrowserWindow(browserOptions);

// Navigate.
if (browserOptions.transparent) {
  win1.loadURL(`file://${__dirname}/index.html`);
} else {
  // No transparency, so we load a fallback that uses basic styles.
  win1.loadURL(`file://${__dirname}/fallback.html`);
}

// app
// https://github.com/electron/electron/blob/main/docs/api/app.md

app.on('certificate-error', function (event, webContents, url, error, certificate, callback) {
  if (url === 'https://github.com') {
    // Verification logic.
    event.preventDefault();
    callback(true);
  } else {
    callback(false);
  }
});

app.on('select-client-certificate', function (event, webContents, url, list, callback) {
  event.preventDefault();
  callback(list[0]);
});

app.on('login', function (event, webContents, request, authInfo, callback) {
  event.preventDefault();
  callback('username', 'secret');
});

const win2 = new BrowserWindow({ show: false });
win2.once('ready-to-show', () => {
  win2.show();
});

app.relaunch({ args: process.argv.slice(1).concat(['--relaunch']) });
app.exit(0);

app.configureHostResolver({ secureDnsMode: 'off' });

// @ts-expect-error Invalid type value
app.configureHostResolver({ secureDnsMode: 'foo' });

// @ts-expect-error Removed API
console.log(app.runningUnderRosettaTranslation);

// @ts-expect-error Removed API
app.on('gpu-process-crashed', () => {});

// @ts-expect-error Removed API
app.on('renderer-process-crashed', () => {});

// auto-updater
// https://github.com/electron/electron/blob/main/docs/api/auto-updater.md

autoUpdater.setFeedURL({
  url: 'http://mycompany.com/myapp/latest?version=' + app.getVersion(),
  headers: {
    key: 'value'
  },
  serverType: 'default'
});
autoUpdater.checkForUpdates();
autoUpdater.quitAndInstall();

autoUpdater.on('error', (error) => {
  console.log('error', error);
});

autoUpdater.on('update-downloaded', (event, releaseNotes, releaseName, releaseDate, updateURL) => {
  console.log('update-downloaded', releaseNotes, releaseName, releaseDate, updateURL);
});

// BrowserWindow
// https://github.com/electron/electron/blob/main/docs/api/browser-window.md

let win3 = new BrowserWindow({ width: 800, height: 600, show: false });
win3.on('closed', () => {
  win3 = null;
});

win3.loadURL('https://github.com');
win3.show();

const toolbarRect = document.getElementById('toolbar').getBoundingClientRect();
win3.setSheetOffset(toolbarRect.height);

let window = new BrowserWindow();
window.setProgressBar(0.5);
window.setRepresentedFilename('/etc/passwd');
window.setDocumentEdited(true);
window.previewFile('/path/to/file');
window.previewFile('/path/to/file', 'Displayed Name');
window.setVibrancy('menu');
window.setVibrancy('titlebar');
window.setVibrancy('selection');
window.setVibrancy('popover');
window.setIcon('/path/to/icon');

// content-tracing
// https://github.com/electron/electron/blob/main/docs/api/content-tracing.md

const options = {
  categoryFilter: '*',
  traceOptions: 'record-until-full,enable-sampling'
};

contentTracing.startRecording(options).then(() => {
  console.log('Tracing started');
  setTimeout(function () {
    contentTracing.stopRecording('').then(path => {
      console.log(`Tracing data recorded to ${path}`);
    });
  }, 5000);
});

// dialog
// https://github.com/electron/electron/blob/main/docs/api/dialog.md

// variant without browserWindow
dialog.showOpenDialogSync({
  title: 'Testing showOpenDialog',
  defaultPath: '/var/log/syslog',
  filters: [{ name: '', extensions: [''] }],
  properties: ['openFile', 'openDirectory', 'multiSelections']
});

// variant with browserWindow
dialog.showOpenDialog(win3, {
  title: 'Testing showOpenDialog',
  defaultPath: '/var/log/syslog',
  filters: [{ name: '', extensions: [''] }],
  properties: ['openFile', 'openDirectory', 'multiSelections']
}).then(ret => {
  console.log(ret);
});

// variants without browserWindow
dialog.showMessageBox({ message: 'test', type: 'warning' });
dialog.showMessageBoxSync({ message: 'test', type: 'error' });

// @ts-expect-error Invalid type value
dialog.showMessageBox({ message: 'test', type: 'foo' });
// @ts-expect-error Invalid type value
dialog.showMessageBoxSync({ message: 'test', type: 'foo' });

// variants with browserWindow
dialog.showMessageBox(win3, { message: 'test', type: 'question' });
dialog.showMessageBoxSync(win3, { message: 'test', type: 'info' });

// @ts-expect-error Invalid type value
dialog.showMessageBox(win3, { message: 'test', type: 'foo' });
// @ts-expect-error Invalid type value
dialog.showMessageBoxSync(win3, { message: 'test', type: 'foo' });

// desktopCapturer
// https://github.com/electron/electron/blob/main/docs/api/desktop-capturer.md

ipcMain.handle('get-sources', (event, options) => desktopCapturer.getSources(options));

desktopCapturer.getSources({ types: ['window', 'screen'] });
// @ts-expect-error Invalid type value
desktopCapturer.getSources({ types: ['unknown'] });

// global-shortcut
// https://github.com/electron/electron/blob/main/docs/api/global-shortcut.md

// Register a 'ctrl+x' shortcut listener.
const ret = globalShortcut.register('ctrl+x', () => {
  console.log('ctrl+x is pressed');
});
if (!ret) { console.log('registration fails'); }

// Check whether a shortcut is registered.
console.log(globalShortcut.isRegistered('ctrl+x'));

// Unregister a shortcut.
globalShortcut.unregister('ctrl+x');

// Unregister all shortcuts.
globalShortcut.unregisterAll();

// ipcMain
// https://github.com/electron/electron/blob/main/docs/api/ipc-main.md

ipcMain.handle('ping-pong', (event, arg: any) => {
  console.log(arg); // prints "ping"
  return 'pong';
});

ipcMain.on('asynchronous-message', (event, arg: any) => {
  console.log(arg); // prints "ping"
  event.sender.send('asynchronous-reply', 'pong');
});

ipcMain.on('synchronous-message', (event, arg: any) => {
  console.log(arg); // prints "ping"
  event.returnValue = 'pong';
});

const winWindows = new BrowserWindow({
  width: 800,
  height: 600,
  show: false,
  thickFrame: false,
  type: 'toolbar'
});
console.log(winWindows.id);

// menu-item
// https://github.com/electron/electron/blob/main/docs/api/menu-item.md

const menuItem = new MenuItem({});

menuItem.label = 'Hello World!';
menuItem.click = (passedMenuItem: Electron.MenuItem, browserWindow: Electron.BrowserWindow) => {
  console.log('click', passedMenuItem, browserWindow);
};

// menu
// https://github.com/electron/electron/blob/main/docs/api/menu.md

let menu = new Menu();
menu.append(new MenuItem({ label: 'MenuItem1', click: () => { console.log('item 1 clicked'); } }));
menu.append(new MenuItem({ type: 'separator' }));
menu.append(new MenuItem({ label: 'MenuItem2', type: 'checkbox', checked: true }));
menu.insert(0, menuItem);

console.log(menu.items);

const pos = screen.getCursorScreenPoint();
menu.popup({ x: pos.x, y: pos.y });

// main.js
const template = <Electron.MenuItemConstructorOptions[]> [
  {
    label: 'Electron',
    submenu: [
      {
        label: 'About Electron',
        role: 'about'
      },
      {
        type: 'separator'
      },
      {
        label: 'Services',
        role: 'services',
        submenu: []
      },
      {
        type: 'separator'
      },
      {
        label: 'Hide Electron',
        accelerator: 'Command+H',
        role: 'hide'
      },
      {
        label: 'Hide Others',
        accelerator: 'Command+Shift+H',
        role: 'hideothers'
      },
      {
        label: 'Show All',
        role: 'unhide'
      },
      {
        type: 'separator'
      },
      {
        label: 'Quit',
        accelerator: 'Command+Q',
        click: () => { app.quit(); }
      }
    ]
  },
  {
    label: 'Edit',
    submenu: [
      {
        label: 'Undo',
        accelerator: 'Command+Z',
        role: 'undo'
      },
      {
        label: 'Redo',
        accelerator: 'Shift+Command+Z',
        role: 'redo'
      },
      {
        type: 'separator'
      },
      {
        label: 'Cut',
        accelerator: 'Command+X',
        role: 'cut'
      },
      {
        label: 'Copy',
        accelerator: 'Command+C',
        role: 'copy'
      },
      {
        label: 'Paste',
        accelerator: 'Command+V',
        role: 'paste'
      },
      {
        label: 'Select All',
        accelerator: 'Command+A',
        role: 'selectall'
      }
    ]
  },
  {
    label: 'View',
    submenu: [
      {
        label: 'Reload',
        accelerator: 'Command+R',
        click: (item, focusedWindow) => {
          if (focusedWindow) {
            focusedWindow.webContents.reloadIgnoringCache();
          }
        }
      },
      {
        label: 'Toggle DevTools',
        accelerator: 'Alt+Command+I',
        click: (item, focusedWindow) => {
          if (focusedWindow) {
            focusedWindow.webContents.toggleDevTools();
          }
        }
      },
      {
        type: 'separator'
      },
      {
        label: 'Actual Size',
        accelerator: 'CmdOrCtrl+0',
        click: (item, focusedWindow) => {
          if (focusedWindow) {
            focusedWindow.webContents.zoomLevel = 0;
          }
        }
      },
      {
        label: 'Zoom In',
        accelerator: 'CmdOrCtrl+Plus',
        click: (item, focusedWindow) => {
          if (focusedWindow) {
            const { webContents } = focusedWindow;
            webContents.zoomLevel += 0.5;
          }
        }
      },
      {
        label: 'Zoom Out',
        accelerator: 'CmdOrCtrl+-',
        click: (item, focusedWindow) => {
          if (focusedWindow) {
            const { webContents } = focusedWindow;
            webContents.zoomLevel -= 0.5;
          }
        }
      }
    ]
  },
  {
    label: 'Window',
    submenu: [
      {
        label: 'Minimize',
        accelerator: 'Command+M',
        role: 'minimize'
      },
      {
        label: 'Close',
        accelerator: 'Command+W',
        role: 'close'
      },
      {
        type: 'separator'
      },
      {
        label: 'Bring All to Front',
        role: 'front'
      }
    ]
  },
  {
    label: 'Help',
    submenu: []
  }
];

menu = Menu.buildFromTemplate(template);

Menu.setApplicationMenu(menu); // Must be called within app.whenReady().then(function(){ ... });

Menu.buildFromTemplate([
  { label: '4', id: '4' },
  { label: '5', id: '5', after: ['4'] },
  { label: '1', id: '1', before: ['4'] },
  { label: '2', id: '2' },
  { label: '3', id: '3' }
]);

Menu.buildFromTemplate([
  { label: 'a' },
  { label: '1' },
  { label: 'b' },
  { label: '2' },
  { label: 'c' },
  { label: '3' }
]);

// All possible MenuItem roles
Menu.buildFromTemplate([
  { role: 'undo' },
  { role: 'redo' },
  { role: 'cut' },
  { role: 'copy' },
  { role: 'paste' },
  { role: 'pasteAndMatchStyle' },
  { role: 'delete' },
  { role: 'selectAll' },
  { role: 'reload' },
  { role: 'forceReload' },
  { role: 'toggleDevTools' },
  { role: 'resetZoom' },
  { role: 'zoomIn' },
  { role: 'zoomOut' },
  { role: 'togglefullscreen' },
  { role: 'window' },
  { role: 'minimize' },
  { role: 'close' },
  { role: 'help' },
  { role: 'about' },
  { role: 'services' },
  { role: 'hide' },
  { role: 'hideOthers' },
  { role: 'unhide' },
  { role: 'quit' },
  { role: 'startSpeaking' },
  { role: 'stopSpeaking' },
  { role: 'close' },
  { role: 'minimize' },
  { role: 'zoom' },
  { role: 'front' },
  { role: 'appMenu' },
  { role: 'fileMenu' },
  { role: 'editMenu' },
  { role: 'viewMenu' },
  { role: 'windowMenu' },
  { role: 'recentDocuments' },
  { role: 'clearRecentDocuments' },
  { role: 'toggleTabBar' },
  { role: 'selectNextTab' },
  { role: 'selectPreviousTab' },
  { role: 'showAllTabs' },
  { role: 'mergeAllWindows' },
  { role: 'clearRecentDocuments' },
  { role: 'moveTabToNewWindow' }
]);

// net
// https://github.com/electron/electron/blob/main/docs/api/net.md

app.whenReady().then(() => {
  const request = net.request('https://github.com');
  request.setHeader('Some-Custom-Header-Name', 'Some-Custom-Header-Value');
  const header = request.getHeader('Some-Custom-Header-Name');
  console.log('header', header);
  request.removeHeader('Some-Custom-Header-Name');
  request.on('response', (response) => {
    console.log(`Status code: ${response.statusCode}`);
    console.log(`Status message: ${response.statusMessage}`);
    console.log(`Headers: ${JSON.stringify(response.headers)}`);
    console.log(`Http version: ${response.httpVersion}`);
    console.log(`Major Http version: ${response.httpVersionMajor}`);
    console.log(`Minor Http version: ${response.httpVersionMinor}`);
    response.on('data', (chunk) => {
      console.log(`BODY: ${chunk}`);
    });
    response.on('end', () => {
      console.log('No more data in response.');
    });
    response.on('error', () => {
      console.log('"error" event emitted');
    });
    response.on('aborted', () => {
      console.log('"aborted" event emitted');
    });
  });
  request.on('login', (authInfo, callback) => {
    callback('username', 'password');
  });
  request.on('finish', () => {
    console.log('"finish" event emitted');
  });
  request.on('abort', () => {
    console.log('"abort" event emitted');
  });
  request.on('error', () => {
    console.log('"error" event emitted');
  });
  request.write('Hello World!', 'utf-8');
  request.end('Hello World!', 'utf-8');
  request.abort();
});

// power-monitor
// https://github.com/electron/electron/blob/main/docs/api/power-monitor.md

app.whenReady().then(() => {
  powerMonitor.on('suspend', () => {
    console.log('The system is going to sleep');
  });
  powerMonitor.on('resume', () => {
    console.log('The system has resumed from sleep');
  });
  powerMonitor.on('on-ac', () => {
    console.log('The system changed to AC power');
  });
  powerMonitor.on('on-battery', () => {
    console.log('The system changed to battery power');
  });
});

// power-save-blocker
// https://github.com/electron/electron/blob/main/docs/api/power-save-blocker.md

const id = powerSaveBlocker.start('prevent-display-sleep');
console.log(powerSaveBlocker.isStarted(id));

const stopped = powerSaveBlocker.stop(id);
console.log(`The powerSaveBlocker is ${stopped ? 'stopped' : 'not stopped'}`);

// protocol
// https://github.com/electron/electron/blob/main/docs/api/protocol.md

app.whenReady().then(() => {
  protocol.registerSchemesAsPrivileged([{ scheme: 'https', privileges: { standard: true, allowServiceWorkers: true } }]);

  protocol.registerFileProtocol('atom', (request, callback) => {
    callback(`${__dirname}/${request.url}`);
  });

  protocol.registerBufferProtocol('atom', (request, callback) => {
    callback({ mimeType: 'text/html', data: Buffer.from('<h5>Response</h5>') });
  });

  protocol.registerStringProtocol('atom', (request, callback) => {
    callback('Hello World!');
  });

  protocol.registerHttpProtocol('atom', (request, callback) => {
    callback({ url: request.url, method: request.method });
  });

  protocol.unregisterProtocol('atom');

  const registered = protocol.isProtocolRegistered('atom');
  console.log('isProtocolRegistered', registered);
});

// tray
// https://github.com/electron/electron/blob/main/docs/api/tray.md

let appIcon: Electron.Tray = null;
app.whenReady().then(() => {
  appIcon = new Tray('/path/to/my/icon');
  const contextMenu = Menu.buildFromTemplate([
    { label: 'Item1', type: 'radio' },
    { label: 'Item2', type: 'radio' },
    { label: 'Item3', type: 'radio', checked: true },
    { label: 'Item4', type: 'radio' }
  ]);

  appIcon.setTitle('title');
  appIcon.setToolTip('This is my application.');

  appIcon.setImage('/path/to/new/icon');
  appIcon.setPressedImage('/path/to/new/icon');

  appIcon.popUpContextMenu(contextMenu, { x: 100, y: 100 });
  appIcon.setContextMenu(contextMenu);

  appIcon.setIgnoreDoubleClickEvents(true);

  appIcon.on('click', (event, bounds) => {
    console.log('click', event, bounds);
  });

  appIcon.on('balloon-show', () => {
    console.log('balloon-show');
  });

  appIcon.displayBalloon({
    title: 'Hello World!',
    content: 'This is the balloon content.',
    iconType: 'error',
    icon: 'path/to/icon',
    respectQuietTime: true,
    largeIcon: true,
    noSound: true
  });
});

// clipboard
// https://github.com/electron/electron/blob/main/docs/api/clipboard.md

clipboard.writeText('Example String');
clipboard.writeText('Example String', 'selection');
clipboard.writeBookmark('foo', 'http://example.com');
clipboard.writeBookmark('foo', 'http://example.com', 'selection');
clipboard.writeFindText('foo');
console.log(clipboard.readText('selection'));
console.log(clipboard.readFindText());
console.log(clipboard.availableFormats());
console.log(clipboard.readBookmark().title);
clipboard.clear();

clipboard.write({
  html: '<html></html>',
  text: 'Hello World!',
  image: clipboard.readImage()
});

// crash-reporter
// https://github.com/electron/electron/blob/main/docs/api/crash-reporter.md

crashReporter.start({
  productName: 'YourName',
  companyName: 'YourCompany',
  submitURL: 'https://your-domain.com/url-to-submit',
  uploadToServer: true,
  extra: {
    someKey: 'value'
  }
});

console.log(crashReporter.getLastCrashReport());
console.log(crashReporter.getUploadedReports());

// nativeImage
// https://github.com/electron/electron/blob/main/docs/api/native-image.md

const appIcon2 = new Tray('/Users/somebody/images/icon.png');
appIcon2.destroy();

const window2 = new BrowserWindow({ icon: '/Users/somebody/images/window.png' });
console.log(window2.id);

const image = clipboard.readImage();
console.log(image.getSize());

const appIcon3 = new Tray(image);
appIcon3.destroy();

const appIcon4 = new Tray('/Users/somebody/images/icon.png');
appIcon4.destroy();

const image2 = nativeImage.createFromPath('/Users/somebody/images/icon.png');
console.log(image2.getSize());

image2.resize({ quality: 'best' });
image2.resize({ quality: 'better' });
image2.resize({ quality: 'good' });
// @ts-expect-error Invalid type value
image2.resize({ quality: 'bad' });

// process
// https://github.com/electron/electron/blob/main/docs/api/process.md

console.log(process.versions.electron);
console.log(process.versions.chrome);
console.log(process.type);
console.log(process.resourcesPath);
console.log(process.mas);
console.log(process.windowsStore);
process.noAsar = true;
process.crash();
process.hang();
process.setFdLimit(8192);

// screen
// https://github.com/electron/electron/blob/main/docs/api/screen.md

app.whenReady().then(() => {
  const size = screen.getPrimaryDisplay().workAreaSize;
  mainWindow = new BrowserWindow({ width: size.width, height: size.height });
});

app.whenReady().then(() => {
  const displays = screen.getAllDisplays();
  let externalDisplay: any = null;
  for (const i in displays) {
    if (displays[i].bounds.x > 0 || displays[i].bounds.y > 0) {
      externalDisplay = displays[i];
      break;
    }
  }

  if (externalDisplay) {
    mainWindow = new BrowserWindow({
      x: externalDisplay.bounds.x + 50,
      y: externalDisplay.bounds.y + 50
    });
  }

  screen.on('display-added', (event, display) => {
    console.log('display-added', display);
  });

  screen.on('display-removed', (event, display) => {
    console.log('display-removed', display);
  });

  screen.on('display-metrics-changed', (event, display, changes) => {
    console.log('display-metrics-changed', display, changes);
  });
});

// shell
// https://github.com/electron/electron/blob/main/docs/api/shell.md

shell.showItemInFolder('/home/user/Desktop/test.txt');
shell.trashItem('/home/user/Desktop/test.txt').then(() => {});

shell.openPath('/home/user/Desktop/test.txt').then(err => {
  if (err) console.log(err);
});

shell.openExternal('https://github.com', {
  activate: false
}).then(() => {});

shell.beep();

shell.writeShortcutLink('/home/user/Desktop/shortcut.lnk', 'update', shell.readShortcutLink('/home/user/Desktop/shortcut.lnk'));

// cookies
// https://github.com/electron/electron/blob/main/docs/api/cookies.md
{
  // Query all cookies.
  session.defaultSession.cookies.get({})
    .then(cookies => {
      console.log(cookies);
    }).catch((error: Error) => {
      console.log(error);
    });

  // Query all cookies associated with a specific url.
  session.defaultSession.cookies.get({ url: 'http://www.github.com' })
    .then(cookies => {
      console.log(cookies);
    }).catch((error: Error) => {
      console.log(error);
    });

  // Set a cookie with the given cookie data;
  // may overwrite equivalent cookies if they exist.
  const cookie = { url: 'http://www.github.com', name: 'dummy_name', value: 'dummy' };
  session.defaultSession.cookies.set(cookie)
    .then(() => {
      // success
    }, (error: Error) => {
      console.error(error);
    });
}

// session
// https://github.com/electron/electron/blob/main/docs/api/session.md

session.defaultSession.clearStorageData({ storages: ['cookies', 'filesystem'] });
session.defaultSession.clearStorageData({ storages: ['localstorage', 'indexdb', 'serviceworkers'] });
session.defaultSession.clearStorageData({ storages: ['shadercache', 'cachestorage'] });
// @ts-expect-error Invalid type value
session.defaultSession.clearStorageData({ storages: ['wrong_path'] });

session.defaultSession.clearStorageData({ quotas: ['syncable', 'temporary'] });
// @ts-expect-error Invalid type value
session.defaultSession.clearStorageData({ quotas: ['bad_type'] });

session.defaultSession.on('will-download', (event, item, webContents) => {
  console.log('will-download', webContents.id);
  event.preventDefault();
  require('got')(item.getURL()).then((data: any) => {
    require('node:fs').writeFileSync('/somewhere', data);
  });
});

// In the main process.
session.defaultSession.on('will-download', (event, item, webContents) => {
  console.log('will-download', webContents.id);
  // Set the save path, making Electron not to prompt a save dialog.
  item.setSavePath('/tmp/save.pdf');
  console.log(item.getSavePath());
  console.log(item.getMimeType());
  console.log(item.getFilename());
  console.log(item.getTotalBytes());

  item.on('updated', (_event, state) => {
    if (state === 'interrupted') {
      console.log('Download is interrupted but can be resumed');
    } else if (state === 'progressing') {
      if (item.isPaused()) {
        console.log('Download is paused');
      } else {
        console.log(`Received bytes: ${item.getReceivedBytes()}`);
      }
    }
  });

  item.on('done', function (e, state) {
    if (state === 'completed') {
      console.log('Download successfully');
    } else {
      console.log(`Download failed: ${state}`);
    }
  });
});

// To emulate a GPRS connection with 50kbps throughput and 500 ms latency.
session.defaultSession.enableNetworkEmulation({
  latency: 500,
  downloadThroughput: 6400,
  uploadThroughput: 6400
});

// To emulate a network outage.
session.defaultSession.enableNetworkEmulation({
  offline: true
});

session.defaultSession.setCertificateVerifyProc((request, callback) => {
  const { hostname } = request;
  if (hostname === 'github.com') {
    callback(0);
  } else {
    callback(-2);
  }
});

session.defaultSession.setPermissionRequestHandler(function (webContents, permission, callback) {
  if (webContents.getURL() === 'github.com') {
    if (permission === 'notifications') {
      callback(false);
      return;
    }
  }

  callback(true);
});

// consider any url ending with `example.com`, `foobar.com`, `baz`
// for integrated authentication.
session.defaultSession.allowNTLMCredentialsForDomains('*example.com, *foobar.com, *baz');

// consider all urls for integrated authentication.
session.defaultSession.allowNTLMCredentialsForDomains('*');

// Modify the user agent for all requests to the following urls.
const filter = {
  urls: ['https://*.github.com/*', '*://electron.github.io']
};

session.defaultSession.webRequest.onBeforeSendHeaders(filter, function (details: any, callback: any) {
  details.requestHeaders['User-Agent'] = 'MyAgent';
  callback({ cancel: false, requestHeaders: details.requestHeaders });
});

app.whenReady().then(function () {
  const protocol = session.defaultSession.protocol;
  protocol.registerFileProtocol('atom', function (request, callback) {
    const url = request.url.substr(7);
    callback(path.normalize(`${__dirname}/${url}`));
  });
});

// webContents
// https://github.com/electron/electron/blob/main/docs/api/web-contents.md

console.log(webContents.getAllWebContents());
console.log(webContents.getFocusedWebContents());

const win4 = new BrowserWindow({
  webPreferences: {
    offscreen: true
  }
});

win4.webContents.on('paint', (event, dirty, _image) => {
  console.log(dirty, _image.getBitmap());
});

win4.webContents.on('devtools-open-url', (event, url) => {
  console.log(url);
});

win4.webContents.insertCSS('body {}', { cssOrigin: 'user' });

// @ts-expect-error Invalid type value
win4.webContents.insertCSS('body {}', { cssOrigin: 'foo' });

win4.loadURL('http://github.com');

// @ts-expect-error Removed API
win4.webContents.getPrinters();

// @ts-expect-error Removed API
win4.webContents.on('scroll-touch-begin', () => {});
// @ts-expect-error Removed API
win4.webContents.on('scroll-touch-edge', () => {});
// @ts-expect-error Removed API
win4.webContents.on('scroll-touch-end', () => {});

// @ts-expect-error Removed API
win4.webContents.on('crashed', () => {});

win4.webContents.on('context-menu', (event, params) => {
  // @ts-expect-error Removed API
  console.log(params.inputFieldType);
});

// TouchBar
// https://github.com/electron/electron/blob/main/docs/api/touch-bar.md

const touchBar = new TouchBar({
  items: [
    new TouchBar.TouchBarButton({ label: '' }),
    new TouchBar.TouchBarLabel({ label: '' })
  ]
});

mainWindow.setTouchBar(touchBar);
