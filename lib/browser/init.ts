import { EventEmitter } from 'events';
import * as fs from 'fs';
import * as path from 'path';

import type * as defaultMenuModule from '@electron/internal/browser/default-menu';
import type * as url from 'url';
import type * as v8 from 'v8';

const Module = require('module') as NodeJS.ModuleInternal;

// We modified the original process.argv to let node.js load the init.js,
// we need to restore it here.
process.argv.splice(1, 1);

// Import common settings.
require('@electron/internal/common/init');

process._linkedBinding('electron_browser_event_emitter').setEventEmitterPrototype(EventEmitter.prototype);

// Don't quit on fatal error.
process.on('uncaughtException', function (error) {
  // Do nothing if the user has a custom uncaught exception handler.
  if (process.listenerCount('uncaughtException') > 1) {
    return;
  }

  // Show error in GUI.
  // We can't import { dialog } at the top of this file as this file is
  // responsible for setting up the require hook for the "electron" module
  // so we import it inside the handler down here
  import('electron')
    .then(({ dialog }) => {
      const stack = error.stack ? error.stack : `${error.name}: ${error.message}`;
      const message = 'Uncaught Exception:\n' + stack;
      dialog.showErrorBox('A JavaScript error occurred in the main process', message);
    });
});

// Emit 'exit' event on quit.
const { app } = require('electron');

app.on('quit', (_event, exitCode) => {
  process.emit('exit', exitCode);
});

if (process.platform === 'win32') {
  // If we are a Squirrel.Windows-installed app, set app user model ID
  // so that users don't have to do this.
  //
  // Squirrel packages are always of the form:
  //
  // PACKAGE-NAME
  // - Update.exe
  // - app-VERSION
  //   - OUREXE.exe
  //
  // Squirrel itself will always set the shortcut's App User Model ID to the
  // form `com.squirrel.PACKAGE-NAME.OUREXE`. We need to call
  // app.setAppUserModelId with a matching identifier so that renderer processes
  // will inherit this value.
  const updateDotExe = path.join(path.dirname(process.execPath), '..', 'update.exe');

  if (fs.existsSync(updateDotExe)) {
    const packageDir = path.dirname(path.resolve(updateDotExe));
    const packageName = path.basename(packageDir).replaceAll(/\s/g, '');
    const exeName = path.basename(process.execPath).replace(/\.exe$/i, '').replaceAll(/\s/g, '');

    app.setAppUserModelId(`com.squirrel.${packageName}.${exeName}`);
  }
}

// Map process.exit to app.exit, which quits gracefully.
process.exit = app.exit as () => never;

// Load the RPC server.
require('@electron/internal/browser/rpc-server');

// Load the guest view manager.
require('@electron/internal/browser/guest-view-manager');

// Now we try to load app's package.json.
const v8Util = process._linkedBinding('electron_common_v8_util');
let packagePath = null;
let packageJson = null;
const searchPaths: string[] = v8Util.getHiddenValue(global, 'appSearchPaths');
const searchPathsOnlyLoadASAR: boolean = v8Util.getHiddenValue(global, 'appSearchPathsOnlyLoadASAR');
// Borrow the _getOrCreateArchive asar helper
const getOrCreateArchive = process._getOrCreateArchive;
delete process._getOrCreateArchive;

if (process.resourcesPath) {
  for (packagePath of searchPaths) {
    try {
      packagePath = path.join(process.resourcesPath, packagePath);
      if (searchPathsOnlyLoadASAR) {
        if (!getOrCreateArchive?.(packagePath)) {
          continue;
        }
      }
      packageJson = Module._load(path.join(packagePath, 'package.json'));
      break;
    } catch {
      continue;
    }
  }
}

if (packageJson == null) {
  process.nextTick(function () {
    return process.exit(1);
  });
  throw new Error('Unable to find a valid app');
}

// Set application's version.
if (packageJson.version != null) {
  app.setVersion(packageJson.version);
}

