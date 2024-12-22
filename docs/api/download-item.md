## Class: DownloadItem

> Control file downloads from remote sources.

Process: [Main](../glossary.md#main-process)<br />
_This class is not exported from the `'electron'` module. It is only available as a return value of other methods in the Electron API._

`DownloadItem` is an [EventEmitter][event-emitter] that represents a download item in Electron.
It is used in `will-download` event of `Session` class, and allows users to
control the download item.

```js
// In the main process.
const { BrowserWindow } = require('electron')

const win = new BrowserWindow()
win.webContents.session.on('will-download', (event, item, webContents) => {
  // Set the save path, making Electron not to prompt a save dialog.
  item.setSavePath('/tmp/save.pdf')

  item.on('updated', (event, state) => {
    if (state === 'interrupted') {
      console.log('Download is interrupted but can be resumed')
    } else if (state === 'progressing') {
      if (item.isPaused()) {
        console.log('Download is paused')
      } else {
        console.log(`Received bytes: ${item.getReceivedBytes()}`)
      }
    }
  })
  item.once('done', (event, state) => {
    if (state === 'completed') {
      console.log('Download successfully')
    } else {
      console.log(`Download failed: ${state}`)
    }
  })
})
```

### Instance Events

#### Event: 'updated'

Returns:

* `event` Event
* `state` string - Can be `progressing` or `interrupted`.

Emitted when the download has been updated and is not done.

The `state` can be one of following:

* `progressing` - The download is in-progress.
* `interrupted` - The download has interrupted and can be resumed.

#### Event: 'done'

Returns:

* `event` Event
* `state` string - Can be `completed`, `cancelled` or `interrupted`.

Emitted when the download is in a terminal state. This includes a completed
download, a cancelled download (via `downloadItem.cancel()`), and interrupted
download that can't be resumed.

The `state` can be one of following:

* `completed` - The download completed successfully.
* `cancelled` - The download has been cancelled.
* `interrupted` - The download has interrupted and can not resume.

### Instance Methods

The `downloadItem` object has the following methods:

#### `downloadItem.setSavePath(path)`

* `path` string - Set the save file path of the download item.

The API is only available in session's `will-download` callback function.
If `path` doesn't exist, Electron will try to make the directory recursively.
If user doesn't set the save path via the API, Electron will use the original
routine to determine the save path; this usually prompts a save dialog.

#### `downloadItem.getSavePath()`

Returns `string` - The save path of the download item. This will be either the path
set via `downloadItem.setSavePath(path)` or the path selected from the shown
save dialog.

#### `downloadItem.setSaveDialogOptions(options)`

* `options` SaveDialogOptions - Set the save file dialog options. This object has the same
properties as the `options` parameter of [`dialog.showSaveDialog()`](dialog.md).

This API allows the user to set custom options for the save dialog that opens
for the download item by default.
The API is only available in session's `will-download` callback function.

#### `downloadItem.getSaveDialogOptions()`

Returns `SaveDialogOptions` - Returns the object previously set by `downloadItem.setSaveDialogOptions(options)`.

#### `downloadItem.pause()`

Pauses the download.

#### `downloadItem.isPaused()`

Returns `boolean` - Whether the download is paused.

#### `downloadItem.resume()`

Resumes the download that has been paused.

**Note:** To enable resumable downloads the server you are downloading from must support range requests and provide both `Last-Modified` and `ETag` header values. Otherwise `resume()` will dismiss previously received bytes and restart the download from the beginning.

#### `downloadItem.canResume()`

Returns `boolean` - Whether the download can resume.

#### `downloadItem.cancel()`

Cancels the download operation.

#### `downloadItem.getURL()`

Returns `string` - The origin URL where the item is downloaded from.

#### `downloadItem.getMimeType()`

Returns `string` - The files mime type.

#### `downloadItem.hasUserGesture()`

Returns `boolean` - Whether the download has user gesture.

#### `downloadItem.getFilename()`

Returns `string` - The file name of the download item.

**Note:** The file name is not always the same as the actual one saved in local
disk. If user changes the file name in a prompted download saving dialog, the
actual name of saved file will be different.

#### `downloadItem.getCurrentBytesPerSecond()`

Returns `Integer` - The current download speed in bytes per second.

#### `downloadItem.getTotalBytes()`

Returns `Integer` - The total size in bytes of the download item.

If the size is unknown, it returns 0.

#### `downloadItem.getReceivedBytes()`

Returns `Integer` - The received bytes of the download item.

#### `downloadItem.getPercentComplete()`

Returns `Integer` - The download completion in percent.

#### `downloadItem.getContentDisposition()`

Returns `string` - The Content-Disposition field from the response
header.

#### `downloadItem.getState()`

Returns `string` - The current state. Can be `progressing`, `completed`, `cancelled` or `interrupted`.

**Note:** The following methods are useful specifically to resume a
`cancelled` item when session is restarted.

#### `downloadItem.getURLChain()`

Returns `string[]` - The complete URL chain of the item including any redirects.

#### `downloadItem.getLastModifiedTime()`

Returns `string` - Last-Modified header value.

#### `downloadItem.getETag()`

Returns `string` - ETag header value.

#### `downloadItem.getStartTime()`

Returns `Double` - Number of seconds since the UNIX epoch when the download was
started.

#### `downloadItem.getEndTime()`

Returns `Double` - Number of seconds since the UNIX epoch when the download ended.

### Instance Properties

#### `downloadItem.savePath`

A `string` property that determines the save file path of the download item.

The property is only available in session's `will-download` callback function.
If user doesn't set the save path via the property, Electron will use the original
routine to determine the save path; this usually prompts a save dialog.

[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter
