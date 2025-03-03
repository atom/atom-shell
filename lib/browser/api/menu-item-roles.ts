import { app, BaseWindow, BrowserWindow, session, webContents, WebContents, MenuItemConstructorOptions } from 'electron/main';

const isMac = process.platform === 'darwin';
const isWindows = process.platform === 'win32';
const isLinux = process.platform === 'linux';

type RoleId = 'about' | 'close' | 'copy' | 'cut' | 'delete' | 'forcereload' | 'front' | 'help' | 'hide' | 'hideothers' | 'minimize' |
  'paste' | 'pasteandmatchstyle' | 'quit' | 'redo' | 'reload' | 'resetzoom' | 'selectall' | 'services' | 'recentdocuments' | 'clearrecentdocuments' |
  'showsubstitutions' | 'togglesmartquotes' | 'togglesmartdashes' | 'toggletextreplacement' | 'startspeaking' | 'stopspeaking' |
  'toggledevtools' | 'togglefullscreen' | 'undo' | 'unhide' | 'window' | 'zoom' | 'zoomin' | 'zoomout' | 'togglespellchecker' |
  'appmenu' | 'filemenu' | 'editmenu' | 'viewmenu' | 'windowmenu' | 'sharemenu' | 'palette' | 'header'
interface Role {
  label: string;
  accelerator?: string;
  checked?: boolean;
  windowMethod?: ((window: BaseWindow) => void);
  webContentsMethod?: ((webContents: WebContents) => void);
  appMethod?: () => void;
  registerAccelerator?: boolean;
  nonNativeMacOSRole?: boolean;
  submenu?: MenuItemConstructorOptions[];
}

