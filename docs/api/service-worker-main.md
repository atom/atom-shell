# ServiceWorkerMain

> An instance of a Service Worker representing a version of a script for a given scope.

Process: [Main](../glossary.md#main-process)

## Class: ServiceWorkerMain

Process: [Main](../glossary.md#main-process)<br />
_This class is not exported from the `'electron'` module. It is only available as a return value of other methods in the Electron API._

### Instance Methods

#### `serviceWorker.isDestroyed()` _Experimental_

Returns `boolean` - Whether the service worker has been destroyed.

#### `serviceWorker.send(channel, ...args)` _Experimental_

- `channel` string
- `...args` any[]

Send an asynchronous message to the service worker process via `channel`, along with
arguments. Arguments will be serialized with the [Structured Clone Algorithm][SCA],
just like [`postMessage`][], so prototype chains will not be included.
Sending Functions, Promises, Symbols, WeakMaps, or WeakSets will throw an exception.

The service worker process can handle the message by listening to `channel` with the
[`ipcRenderer`](ipc-renderer.md) module.

#### `serviceWorker.startTask()` _Experimental_

Returns `Object`:

- `end` Function - Method to call when the task has ended. If never called, the service won't terminate while otherwise idle.

Initiate a task to keep the service worker alive until ended.

### Instance Properties

#### `serviceWorker.ipc` _Readonly_ _Experimental_

An [`IpcMainServiceWorker`](ipc-main-service-worker.md) instance scoped to the service worker.

#### `serviceWorker.scope` _Readonly_ _Experimental_

A `string` representing the scope URL of the service worker.

#### `serviceWorker.scriptURL` _Readonly_ _Experimental_

A `string` representing the script URL of the service worker.

#### `serviceWorker.versionId` _Readonly_ _Experimental_

A `number` representing the ID of the specific version of the service worker script in its scope.

[SCA]: https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Structured_clone_algorithm
[`postMessage`]: https://developer.mozilla.org/en-US/docs/Web/API/Window/postMessage
