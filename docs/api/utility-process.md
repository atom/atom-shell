# utilityProcess

`utilityProcess` creates a child process with
Node.js and Message ports enabled. It provides the equivalent of [`child_process.fork`][] API from Node.js
but instead uses [Services API][] from Chromium to launch the child process.

Process: [Main](../glossary.md#main-process)<br />

## Methods

### `utilityProcess.fork(modulePath[, args][, options])`

* `modulePath` string - Path to the script that should run as entrypoint in the child process.
* `args` string[] (optional) - List of string arguments that will be available as `process.argv`
  in the child process.
* `options` Object (optional)
  * `env` Object (optional) - Environment key-value pairs. Default is `process.env`.
  * `execArgv` string[] (optional) - List of string arguments passed to the executable.
  * `cwd` string (optional) - Current working directory of the child process.
  * `stdio` (string[] | string) (optional) - Allows configuring the mode for `stdout` and `stderr`
    of the child process. Default is `inherit`.
    String value can be one of `pipe`, `ignore`, `inherit`, for more details on these values you can refer to
    [stdio][] documentation from Node.js. Currently this option only supports configuring `stdout` and
    `stderr` to either `pipe`, `inherit` or `ignore`. Configuring `stdin` to any property other than `ignore` is not supported and will result in an error.
    For example, the supported values will be processed as following:
    * `pipe`: equivalent to \['ignore', 'pipe', 'pipe']
    * `ignore`: equivalent to \['ignore', 'ignore', 'ignore']
    * `inherit`: equivalent to \['ignore', 'inherit', 'inherit'] (the default)
  * `serviceName` string (optional) - Name of the process that will appear in `name` property of
    [`ProcessMetric`](structures/process-metric.md) returned by [`app.getAppMetrics`](app.md#appgetappmetrics)
    and [`child-process-gone` event of `app`](app.md#event-child-process-gone).
    Default is `Node Utility Process`.
  * `allowLoadingUnsignedLibraries` boolean (optional) _macOS_ - With this flag, the utility process will be
    launched via the `Electron Helper (Plugin).app` helper executable on macOS, which can be
    codesigned with `com.apple.security.cs.disable-library-validation` and
    `com.apple.security.cs.allow-unsigned-executable-memory` entitlements. This will allow the utility process
    to load unsigned libraries. Unless you specifically need this capability, it is best to leave this disabled.
    Default is `false`.
  * `respondToAuthRequestsFromMainProcess` boolean (optional) - With this flag, all HTTP 401 and 407 network
    requests created via the [net module](net.md) will allow responding to them via the [`app#login`](app.md#event-login) event in the main process instead of the default [`login`](client-request.md#event-login) event on the [`ClientRequest`](client-request.md) object.

Returns [`UtilityProcess`](utility-process.md#class-utilityprocess)

## Class: UtilityProcess

> Instances of the `UtilityProcess` represent the Chromium spawned child process
> with Node.js integration.

`UtilityProcess` is an [EventEmitter][event-emitter].

### Instance Methods

#### `child.postMessage(message, [transfer])`

* `message` any
* `transfer` MessagePortMain[] (optional)

Send a message to the child process, optionally transferring ownership of
zero or more [`MessagePortMain`][] objects.

For example:

```js
// Main process
const { port1, port2 } = new MessageChannelMain()
const child = utilityProcess.fork(path.join(__dirname, 'test.js'))
child.postMessage({ message: 'hello' }, [port1])

// Child process
process.parentPort.once('message', (e) => {
  const [port] = e.ports
  // ...
})
```

#### `child.kill()`

Returns `boolean`

Terminates the process gracefully. On POSIX, it uses SIGTERM
but will ensure the process is reaped on exit. This function returns
true if the kill is successful, and false otherwise.

### Instance Properties

#### `child.pid`

A `Integer | undefined` representing the process identifier (PID) of the child process.
If the child process fails to spawn due to errors, then the value is `undefined`. When
the child process exits, then the value is `undefined` after the `exit` event is emitted.

#### `child.stdout`

A `NodeJS.ReadableStream | null` that represents the child process's stdout.
If the child was spawned with options.stdio\[1] set to anything other than 'pipe', then this will be `null`.
When the child process exits, then the value is `null` after the `exit` event is emitted.

```js
// Main process
const { port1, port2 } = new MessageChannelMain()
const child = utilityProcess.fork(path.join(__dirname, 'test.js'))
child.stdout.on('data', (data) => {
  console.log(`Received chunk ${data}`)
})
```

#### `child.stderr`

A `NodeJS.ReadableStream | null` that represents the child process's stderr.
If the child was spawned with options.stdio\[2] set to anything other than 'pipe', then this will be `null`.
When the child process exits, then the value is `null` after the `exit` event is emitted.

### Instance Events

#### Event: 'spawn'

Emitted once the child process has spawned successfully.

#### Event: 'exit'

Returns:

* `code` number - Contains the exit code for
the process obtained from waitpid on posix, or GetExitCodeProcess on windows.

Emitted after the child process ends.

#### Event: 'message'

Returns:

* `message` any

Emitted when the child process sends a message using [`process.parentPort.postMessage()`](process.md#processparentport).

[`child_process.fork`]: https://nodejs.org/dist/latest-v16.x/docs/api/child_process.html#child_processforkmodulepath-args-options
[Services API]: https://chromium.googlesource.com/chromium/src/+/main/docs/mojo_and_services.md
[stdio]: https://nodejs.org/dist/latest/docs/api/child_process.html#optionsstdio
[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter
[`MessagePortMain`]: message-port-main.md
