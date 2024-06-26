# Environment Variables

> Control application configuration and behavior without changing code.

Certain Electron behaviors are controlled by environment variables because they
are initialized earlier than the command line flags and the app's code.

POSIX shell example:

```sh
$ export ELECTRON_ENABLE_LOGGING=true
$ electron
```

Windows console example:

```powershell
> set ELECTRON_ENABLE_LOGGING=true
> electron
```

## Production Variables

The following environment variables are intended primarily for use at runtime
in packaged Electron applications.

### `NODE_OPTIONS`

Electron includes support for a subset of Node's [`NODE_OPTIONS`](https://nodejs.org/api/cli.html#cli_node_options_options). The majority are supported with the exception of those which conflict with Chromium's use of BoringSSL.

Example:

```sh
export NODE_OPTIONS="--no-warnings --max-old-space-size=2048"
```

Unsupported options are:

```sh
--use-bundled-ca
--force-fips
--enable-fips
--openssl-config
--use-openssl-ca
```

`NODE_OPTIONS` are explicitly disallowed in packaged apps, except for the following:

```sh
--max-http-header-size
--http-parser
```

If the [`nodeOptions` fuse](../tutorial/fuses.md#nodeoptions) is disabled, `NODE_OPTIONS` will be ignored.

### `NODE_EXTRA_CA_CERTS`

See [Node.js cli documentation](https://github.com/nodejs/node/blob/main/doc/api/cli.md#node_extra_ca_certsfile) for details.

```sh
export NODE_EXTRA_CA_CERTS=/path/to/cert.pem 
```

If the [`nodeOptions` fuse](../tutorial/fuses.md#nodeoptions) is disabled, `NODE_EXTRA_CA_CERTS` will be ignored.

### `GOOGLE_API_KEY`

Geolocation support in Electron requires the use of Google Cloud Platform's
geolocation webservice. To enable this feature, acquire a
[Google API key](https://developers.google.com/maps/documentation/geolocation/get-api-key)
and place the following code in your main process file, before opening any
browser windows that will make geolocation requests:

```js
process.env.GOOGLE_API_KEY = 'YOUR_KEY_HERE'
```

By default, a newly generated Google API key may not be allowed to make geolocation requests.
To enable the geolocation webservice for your project, enable it through the
[API library](https://console.cloud.google.com/apis/library).

N.B. You will need to add a
[Billing Account](https://cloud.google.com/billing/docs/how-to/payment-methods#add_a_payment_method)
to the project associated to the API key for the geolocation webservice to work.

### `ELECTRON_NO_ASAR`

Disables ASAR support. This variable is only supported in forked child processes
and spawned child processes that set `ELECTRON_RUN_AS_NODE`.

### `ELECTRON_RUN_AS_NODE`

Starts the process as a normal Node.js process.

In this mode, you will be able to pass [cli options](https://nodejs.org/api/cli.html) to Node.js as
you would when running the normal Node.js executable, with the exception of the following flags:

* "--openssl-config"
* "--use-bundled-ca"
* "--use-openssl-ca",
* "--force-fips"
* "--enable-fips"

These flags are disabled owing to the fact that Electron uses BoringSSL instead of OpenSSL when building Node.js'
`crypto` module, and so will not work as designed.

If the [`runAsNode` fuse](../tutorial/fuses.md#L13) is disabled, `ELECTRON_RUN_AS_NODE` will be ignored.

### `ELECTRON_NO_ATTACH_CONSOLE` _Windows_

Don't attach to the current console session.

### `ELECTRON_FORCE_WINDOW_MENU_BAR` _Linux_

Don't use the global menu bar on Linux.

### `ELECTRON_TRASH` _Linux_

Set the trash implementation on Linux. Default is `gio`.

Options:

* `gvfs-trash`
* `trash-cli`
* `kioclient5`
* `kioclient`

### `ELECTRON_OZONE_PLATFORM_HINT` _Linux_

Selects the preferred platform backend used on Linux. The default one is `x11`. `auto` selects Wayland if possible, X11 otherwise.

Options:

* `auto`
* `wayland`
* `x11`

## Development Variables

The following environment variables are intended primarily for development and
debugging purposes.

### `ELECTRON_ENABLE_LOGGING`

Prints Chromium's internal logging to the console.

Setting this variable is the same as passing `--enable-logging`
on the command line. For more info, see `--enable-logging` in
[command-line switches](./command-line-switches.md#--enable-loggingfile).

### `ELECTRON_LOG_FILE`

Sets the file destination for Chromium's internal logging.

Setting this variable is the same as passing `--log-file`
on the command line. For more info, see `--log-file` in
[command-line switches](./command-line-switches.md#--log-filepath).

### `ELECTRON_DEBUG_NOTIFICATIONS`

Adds extra logs to [`Notification`](./notification.md) lifecycles on macOS to aid in debugging. Extra logging will be displayed when new Notifications are created or activated. They will also be displayed when common actions are taken: a notification is shown, dismissed, its button is clicked, or it is replied to.

Sample output:

```sh
Notification created (com.github.Electron:notification:EAF7B87C-A113-43D7-8E76-F88EC9D73D44)
Notification displayed (com.github.Electron:notification:EAF7B87C-A113-43D7-8E76-F88EC9D73D44)
Notification activated (com.github.Electron:notification:EAF7B87C-A113-43D7-8E76-F88EC9D73D44)
Notification replied to (com.github.Electron:notification:EAF7B87C-A113-43D7-8E76-F88EC9D73D44)
```

### `ELECTRON_LOG_ASAR_READS`

When Electron reads from an ASAR file, log the read offset and file path to
the system `tmpdir`. The resulting file can be provided to the ASAR module
to optimize file ordering.

### `ELECTRON_ENABLE_STACK_DUMPING`

Prints the stack trace to the console when Electron crashes.

This environment variable will not work if the `crashReporter` is started.

### `ELECTRON_DEFAULT_ERROR_MODE` _Windows_

Shows the Windows's crash dialog when Electron crashes.

This environment variable will not work if the `crashReporter` is started.

### `ELECTRON_OVERRIDE_DIST_PATH`

When running from the `electron` package, this variable tells
the `electron` command to use the specified build of Electron instead of
the one downloaded by `npm install`. Usage:

```sh
export ELECTRON_OVERRIDE_DIST_PATH=/Users/username/projects/electron/out/Testing
```

## Set By Electron

Electron sets some variables in your environment at runtime.

### `ORIGINAL_XDG_CURRENT_DESKTOP`

This variable is set to the value of `XDG_CURRENT_DESKTOP` that your application
originally launched with.  Electron sometimes modifies the value of `XDG_CURRENT_DESKTOP`
to affect other logic within Chromium so if you want access to the _original_ value
you should look up this environment variable instead.
