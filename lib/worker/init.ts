import * as path from 'path';

const Module = require('module') as NodeJS.ModuleInternal;

// We modified the original process.argv to let node.js load the
// init.js, we need to restore it here.
process.argv.splice(1, 1);

// Import common settings.
require('@electron/internal/common/init');

// Process command line arguments.
const { hasSwitch, getSwitchValue } = process._linkedBinding('electron_common_command_line');

// Export node bindings to global.
const { makeRequireFunction } = __non_webpack_require__('internal/modules/helpers');
global.module = new Module('electron/js2c/worker_init');
global.require = makeRequireFunction(global.module);

// See WebWorkerObserver::WorkerScriptReadyForEvaluation.
if ((globalThis as any).blinkfetch) {
  const keys = ['fetch', 'Response', 'FormData', 'Request', 'Headers'];
  for (const key of keys) {
    (globalThis as any)[key] = (globalThis as any)[`blink${key}`];
  }
}

// Set the __filename to the path of html file if it is file: protocol.
// NB. 'self' isn't defined in an AudioWorklet.
if (typeof self !== 'undefined' && self.location.protocol === 'file:') {
  const pathname = process.platform === 'win32' && self?.location.pathname[0] === '/' ? self?.location.pathname.substr(1) : self?.location.pathname;
  global.__filename = path.normalize(decodeURIComponent(pathname));
  global.__dirname = path.dirname(global.__filename);

  // Set module's filename so relative require can work as expected.
  global.module.filename = global.__filename;

  // Also search for module under the html file.
  global.module.paths = Module._nodeModulePaths(global.__dirname);
} else {
  // For backwards compatibility we fake these two paths here
  global.__filename = path.join(process.resourcesPath, 'electron.asar', 'worker', 'init.js');
  global.__dirname = path.join(process.resourcesPath, 'electron.asar', 'worker');

  const appPath = hasSwitch('app-path') ? getSwitchValue('app-path') : null;
  if (appPath) {
    // Search for module under the app directory.
    global.module.paths = Module._nodeModulePaths(appPath);
  }
}
