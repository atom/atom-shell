# systemPreferences

> Get system preferences.

Process: [Main](../glossary.md#main-process), [Utility](../glossary.md#utility-process)

```js
const { systemPreferences } = require('electron')
console.log(systemPreferences.isAeroGlassEnabled())
```

## Events

The `systemPreferences` object emits the following events:

### Event: 'accent-color-changed' _Windows_

Returns:

* `event` Event
* `newColor` string - The new RGBA color the user assigned to be their system
  accent color.

### Event: 'color-changed' _Windows_

Returns:

* `event` Event

## Methods

### `systemPreferences.isSwipeTrackingFromScrollEventsEnabled()` _macOS_

Returns `boolean` - Whether the Swipe between pages setting is on.

### `systemPreferences.postNotification(event, userInfo[, deliverImmediately])` _macOS_

* `event` string
* `userInfo` Record\<string, any\>
* `deliverImmediately` boolean (optional) - `true` to post notifications immediately even when the subscribing app is inactive.

Posts `event` as native notifications of macOS. The `userInfo` is an Object
that contains the user information dictionary sent along with the notification.

### `systemPreferences.postLocalNotification(event, userInfo)` _macOS_

* `event` string
* `userInfo` Record\<string, any\>

Posts `event` as native notifications of macOS. The `userInfo` is an Object
that contains the user information dictionary sent along with the notification.

### `systemPreferences.postWorkspaceNotification(event, userInfo)` _macOS_

* `event` string
* `userInfo` Record\<string, any\>

Posts `event` as native notifications of macOS. The `userInfo` is an Object
that contains the user information dictionary sent along with the notification.

### `systemPreferences.subscribeNotification(event, callback)` _macOS_

* `event` string | null
* `callback` Function
  * `event` string
  * `userInfo` Record\<string, unknown\>
  * `object` string

Returns `number` - The ID of this subscription

Subscribes to native notifications of macOS, `callback` will be called with
`callback(event, userInfo)` when the corresponding `event` happens. The
`userInfo` is an Object that contains the user information dictionary sent
along with the notification. The `object` is the sender of the notification,
and only supports `NSString` values for now.

The `id` of the subscriber is returned, which can be used to unsubscribe the
`event`.

Under the hood this API subscribes to `NSDistributedNotificationCenter`,
example values of `event` are:

* `AppleInterfaceThemeChangedNotification`
* `AppleAquaColorVariantChanged`
* `AppleColorPreferencesChangedNotification`
* `AppleShowScrollBarsSettingChanged`

If `event` is null, the `NSDistributedNotificationCenter` doesn’t use it as criteria for delivery to the observer. See [docs](https://developer.apple.com/documentation/foundation/nsnotificationcenter/1411723-addobserverforname?language=objc)  for more information.

### `systemPreferences.subscribeLocalNotification(event, callback)` _macOS_

* `event` string | null
* `callback` Function
  * `event` string
  * `userInfo` Record\<string, unknown\>
  * `object` string

Returns `number` - The ID of this subscription

Same as `subscribeNotification`, but uses `NSNotificationCenter` for local defaults.
This is necessary for events such as `NSUserDefaultsDidChangeNotification`.

If `event` is null, the `NSNotificationCenter` doesn’t use it as criteria for delivery to the observer. See [docs](https://developer.apple.com/documentation/foundation/nsnotificationcenter/1411723-addobserverforname?language=objc) for more information.

### `systemPreferences.subscribeWorkspaceNotification(event, callback)` _macOS_

* `event` string | null
* `callback` Function
  * `event` string
  * `userInfo` Record\<string, unknown\>
  * `object` string

Returns `number` - The ID of this subscription

Same as `subscribeNotification`, but uses `NSWorkspace.sharedWorkspace.notificationCenter`.
This is necessary for events such as `NSWorkspaceDidActivateApplicationNotification`.

If `event` is null, the `NSWorkspaceNotificationCenter` doesn’t use it as criteria for delivery to the observer. See [docs](https://developer.apple.com/documentation/foundation/nsnotificationcenter/1411723-addobserverforname?language=objc) for more information.

### `systemPreferences.unsubscribeNotification(id)` _macOS_

* `id` Integer

Removes the subscriber with `id`.

### `systemPreferences.unsubscribeLocalNotification(id)` _macOS_

* `id` Integer

Same as `unsubscribeNotification`, but removes the subscriber from `NSNotificationCenter`.

### `systemPreferences.unsubscribeWorkspaceNotification(id)` _macOS_

* `id` Integer

Same as `unsubscribeNotification`, but removes the subscriber from `NSWorkspace.sharedWorkspace.notificationCenter`.

### `systemPreferences.registerDefaults(defaults)` _macOS_

* `defaults` Record\<string, string | boolean | number\> - a dictionary of (`key: value`) user defaults

Add the specified defaults to your application's `NSUserDefaults`.

### `systemPreferences.getUserDefault<Type extends keyof UserDefaultTypes>(key, type)` _macOS_

* `key` string
* `type` Type - Can be `string`, `boolean`, `integer`, `float`, `double`,
  `url`, `array` or `dictionary`.

Returns [`UserDefaultTypes[Type]`](structures/user-default-types.md) - The value of `key` in `NSUserDefaults`.

Some popular `key` and `type`s are:

* `AppleInterfaceStyle`: `string`
* `AppleAquaColorVariant`: `integer`
* `AppleHighlightColor`: `string`
* `AppleShowScrollBars`: `string`
* `NSNavRecentPlaces`: `array`
* `NSPreferredWebServices`: `dictionary`
* `NSUserDictionaryReplacementItems`: `array`

### `systemPreferences.setUserDefault<Type extends keyof UserDefaultTypes>(key, type, value)` _macOS_

* `key` string
* `type` Type - Can be `string`, `boolean`, `integer`, `float`, `double`, `url`, `array` or `dictionary`.
* `value` UserDefaultTypes\[Type]

Set the value of `key` in `NSUserDefaults`.

Note that `type` should match actual type of `value`. An exception is thrown
if they don't.

Some popular `key` and `type`s are:

* `ApplePressAndHoldEnabled`: `boolean`

### `systemPreferences.removeUserDefault(key)` _macOS_

* `key` string

Removes the `key` in `NSUserDefaults`. This can be used to restore the default
or global value of a `key` previously set with `setUserDefault`.

### `systemPreferences.isAeroGlassEnabled()` _Windows_

Returns `boolean` - `true` if [DWM composition][dwm-composition] (Aero Glass) is
enabled, and `false` otherwise.

An example of using it to determine if you should create a transparent window or
not (transparent windows won't work correctly when DWM composition is disabled):

```js
const { BrowserWindow, systemPreferences } = require('electron')
const browserOptions = { width: 1000, height: 800 }

// Make the window transparent only if the platform supports it.
if (process.platform !== 'win32' || systemPreferences.isAeroGlassEnabled()) {
  browserOptions.transparent = true
  browserOptions.frame = false
}

// Create the window.
const win = new BrowserWindow(browserOptions)

// Navigate.
if (browserOptions.transparent) {
  win.loadFile('index.html')
} else {
  // No transparency, so we load a fallback that uses basic styles.
  win.loadFile('fallback.html')
}
```

[dwm-composition]: https://learn.microsoft.com/en-us/windows/win32/dwm/composition-ovw

### `systemPreferences.getAccentColor()` _Windows_ _macOS_

Returns `string` - The users current system wide accent color preference in RGBA
hexadecimal form.

```js
const color = systemPreferences.getAccentColor() // `"aabbccdd"`
const red = color.substr(0, 2) // "aa"
const green = color.substr(2, 2) // "bb"
const blue = color.substr(4, 2) // "cc"
const alpha = color.substr(6, 2) // "dd"
```

This API is only available on macOS 10.14 Mojave or newer.

### `systemPreferences.getColor(color)` _Windows_ _macOS_

* `color` string - One of the following values:
  * On **Windows**:
    * `3d-dark-shadow` - Dark shadow for three-dimensional display elements.
    * `3d-face` - Face color for three-dimensional display elements and for dialog
      box backgrounds.
    * `3d-highlight` - Highlight color for three-dimensional display elements.
    * `3d-light` - Light color for three-dimensional display elements.
    * `3d-shadow` - Shadow color for three-dimensional display elements.
    * `active-border` - Active window border.
    * `active-caption` - Active window title bar. Specifies the left side color in
      the color gradient of an active window's title bar if the gradient effect is
      enabled.
    * `active-caption-gradient` - Right side color in the color gradient of an
      active window's title bar.
    * `app-workspace` - Background color of multiple document interface (MDI)
      applications.
    * `button-text` - Text on push buttons.
    * `caption-text` - Text in caption, size box, and scroll bar arrow box.
    * `desktop` - Desktop background color.
    * `disabled-text` - Grayed (disabled) text.
    * `highlight` - Item(s) selected in a control.
    * `highlight-text` - Text of item(s) selected in a control.
    * `hotlight` - Color for a hyperlink or hot-tracked item.
    * `inactive-border` - Inactive window border.
    * `inactive-caption` - Inactive window caption. Specifies the left side color
      in the color gradient of an inactive window's title bar if the gradient
      effect is enabled.
    * `inactive-caption-gradient` - Right side color in the color gradient of an
      inactive window's title bar.
    * `inactive-caption-text` - Color of text in an inactive caption.
    * `info-background` - Background color for tooltip controls.
    * `info-text` - Text color for tooltip controls.
    * `menu` - Menu background.
    * `menu-highlight` - The color used to highlight menu items when the menu
      appears as a flat menu.
    * `menubar` - The background color for the menu bar when menus appear as flat
      menus.
    * `menu-text` - Text in menus.
    * `scrollbar` - Scroll bar gray area.
    * `window` - Window background.
    * `window-frame` - Window frame.
    * `window-text` - Text in windows.
  * On **macOS**
    * `control-background` - The background of a large interface element, such as a browser or table.
    * `control` - The surface of a control.
    * `control-text` -The text of a control that isn’t disabled.
    * `disabled-control-text` - The text of a control that’s disabled.
    * `find-highlight` - The color of a find indicator.
    * `grid` - The gridlines of an interface element such as a table.
    * `header-text` - The text of a header cell in a table.
    * `highlight` - The virtual light source onscreen.
    * `keyboard-focus-indicator` - The ring that appears around the currently focused control when using the keyboard for interface navigation.
    * `label` - The text of a label containing primary content.
    * `link` - A link to other content.
    * `placeholder-text` -  A placeholder string in a control or text view.
    * `quaternary-label` - The text of a label of lesser importance than a tertiary label such as watermark text.
    * `scrubber-textured-background` - The background of a scrubber in the Touch Bar.
    * `secondary-label` - The text of a label of lesser importance than a normal label such as a label used to represent a subheading or additional information.
    * `selected-content-background` - The background for selected content in a key window or view.
    * `selected-control` - The surface of a selected control.
    * `selected-control-text` - The text of a selected control.
    * `selected-menu-item-text` - The text of a selected menu.
    * `selected-text-background` - The background of selected text.
    * `selected-text` - Selected text.
    * `separator` - A separator between different sections of content.
    * `shadow` - The virtual shadow cast by a raised object onscreen.
    * `tertiary-label` - The text of a label of lesser importance than a secondary label such as a label used to represent disabled text.
    * `text-background` - Text background.
    * `text` -  The text in a document.
    * `under-page-background` -  The background behind a document's content.
    * `unemphasized-selected-content-background` - The selected content in a non-key window or view.
    * `unemphasized-selected-text-background` - A background for selected text in a non-key window or view.
    * `unemphasized-selected-text` - Selected text in a non-key window or view.
    * `window-background` - The background of a window.
    * `window-frame-text` - The text in the window's titlebar area.

Returns `string` - The system color setting in RGBA hexadecimal form (`#RRGGBBAA`).
See the [Windows docs][windows-colors] and the [macOS docs][macos-colors] for more details.

The following colors are only available on macOS 10.14: `find-highlight`, `selected-content-background`, `separator`, `unemphasized-selected-content-background`, `unemphasized-selected-text-background`, and `unemphasized-selected-text`.

[windows-colors]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getsyscolor
[macos-colors]: https://developer.apple.com/design/human-interface-guidelines/macos/visual-design/color#dynamic-system-colors

### `systemPreferences.getSystemColor(color)` _macOS_

* `color` string - One of the following values:
  * `blue`
  * `brown`
  * `gray`
  * `green`
  * `orange`
  * `pink`
  * `purple`
  * `red`
  * `yellow`

Returns `string` - The standard system color formatted as `#RRGGBBAA`.

Returns one of several standard system colors that automatically adapt to vibrancy and changes in accessibility settings like 'Increase contrast' and 'Reduce transparency'. See [Apple Documentation](https://developer.apple.com/design/human-interface-guidelines/macos/visual-design/color#system-colors) for  more details.

### `systemPreferences.getEffectiveAppearance()` _macOS_

Returns `string` - Can be `dark`, `light` or `unknown`.

Gets the macOS appearance setting that is currently applied to your application,
maps to [NSApplication.effectiveAppearance](https://developer.apple.com/documentation/appkit/nsapplication/2967171-effectiveappearance?language=objc)

### `systemPreferences.canPromptTouchID()` _macOS_

Returns `boolean` - whether or not this device has the ability to use Touch ID.

### `systemPreferences.promptTouchID(reason)` _macOS_

* `reason` string - The reason you are asking for Touch ID authentication

Returns `Promise<void>` - resolves if the user has successfully authenticated with Touch ID.

```js
const { systemPreferences } = require('electron')

systemPreferences.promptTouchID('To get consent for a Security-Gated Thing').then(success => {
  console.log('You have successfully authenticated with Touch ID!')
}).catch(err => {
  console.log(err)
})
```

This API itself will not protect your user data; rather, it is a mechanism to allow you to do so. Native apps will need to set [Access Control Constants](https://developer.apple.com/documentation/security/secaccesscontrolcreateflags?language=objc) like [`kSecAccessControlUserPresence`](https://developer.apple.com/documentation/security/secaccesscontrolcreateflags/ksecaccesscontroluserpresence?language=objc) on their keychain entry so that reading it would auto-prompt for Touch ID biometric consent. This could be done with [`node-keytar`](https://github.com/atom/node-keytar), such that one would store an encryption key with `node-keytar` and only fetch it if `promptTouchID()` resolves.

### `systemPreferences.isTrustedAccessibilityClient(prompt)` _macOS_

* `prompt` boolean - whether or not the user will be informed via prompt if the current process is untrusted.

Returns `boolean` - `true` if the current process is a trusted accessibility client and `false` if it is not.

### `systemPreferences.getMediaAccessStatus(mediaType)` _Windows_ _macOS_

* `mediaType` string - Can be `microphone`, `camera` or `screen`.

Returns `string` - Can be `not-determined`, `granted`, `denied`, `restricted` or `unknown`.

This user consent was not required on macOS 10.13 High Sierra so this method will always return `granted`.
macOS 10.14 Mojave or higher requires consent for `microphone` and `camera` access.
macOS 10.15 Catalina or higher requires consent for `screen` access.

Windows 10 has a global setting controlling `microphone` and `camera` access for all win32 applications.
It will always return `granted` for `screen` and for all media types on older versions of Windows.

### `systemPreferences.askForMediaAccess(mediaType)` _macOS_

* `mediaType` string - the type of media being requested; can be `microphone`, `camera`.

Returns `Promise<boolean>` - A promise that resolves with `true` if consent was granted and `false` if it was denied. If an invalid `mediaType` is passed, the promise will be rejected. If an access request was denied and later is changed through the System Preferences pane, a restart of the app will be required for the new permissions to take effect. If access has already been requested and denied, it _must_ be changed through the preference pane; an alert will not pop up and the promise will resolve with the existing access status.

**Important:** In order to properly leverage this API, you [must set](https://developer.apple.com/documentation/avfoundation/cameras_and_media_capture/requesting_authorization_for_media_capture_on_macos?language=objc) the `NSMicrophoneUsageDescription` and `NSCameraUsageDescription` strings in your app's `Info.plist` file. The values for these keys will be used to populate the permission dialogs so that the user will be properly informed as to the purpose of the permission request. See [Electron Application Distribution](../tutorial/application-distribution.md#rebranding-with-downloaded-binaries) for more information about how to set these in the context of Electron.

This user consent was not required until macOS 10.14 Mojave, so this method will always return `true` if your system is running 10.13 High Sierra.

### `systemPreferences.getAnimationSettings()`

Returns `Object`:

* `shouldRenderRichAnimation` boolean - Returns true if rich animations should be rendered. Looks at session type (e.g. remote desktop) and accessibility settings to give guidance for heavy animations.
* `scrollAnimationsEnabledBySystem` boolean - Determines on a per-platform basis whether scroll animations (e.g. produced by home/end key) should be enabled.
* `prefersReducedMotion` boolean - Determines whether the user desires reduced motion based on platform APIs.

Returns an object with system animation settings.

## Properties

### `systemPreferences.accessibilityDisplayShouldReduceTransparency()` _macOS_

A `boolean` property which determines whether the app avoids using semitransparent backgrounds. This maps to [NSWorkspace.accessibilityDisplayShouldReduceTransparency](https://developer.apple.com/documentation/appkit/nsworkspace/1533006-accessibilitydisplayshouldreduce)

### `systemPreferences.effectiveAppearance` _macOS_ _Readonly_

A `string` property that can be `dark`, `light` or `unknown`.

Returns the macOS appearance setting that is currently applied to your application,
maps to [NSApplication.effectiveAppearance](https://developer.apple.com/documentation/appkit/nsapplication/2967171-effectiveappearance?language=objc)
