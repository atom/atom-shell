# WebContentsView

> A View that displays a WebContents.

Process: [Main](../glossary.md#main-process)

This module cannot be used until the `ready` event of the `app`
module is emitted.

```js
const { BaseWindow, WebContentsView } = require('electron')
const win = new BaseWindow({ width: 800, height: 400 })

const view1 = new WebContentsView()
win.contentView.addChildView(view1)
view1.webContents.loadURL('https://electronjs.org')
view1.setBounds({ x: 0, y: 0, width: 400, height: 400 })

const view2 = new WebContentsView()
win.contentView.addChildView(view2)
view2.webContents.loadURL('https://github.com/electron/electron')
view2.setBounds({ x: 400, y: 0, width: 400, height: 400 })
```

## Class: WebContentsView extends `View`

> A View that displays a WebContents.

Process: [Main](../glossary.md#main-process)

`WebContentsView` inherits from [`View`](view.md).

`WebContentsView` is an [EventEmitter][event-emitter].

### `new WebContentsView([options])`

* `options` Object (optional)
  * `webPreferences` [WebPreferences](structures/web-preferences.md) (optional) - Settings of web page's features.
  * `webContents` [WebContents](web-contents.md) (optional) - If present, the given WebContents will be adopted by the WebContentsView. A WebContents may only be presented in one WebContentsView at a time.

Creates a WebContentsView.

### Instance Properties

Objects created with `new WebContentsView` have the following properties, in
addition to those inherited from [View](view.md):

#### `view.webContents` _Readonly_

A `WebContents` property containing a reference to the displayed `WebContents`.
Use this to interact with the `WebContents`, for instance to load a URL.

```js
const { WebContentsView } = require('electron')
const view = new WebContentsView()
view.webContents.loadURL('https://electronjs.org/')
```

[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter
