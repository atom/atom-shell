## Class: CommandLine

> Manipulate the command line arguments for your app that Chromium reads

Process: [Main](../glossary.md#main-process)<br />
_This class is not exported from the `'electron'` module. It is only available as a return value of other methods in the Electron API._

The following example shows how to check if the `--disable-gpu` flag is set.

```js
const { app } = require('electron')

app.commandLine.hasSwitch('disable-gpu')
```

For more information on what kinds of flags and switches you can use, check
out the [Command Line Switches](./command-line-switches.md)
document.

### Instance Methods

#### `commandLine.appendSwitch(switch[, value])`

* `switch` string - A command-line switch, without the leading `--`
* `value` string (optional) - A value for the given switch

Append a switch (with optional `value`) to Chromium's command line.

**Note:** This will not affect `process.argv`. The intended usage of this function is to
control Chromium's behavior.

#### `commandLine.appendArgument(value)`

* `value` string - The argument to append to the command line

Append an argument to Chromium's command line. The argument will be quoted
correctly. Switches will precede arguments regardless of appending order.

If you're appending an argument like `--switch=value`, consider using `appendSwitch('switch', 'value')` instead.

**Note:** This will not affect `process.argv`. The intended usage of this function is to
control Chromium's behavior.

#### `commandLine.hasSwitch(switch)`

* `switch` string - A command-line switch

Returns `boolean` - Whether the command-line switch is present.

#### `commandLine.getSwitchValue(switch)`

* `switch` string - A command-line switch

Returns `string` - The command-line switch value.

**Note:** When the switch is not present or has no value, it returns empty string.

#### `commandLine.removeSwitch(switch)`

* `switch` string - A command-line switch

Removes the specified switch from Chromium's command line.

**Note:** This will not affect `process.argv`. The intended usage of this function is to
control Chromium's behavior.
