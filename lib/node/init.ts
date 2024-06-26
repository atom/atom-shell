// Initialize ASAR support in fs module.
import { wrapFsWithAsar } from './asar-fs-wrapper';
wrapFsWithAsar(require('fs'));

// See ElectronRendererClient::DidCreateScriptContext.
if ((globalThis as any).blinkfetch) {
  const keys = ['fetch', 'Response', 'FormData', 'Request', 'Headers'];
  for (const key of keys) {
    (globalThis as any)[key] = (globalThis as any)[`blink${key}`];
  }
}

// Hook child_process.fork.
import cp = require('child_process'); // eslint-disable-line import/first
const originalFork = cp.fork;
cp.fork = (modulePath, args?, options?: cp.ForkOptions) => {
  // Parse optional args.
  if (args == null) {
    args = [];
  } else if (typeof args === 'object' && !Array.isArray(args)) {
    options = args as cp.ForkOptions;
    args = [];
  }
  // Fallback to original fork to report arg type errors.
  if (typeof modulePath !== 'string' || !Array.isArray(args) ||
      (typeof options !== 'object' && typeof options !== 'undefined')) {
    return originalFork(modulePath, args, options);
  }
  // When forking a child script, we setup a special environment to make
  // the electron binary run like upstream Node.js.
  options = options ?? {};
  options.env = Object.create(options.env || process.env);
  options.env!.ELECTRON_RUN_AS_NODE = '1';
  // On mac the child script runs in helper executable.
  if (!options.execPath && process.platform === 'darwin') {
    options.execPath = process.helperExecPath;
  }
  return originalFork(modulePath, args, options);
};

// Prevent Node from adding paths outside this app to search paths.
import path = require('path'); // eslint-disable-line import/first
const Module = require('module') as NodeJS.ModuleInternal;
const resourcesPathWithTrailingSlash = process.resourcesPath + path.sep;
const originalNodeModulePaths = Module._nodeModulePaths;
Module._nodeModulePaths = function (from) {
  const paths: string[] = originalNodeModulePaths(from);
  const fromPath = path.resolve(from) + path.sep;
  // If "from" is outside the app then we do nothing.
  if (fromPath.startsWith(resourcesPathWithTrailingSlash)) {
    return paths.filter(function (candidate) {
      return candidate.startsWith(resourcesPathWithTrailingSlash);
    });
  } else {
    return paths;
  }
};