export const roleList: Record<RoleId, Role> = {
  about: {
    get label () {
      return isLinux ? 'About' : `About ${app.name}`;
    },
    ...((isWindows || isLinux) && { appMethod: () => app.showAboutPanel() })
  },
  close: {
    label: isMac ? 'Close Window' : 'Close',
    accelerator: 'CommandOrControl+W',
    windowMethod: w => w.close()
  },
  copy: {
    label: 'Copy',
    accelerator: 'CommandOrControl+C',
    webContentsMethod: wc => wc.copy(),
    registerAccelerator: false
  },
  cut: {
    label: 'Cut',
    accelerator: 'CommandOrControl+X',
    webContentsMethod: wc => wc.cut(),
    registerAccelerator: false
  },
  delete: {
    label: 'Delete',
    webContentsMethod: wc => wc.delete()
  },
  forcereload: {
    label: 'Force Reload',
    accelerator: 'Shift+CmdOrCtrl+R',
    nonNativeMacOSRole: true,
    windowMethod: (window: BaseWindow) => {
      if (window instanceof BrowserWindow) {
        window.webContents.reloadIgnoringCache();
      }
    }
  },
  front: {
    label: 'Bring All to Front'
  },
  help: {
    label: 'Help'
  },
  hide: {
    get label () {
      return `Hide ${app.name}`;
    },
    accelerator: 'Command+H'
  },
  hideothers: {
    label: 'Hide Others',
    accelerator: 'Command+Alt+H'
  },
  minimize: {
    label: 'Minimize',
    accelerator: 'CommandOrControl+M',
    windowMethod: w => w.minimize()
  },
  paste: {
    label: 'Paste',
    accelerator: 'CommandOrControl+V',
    webContentsMethod: wc => wc.paste(),
    registerAccelerator: false
  },
  pasteandmatchstyle: {
    label: 'Paste and Match Style',
    accelerator: isMac ? 'Cmd+Option+Shift+V' : 'Shift+CommandOrControl+V',
    webContentsMethod: wc => wc.pasteAndMatchStyle(),
    registerAccelerator: false
  },
  quit: {
    get label () {
      switch (process.platform) {
        case 'darwin': return `Quit ${app.name}`;
        case 'win32': return 'Exit';
        default: return 'Quit';
      }
    },
    accelerator: isWindows ? undefined : 'CommandOrControl+Q',
    appMethod: () => app.quit()
  },
  redo: {
    label: 'Redo',
    accelerator: isWindows ? 'Control+Y' : 'Shift+CommandOrControl+Z',
    webContentsMethod: wc => wc.redo()
  },
  reload: {
    label: 'Reload',
    accelerator: 'CmdOrCtrl+R',
    nonNativeMacOSRole: true,
    windowMethod: (w: BaseWindow) => {
      if (w instanceof BrowserWindow) {
        w.reload();
      }
    }
  },
  resetzoom: {
    label: 'Actual Size',
    accelerator: 'CommandOrControl+0',
    nonNativeMacOSRole: true,
    webContentsMethod: (webContents: WebContents) => {
      webContents.zoomLevel = 0;
    }
  },
  selectall: {
    label: 'Select All',
    accelerator: 'CommandOrControl+A',
    webContentsMethod: wc => wc.selectAll()
  },
  services: {
    label: 'Services'
  },
  recentdocuments: {
    label: 'Open Recent'
  },
  clearrecentdocuments: {
    label: 'Clear Menu'
  },
  showsubstitutions: {
    label: 'Show Substitutions'
  },
  togglesmartquotes: {
    label: 'Smart Quotes'
  },
  togglesmartdashes: {
    label: 'Smart Dashes'
  },
  toggletextreplacement: {
    label: 'Text Replacement'
  },
  startspeaking: {
    label: 'Start Speaking'
  },
  stopspeaking: {
    label: 'Stop Speaking'
  },
  toggledevtools: {
    label: 'Toggle Developer Tools',
    accelerator: isMac ? 'Alt+Command+I' : 'Ctrl+Shift+I',
    nonNativeMacOSRole: true,
    webContentsMethod: wc => {
      const bw = wc.getOwnerBrowserWindow();
      if (bw) bw.webContents.toggleDevTools();
    }
  },
  togglefullscreen: {
    label: 'Toggle Full Screen',
    accelerator: isMac ? 'Control+Command+F' : 'F11',
    windowMethod: (window: BaseWindow) => {
      window.setFullScreen(!window.isFullScreen());
    }
  },
  undo: {
    label: 'Undo',
    accelerator: 'CommandOrControl+Z',
    webContentsMethod: wc => wc.undo()
  },
  unhide: {
    label: 'Show All'
  },
  window: {
    label: 'Window'
  },
  zoom: {
    label: 'Zoom'
  },
  zoomin: {
    label: 'Zoom In',
    accelerator: 'CommandOrControl+Plus',
    nonNativeMacOSRole: true,
    webContentsMethod: (webContents: WebContents) => {
      webContents.zoomLevel += 0.5;
    }
  },
  zoomout: {
    label: 'Zoom Out',
    accelerator: 'CommandOrControl+-',
    nonNativeMacOSRole: true,
    webContentsMethod: (webContents: WebContents) => {
      webContents.zoomLevel -= 0.5;
    }
  },
  togglespellchecker: {
    label: 'Check Spelling While Typing',
    get checked () {
      const wc = webContents.getFocusedWebContents();
      const ses = wc ? wc.session : session.defaultSession;
      return ses.spellCheckerEnabled;
    },
    nonNativeMacOSRole: true,
    webContentsMethod: (wc: WebContents) => {
      const ses = wc ? wc.session : session.defaultSession;
      ses.spellCheckerEnabled = !ses.spellCheckerEnabled;
    }
  },
  // App submenu should be used for Mac only
  appmenu: {
    get label () {
      return app.name;
    },
    submenu: [
      { role: 'about' },
      { type: 'separator' },
      { role: 'services' },
      { type: 'separator' },
      { role: 'hide' },
      { role: 'hideOthers' },
      { role: 'unhide' },
      { type: 'separator' },
      { role: 'quit' }
    ]
  },
  // File submenu
  filemenu: {
    label: 'File',
    submenu: [
      isMac ? { role: 'close' } : { role: 'quit' }
    ]
  },
  // Edit submenu
  editmenu: {
    label: 'Edit',
    submenu: [
      { role: 'undo' },
      { role: 'redo' },
      { type: 'separator' },
      { role: 'cut' },
      { role: 'copy' },
      { role: 'paste' },
      ...(isMac
        ? [
          { role: 'pasteAndMatchStyle' },
          { role: 'delete' },
          { role: 'selectAll' },
          { type: 'separator' },
          {
            label: 'Substitutions',
            submenu: [
              { role: 'showSubstitutions' },
              { type: 'separator' },
              { role: 'toggleSmartQuotes' },
              { role: 'toggleSmartDashes' },
              { role: 'toggleTextReplacement' }
            ]
          },
          {
            label: 'Speech',
            submenu: [
              { role: 'startSpeaking' },
              { role: 'stopSpeaking' }
            ]
          }
        ] as MenuItemConstructorOptions[]
        : [
          { role: 'delete' },
          { type: 'separator' },
          { role: 'selectAll' }
        ] as MenuItemConstructorOptions[])
    ]
  },
  // View submenu
  viewmenu: {
    label: 'View',
    submenu: [
      { role: 'reload' },
      { role: 'forceReload' },
      { role: 'toggleDevTools' },
      { type: 'separator' },
      { role: 'resetZoom' },
      { role: 'zoomIn' },
      { role: 'zoomOut' },
      { type: 'separator' },
      { role: 'togglefullscreen' }
    ]
  },
  // Window submenu
  windowmenu: {
    label: 'Window',
    submenu: [
      { role: 'minimize' },
      { role: 'zoom' },
      ...(isMac
        ? [
          { type: 'separator' },
          { role: 'front' }
        ] as MenuItemConstructorOptions[]
        : [
          { role: 'close' }
        ] as MenuItemConstructorOptions[])
    ]
  },
  // Share submenu
  sharemenu: {
    label: 'Share',
    submenu: []
  },
  palette: {
    label: 'Palette',
    submenu: []
  },
  header: {
    label: 'Section Header'
  }
};

