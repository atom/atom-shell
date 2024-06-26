# Notifications

Each operating system has its own mechanism to display notifications to users. Electron's notification
APIs are cross-platform, but are different for each process type.

If you want to use a renderer process API in the main process or vice-versa, consider using
[inter-process communication](./ipc.md).

## Usage

Below are two examples showing how to display notifications for each process type.

### Show notifications in the main process

Main process notifications are displayed using Electron's [Notification module](../api/notification.md).
Notification objects created using this module do not appear unless their `show()` instance
method is called.

```js title='Main Process'
const { Notification } = require('electron')

const NOTIFICATION_TITLE = 'Basic Notification'
const NOTIFICATION_BODY = 'Notification from the Main process'

new Notification({
  title: NOTIFICATION_TITLE,
  body: NOTIFICATION_BODY
}).show()
```

Here's a full example that you can open with Electron Fiddle:

```fiddle docs/fiddles/features/notifications/main
const { app, BrowserWindow, Notification } = require('electron/main')

function createWindow () {
  const win = new BrowserWindow({
    width: 800,
    height: 600
  })

  win.loadFile('index.html')
}

const NOTIFICATION_TITLE = 'Basic Notification'
const NOTIFICATION_BODY = 'Notification from the Main process'

function showNotification () {
  new Notification({ title: NOTIFICATION_TITLE, body: NOTIFICATION_BODY }).show()
}

app.whenReady().then(createWindow).then(showNotification)

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit()
  }
})

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow()
  }
})
```

### Show notifications in the renderer process

Notifications can be displayed directly from the renderer process with the
[web Notifications API](https://developer.mozilla.org/en-US/docs/Web/API/Notifications_API/Using_the_Notifications_API).

```js title='Renderer Process'
const NOTIFICATION_TITLE = 'Title'
const NOTIFICATION_BODY =
  'Notification from the Renderer process. Click to log to console.'
const CLICK_MESSAGE = 'Notification clicked'

new Notification(NOTIFICATION_TITLE, { body: NOTIFICATION_BODY }).onclick =
  () => console.log(CLICK_MESSAGE)
```

Here's a full example that you can open with Electron Fiddle:

```fiddle docs/fiddles/features/notifications/renderer|focus=renderer.js
const NOTIFICATION_TITLE = 'Title'
const NOTIFICATION_BODY = 'Notification from the Renderer process. Click to log to console.'
const CLICK_MESSAGE = 'Notification clicked!'

new window.Notification(NOTIFICATION_TITLE, { body: NOTIFICATION_BODY })
  .onclick = () => { document.getElementById('output').innerText = CLICK_MESSAGE }
```

## Platform considerations

While code and user experience across operating systems are similar, there
are subtle differences.

### Windows

For notifications on Windows, your Electron app needs to have a Start Menu shortcut with an
[AppUserModelID][app-user-model-id] and a corresponding [ToastActivatorCLSID][toast-activator-clsid].

Electron attempts to automate the work around the AppUserModelID and ToastActivatorCLSID. When
Electron is used together with Squirrel.Windows (e.g. if you're using electron-winstaller),
[shortcuts will automatically be set correctly][squirrel-events].

In production, Electron will also detect that Squirrel was used and will automatically call
`app.setAppUserModelId()` with the correct value. During development, you may have
to call [`app.setAppUserModelId()`][set-app-user-model-id] yourself.

:::info Notifications in development

To quickly bootstrap notifications during development, adding
`node_modules\electron\dist\electron.exe` to your Start Menu also does the
trick. Navigate to the file in Explorer, right-click and 'Pin to Start Menu'.
Then, call `app.setAppUserModelId(process.execPath)` in the main process to see notifications.

:::

#### Use advanced notifications

Windows also allow for advanced notifications with custom templates, images, and other flexible
elements.

To send those notifications from the main process, you can use the userland module
[`electron-windows-notifications`](https://github.com/felixrieseberg/electron-windows-notifications),
which uses native Node addons to send `ToastNotification` and `TileNotification` objects.

While notifications including buttons work with `electron-windows-notifications`,
handling replies requires the use of
[`electron-windows-interactive-notifications`](https://github.com/felixrieseberg/electron-windows-interactive-notifications),
which helps with registering the required COM components and calling your
Electron app with the entered user data.

#### Query notification state

To detect whether or not you're allowed to send a notification, use the
userland module [`windows-notification-state`][windows-notification-state].

This module allows you to determine ahead of time whether or not Windows will silently throw the
notification away.

### macOS

Notifications are straightforward on macOS, but you should be aware of
[Apple's Human Interface guidelines regarding notifications][apple-notification-guidelines].

Note that notifications are limited to 256 bytes in size and will be truncated
if you exceed that limit.

#### Query notification state

To detect whether or not you're allowed to send a notification, use the userland module
[`macos-notification-state`][macos-notification-state].

This module allows you to detect ahead of time whether or not the notification will be displayed.

### Linux

Notifications are sent using `libnotify`, which can show notifications on any
desktop environment that follows [Desktop Notifications Specification][notification-spec],
including Cinnamon, Enlightenment, Unity, GNOME, and KDE.

[notification-spec]: https://specifications.freedesktop.org/notification-spec/notification-spec-latest.html
[app-user-model-id]: https://learn.microsoft.com/en-us/windows/win32/shell/appids
[set-app-user-model-id]: ../api/app.md#appsetappusermodelidid-windows
[squirrel-events]: https://github.com/electron/windows-installer/blob/main/README.md#handling-squirrel-events
[toast-activator-clsid]: https://learn.microsoft.com/en-us/windows/win32/properties/props-system-appusermodel-toastactivatorclsid
[apple-notification-guidelines]: https://developer.apple.com/design/human-interface-guidelines/notifications
[windows-notification-state]: https://github.com/felixrieseberg/windows-notification-state
[macos-notification-state]: https://github.com/felixrieseberg/macos-notification-state
