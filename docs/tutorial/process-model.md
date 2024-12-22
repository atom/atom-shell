---
title: 'Process Model'
description: 'Electron inherits its multi-process architecture from Chromium, which makes the framework architecturally very similar to a modern web browser. This guide will expand on the concepts applied in the tutorial.'
slug: process-model
hide_title: false
---

# Process Model

Electron inherits its multi-process architecture from Chromium, which makes the framework
architecturally very similar to a modern web browser. This guide will expand on the
concepts applied in the [Tutorial][tutorial].

[tutorial]: ./tutorial-1-prerequisites.md

## Why not a single process?

Web browsers are incredibly complicated applications. Aside from their primary ability
to display web content, they have many secondary responsibilities,
such as managing multiple windows (or tabs) and loading third-party extensions.

In the earlier days, browsers usually used a single process for all of this
functionality. Although this pattern meant less overhead for each tab you had open,
it also meant that one website crashing or hanging would affect the entire browser.

## The multi-process model

To solve this problem, the Chrome team decided that each tab would render in its own
process, limiting the harm that buggy or malicious code on a web page could cause to
the app as a whole. A single browser process then controls these processes, as well
as the application lifecycle as a whole. This diagram below from the [Chrome Comic][]
visualizes this model:

![Chrome's multi-process architecture](../images/chrome-processes.png)

Electron applications are structured very similarly. As an app developer, you control
two types of processes: [main](#the-main-process) and [renderer](#the-renderer-process).
These are analogous to Chrome's own browser and renderer processes outlined above.

[chrome comic]: https://www.google.com/googlebooks/chrome/

## The main process

Each Electron app has a single main process, which acts as the application's entry
point. The main process runs in a Node.js environment, meaning it has the ability
to `require` modules and use all of Node.js APIs.

### Window management

The main process' primary purpose is to create and manage application windows with the
[`BrowserWindow`][browser-window] module.

Each instance of the `BrowserWindow` class creates an application window that loads
a web page in a separate renderer process. You can interact with this web content
from the main process using the window's [`webContents`][web-contents] object.

```js title='main.js'
const { BrowserWindow } = require('electron')

const win = new BrowserWindow({ width: 800, height: 1500 })
win.loadURL('https://github.com')

const contents = win.webContents
console.log(contents)
```

> Note: A renderer process is also created for [web embeds][web-embed] such as the
> `BrowserView` module. The `webContents` object is also accessible for embedded
> web content.

Because the `BrowserWindow` module is an [`EventEmitter`][event-emitter], you can also
add handlers for various user events (for example, minimizing or maximizing your window).

When a `BrowserWindow` instance is destroyed, its corresponding renderer process gets
terminated as well.

[browser-window]: ../api/browser-window.md
[web-embed]: ../tutorial/web-embeds.md
[web-contents]: ../api/web-contents.md
[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter

### Application lifecycle

The main process also controls your application's lifecycle through Electron's
[`app`][app] module. This module provides a large set of events and methods
that you can use to add custom application behavior (for instance, programmatically
quitting your application, modifying the application dock, or showing an About panel).

As a practical example, the app shown in the [quick start guide][quick-start-lifecycle]
uses `app` APIs to create a more native application window experience.

```js title='main.js'
// quitting the app when no windows are open on non-macOS platforms
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})
```

[app]: ../api/app.md
[quick-start-lifecycle]: ../tutorial/quick-start.md#manage-your-windows-lifecycle

### Native APIs

To extend Electron's features beyond being a Chromium wrapper for web contents, the
main process also adds custom APIs to interact with the user's operating system.
Electron exposes various modules that control native desktop functionality, such
as menus, dialogs, and tray icons.

For a full list of Electron's main process modules, check out our API documentation.

## The renderer process

Each Electron app spawns a separate renderer process for each open `BrowserWindow`
(and each web embed). As its name implies, a renderer is responsible for
_rendering_ web content. For all intents and purposes, code ran in renderer processes
should behave according to web standards (insofar as Chromium does, at least).

Therefore, all user interfaces and app functionality within a single browser
window should be written with the same tools and paradigms that you use on the
web.

Although explaining every web spec is out of scope for this guide, the bare minimum
to understand is:

- An HTML file is your entry point for the renderer process.
- UI styling is added through Cascading Style Sheets (CSS).
- Executable JavaScript code can be added through `<script>` elements.

Moreover, this also means that the renderer has no direct access to `require`
or other Node.js APIs. In order to directly include NPM modules in the renderer,
you must use the same bundler toolchains (for example, `webpack` or `parcel`) that you
use on the web.

:::warning

Renderer processes can be spawned with a full Node.js environment for ease of
development. Historically, this used to be the default, but this feature was disabled
for security reasons.

:::

At this point, you might be wondering how your renderer process user interfaces
can interact with Node.js and Electron's native desktop functionality if these
features are only accessible from the main process. In fact, there is no direct
way to import Electron's content scripts.

## Preload scripts

<!-- Note: This guide doesn't take sandboxing into account, which might fundamentally
change the statements here. -->

Preload scripts contain code that executes in a renderer process before its web content
begins loading. These scripts run within the renderer context, but are granted more
privileges by having access to Node.js APIs.

A preload script can be attached to the main process in the `BrowserWindow` constructor's
`webPreferences` option.

```js title='main.js'
const { BrowserWindow } = require('electron')
// ...
const win = new BrowserWindow({
  webPreferences: {
    preload: 'path/to/preload.js'
  }
})
// ...
```

Because the preload script shares a global [`Window`][window-mdn] interface with the
renderers and can access Node.js APIs, it serves to enhance your renderer by exposing
arbitrary APIs in the `window` global that your web contents can then consume.

Although preload scripts share a `window` global with the renderer they're attached to,
you cannot directly attach any variables from the preload script to `window` because of
the [`contextIsolation`][context-isolation] default.

```js title='preload.js' @ts-nocheck
window.myAPI = {
  desktop: true
}
```

```js title='renderer.js' @ts-nocheck
console.log(window.myAPI)
// => undefined
```

Context Isolation means that preload scripts are isolated from the renderer's main world
to avoid leaking any privileged APIs into your web content's code.

Instead, use the [`contextBridge`][context-bridge] module to accomplish this
securely:

```js title='preload.js'
const { contextBridge } = require('electron')

contextBridge.exposeInMainWorld('myAPI', {
  desktop: true
})
```

```js title='renderer.js' @ts-nocheck
console.log(window.myAPI)
// => { desktop: true }
```

This feature is incredibly useful for two main purposes:

- By exposing [`ipcRenderer`][ipcrenderer] helpers to the renderer, you can use
  inter-process communication (IPC) to trigger main process tasks from the
  renderer (and vice-versa).
- If you're developing an Electron wrapper for an existing web app hosted on a remote
  URL, you can add custom properties onto the renderer's `window` global that can
  be used for desktop-only logic on the web client's side.

## The utility process

Each Electron app can spawn multiple child processes from the main process using
the [UtilityProcess][] API. The utility process runs in a Node.js environment,
meaning it has the ability to `require` modules and use all of Node.js APIs.
The utility process can be used to host for example: untrusted services,
CPU intensive tasks or crash prone components which would have previously
been hosted in the main process or process spawned with Node.js [`child_process.fork`][] API.
The primary difference between the utility process and process spawned by Node.js
child_process module is that the utility process can establish a communication
channel with a renderer process using [`MessagePort`][]s. An Electron app can
always prefer the [UtilityProcess][] API over Node.js [`child_process.fork`][] API when
there is need to fork a child process from the main process.

## Process-specific module aliases (TypeScript)

Electron's npm package also exports subpaths that contain a subset of
Electron's TypeScript type definitions.

- `electron/main` includes types for all main process modules.
- `electron/renderer` includes types for all renderer process modules.
- `electron/common` includes types for modules that can run in main and renderer processes.

These aliases have no impact on runtime, but can be used for typechecking
and autocomplete.

```js title="Usage example"
const { shell } = require('electron/common')
const { app } = require('electron/main')
```

[window-mdn]: https://developer.mozilla.org/en-US/docs/Web/API/Window
[`MessagePort`]: https://developer.mozilla.org/en-US/docs/Web/API/MessagePort
[`child_process.fork`]: https://nodejs.org/dist/latest-v16.x/docs/api/child_process.html#child_processforkmodulepath-args-options
[context-isolation]: ./context-isolation.md
[context-bridge]: ../api/context-bridge.md
[ipcrenderer]: ../api/ipc-renderer.md
[UtilityProcess]: ../api/utility-process.md