const hasRole = (role: keyof typeof roleList) => {
  return Object.hasOwn(roleList, role);
};

const canExecuteRole = (role: keyof typeof roleList) => {
  if (!hasRole(role)) return false;
  if (!isMac) return true;

  // macOS handles all roles natively except for a few
  return roleList[role].nonNativeMacOSRole;
};

export function getDefaultType (role: RoleId) {
  if (shouldOverrideCheckStatus(role)) return 'checkbox';
  return 'normal';
}

export function getDefaultLabel (role: RoleId) {
  return hasRole(role) ? roleList[role].label : '';
}

export function getCheckStatus (role: RoleId) {
  if (hasRole(role)) return roleList[role].checked;
}

export function shouldOverrideCheckStatus (role: RoleId) {
  return hasRole(role) && Object.hasOwn(roleList[role], 'checked');
}

export function getDefaultAccelerator (role: RoleId) {
  if (hasRole(role)) return roleList[role].accelerator;
}

export function shouldRegisterAccelerator (role: RoleId) {
  const hasRoleRegister = hasRole(role) && roleList[role].registerAccelerator !== undefined;
  return hasRoleRegister ? roleList[role].registerAccelerator : true;
}

export function getDefaultSubmenu (role: RoleId) {
  if (!hasRole(role)) return;

  let { submenu } = roleList[role];

  // remove null items from within the submenu
  if (Array.isArray(submenu)) {
    submenu = submenu.filter((item) => item != null);
  }

  return submenu;
}

export function execute (role: RoleId, focusedWindow: BaseWindow, focusedWebContents: WebContents) {
  if (!canExecuteRole(role)) return false;

  const { appMethod, webContentsMethod, windowMethod } = roleList[role];

  if (appMethod) {
    appMethod();
    return true;
  }

  if (windowMethod && focusedWindow != null) {
    windowMethod(focusedWindow);
    return true;
  }

  if (webContentsMethod && focusedWebContents != null) {
    webContentsMethod(focusedWebContents);
    return true;
  }

  return false;
}
