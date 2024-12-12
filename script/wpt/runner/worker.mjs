import { net } from 'electron';

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { runInThisContext } from 'node:vm';

const basePath = process.env.WPT_DIR;

process.on('uncaughtException', (err) => {
  console.log('uncaughtException', err);
  process.parentPort.postMessage({
    type: 'error',
    error: {
      message: err.message,
      name: err.name,
      stack: err.stack
    }
  });
});

const globalPropertyDescriptors = {
  writable: true,
  enumerable: false,
  configurable: true
};

Object.defineProperties(globalThis, {
  fetch: {
    ...globalPropertyDescriptors,
    enumerable: true,
    value: net.fetch
  }
});

// TODO: remove once Float16Array is added. Otherwise a test throws with an uncaught exception.
globalThis.Float16Array ??= class Float16Array {};

process.parentPort.on('message', (message) => {
  const { meta, test, url, path } = message.data.workerData;
  const urlPath = path.slice(basePath.length);

  // self is required by testharness
  // GLOBAL is required by self
  runInThisContext(`
    globalThis.self = globalThis
    globalThis.GLOBAL = {
      isWorker () {
        return false
      },
      isShadowRealm () {
        return false
      },
      isWindow () {
        return false
      }
    }
    globalThis.window = globalThis
    globalThis.location = new URL('${urlPath.replace(/\\/g, '/')}', '${url}')
    globalThis.Window = Object.getPrototypeOf(globalThis).constructor
  `);

  if (meta.title) {
    runInThisContext(`globalThis.META_TITLE = "${meta.title.replace(/"/g, '\\"')}"`);
  }

  const harness = readFileSync(join(basePath, '/resources/testharness.js'), 'utf-8');
  runInThisContext(harness);

  // add_*_callback comes from testharness
  // stolen from node's wpt test runner
  // eslint-disable-next-line no-undef
  add_result_callback((result) => {
    process.parentPort.postMessage({
      type: 'result',
      result: {
        status: result.status,
        name: result.name,
        message: result.message,
        stack: result.stack
      }
    });
  });

  // eslint-disable-next-line no-undef
  add_completion_callback((_, status) => {
    process.parentPort.postMessage({
      type: 'completion',
      status
    });
  });

  const globalOrigin = Symbol.for('undici.globalOrigin.1');
  function setGlobalOrigin (newOrigin) {
    if (newOrigin === undefined) {
      Object.defineProperty(globalThis, globalOrigin, {
        value: undefined,
        writable: true,
        enumerable: false,
        configurable: false
      });

      return;
    }

    const parsedURL = new URL(newOrigin);

    if (parsedURL.protocol !== 'http:' && parsedURL.protocol !== 'https:') {
      throw new TypeError(`Only http & https urls are allowed, received ${parsedURL.protocol}`);
    }

    Object.defineProperty(globalThis, globalOrigin, {
      value: parsedURL,
      writable: true,
      enumerable: false,
      configurable: false
    });
  }

  setGlobalOrigin(globalThis.location);

  // Inject any files from the META tags
  for (const script of meta.scripts) {
    runInThisContext(script);
  }

  // Finally, run the test.
  runInThisContext(test);
});
