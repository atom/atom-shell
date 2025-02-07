# powerMonitor

> Monitor power state changes.

Process: [Main](../glossary.md#main-process)

## Events

The `powerMonitor` module emits the following events:

### Event: 'suspend'

Emitted when the system is suspending.

### Event: 'resume'

Emitted when system is resuming.

### Event: 'on-ac' _macOS_ _Windows_

Emitted when the system changes to AC power.

### Event: 'on-battery' _macOS_  _Windows_

Emitted when system changes to battery power.

### Event: 'thermal-state-change' _macOS_

Returns:

* `details` Object
  * `state` string - The system's new thermal state. Can be `unknown`, `nominal`, `fair`, `serious`, `critical`.

Emitted when the thermal state of the system changes. Notification of a change
in the thermal status of the system, such as entering a critical temperature
range. Depending on the severity, the system might take steps to reduce said
temperature, for example, throttling the CPU or switching on the fans if
available.

Apps may react to the new state by reducing expensive computing tasks (e.g.
video encoding), or notifying the user. The same state might be received
repeatedly.

See https://developer.apple.com/library/archive/documentation/Performance/Conceptual/power_efficiency_guidelines_osx/RespondToThermalStateChanges.html

### Event: 'speed-limit-change' _macOS_ _Windows_

Returns:

* `details` Object
  * `limit` number - The operating system's advertised speed limit for CPUs, in percent.

Notification of a change in the operating system's advertised speed limit for
CPUs, in percent. Values below 100 indicate that the system is impairing
processing power due to thermal management.

### Event: 'shutdown' _Linux_ _macOS_

Emitted when the system is about to reboot or shut down. If the event handler
invokes `e.preventDefault()`, Electron will attempt to delay system shutdown in
order for the app to exit cleanly. If `e.preventDefault()` is called, the app
should exit as soon as possible by calling something like `app.quit()`.

### Event: 'lock-screen' _macOS_ _Windows_

Emitted when the system is about to lock the screen.

### Event: 'unlock-screen' _macOS_ _Windows_

Emitted as soon as the systems screen is unlocked.

### Event: 'user-did-become-active' _macOS_

Emitted when a login session is activated. See [documentation](https://developer.apple.com/documentation/appkit/nsworkspacesessiondidbecomeactivenotification?language=objc) for more information.

### Event: 'user-did-resign-active' _macOS_

Emitted when a login session is deactivated. See [documentation](https://developer.apple.com/documentation/appkit/nsworkspacesessiondidresignactivenotification?language=objc) for more information.

## Methods

The `powerMonitor` module has the following methods:

### `powerMonitor.getSystemIdleState(idleThreshold)`

* `idleThreshold` Integer

Returns `string` - The system's current idle state. Can be `active`, `idle`, `locked` or `unknown`.

Calculate the system idle state. `idleThreshold` is the amount of time (in seconds)
before considered idle.  `locked` is available on supported systems only.

### `powerMonitor.getSystemIdleTime()`

Returns `Integer` - Idle time in seconds

Calculate system idle time in seconds.

### `powerMonitor.getCurrentThermalState()` _macOS_

Returns `string` - The system's current thermal state. Can be `unknown`, `nominal`, `fair`, `serious`, or `critical`.

### `powerMonitor.isOnBatteryPower()`

Returns `boolean` - Whether the system is on battery power.

To monitor for changes in this property, use the `on-battery` and `on-ac`
events.

## Properties

### `powerMonitor.onBatteryPower`

A `boolean` property. True if the system is on battery power.

See [`powerMonitor.isOnBatteryPower()`](#powermonitorisonbatterypower).
