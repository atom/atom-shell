---
title: "ES Modules (ESM) in Electron"
description: "The ES module (ESM) format is the standard way of loading JavaScript packages."
slug: esm
hide_title: false
---

# ES Modules (ESM) in Electron

## Introduction

The ECMAScript module (ESM) format is [the standard way of loading JavaScript packages](https://tc39.es/ecma262/#sec-modules).

Chromium and Node.js have their own implementations of the ESM specification, and Electron
chooses which module loader to use depending on the context.

This document serves to outline the limitations of ESM in Electron and the differences between
ESM in Electron and ESM in Node.js and Chromium.

:::info

This feature was added in `electron@28.0.0`.

:::

## Summary: ESM support matrix

This table gives a general overview of where ESM is supported and which ESM loader is used.

| Process              | ESM Loader | ESM Loader in Preload | Applicable Requirements |
|----------------------|------------|-----------------------|-------------------------|
| Main                 | Node.js    | N/A                   | <ul><li> [You must use `await` generously before the app's `ready` event](#you-must-use-await-generously-before-the-apps-ready-event) </li></ul> |
| Renderer (Sandboxed) | Chromium   | Unsupported           | <ul><li> [Sandboxed preload scripts can't use ESM imports](#sandboxed-preload-scripts-cant-use-esm-imports) </li></ul> |
| Renderer (Unsandboxed & Context Isolated) | Chromium | Node.js | <ul><li> [Unsandboxed ESM preload scripts will run after page load on pages with no content](#unsandboxed-esm-preload-scripts-will-run-after-page-load-on-pages-with-no-content) </li> <li>[ESM Preload Scripts must have the `.mjs` extension](#esm-preload-scripts-must-have-the-mjs-extension)</li></ul> |
| Renderer (Unsandboxed & Non Context Isolated) | Chromium | Node.js | <ul><li>[Unsandboxed ESM preload scripts will run after page load on pages with no content](#unsandboxed-esm-preload-scripts-will-run-after-page-load-on-pages-with-no-content)</li><li>[ESM Preload Scripts must have the `.mjs` extension](#esm-preload-scripts-must-have-the-mjs-extension)</li><li>[ESM preload scripts must be context isolated to use dynamic Node.js ESM imports](#esm-preload-scripts-must-be-context-isolated-to-use-dynamic-nodejs-esm-imports)</li></ul> |

## Main process

Electron's main process runs in a Node.js context and uses its ESM loader. Usage should follow
[Node's ESM documentation](https://nodejs.org/api/esm.html). To enable ESM in a file in the
main process, one of the following conditions must be met:

- The file ends with the `.mjs` extension
- The nearest parent package.json has `"type": "module"` set

See Node's [Determining Module System](https://nodejs.org/api/packages.html#determining-module-system)
doc for more details.

### Caveats

#### You must use `await` generously before the app's `ready` event

ES Modules are loaded **asynchronously**. This means that only side effects
from the main process entry point's imports will execute before the `ready` event.

This is important because certain Electron APIs (e.g. [`app.setPath`](../api/app.md#appsetpathname-path))
need to be called **before** the app's `ready` event is emitted.

With top-level `await` available in Node.js ESM, make sure to `await` every Promise that you need to
execute before the `ready` event. Otherwise, your app may be `ready` before your code executes.

This is particularly important to keep in mind for dynamic ESM import statements (static imports are unaffected).
For example, if `index.mjs` calls `import('./set-up-paths.mjs')` at the top level, the app will
likely already be `ready` by the time that dynamic import resolves.

```js @ts-expect-error=[2] title='index.mjs (Main Process)'
// add an await call here to guarantee that path setup will finish before `ready`
import('./set-up-paths.mjs')

app.whenReady().then(() => {
  console.log('This code may execute before the above import')
})
```

:::caution Transpiler translations

JavaScript transpilers (e.g. Babel, TypeScript) have historically supported ES Module
syntax before Node.js supported ESM imports by turning these calls to CommonJS
`require` calls.

<details>
<summary>Example: @babel/plugin-transform-modules-commonjs</summary>

The `@babel/plugin-transform-modules-commonjs` plugin will transform
ESM imports down to `require` calls. The exact syntax will depend on the
[`importInterop` setting](https://babeljs.io/docs/babel-plugin-transform-modules-commonjs#importinterop).

```js @nolint @ts-nocheck title='@babel/plugin-transform-modules-commonjs'
import foo from "foo";
import { bar } from "bar";
foo;
bar;

// with "importInterop: node", compiles to ...

"use strict";

var _foo = require("foo");
var _bar = require("bar");

_foo;
_bar.bar;
```

</details>

These CommonJS calls load module code synchronously. If you are migrating transpiled CJS code
to native ESM, be careful about the timing differences between CJS and ESM.

:::

## Renderer process

Electron's renderer processes run in a Chromium context and will use Chromium's ESM loader.
In practice, this means that `import` statements:

- will not have access to Node.js built-in modules
- will not be able to load npm packages from `node_modules`

```html
<script type="module">
    import { exists } from 'node:fs' // ❌ will not work!
</script>
```

If you wish to load JavaScript packages via npm directly into the renderer process, we recommend
using a bundler such as webpack or Vite to compile your code for client-side consumption.

## Preload scripts

A renderer's preload script will use the Node.js ESM loader _when available_.
ESM availability will depend on the values of its renderer's `sandbox` and `contextIsolation`
preferences, and comes with a few other caveats due to the asynchronous nature of ESM loading.

### Caveats

#### ESM preload scripts must have the `.mjs` extension

Preload scripts will ignore `"type": "module"` fields, so you _must_ use the `.mjs` file
extension in your ESM preload scripts.

#### Sandboxed preload scripts can't use ESM imports

Sandboxed preload scripts are run as plain JavaScript without an ESM context. If you need to
use external modules, we recommend using a bundler for your preload code. Loading the
`electron` API is still done via `require('electron')`.

For more information on sandboxing, see the [Process Sandboxing](./sandbox.md) docs.

#### Unsandboxed ESM preload scripts will run after page load on pages with no content

If the response body for a renderer's loaded page is _completely_ empty (i.e. `Content-Length: 0`),
its preload script will not block the page load, which may result in race conditions.

If this impacts you, change your response body to have _something_ in it
(e.g. an empty `html` tag (`<html></html>`)) or swap back to using a CommonJS preload script
(`.js` or `.cjs`), which will block the page load.

### ESM preload scripts must be context isolated to use dynamic Node.js ESM imports

If your unsandboxed renderer process does not have the `contextIsolation` flag enabled,
you cannot dynamically `import()` files via Node's ESM loader.

```js @ts-nocheck title='preload.mjs'
// ❌ these won't work without context isolation
const fs = await import('node:fs')
await import('./foo')
```

This is because Chromium's dynamic ESM `import()` function usually takes precedence in the
renderer process and without context isolation, there is no way of knowing if Node.js is available
in a dynamic import statement. If you enable context isolation, `import()` statements
from the renderer's isolated preload context can be routed to the Node.js module loader.