// Set application's name.
if (packageJson.productName != null) {
  app.name = `${packageJson.productName}`.trim();
} else if (packageJson.name != null) {
  app.name = `${packageJson.name}`.trim();
}

// Set application's desktop name.
if (packageJson.desktopName != null) {
  app.setDesktopName(packageJson.desktopName);
} else {
  app.setDesktopName(`${app.name}.desktop`);
}

// Set v8 flags, deliberately lazy load so that apps that do not use this
// feature do not pay the price
if (packageJson.v8Flags != null) {
  (require('v8') as typeof v8).setFlagsFromString(packageJson.v8Flags);
}

app.setAppPath(packagePath);

// Load the chrome devtools support.
require('@electron/internal/browser/devtools');

// Load protocol module to ensure it is populated on app ready
require('@electron/internal/browser/api/protocol');

// Load web-contents module to ensure it is populated on app ready
require('@electron/internal/browser/api/web-contents');

// Load web-frame-main module to ensure it is populated on app ready
require('@electron/internal/browser/api/web-frame-main');

// Required because `new BrowserWindow` calls some WebContentsView stuff, so
// the inheritance needs to be set up before that happens.
require('@electron/internal/browser/api/web-contents-view');

// Set main startup script of the app.
const mainStartupScript = packageJson.main || 'index.js';

const KNOWN_XDG_DESKTOP_VALUES = new Set(['Pantheon', 'Unity:Unity7', 'pop:GNOME']);

function currentPlatformSupportsAppIndicator () {
  if (process.platform !== 'linux') return false;
  const currentDesktop = process.env.XDG_CURRENT_DESKTOP;

  if (!currentDesktop) return false;
  if (KNOWN_XDG_DESKTOP_VALUES.has(currentDesktop)) return true;
  // ubuntu based or derived session (default ubuntu one, communitheme…) supports
  // indicator too.
  if (/ubuntu/ig.test(currentDesktop)) return true;

  return false;
}

// Workaround for electron/electron#5050 and electron/electron#9046
process.env.ORIGINAL_XDG_CURRENT_DESKTOP = process.env.XDG_CURRENT_DESKTOP;
if (currentPlatformSupportsAppIndicator()) {
  process.env.XDG_CURRENT_DESKTOP = 'Unity';
}

// Quit when all windows are closed and no other one is listening to this.
app.on('window-all-closed', () => {
  if (app.listenerCount('window-all-closed') === 1) {
    app.quit();
  }
});

const { setDefaultApplicationMenu } = require('@electron/internal/browser/default-menu') as typeof defaultMenuModule;

// Create default menu.
//
// The |will-finish-launching| event is emitted before |ready| event, so default
// menu is set before any user window is created.
app.once('will-finish-launching', setDefaultApplicationMenu);

const { appCodeLoaded } = process;
delete process.appCodeLoaded;

if (packagePath) {
  // Finally load app's main.js and transfer control to C++.
  if ((packageJson.type === 'module' && !mainStartupScript.endsWith('.cjs')) || mainStartupScript.endsWith('.mjs')) {
    const { runEntryPointWithESMLoader } = __non_webpack_require__('internal/modules/run_main');
    const main = (require('url') as typeof url).pathToFileURL(path.join(packagePath, mainStartupScript));
    runEntryPointWithESMLoader(async (cascadedLoader: any) => {
      try {
        await cascadedLoader.import(main.toString(), undefined, Object.create(null));
        appCodeLoaded!();
      } catch (err) {
        appCodeLoaded!();
        process.emit('uncaughtException', err as Error);
      }
    });
  } else {
    // Call appCodeLoaded before just for safety, it doesn't matter here as _load is syncronous
    appCodeLoaded!();
    process._firstFileName = Module._resolveFilename(path.join(packagePath, mainStartupScript), null, false);
    Module._load(path.join(packagePath, mainStartupScript), Module, true);
  }
} else {
  console.error('Failed to locate a valid package to load (app, app.asar or default_app.asar)');
  console.error('This normally means you\'ve damaged the Electron package somehow');
  appCodeLoaded!();
}
