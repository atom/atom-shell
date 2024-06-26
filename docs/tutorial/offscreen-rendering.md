# Offscreen Rendering

## Overview

Offscreen rendering lets you obtain the content of a `BrowserWindow` in a
bitmap, so it can be rendered anywhere, for example, on texture in a 3D scene.
The offscreen rendering in Electron uses a similar approach to that of the
[Chromium Embedded Framework](https://bitbucket.org/chromiumembedded/cef)
project.

_Notes_:

* There are two rendering modes that can be used (see the section below) and only
the dirty area is passed to the `paint` event to be more efficient.
* You can stop/continue the rendering as well as set the frame rate.
* The maximum frame rate is 240 because greater values bring only performance
losses with no benefits.
* When nothing is happening on a webpage, no frames are generated.
* An offscreen window is always created as a
[Frameless Window](../tutorial/window-customization.md)..

### Rendering Modes

#### GPU accelerated

GPU accelerated rendering means that the GPU is used for composition. Because of
that, the frame has to be copied from the GPU which requires more resources,
thus this mode is slower than the Software output device. The benefit of this
mode is that WebGL and 3D CSS animations are supported.

#### Software output device

This mode uses a software output device for rendering in the CPU, so the frame
generation is much faster. As a result, this mode is preferred over the GPU
accelerated one.

To enable this mode, GPU acceleration has to be disabled by calling the
[`app.disableHardwareAcceleration()`][disablehardwareacceleration] API.

## Example

```fiddle docs/fiddles/features/offscreen-rendering
const { app, BrowserWindow } = require('electron/main')
const fs = require('node:fs')
const path = require('node:path')

app.disableHardwareAcceleration()

function createWindow () {
  const win = new BrowserWindow({
    width: 800,
    height: 600,
    webPreferences: {
      offscreen: true
    }
  })

  win.loadURL('https://github.com')
  win.webContents.on('paint', (event, dirty, image) => {
    fs.writeFileSync('ex.png', image.toPNG())
  })
  win.webContents.setFrameRate(60)
  console.log(`The screenshot has been successfully saved to ${path.join(process.cwd(), 'ex.png')}`)
}

app.whenReady().then(() => {
  createWindow()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow()
    }
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})
```

After launching the Electron application, navigate to your application's
working folder, where you'll find the rendered image.

[disablehardwareacceleration]: ../api/app.md#appdisablehardwareacceleration
