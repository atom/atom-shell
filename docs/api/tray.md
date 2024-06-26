# Tray

## Class: Tray

> Add icons and context menus to the system's notification area.

Process: [Main](../glossary.md#main-process)

`Tray` is an [EventEmitter][event-emitter].

```js
const { app, Menu, Tray } = require('electron')

let tray = null
app.whenReady().then(() => {
  tray = new Tray('/path/to/my/icon')
  const contextMenu = Menu.buildFromTemplate([
    { label: 'Item1', type: 'radio' },
    { label: 'Item2', type: 'radio' },
    { label: 'Item3', type: 'radio', checked: true },
    { label: 'Item4', type: 'radio' }
  ])
  tray.setToolTip('This is my application.')
  tray.setContextMenu(contextMenu)
})
```

**Platform Considerations**

**Linux**

* Tray icon uses [StatusNotifierItem](https://www.freedesktop.org/wiki/Specifications/StatusNotifierItem/)
  by default, when it is not available in user's desktop environment the
  `GtkStatusIcon` will be used instead.
* The `click` event is emitted when the tray icon receives activation from
  user, however the StatusNotifierItem spec does not specify which action would
  cause an activation, for some environments it is left mouse click, but for
  some it might be double left mouse click.
* In order for changes made to individual `MenuItem`s to take effect,
  you have to call `setContextMenu` again. For example:

```js
const { app, Menu, Tray } = require('electron')

let appIcon = null
app.whenReady().then(() => {
  appIcon = new Tray('/path/to/my/icon')
  const contextMenu = Menu.buildFromTemplate([
    { label: 'Item1', type: 'radio' },
    { label: 'Item2', type: 'radio' }
  ])

  // Make a change to the context menu
  contextMenu.items[1].checked = false

  // Call this again for Linux because we modified the context menu
  appIcon.setContextMenu(contextMenu)
})
```

**MacOS**

* Icons passed to the Tray constructor should be [Template Images](native-image.md#template-image-macos).
* To make sure your icon isn't grainy on retina monitors, be sure your `@2x` image is 144dpi.
* If you are bundling your application (e.g., with webpack for development), be sure that the file names are not being mangled or hashed. The filename needs to end in Template, and the `@2x` image needs to have the same filename as the standard image, or MacOS will not magically invert your image's colors or use the high density image.
* 16x16 (72dpi) and 32x32@2x (144dpi) work well for most icons.

**Windows**

* It is recommended to use `ICO` icons to get best visual effects.

### `new Tray(image, [guid])`

* `image` ([NativeImage](native-image.md) | string)
* `guid` string (optional) _Windows_ - Assigns a GUID to the tray icon. If the executable is signed and the signature contains an organization in the subject line then the GUID is permanently associated with that signature. OS level settings like the position of the tray icon in the system tray will persist even if the path to the executable changes. If the executable is not code-signed then the GUID is permanently associated with the path to the executable. Changing the path to the executable will break the creation of the tray icon and a new GUID must be used. However, it is highly recommended to use the GUID parameter only in conjunction with code-signed executable. If an App defines multiple tray icons then each icon must use a separate GUID.

Creates a new tray icon associated with the `image`.

### Instance Events

The `Tray` module emits the following events:

#### Event: 'click'

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `bounds` [Rectangle](structures/rectangle.md) - The bounds of tray icon.
* `position` [Point](structures/point.md) - The position of the event.

Emitted when the tray icon is clicked.

Note that on Linux this event is emitted when the tray icon receives an
activation, which might not necessarily be left mouse click.

#### Event: 'right-click' _macOS_ _Windows_

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `bounds` [Rectangle](structures/rectangle.md) - The bounds of tray icon.

Emitted when the tray icon is right clicked.

#### Event: 'double-click' _macOS_ _Windows_

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `bounds` [Rectangle](structures/rectangle.md) - The bounds of tray icon.

Emitted when the tray icon is double clicked.

#### Event: 'middle-click' _Windows_

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `bounds` [Rectangle](structures/rectangle.md) - The bounds of tray icon.

Emitted when the tray icon is middle clicked.

#### Event: 'balloon-show' _Windows_

Emitted when the tray balloon shows.

#### Event: 'balloon-click' _Windows_

Emitted when the tray balloon is clicked.

#### Event: 'balloon-closed' _Windows_

Emitted when the tray balloon is closed because of timeout or user manually
closes it.

#### Event: 'drop' _macOS_

Emitted when any dragged items are dropped on the tray icon.

#### Event: 'drop-files' _macOS_

Returns:

* `event` Event
* `files` string[] - The paths of the dropped files.

Emitted when dragged files are dropped in the tray icon.

#### Event: 'drop-text' _macOS_

Returns:

* `event` Event
* `text` string - the dropped text string.

Emitted when dragged text is dropped in the tray icon.

#### Event: 'drag-enter' _macOS_

Emitted when a drag operation enters the tray icon.

#### Event: 'drag-leave' _macOS_

Emitted when a drag operation exits the tray icon.

#### Event: 'drag-end' _macOS_

Emitted when a drag operation ends on the tray or ends at another location.

#### Event: 'mouse-up' _macOS_

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `position` [Point](structures/point.md) - The position of the event.

Emitted when the mouse is released from clicking the tray icon.

Note: This will not be emitted if you have set a context menu for your Tray using `tray.setContextMenu`, as a result of macOS-level constraints.

#### Event: 'mouse-down' _macOS_

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `position` [Point](structures/point.md) - The position of the event.

Emitted when the mouse clicks the tray icon.

#### Event: 'mouse-enter' _macOS_ _Windows_

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `position` [Point](structures/point.md) - The position of the event.

Emitted when the mouse enters the tray icon.

#### Event: 'mouse-leave' _macOS_ _Windows_

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `position` [Point](structures/point.md) - The position of the event.

Emitted when the mouse exits the tray icon.

#### Event: 'mouse-move' _macOS_ _Windows_

Returns:

* `event` [KeyboardEvent](structures/keyboard-event.md)
* `position` [Point](structures/point.md) - The position of the event.

Emitted when the mouse moves in the tray icon.

### Instance Methods

The `Tray` class has the following methods:

#### `tray.destroy()`

Destroys the tray icon immediately.

#### `tray.setImage(image)`

* `image` ([NativeImage](native-image.md) | string)

Sets the `image` associated with this tray icon.

#### `tray.setPressedImage(image)` _macOS_

* `image` ([NativeImage](native-image.md) | string)

Sets the `image` associated with this tray icon when pressed on macOS.

#### `tray.setToolTip(toolTip)`

* `toolTip` string

Sets the hover text for this tray icon.

#### `tray.setTitle(title[, options])` _macOS_

* `title` string
* `options` Object (optional)
  * `fontType` string (optional) - The font family variant to display, can be `monospaced` or `monospacedDigit`. `monospaced` is available in macOS 10.15+ When left blank, the title uses the default system font.

Sets the title displayed next to the tray icon in the status bar (Support ANSI colors).

#### `tray.getTitle()` _macOS_

Returns `string` - the title displayed next to the tray icon in the status bar

#### `tray.setIgnoreDoubleClickEvents(ignore)` _macOS_

* `ignore` boolean

Sets the option to ignore double click events. Ignoring these events allows you
to detect every individual click of the tray icon.

This value is set to false by default.

#### `tray.getIgnoreDoubleClickEvents()` _macOS_

Returns `boolean` - Whether double click events will be ignored.

#### `tray.displayBalloon(options)` _Windows_

* `options` Object
  * `icon` ([NativeImage](native-image.md) | string) (optional) - Icon to use when `iconType` is `custom`.
  * `iconType` string (optional) - Can be `none`, `info`, `warning`, `error` or `custom`. Default is `custom`.
  * `title` string
  * `content` string
  * `largeIcon` boolean (optional) - The large version of the icon should be used. Default is `true`. Maps to [`NIIF_LARGE_ICON`][NIIF_LARGE_ICON].
  * `noSound` boolean (optional) - Do not play the associated sound. Default is `false`. Maps to [`NIIF_NOSOUND`][NIIF_NOSOUND].
  * `respectQuietTime` boolean (optional) - Do not display the balloon notification if the current user is in "quiet time". Default is `false`. Maps to [`NIIF_RESPECT_QUIET_TIME`][NIIF_RESPECT_QUIET_TIME].

Displays a tray balloon.

[NIIF_NOSOUND]: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataa#niif_nosound-0x00000010
[NIIF_LARGE_ICON]: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataa#niif_large_icon-0x00000020
[NIIF_RESPECT_QUIET_TIME]: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataa#niif_respect_quiet_time-0x00000080

#### `tray.removeBalloon()` _Windows_

Removes a tray balloon.

#### `tray.focus()` _Windows_

Returns focus to the taskbar notification area.
Notification area icons should use this message when they have completed their UI operation.
For example, if the icon displays a shortcut menu, but the user presses ESC to cancel it,
use `tray.focus()` to return focus to the notification area.

#### `tray.popUpContextMenu([menu, position])` _macOS_ _Windows_

* `menu` Menu (optional)
* `position` [Point](structures/point.md) (optional) - The pop up position.

Pops up the context menu of the tray icon. When `menu` is passed, the `menu` will
be shown instead of the tray icon's context menu.

The `position` is only available on Windows, and it is (0, 0) by default.

#### `tray.closeContextMenu()` _macOS_ _Windows_

Closes an open context menu, as set by `tray.setContextMenu()`.

#### `tray.setContextMenu(menu)`

* `menu` Menu | null

Sets the context menu for this icon.

#### `tray.getBounds()` _macOS_ _Windows_

Returns [`Rectangle`](structures/rectangle.md)

The `bounds` of this tray icon as `Object`.

#### `tray.isDestroyed()`

Returns `boolean` - Whether the tray icon is destroyed.

[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter
