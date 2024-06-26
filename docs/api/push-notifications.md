# pushNotifications

Process: [Main](../glossary.md#main-process)

> Register for and receive notifications from remote push notification services

For example, when registering for push notifications via Apple push notification services (APNS):

```js
const { pushNotifications, Notification } = require('electron')

pushNotifications.registerForAPNSNotifications().then((token) => {
  // forward token to your remote notification server
})

pushNotifications.on('received-apns-notification', (event, userInfo) => {
  // generate a new Notification object with the relevant userInfo fields
})
```

## Events

The `pushNotification` module emits the following events:

#### Event: 'received-apns-notification' _macOS_

Returns:

* `event` Event
* `userInfo` Record\<String, any\>

Emitted when the app receives a remote notification while running.
See: https://developer.apple.com/documentation/appkit/nsapplicationdelegate/1428430-application?language=objc

## Methods

The `pushNotification` module has the following methods:

### `pushNotifications.registerForAPNSNotifications()` _macOS_

Returns `Promise<string>`

Registers the app with Apple Push Notification service (APNS) to receive [Badge, Sound, and Alert](https://developer.apple.com/documentation/appkit/nsremotenotificationtype?language=objc) notifications. If registration is successful, the promise will be resolved with the APNS device token. Otherwise, the promise will be rejected with an error message.
See: https://developer.apple.com/documentation/appkit/nsapplication/1428476-registerforremotenotificationtyp?language=objc

### `pushNotifications.unregisterForAPNSNotifications()` _macOS_

Unregisters the app from notifications received from APNS.
See: https://developer.apple.com/documentation/appkit/nsapplication/1428747-unregisterforremotenotifications?language=objc
