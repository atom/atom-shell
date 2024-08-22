# Notification

> Create OS desktop notifications

Process: [Main](../glossary.md#main-process)

:::info Renderer process notifications

If you want to show notifications from a renderer process you should use the
[web Notifications API](../tutorial/notifications.md)

:::

## Class: Notification

> Create OS desktop notifications

Process: [Main](../glossary.md#main-process)

`Notification` is an [EventEmitter][event-emitter].

It creates a new `Notification` with native properties as set by the `options`.

### Static Methods

The `Notification` class has the following static methods:

#### `Notification.isSupported()`

Returns `boolean` - Whether or not desktop notifications are supported on the current system

### `new Notification([options])`

* `options` Object (optional)
  * `title` string (optional) - A title for the notification, which will be displayed at the top of the notification window when it is shown.
  * `subtitle` string (optional) _macOS_ - A subtitle for the notification, which will be displayed below the title.
  * `body` string (optional) - The body text of the notification, which will be displayed below the title or subtitle.
  * `silent` boolean (optional) - Whether or not to suppress the OS notification noise when showing the notification.
  * `icon` (string | [NativeImage](native-image.md)) (optional) - An icon to use in the notification. If a string is passed, it must be a valid path to a local icon file.
  * `hasReply` boolean (optional) _macOS_ - Whether or not to add an inline reply option to the notification.
  * `timeoutType` string (optional) _Linux_ _Windows_ - The timeout duration of the notification. Can be 'default' or 'never'.
  * `replyPlaceholder` string (optional) _macOS_ - The placeholder to write in the inline reply input field.
  * `sound` string (optional) _macOS_ - The name of the sound file to play when the notification is shown.
  * `urgency` string (optional) _Linux_ - The urgency level of the notification. Can be 'normal', 'critical', or 'low'.
  * `actions` [NotificationAction[]](structures/notification-action.md) (optional) _macOS_ - Actions to add to the notification. Please read the available actions and limitations in the `NotificationAction` documentation.
  * `closeButtonText` string (optional) _macOS_ - A custom title for the close button of an alert. An empty string will cause the default localized text to be used.
  * `toastXml` string (optional) _Windows_ - A custom description of the Notification on Windows superseding all properties above. Provides full customization of design and behavior of the notification.

### Instance Events

Objects created with `new Notification` emit the following events:

:::info

Some events are only available on specific operating systems and are labeled as such.

:::

#### Event: 'show'

Returns:

* `event` Event

Emitted when the notification is shown to the user. Note that this event can be fired
multiple times as a notification can be shown multiple times through the
`show()` method.

#### Event: 'click'

Returns:

* `event` Event

Emitted when the notification is clicked by the user.

#### Event: 'close'

Returns:

* `event` Event

Emitted when the notification is closed by manual intervention from the user.

This event is not guaranteed to be emitted in all cases where the notification
is closed.

On Windows, the `close` event can be emitted in one of three ways: programmatic dismissal with `notification.close()`, by the user closing the notification, or via system timeout. If a notification is in the Action Center after the initial `close` event is emitted, a call to `notification.close()` will remove the notification from the action center but the `close` event will not be emitted again.

#### Event: 'reply' _macOS_

Returns:

* `event` Event
* `reply` string - The string the user entered into the inline reply field.

Emitted when the user clicks the "Reply" button on a notification with `hasReply: true`.

#### Event: 'action' _macOS_

Returns:

* `event` Event
* `index` number - The index of the action that was activated.

#### Event: 'failed' _Windows_

Returns:

* `event` Event
* `error` string - The error encountered during execution of the `show()` method.

Emitted when an error is encountered while creating and showing the native notification.

### Instance Methods

Objects created with the `new Notification()` constructor have the following instance methods:

#### `notification.show()`

Immediately shows the notification to the user. Unlike the web notification API,
instantiating a `new Notification()` does not immediately show it to the user. Instead, you need to
call this method before the OS will display it.

If the notification has been shown before, this method will dismiss the previously
shown notification and create a new one with identical properties.

#### `notification.close()`

Dismisses the notification.

On Windows, calling `notification.close()` while the notification is visible on screen will dismiss the notification and remove it from the Action Center. If `notification.close()` is called after the notification is no longer visible on screen, calling `notification.close()` will try remove it from the Action Center.

### Instance Properties

#### `notification.title`

A `string` property representing the title of the notification.

#### `notification.subtitle`

A `string` property representing the subtitle of the notification.

#### `notification.body`

A `string` property representing the body of the notification.

#### `notification.replyPlaceholder`

A `string` property representing the reply placeholder of the notification.

#### `notification.sound`

A `string` property representing the sound of the notification.

#### `notification.closeButtonText`

A `string` property representing the close button text of the notification.

#### `notification.silent`

A `boolean` property representing whether the notification is silent.

#### `notification.hasReply`

A `boolean` property representing whether the notification has a reply action.

#### `notification.urgency` _Linux_

A `string` property representing the urgency level of the notification. Can be 'normal', 'critical', or 'low'.

Default is 'low' - see [NotifyUrgency](https://developer-old.gnome.org/notification-spec/#urgency-levels) for more information.

#### `notification.timeoutType` _Linux_ _Windows_

A `string` property representing the type of timeout duration for the notification. Can be 'default' or 'never'.

If `timeoutType` is set to 'never', the notification never expires. It stays open until closed by the calling API or the user.

#### `notification.actions`

A [`NotificationAction[]`](structures/notification-action.md) property representing the actions of the notification.

#### `notification.toastXml` _Windows_

A `string` property representing the custom Toast XML of the notification.

### Playing Sounds

On macOS, you can specify the name of the sound you'd like to play when the
notification is shown. Any of the default sounds (under System Preferences >
Sound) can be used, in addition to custom sound files. Be sure that the sound
file is copied under the app bundle (e.g., `YourApp.app/Contents/Resources`),
or one of the following locations:

* `~/Library/Sounds`
* `/Library/Sounds`
* `/Network/Library/Sounds`
* `/System/Library/Sounds`

See the [`NSSound`](https://developer.apple.com/documentation/appkit/nssound) docs for more information.

[event-emitter]: https://nodejs.org/api/events.html#events_class_eventemitter
