---
title: 'Building your First App'
description: 'This guide will step you through the process of creating a barebones Hello World app in Electron, similar to electron/electron-quick-start.'
slug: tutorial-first-app
hide_title: false
---

:::info Follow along the tutorial

This is **part 2** of the Electron tutorial.

1. [Prerequisites][prerequisites]
1. **[Building your First App][building your first app]**
1. [Using Preload Scripts][preload]
1. [Adding Features][features]
1. [Packaging Your Application][packaging]
1. [Publishing and Updating][updates]

:::

## Learning goals

In this part of the tutorial, you will learn how to set up your Electron project
and write a minimal starter application. By the end of this section,
you should be able to run a working Electron app in development mode from
your terminal.

## Setting up your project

:::caution Avoid WSL

If you are on a Windows machine, please do not use [Windows Subsystem for Linux][wsl] (WSL)
when following this tutorial as you will run into issues when trying to execute the
application.

<!--https://www.electronforge.io/guides/developing-with-wsl-->

:::

### Initializing your npm project

Electron apps are scaffolded using npm, with the package.json file
as an entry point. Start by creating a folder and initializing an npm package
within it with `npm init`.

```sh npm2yarn
mkdir my-electron-app && cd my-electron-app
npm init
```

This command will prompt you to configure some fields in your package.json.
There are a few rules to follow for the purposes of this tutorial:

- _entry point_ should be `main.js` (you will be creating that file soon).
- _author_, _license_, and _description_ can be any value, but will be necessary for
  [packaging][packaging] later on.

Then, install Electron into your app's **devDependencies**, which is the list of external
development-only package dependencies not required in production.

:::info Why is Electron a devDependency?

This may seem counter-intuitive since your production code is running Electron APIs.
However, packaged apps will come bundled with the Electron binary, eliminating the need to specify
it as a production dependency.

:::

```sh npm2yarn
npm install electron --save-dev
```

Your package.json file should look something like this after initializing your package
and installing Electron. You should also now have a `node_modules` folder containing
the Electron executable, as well as a `package-lock.json` lockfile that specifies
the exact dependency versions to install.

```json title='package.json'
{
  "name": "my-electron-app",
  "version": "1.0.0",
  "description": "Hello World!",
  "main": "main.js",
  "scripts": {
    "test": "echo \"Error: no test specified\" && exit 1"
  },
  "author": "Jane Doe",
  "license": "MIT",
  "devDependencies": {
    "electron": "23.1.3"
  }
}
```

:::info Advanced Electron installation steps

If installing Electron directly fails, please refer to our [Advanced Installation][installation]
documentation for instructions on download mirrors, proxies, and troubleshooting steps.

:::

### Adding a .gitignore

