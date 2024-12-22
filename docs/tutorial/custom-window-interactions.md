# Custom Window Interactions

## Custom draggable regions

By default, windows are dragged using the title bar provided by the OS chrome. Apps
that remove the default title bar need to use the `app-region` CSS property to define
specific areas that can be used to drag the window. Setting `app-region: drag` marks
a rectagular area as draggable.

It is important to note that draggable areas ignore all pointer events. For example,
a button element that overlaps a draggable region will not emit mouse clicks or mouse
enter/exit events within that overlapping area. Setting `app-region: no-drag` reenables
pointer events by excluding a rectagular area from a draggable region.

To make the whole window draggable, you can add `app-region: drag` as
`body`'s style:

```css title='styles.css'
body {
  app-region: drag;
}
```

And note that if you have made the whole window draggable, you must also mark
buttons as non-draggable, otherwise it would be impossible for users to click on
them:

```css title='styles.css'
button {
  app-region: no-drag;
}
```

If you're only setting a custom title bar as draggable, you also need to make all
buttons in title bar non-draggable.

### Tip: disable text selection

When creating a draggable region, the dragging behavior may conflict with text selection.
For example, when you drag the title bar, you may accidentally select its text contents.
To prevent this, you need to disable text selection within a draggable area like this:

```css
.titlebar {
  user-select: none;
  app-region: drag;
}
```

### Tip: disable context menus

On some platforms, the draggable area will be treated as a non-client frame, so
when you right click on it, a system menu will pop up. To make the context menu
behave correctly on all platforms, you should never use a custom context menu on
draggable areas.

## Click-through windows

To create a click-through window, i.e. making the window ignore all mouse
events, you can call the [win.setIgnoreMouseEvents(ignore)][ignore-mouse-events]
API:

```js title='main.js'
const { BrowserWindow } = require('electron')

const win = new BrowserWindow()
win.setIgnoreMouseEvents(true)
```

### Forward mouse events _macOS_ _Windows_

Ignoring mouse messages makes the web contents oblivious to mouse movement,
meaning that mouse movement events will not be emitted. On Windows and macOS, an
optional parameter can be used to forward mouse move messages to the web page,
allowing events such as `mouseleave` to be emitted:

```js title='main.js'
const { BrowserWindow, ipcMain } = require('electron')

const path = require('node:path')

const win = new BrowserWindow({
  webPreferences: {
    preload: path.join(__dirname, 'preload.js')
  }
})

ipcMain.on('set-ignore-mouse-events', (event, ignore, options) => {
  const win = BrowserWindow.fromWebContents(event.sender)
  win.setIgnoreMouseEvents(ignore, options)
})
```

```js title='preload.js'
window.addEventListener('DOMContentLoaded', () => {
  const el = document.getElementById('clickThroughElement')
  el.addEventListener('mouseenter', () => {
    ipcRenderer.send('set-ignore-mouse-events', true, { forward: true })
  })
  el.addEventListener('mouseleave', () => {
    ipcRenderer.send('set-ignore-mouse-events', false)
  })
})
```

This makes the web page click-through when over the `#clickThroughElement` element,
and returns to normal outside it.

[ignore-mouse-events]: ../api/browser-window.md#winsetignoremouseeventsignore-options
