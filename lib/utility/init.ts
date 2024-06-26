import { EventEmitter } from 'events';
import { pathToFileURL } from 'url';

import { ParentPort } from '@electron/internal/utility/parent-port';

const v8Util = process._linkedBinding('electron_common_v8_util');

const entryScript: string = v8Util.getHiddenValue(process, '_serviceStartupScript');
// We modified the original process.argv to let node.js load the init.js,
// we need to restore it here.
process.argv.splice(1, 1, entryScript);

// Import common settings.
require('@electron/internal/common/init');

process._linkedBinding('electron_browser_event_emitter').setEventEmitterPrototype(EventEmitter.prototype);

const parentPort: ParentPort = new ParentPort();
Object.defineProperty(process, 'parentPort', {
  enumerable: true,
  writable: false,
  value: parentPort
});

// Based on third_party/electron_node/lib/internal/worker/io.js
parentPort.on('newListener', (name: string) => {
  if (name === 'message' && parentPort.listenerCount('message') === 0) {
    parentPort.start();
  }
});

parentPort.on('removeListener', (name: string) => {
  if (name === 'message' && parentPort.listenerCount('message') === 0) {
    parentPort.pause();
  }
});

// Finally load entry script.
const { runEntryPointWithESMLoader } = __non_webpack_require__('internal/modules/run_main');
const mainEntry = pathToFileURL(entryScript);

runEntryPointWithESMLoader(async (cascadedLoader: any) => {
  try {
    await cascadedLoader.import(mainEntry.toString(), undefined, Object.create(null));
  } catch (err) {
    // @ts-ignore internalBinding is a secret internal global that we shouldn't
    // really be using, so we ignore the type error instead of declaring it in types
    internalBinding('errors').triggerUncaughtException(err);
  }
});