The [`.gitignore`][gitignore] file specifies which files and directories to avoid tracking
with Git. You should place a copy of [GitHub's Node.js gitignore template][gitignore-template]
into your project's root folder to avoid committing your project's `node_modules` folder.

## Running an Electron app

:::tip Further reading

Read [Electron's process model][process-model] documentation to better
understand how Electron's multiple processes work together.

:::

The [`main`][package-json-main] script you defined in package.json is the entry point of any
Electron application. This script controls the **main process**, which runs in a Node.js
environment and is responsible for controlling your app's lifecycle, displaying native
interfaces, performing privileged operations, and managing renderer processes
(more on that later).

Before creating your first Electron app, you will first use a trivial script to ensure your
main process entry point is configured correctly. Create a `main.js` file in the root folder
of your project with a single line of code:

```js title='main.js'
console.log('Hello from Electron 👋')
```

Because Electron's main process is a Node.js runtime, you can execute arbitrary Node.js code
with the `electron` command (you can even use it as a [REPL][]). To execute this script,
add `electron .` to the `start` command in the [`scripts`][package-scripts]
field of your package.json. This command will tell the Electron executable to look for the main
script in the current directory and run it in dev mode.

```json {7} title='package.json'
{
  "name": "my-electron-app",
  "version": "1.0.0",
  "description": "Hello World!",
  "main": "main.js",
  "scripts": {
    "start": "electron .",
    "test": "echo \"Error: no test specified\" && exit 1"
  },
  "author": "Jane Doe",
  "license": "MIT",
  "devDependencies": {
    "electron": "23.1.3"
  }
}
```

```sh npm2yarn
npm run start
```

Your terminal should print out `Hello from Electron 👋`. Congratulations,
you have executed your first line of code in Electron! Next, you will learn
how to create user interfaces with HTML and load that into a native window.

## Loading a web page into a BrowserWindow

In Electron, each window displays a web page that can be loaded either from a local HTML
file or a remote web address. For this example, you will be loading in a local file. Start
by creating a barebones web page in an `index.html` file in the root folder of your project:

```html title='index.html'
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8" />
    <!-- https://developer.mozilla.org/en-US/docs/Web/HTTP/CSP -->
    <meta
      http-equiv="Content-Security-Policy"
      content="default-src 'self'; script-src 'self'"
    />
    <meta
      http-equiv="X-Content-Security-Policy"
      content="default-src 'self'; script-src 'self'"
    />
    <title>Hello from Electron renderer!</title>
  </head>
  <body>
    <h1>Hello from Electron renderer!</h1>
    <p>👋</p>
  </body>
</html>
```

Now that you have a web page, you can load it into an Electron [BrowserWindow][browser-window].
Replace the contents of your `main.js` file with the following code. We will explain each
highlighted block separately.

```js {1,3-10,12-14} title='main.js' showLineNumbers
const { app, BrowserWindow } = require('electron')

const createWindow = () => {
  const win = new BrowserWindow({
    width: 800,
    height: 600
  })

  win.loadFile('index.html')
}

app.whenReady().then(() => {
  createWindow()
})
```

### Importing modules

```js title='main.js (Line 1)'
const { app, BrowserWindow } = require('electron')
```

In the first line, we are importing two Electron modules
with CommonJS module syntax:

- [app][app], which controls your application's event lifecycle.
- [BrowserWindow][browser-window], which creates and manages app windows.

<details>
<summary>Module capitalization conventions</summary>

You might have noticed the capitalization difference between the **a**pp
and **B**rowser**W**indow modules. Electron follows typical JavaScript conventions here,
where PascalCase modules are instantiable class constructors (e.g. BrowserWindow, Tray,
Notification) whereas camelCase modules are not instantiable (e.g. app, ipcRenderer, webContents).

</details>

<details>
<summary>Typed import aliases</summary>

For better type checking when writing TypeScript code, you can choose to import
main process modules from `electron/main`.

```js
const { app, BrowserWindow } = require('electron/main')
```

For more information, see the [Process Model docs](../tutorial/process-model.md#process-specific-module-aliases-typescript).
</details>

:::info ES Modules in Electron

[ECMAScript modules](https://nodejs.org/api/esm.html) (i.e. using `import` to load a module)
are supported in Electron as of Electron 28. You can find more information about the
state of ESM in Electron and how to use them in our app in [our ESM guide](../tutorial/esm.md).

:::

### Writing a reusable function to instantiate windows

The `createWindow()` function loads your web page into a new BrowserWindow instance:

```js title='main.js (Lines 3-10)'
const createWindow = () => {
  const win = new BrowserWindow({
    width: 800,
    height: 600
  })

  win.loadFile('index.html')
}
```

### Calling your function when the app is ready

```js title='main.js (Lines 12-14)' @ts-type={createWindow:()=>void}
app.whenReady().then(() => {
  createWindow()
})
```

Many of Electron's core modules are Node.js [event emitters][] that adhere to Node's asynchronous
event-driven architecture. The app module is one of these emitters.

In Electron, BrowserWindows can only be created after the app module's [`ready`][app-ready] event
is fired. You can wait for this event by using the [`app.whenReady()`][app-when-ready] API and
calling `createWindow()` once its promise is fulfilled.

:::info

You typically listen to Node.js events by using an emitter's `.on` function.

```diff
+ app.on('ready', () => {
- app.whenReady().then(() => {
  createWindow()
})
```

However, Electron exposes `app.whenReady()` as a helper specifically for the `ready` event to
avoid subtle pitfalls with directly listening to that event in particular.
See [electron/electron#21972](https://github.com/electron/electron/pull/21972) for details.

:::

At this point, running your Electron application's `start` command should successfully
open a window that displays your web page!

Each web page your app displays in a window will run in a separate process called a
**renderer** process (or simply _renderer_ for short). Renderer processes have access
to the same JavaScript APIs and tooling you use for typical front-end web
development, such as using [webpack][] to bundle and minify your code or [React][react]
to build your user interfaces.

## Managing your app's window lifecycle

Application windows behave differently on each operating system. Rather than
enforce these conventions by default, Electron gives you the choice to implement
them in your app code if you wish to follow them. You can implement basic window
conventions by listening for events emitted by the app and BrowserWindow modules.

:::tip Process-specific control flow

Checking against Node's [`process.platform`][node-platform] variable can help you
to run code conditionally on certain platforms. Note that there are only three
possible platforms that Electron can run in: `win32` (Windows), `linux` (Linux),
and `darwin` (macOS).

:::

### Quit the app when all windows are closed (Windows & Linux)

On Windows and Linux, closing all windows will generally quit an application entirely.
To implement this pattern in your Electron app, listen for the app module's
[`window-all-closed`][window-all-closed] event, and call [`app.quit()`][app-quit]
to exit your app if the user is not on macOS.

```js
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
})
```

### Open a window if none are open (macOS)

In contrast, macOS apps generally continue running even without any windows open.
Activating the app when no windows are available should open a new one.

To implement this feature, listen for the app module's [`activate`][activate]
event, and call your existing `createWindow()` method if no BrowserWindows are open.

Because windows cannot be created before the `ready` event, you should only listen for
`activate` events after your app is initialized. Do this by only listening for activate
events inside your existing `whenReady()` callback.

```js @ts-type={createWindow:()=>void}
app.whenReady().then(() => {
  createWindow()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})
```

## Final starter code

```fiddle docs/fiddles/tutorial-first-app

```

## Optional: Debugging from VS Code

If you want to debug your application using VS Code, you need to attach VS Code to
both the main and renderer processes. Here is a sample configuration for you to
run. Create a launch.json configuration in a new `.vscode` folder in your project:

```json title='.vscode/launch.json'
{
  "version": "0.2.0",
  "compounds": [
    {
      "name": "Main + renderer",
      "configurations": ["Main", "Renderer"],
      "stopAll": true
    }
  ],
  "configurations": [
    {
      "name": "Renderer",
      "port": 9222,
      "request": "attach",
      "type": "chrome",
      "webRoot": "${workspaceFolder}"
    },
    {
      "name": "Main",
      "type": "node",
      "request": "launch",
      "cwd": "${workspaceFolder}",
      "runtimeExecutable": "${workspaceFolder}/node_modules/.bin/electron",
      "windows": {
        "runtimeExecutable": "${workspaceFolder}/node_modules/.bin/electron.cmd"
      },
      "args": [".", "--remote-debugging-port=9222"],
      "outputCapture": "std",
      "console": "integratedTerminal"
    }
  ]
}
```

The "Main + renderer" option will appear when you select "Run and Debug"
from the sidebar, allowing you to set breakpoints and inspect all the variables among
other things in both the main and renderer processes.

What we have done in the `launch.json` file is to create 3 configurations:

- `Main` is used to start the main process and also expose port 9222 for remote debugging
  (`--remote-debugging-port=9222`). This is the port that we will use to attach the debugger
  for the `Renderer`. Because the main process is a Node.js process, the type is set to
  `node`.
- `Renderer` is used to debug the renderer process. Because the main process is the one
  that creates the process, we have to "attach" to it (`"request": "attach"`) instead of
  creating a new one.
  The renderer process is a web one, so the debugger we have to use is `chrome`.
- `Main + renderer` is a [compound task][] that executes the previous ones simultaneously.

:::caution

Because we are attaching to a process in `Renderer`, it is possible that the first lines of
your code will be skipped as the debugger will not have had enough time to connect before they are
being executed.
You can work around this by refreshing the page or setting a timeout before executing the code
in development mode.

:::

:::info Further reading

If you want to dig deeper in the debugging area, the following guides provide more information:

- [Application Debugging][]
- [DevTools Extensions][devtools extension]

:::

## Summary

Electron applications are set up using npm packages. The Electron executable should be installed
in your project's `devDependencies` and can be run in development mode using a script in your
package.json file.

The executable runs the JavaScript entry point found in the `main` property of your package.json.
This file controls Electron's **main process**, which runs an instance of Node.js and is
responsible for your app's lifecycle, displaying native interfaces, performing privileged operations,
and managing renderer processes.

**Renderer processes** (or renderers for short) are responsible for displaying graphical content. You can
load a web page into a renderer by pointing it to either a web address or a local HTML file.
Renderers behave very similarly to regular web pages and have access to the same web APIs.

In the next section of the tutorial, we will be learning how to augment the renderer process with
privileged APIs and how to communicate between processes.

<!-- Links -->

[activate]: ../api/app.md#event-activate-macos
[app]: ../api/app.md
[app-quit]: ../api/app.md#appquit
[app-ready]: ../api/app.md#event-ready
[app-when-ready]: ../api/app.md#appwhenready
[application debugging]: ./application-debugging.md
[browser-window]: ../api/browser-window.md
[compound task]: https://code.visualstudio.com/Docs/editor/tasks#_compound-tasks
[devtools extension]: ./devtools-extension.md
[event emitters]: https://nodejs.org/api/events.html#events
[gitignore]: https://git-scm.com/docs/gitignore
[gitignore-template]: https://github.com/github/gitignore/blob/main/Node.gitignore
[installation]: ./installation.md
[node-platform]: https://nodejs.org/api/process.html#process_process_platform
[package-json-main]: https://docs.npmjs.com/cli/v7/configuring-npm/package-json#main
[package-scripts]: https://docs.npmjs.com/cli/v7/using-npm/scripts
[process-model]: process-model.md
[react]: https://reactjs.org
[repl]: ./repl.md
[webpack]: https://webpack.js.org
[window-all-closed]: ../api/app.md#event-window-all-closed
[wsl]: https://learn.microsoft.com/en-us/windows/wsl/about#what-is-wsl-2

<!-- Tutorial links -->

[prerequisites]: tutorial-1-prerequisites.md
[building your first app]: tutorial-2-first-app.md
[preload]: tutorial-3-preload.md
[features]: tutorial-4-adding-features.md
[packaging]: tutorial-5-packaging.md
[updates]: tutorial-6-publishing-updating.md
