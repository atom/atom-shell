# Process Sandboxing

One key security feature in Chromium is that processes can be executed within a sandbox.
The sandbox limits the harm that malicious code can cause by limiting access to most
system resources — sandboxed processes can only freely use CPU cycles and memory.
In order to perform operations requiring additional privilege, sandboxed processes
use dedicated communication channels to delegate tasks to more privileged processes.

In Chromium, sandboxing is applied to most processes other than the main process.
This includes renderer processes, as well as utility processes such as the audio service,
the GPU service and the network service.

See Chromium's [Sandbox design document][sandbox] for more information.

Starting from Electron 20, the sandbox is enabled for renderer processes without any
further configuration.

Sandboxing is tied to Node.js integration. _Enabling Node.js integration_ for a
renderer process by setting `nodeIntegration: true` _disables the sandbox_ for the
process.

If you want to disable the sandbox for a process, see the
[Disabling the sandbox for a single process](#disabling-the-sandbox-for-a-single-process)
section.

## Sandbox behavior in Electron

Sandboxed processes in Electron behave _mostly_ in the same way as Chromium's do, but
Electron has a few additional concepts to consider because it interfaces with Node.js.

### Renderer processes

When renderer processes in Electron are sandboxed, they behave in the same way as a
regular Chrome renderer would. A sandboxed renderer won't have a Node.js
environment initialized.

Therefore, when the sandbox is enabled, renderer processes can only perform privileged
tasks (such as interacting with the filesystem, making changes to the system, or spawning
subprocesses) by delegating these tasks to the main process via inter-process
communication (IPC).

:::note

For more info on inter-process communication, check out our [IPC guide](./ipc.md).

:::

### Preload scripts

In order to allow renderer processes to communicate with the main process, preload
scripts attached to sandboxed renderers will still have a polyfilled subset of Node.js
APIs available. A `require` function similar to Node's `require` module is exposed,
but can only import a subset of Electron and Node's built-in modules:

* `electron` (following renderer process modules: `contextBridge`, `crashReporter`, `ipcRenderer`, `nativeImage`, `webFrame`, `webUtils`)
* [`events`](https://nodejs.org/api/events.html)
* [`timers`](https://nodejs.org/api/timers.html)
* [`url`](https://nodejs.org/api/url.html)

[node: imports](https://nodejs.org/api/esm.html#node-imports) are supported as well:

* [`node:events`](https://nodejs.org/api/events.html)
* [`node:timers`](https://nodejs.org/api/timers.html)
* [`node:url`](https://nodejs.org/api/url.html)

In addition, the preload script also polyfills certain Node.js primitives as globals:

* [`Buffer`](https://nodejs.org/api/buffer.html)
* [`process`](../api/process.md)
* [`clearImmediate`](https://nodejs.org/api/timers.html#timers_clearimmediate_immediate)
* [`setImmediate`](https://nodejs.org/api/timers.html#timers_setimmediate_callback_args)

Because the `require` function is a polyfill with limited functionality, you will not be
able to use [CommonJS modules][commonjs] to separate your preload script into multiple
files. If you need to split your preload code, use a bundler such as [webpack][webpack]
or [Parcel][parcel].

Note that because the environment presented to the `preload` script is substantially
more privileged than that of a sandboxed renderer, it is still possible to leak
privileged APIs to untrusted code running in the renderer process unless
[`contextIsolation`][context-isolation] is enabled.

## Configuring the sandbox

For most apps, sandboxing is the best choice. In certain use cases that are incompatible with
the sandbox (for instance, when using native node modules in the renderer),
it is possible to disable the sandbox for specific processes. This comes with security
risks, especially if any untrusted code or content is present in the unsandboxed process.

### Disabling the sandbox for a single process

In Electron, renderer sandboxing can be disabled on a per-process basis with
the `sandbox: false` preference in the [`BrowserWindow`][browser-window] constructor.

```js title='main.js'
app.whenReady().then(() => {
  const win = new BrowserWindow({
    webPreferences: {
      sandbox: false
    }
  })
  win.loadURL('https://google.com')
})
```

Sandboxing is also disabled whenever Node.js integration is enabled in the renderer.
This can be done through the BrowserWindow constructor with the `nodeIntegration: true` flag
or by providing the respective HTML boolean attribute for a `webview`.

```js title='main.js'
app.whenReady().then(() => {
  const win = new BrowserWindow({
    webPreferences: {
      nodeIntegration: true
    }
  })
  win.loadURL('https://google.com')
})
```

```html title='index.html (Renderer Process)'
<webview nodeIntegration src="page.html"></webview>
```

### Enabling the sandbox globally

If you want to force sandboxing for all renderers, you can also use the
[`app.enableSandbox`][enable-sandbox] API. Note that this API has to be called before the
app's `ready` event.

```js title='main.js'
app.enableSandbox()
app.whenReady().then(() => {
  // any sandbox:false calls are overridden since `app.enableSandbox()` was called.
  const win = new BrowserWindow()
  win.loadURL('https://google.com')
})
```

### Disabling Chromium's sandbox (testing only)

You can also disable Chromium's sandbox entirely with the [`--no-sandbox`][no-sandbox]
CLI flag, which will disable the sandbox for all processes (including utility processes).
We highly recommend that you only use this flag for testing purposes, and **never**
in production.

Note that the `sandbox: true` option will still disable the renderer's Node.js
environment.

## A note on rendering untrusted content

Rendering untrusted content in Electron is still somewhat uncharted territory,
though some apps are finding success (e.g. [Beaker Browser][beaker]).
Our goal is to get as close to Chrome as we can in terms of the security of
sandboxed content, but ultimately we will always be behind due to a few fundamental
issues:

1. We do not have the dedicated resources or expertise that Chromium has to
   apply to the security of its product. We do our best to make use of what we
   have, to inherit everything we can from Chromium, and to respond quickly to
   security issues, but Electron cannot be as secure as Chromium without the
   resources that Chromium is able to dedicate.
1. Some security features in Chrome (such as Safe Browsing and Certificate
   Transparency) require a centralized authority and dedicated servers, both of
   which run counter to the goals of the Electron project. As such, we disable
   those features in Electron, at the cost of the associated security they
   would otherwise bring.
1. There is only one Chromium, whereas there are many thousands of apps built
   on Electron, all of which behave slightly differently. Accounting for those
   differences can yield a huge possibility space, and make it challenging to
   ensure the security of the platform in unusual use cases.
1. We can't push security updates to users directly, so we rely on app vendors
   to upgrade the version of Electron underlying their app in order for
   security updates to reach users.

While we make our best effort to backport Chromium security fixes to older
versions of Electron, we do not make a guarantee that every fix will be
backported. Your best chance at staying secure is to be on the latest stable
version of Electron.

[sandbox]: https://chromium.googlesource.com/chromium/src/+/main/docs/design/sandbox.md
[browser-window]: ../api/browser-window.md
[enable-sandbox]: ../api/app.md#appenablesandbox
[no-sandbox]: ../api/command-line-switches.md#--no-sandbox
[commonjs]: https://nodejs.org/api/modules.html#modules_modules_commonjs_modules
[webpack]: https://webpack.js.org/
[parcel]: https://parceljs.org/
[context-isolation]: ./context-isolation.md
[beaker]: https://github.com/beakerbrowser/beaker
