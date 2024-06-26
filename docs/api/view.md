# View

> Create and layout native views.

Process: [Main](../glossary.md#main-process)

This module cannot be used until the `ready` event of the `app`
module is emitted.

```js
const { BaseWindow, View } = require('electron')
const win = new BaseWindow()
const view = new View()

view.setBackgroundColor('red')
view.setBounds({ x: 0, y: 0, width: 100, height: 100 })
win.contentView.addChildView(view)
```

## Class: View

> A basic native view.

Process: [Main](../glossary.md#main-process)

`View` is an [EventEmitter][event-emitter].

### `new View()`

Creates a new `View`.

### Instance Events

Objects created with `new View` emit the following events:

#### Event: 'bounds-changed'

Emitted when the view's bounds have changed in response to being laid out. The
new bounds can be retrieved with [`view.getBounds()`](#viewgetbounds).

### Instance Methods

Objects created with `new View` have the following instance methods:

#### `view.addChildView(view[, index])`

* `view` View - Child view to add.
* `index` Integer (optional) - Index at which to insert the child view.
  Defaults to adding the child at the end of the child list.

If the same View is added to a parent which already contains it, it will be reordered such that
it becomes the topmost view.

#### `view.removeChildView(view)`

* `view` View - Child view to remove.

#### `view.setBounds(bounds)`

* `bounds` [Rectangle](structures/rectangle.md) - New bounds of the View.

#### `view.getBounds()`

Returns [`Rectangle`](structures/rectangle.md) - The bounds of this View, relative to its parent.

#### `view.setBackgroundColor(color)`

* `color` string - Color in Hex, RGB, ARGB, HSL, HSLA or named CSS color format. The alpha channel is
  optional for the hex type.

Examples of valid `color` values:

* Hex
  * `#fff` (RGB)
  * `#ffff` (ARGB)
  * `#ffffff` (RRGGBB)
  * `#ffffffff` (AARRGGBB)
* RGB
  * `rgb\(([\d]+),\s*([\d]+),\s*([\d]+)\)`
    * e.g. `rgb(255, 255, 255)`
* RGBA
  * `rgba\(([\d]+),\s*([\d]+),\s*([\d]+),\s*([\d.]+)\)`
    * e.g. `rgba(255, 255, 255, 1.0)`
* HSL
  * `hsl\((-?[\d.]+),\s*([\d.]+)%,\s*([\d.]+)%\)`
    * e.g. `hsl(200, 20%, 50%)`
* HSLA
  * `hsla\((-?[\d.]+),\s*([\d.]+)%,\s*([\d.]+)%,\s*([\d.]+)\)`
    * e.g. `hsla(200, 20%, 50%, 0.5)`
* Color name
  * Options are listed in [SkParseColor.cpp](https://source.chromium.org/chromium/chromium/src/+/main:third_party/skia/src/utils/SkParseColor.cpp;l=11-152;drc=eea4bf52cb0d55e2a39c828b017c80a5ee054148)
  * Similar to CSS Color Module Level 3 keywords, but case-sensitive.
    * e.g. `blueviolet` or `red`

**Note:** Hex format with alpha takes `AARRGGBB` or `ARGB`, _not_ `RRGGBBAA` or `RGB`.

#### `view.setVisible(visible)`

* `visible` boolean - If false, the view will be hidden from display.

### Instance Properties

Objects created with `new View` have the following properties:

#### `view.children` _Readonly_

A `View[]` property representing the child views of this view.

[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter
