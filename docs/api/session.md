# session

> Manage browser sessions, cookies, cache, proxy settings, etc.

Process: [Main](../glossary.md#main-process)

The `session` module can be used to create new `Session` objects.

You can also access the `session` of existing pages by using the `session`
property of [`WebContents`](web-contents.md), or from the `session` module.

```javascript
const { BrowserWindow } = require('electron')

const win = new BrowserWindow({ width: 800, height: 600 })
win.loadURL('http://github.com')

const ses = win.webContents.session
console.log(ses.getUserAgent())
```

## Methods

The `session` module has the following methods:

### `session.fromPartition(partition[, options])`

* `partition` string
* `options` Object (optional)
  * `cache` boolean - Whether to enable cache.

Returns `Session` - A session instance from `partition` string. When there is an existing
`Session` with the same `partition`, it will be returned; otherwise a new
`Session` instance will be created with `options`.

If `partition` starts with `persist:`, the page will use a persistent session
available to all pages in the app with the same `partition`. if there is no
`persist:` prefix, the page will use an in-memory session. If the `partition` is
empty then default session of the app will be returned.

To create a `Session` with `options`, you have to ensure the `Session` with the
`partition` has never been used before. There is no way to change the `options`
of an existing `Session` object.

### `session.fromPath(path[, options])`

* `path` string
* `options` Object (optional)
  * `cache` boolean (optional) - Whether to enable cache.
  * `quota` number (optional) - Use a quota size for a partition (in bytes).

Returns `Session` - A session instance from the absolute path as specified by the `path`
string. When there is an existing `Session` with the same absolute path, it
will be returned; otherwise a new `Session` instance will be created with `options`. The
call will throw an error if the path is not an absolute path. Additionally, an error will
be thrown if an empty string is provided.

To create a `Session` with `options`, you have to ensure the `Session` with the
`path` has never been used before. There is no way to change the `options`
of an existing `Session` object. The optional `quota` parameter can be used to
specify the a quota size that can be used by the session, which will be returned as the quota provided by navigator.storage.estimate(). If the `quota` is not provided, the
size of the volume on which path is located will be used. A smaller quota size will allow
the [LRU](https://en.wikipedia.org/wiki/Page_replacement_algorithm#Least_recently_used)
algorithm to evict more data from the cache, though there is no guarantee that the
cache will not exceed the quota size.

## Properties

The `session` module has the following properties:

### `session.defaultSession`

A `Session` object, the default session object of the app.

## Class: Session

> Get and set properties of a session.

Process: [Main](../glossary.md#main-process)<br />
_This class is not exported from the `'electron'` module. It is only available as a return value of other methods in the Electron API._

You can create a `Session` object in the `session` module:

```javascript
const { session } = require('electron')
const ses = session.fromPartition('persist:name')
console.log(ses.getUserAgent())
```

### Instance Events

The following events are available on instances of `Session`:

#### Event: 'will-download'

Returns:

* `event` Event
* `item` [DownloadItem](download-item.md)
* `webContents` [WebContents](web-contents.md)

Emitted when Electron is about to download `item` in `webContents`.

Calling `event.preventDefault()` will cancel the download and `item` will not be
available from next tick of the process.

```javascript
const { session } = require('electron')
session.defaultSession.on('will-download', (event, item, webContents) => {
  event.preventDefault()
  require('got')(item.getURL()).then((response) => {
    require('fs').writeFileSync('/somewhere', response.body)
  })
})
```

#### Event: 'extension-loaded'

Returns:

* `event` Event
* `extension` [Extension](structures/extension.md)

Emitted after an extension is loaded. This occurs whenever an extension is
added to the "enabled" set of extensions. This includes:

* Extensions being loaded from `Session.loadExtension`.
* Extensions being reloaded:
  * from a crash.
  * if the extension requested it ([`chrome.runtime.reload()`](https://developer.chrome.com/extensions/runtime#method-reload)).

#### Event: 'extension-unloaded'

Returns:

* `event` Event
* `extension` [Extension](structures/extension.md)

Emitted after an extension is unloaded. This occurs when
`Session.removeExtension` is called.

#### Event: 'extension-ready'

Returns:

* `event` Event
* `extension` [Extension](structures/extension.md)

Emitted after an extension is loaded and all necessary browser state is
initialized to support the start of the extension's background page.

#### Event: 'preconnect'

Returns:

* `event` Event
* `preconnectUrl` string - The URL being requested for preconnection by the
  renderer.
* `allowCredentials` boolean - True if the renderer is requesting that the
  connection include credentials (see the
  [spec](https://w3c.github.io/resource-hints/#preconnect) for more details.)

Emitted when a render process requests preconnection to a URL, generally due to
a [resource hint](https://w3c.github.io/resource-hints/).

#### Event: 'spellcheck-dictionary-initialized'

Returns:

* `event` Event
* `languageCode` string - The language code of the dictionary file

Emitted when a hunspell dictionary file has been successfully initialized. This
occurs after the file has been downloaded.

#### Event: 'spellcheck-dictionary-download-begin'

Returns:

* `event` Event
* `languageCode` string - The language code of the dictionary file

Emitted when a hunspell dictionary file starts downloading

#### Event: 'spellcheck-dictionary-download-success'

Returns:

* `event` Event
* `languageCode` string - The language code of the dictionary file

Emitted when a hunspell dictionary file has been successfully downloaded

#### Event: 'spellcheck-dictionary-download-failure'

Returns:

* `event` Event
* `languageCode` string - The language code of the dictionary file

Emitted when a hunspell dictionary file download fails.  For details
on the failure you should collect a netlog and inspect the download
request.

#### Event: 'select-hid-device'

Returns:

* `event` Event
* `details` Object
  * `deviceList` [HIDDevice[]](structures/hid-device.md)
  * `frame` [WebFrameMain](web-frame-main.md)
* `callback` Function
  * `deviceId` string | null (optional)

Emitted when a HID device needs to be selected when a call to
`navigator.hid.requestDevice` is made. `callback` should be called with
`deviceId` to be selected; passing no arguments to `callback` will
cancel the request.  Additionally, permissioning on `navigator.hid` can
be further managed by using [`ses.setPermissionCheckHandler(handler)`](#sessetpermissioncheckhandlerhandler)
and [`ses.setDevicePermissionHandler(handler)`](#sessetdevicepermissionhandlerhandler).

```javascript
const { app, BrowserWindow } = require('electron')

let win = null

app.whenReady().then(() => {
  win = new BrowserWindow()

  win.webContents.session.setPermissionCheckHandler((webContents, permission, requestingOrigin, details) => {
    if (permission === 'hid') {
      // Add logic here to determine if permission should be given to allow HID selection
      return true
    }
    return false
  })

  // Optionally, retrieve previously persisted devices from a persistent store
  const grantedDevices = fetchGrantedDevices()

  win.webContents.session.setDevicePermissionHandler((details) => {
    if (new URL(details.origin).hostname === 'some-host' && details.deviceType === 'hid') {
      if (details.device.vendorId === 123 && details.device.productId === 345) {
        // Always allow this type of device (this allows skipping the call to `navigator.hid.requestDevice` first)
        return true
      }

      // Search through the list of devices that have previously been granted permission
      return grantedDevices.some((grantedDevice) => {
        return grantedDevice.vendorId === details.device.vendorId &&
              grantedDevice.productId === details.device.productId &&
              grantedDevice.serialNumber && grantedDevice.serialNumber === details.device.serialNumber
      })
    }
    return false
  })

  win.webContents.session.on('select-hid-device', (event, details, callback) => {
    event.preventDefault()
    const selectedDevice = details.deviceList.find((device) => {
      return device.vendorId === '9025' && device.productId === '67'
    })
    callback(selectedDevice?.deviceId)
  })
})
```

#### Event: 'hid-device-added'

Returns:

* `event` Event
* `details` Object
  * `device` [HIDDevice[]](structures/hid-device.md)
  * `frame` [WebFrameMain](web-frame-main.md)

Emitted after `navigator.hid.requestDevice` has been called and
`select-hid-device` has fired if a new device becomes available before
the callback from `select-hid-device` is called.  This event is intended for
use when using a UI to ask users to pick a device so that the UI can be updated
with the newly added device.

#### Event: 'hid-device-removed'

Returns:

* `event` Event
* `details` Object
  * `device` [HIDDevice[]](structures/hid-device.md)
  * `frame` [WebFrameMain](web-frame-main.md)

Emitted after `navigator.hid.requestDevice` has been called and
`select-hid-device` has fired if a device has been removed before the callback
from `select-hid-device` is called.  This event is intended for use when using
a UI to ask users to pick a device so that the UI can be updated to remove the
specified device.

#### Event: 'hid-device-revoked'

Returns:

* `event` Event
* `details` Object
  * `device` [HIDDevice[]](structures/hid-device.md)
  * `origin` string (optional) - The origin that the device has been revoked from.

Emitted after `HIDDevice.forget()` has been called.  This event can be used
to help maintain persistent storage of permissions when
`setDevicePermissionHandler` is used.

#### Event: 'select-serial-port'

Returns:

* `event` Event
* `portList` [SerialPort[]](structures/serial-port.md)
* `webContents` [WebContents](web-contents.md)
* `callback` Function
  * `portId` string

Emitted when a serial port needs to be selected when a call to
`navigator.serial.requestPort` is made. `callback` should be called with
`portId` to be selected, passing an empty string to `callback` will
cancel the request.  Additionally, permissioning on `navigator.serial` can
be managed by using [ses.setPermissionCheckHandler(handler)](#sessetpermissioncheckhandlerhandler)
with the `serial` permission.

```javascript
const { app, BrowserWindow } = require('electron')

let win = null

app.whenReady().then(() => {
  win = new BrowserWindow({
    width: 800,
    height: 600
  })

  win.webContents.session.setPermissionCheckHandler((webContents, permission, requestingOrigin, details) => {
    if (permission === 'serial') {
      // Add logic here to determine if permission should be given to allow serial selection
      return true
    }
    return false
  })

  // Optionally, retrieve previously persisted devices from a persistent store
  const grantedDevices = fetchGrantedDevices()

  win.webContents.session.setDevicePermissionHandler((details) => {
    if (new URL(details.origin).hostname === 'some-host' && details.deviceType === 'serial') {
      if (details.device.vendorId === 123 && details.device.productId === 345) {
        // Always allow this type of device (this allows skipping the call to `navigator.serial.requestPort` first)
        return true
      }

      // Search through the list of devices that have previously been granted permission
      return grantedDevices.some((grantedDevice) => {
        return grantedDevice.vendorId === details.device.vendorId &&
              grantedDevice.productId === details.device.productId &&
              grantedDevice.serialNumber && grantedDevice.serialNumber === details.device.serialNumber
      })
    }
    return false
  })

  win.webContents.session.on('select-serial-port', (event, portList, webContents, callback) => {
    event.preventDefault()
    const selectedPort = portList.find((device) => {
      return device.vendorId === '9025' && device.productId === '67'
    })
    if (!selectedPort) {
      callback('')
    } else {
      callback(selectedPort.portId)
    }
  })
})
```

#### Event: 'serial-port-added'

Returns:

* `event` Event
* `port` [SerialPort](structures/serial-port.md)
* `webContents` [WebContents](web-contents.md)

Emitted after `navigator.serial.requestPort` has been called and
`select-serial-port` has fired if a new serial port becomes available before
the callback from `select-serial-port` is called.  This event is intended for
use when using a UI to ask users to pick a port so that the UI can be updated
with the newly added port.

#### Event: 'serial-port-removed'

Returns:

* `event` Event
* `port` [SerialPort](structures/serial-port.md)
* `webContents` [WebContents](web-contents.md)

Emitted after `navigator.serial.requestPort` has been called and
`select-serial-port` has fired if a serial port has been removed before the
callback from `select-serial-port` is called.  This event is intended for use
when using a UI to ask users to pick a port so that the UI can be updated
to remove the specified port.

#### Event: 'serial-port-revoked'

Returns:

* `event` Event
* `details` Object
  * `port` [SerialPort](structures/serial-port.md)
  * `frame` [WebFrameMain](web-frame-main.md)
  * `origin` string - The origin that the device has been revoked from.

Emitted after `SerialPort.forget()` has been called.  This event can be used
to help maintain persistent storage of permissions when `setDevicePermissionHandler` is used.

```js
// Browser Process
const { app, BrowserWindow } = require('electron')

app.whenReady().then(() => {
  const win = new BrowserWindow({
    width: 800,
    height: 600
  })

  win.webContents.session.on('serial-port-revoked', (event, details) => {
    console.log(`Access revoked for serial device from origin ${details.origin}`)
  })
})
```

```js
// Renderer Process

const portConnect = async () => {
  // Request a port.
  const port = await navigator.serial.requestPort()

  // Wait for the serial port to open.
  await port.open({ baudRate: 9600 })

  // ...later, revoke access to the serial port.
  await port.forget()
}
```

#### Event: 'select-usb-device'

Returns:

* `event` Event
* `details` Object
  * `deviceList` [USBDevice[]](structures/usb-device.md)
  * `frame` [WebFrameMain](web-frame-main.md)
* `callback` Function
  * `deviceId` string (optional)

Emitted when a USB device needs to be selected when a call to
`navigator.usb.requestDevice` is made. `callback` should be called with
`deviceId` to be selected; passing no arguments to `callback` will
cancel the request.  Additionally, permissioning on `navigator.usb` can
be further managed by using [`ses.setPermissionCheckHandler(handler)`](#sessetpermissioncheckhandlerhandler)
and [`ses.setDevicePermissionHandler(handler)`](#sessetdevicepermissionhandlerhandler).

```javascript
const { app, BrowserWindow } = require('electron')

let win = null

app.whenReady().then(() => {
  win = new BrowserWindow()

  win.webContents.session.setPermissionCheckHandler((webContents, permission, requestingOrigin, details) => {
    if (permission === 'usb') {
      // Add logic here to determine if permission should be given to allow USB selection
      return true
    }
    return false
  })

  // Optionally, retrieve previously persisted devices from a persistent store (fetchGrantedDevices needs to be implemented by developer to fetch persisted permissions)
  const grantedDevices = fetchGrantedDevices()

  win.webContents.session.setDevicePermissionHandler((details) => {
    if (new URL(details.origin).hostname === 'some-host' && details.deviceType === 'usb') {
      if (details.device.vendorId === 123 && details.device.productId === 345) {
        // Always allow this type of device (this allows skipping the call to `navigator.usb.requestDevice` first)
        return true
      }

      // Search through the list of devices that have previously been granted permission
      return grantedDevices.some((grantedDevice) => {
        return grantedDevice.vendorId === details.device.vendorId &&
              grantedDevice.productId === details.device.productId &&
              grantedDevice.serialNumber && grantedDevice.serialNumber === details.device.serialNumber
      })
    }
    return false
  })

  win.webContents.session.on('select-usb-device', (event, details, callback) => {
    event.preventDefault()
    const selectedDevice = details.deviceList.find((device) => {
      return device.vendorId === '9025' && device.productId === '67'
    })
    if (selectedDevice) {
      // Optionally, add this to the persisted devices (updateGrantedDevices needs to be implemented by developer to persist permissions)
      grantedDevices.push(selectedDevice)
      updateGrantedDevices(grantedDevices)
    }
    callback(selectedDevice?.deviceId)
  })
})
```

#### Event: 'usb-device-added'

Returns:

* `event` Event
* `device` [USBDevice](structures/usb-device.md)
* `webContents` [WebContents](web-contents.md)

Emitted after `navigator.usb.requestDevice` has been called and
`select-usb-device` has fired if a new device becomes available before
the callback from `select-usb-device` is called.  This event is intended for
use when using a UI to ask users to pick a device so that the UI can be updated
with the newly added device.

#### Event: 'usb-device-removed'

Returns:

* `event` Event
* `device` [USBDevice](structures/usb-device.md)
* `webContents` [WebContents](web-contents.md)

Emitted after `navigator.usb.requestDevice` has been called and
`select-usb-device` has fired if a device has been removed before the callback
from `select-usb-device` is called.  This event is intended for use when using
a UI to ask users to pick a device so that the UI can be updated to remove the
specified device.

#### Event: 'usb-device-revoked'

Returns:

* `event` Event
* `details` Object
  * `device` [USBDevice](structures/usb-device.md)
  * `origin` string (optional) - The origin that the device has been revoked from.

Emitted after `USBDevice.forget()` has been called.  This event can be used
to help maintain persistent storage of permissions when
`setDevicePermissionHandler` is used.

### Instance Methods

The following methods are available on instances of `Session`:

#### `ses.getCacheSize()`

Returns `Promise<Integer>` - the session's current cache size, in bytes.

#### `ses.clearCache()`

Returns `Promise<void>` - resolves when the cache clear operation is complete.

Clears the session’s HTTP cache.

#### `ses.clearStorageData([options])`

* `options` Object (optional)
  * `origin` string (optional) - Should follow `window.location.origin`’s representation
    `scheme://host:port`.
  * `storages` string[] (optional) - The types of storages to clear, can contain:
    `cookies`, `filesystem`, `indexdb`, `localstorage`,
    `shadercache`, `websql`, `serviceworkers`, `cachestorage`. If not
    specified, clear all storage types.
  * `quotas` string[] (optional) - The types of quotas to clear, can contain:
    `temporary`, `syncable`. If not specified, clear all quotas.

Returns `Promise<void>` - resolves when the storage data has been cleared.

#### `ses.flushStorageData()`

Writes any unwritten DOMStorage data to disk.

#### `ses.setProxy(config)`

* `config` Object
  * `mode` string (optional) - The proxy mode. Should be one of `direct`,
    `auto_detect`, `pac_script`, `fixed_servers` or `system`. If it's
    unspecified, it will be automatically determined based on other specified
    options.
    * `direct`
      In direct mode all connections are created directly, without any proxy involved.
    * `auto_detect`
      In auto_detect mode the proxy configuration is determined by a PAC script that can
      be downloaded at http://wpad/wpad.dat.
    * `pac_script`
      In pac_script mode the proxy configuration is determined by a PAC script that is
      retrieved from the URL specified in the `pacScript`. This is the default mode
      if `pacScript` is specified.
    * `fixed_servers`
      In fixed_servers mode the proxy configuration is specified in `proxyRules`.
      This is the default mode if `proxyRules` is specified.
    * `system`
      In system mode the proxy configuration is taken from the operating system.
      Note that the system mode is different from setting no proxy configuration.
      In the latter case, Electron falls back to the system settings
      only if no command-line options influence the proxy configuration.
  * `pacScript` string (optional) - The URL associated with the PAC file.
  * `proxyRules` string (optional) - Rules indicating which proxies to use.
  * `proxyBypassRules` string (optional) - Rules indicating which URLs should
    bypass the proxy settings.

Returns `Promise<void>` - Resolves when the proxy setting process is complete.

Sets the proxy settings.

When `mode` is unspecified, `pacScript` and `proxyRules` are provided together, the `proxyRules`
option is ignored and `pacScript` configuration is applied.

You may need `ses.closeAllConnections` to close currently in flight connections to prevent
pooled sockets using previous proxy from being reused by future requests.

The `proxyRules` has to follow the rules below:

```sh
proxyRules = schemeProxies[";"<schemeProxies>]
schemeProxies = [<urlScheme>"="]<proxyURIList>
urlScheme = "http" | "https" | "ftp" | "socks"
proxyURIList = <proxyURL>[","<proxyURIList>]
proxyURL = [<proxyScheme>"://"]<proxyHost>[":"<proxyPort>]
```

For example:

* `http=foopy:80;ftp=foopy2` - Use HTTP proxy `foopy:80` for `http://` URLs, and
  HTTP proxy `foopy2:80` for `ftp://` URLs.
* `foopy:80` - Use HTTP proxy `foopy:80` for all URLs.
* `foopy:80,bar,direct://` - Use HTTP proxy `foopy:80` for all URLs, failing
  over to `bar` if `foopy:80` is unavailable, and after that using no proxy.
* `socks4://foopy` - Use SOCKS v4 proxy `foopy:1080` for all URLs.
* `http=foopy,socks5://bar.com` - Use HTTP proxy `foopy` for http URLs, and fail
  over to the SOCKS5 proxy `bar.com` if `foopy` is unavailable.
* `http=foopy,direct://` - Use HTTP proxy `foopy` for http URLs, and use no
  proxy if `foopy` is unavailable.
* `http=foopy;socks=foopy2` - Use HTTP proxy `foopy` for http URLs, and use
  `socks4://foopy2` for all other URLs.

The `proxyBypassRules` is a comma separated list of rules described below:

* `[ URL_SCHEME "://" ] HOSTNAME_PATTERN [ ":" <port> ]`

   Match all hostnames that match the pattern HOSTNAME_PATTERN.

   Examples:
     "foobar.com", "\*foobar.com", "\*.foobar.com", "\*foobar.com:99",
     "https://x.\*.y.com:99"

* `"." HOSTNAME_SUFFIX_PATTERN [ ":" PORT ]`

   Match a particular domain suffix.

   Examples:
     ".google.com", ".com", "http://.google.com"

* `[ SCHEME "://" ] IP_LITERAL [ ":" PORT ]`

   Match URLs which are IP address literals.

   Examples:
     "127.0.1", "\[0:0::1]", "\[::1]", "http://\[::1]:99"

* `IP_LITERAL "/" PREFIX_LENGTH_IN_BITS`

   Match any URL that is to an IP literal that falls between the
   given range. IP range is specified using CIDR notation.

   Examples:
     "192.168.1.1/16", "fefe:13::abc/33".

* `<local>`

   Match local addresses. The meaning of `<local>` is whether the
   host matches one of: "127.0.0.1", "::1", "localhost".

#### `ses.resolveHost(host, [options])`

* `host` string - Hostname to resolve.
* `options` Object (optional)
  * `queryType` string (optional) - Requested DNS query type. If unspecified,
    resolver will pick A or AAAA (or both) based on IPv4/IPv6 settings:
    * `A` - Fetch only A records
    * `AAAA` - Fetch only AAAA records.
  * `source` string (optional) - The source to use for resolved addresses.
    Default allows the resolver to pick an appropriate source. Only affects use
    of big external sources (e.g. calling the system for resolution or using
    DNS). Even if a source is specified, results can still come from cache,
    resolving "localhost" or IP literals, etc. One of the following values:
    * `any` (default) - Resolver will pick an appropriate source. Results could
      come from DNS, MulticastDNS, HOSTS file, etc
    * `system` - Results will only be retrieved from the system or OS, e.g. via
      the `getaddrinfo()` system call
    * `dns` - Results will only come from DNS queries
    * `mdns` - Results will only come from Multicast DNS queries
    * `localOnly` - No external sources will be used. Results will only come
      from fast local sources that are available no matter the source setting,
      e.g. cache, hosts file, IP literal resolution, etc.
  * `cacheUsage` string (optional) - Indicates what DNS cache entries, if any,
    can be used to provide a response. One of the following values:
    * `allowed` (default) - Results may come from the host cache if non-stale
    * `staleAllowed` - Results may come from the host cache even if stale (by
      expiration or network changes)
    * `disallowed` - Results will not come from the host cache.
  * `secureDnsPolicy` string (optional) - Controls the resolver's Secure DNS
    behavior for this request. One of the following values:
    * `allow` (default)
    * `disable`

Returns [`Promise<ResolvedHost>`](structures/resolved-host.md) - Resolves with the resolved IP addresses for the `host`.

#### `ses.resolveProxy(url)`

* `url` URL

Returns `Promise<string>` - Resolves with the proxy information for `url`.

#### `ses.forceReloadProxyConfig()`

Returns `Promise<void>` - Resolves when the all internal states of proxy service is reset and the latest proxy configuration is reapplied if it's already available. The pac script will be fetched from `pacScript` again if the proxy mode is `pac_script`.

#### `ses.setDownloadPath(path)`

* `path` string - The download location.

Sets download saving directory. By default, the download directory will be the
`Downloads` under the respective app folder.

#### `ses.enableNetworkEmulation(options)`

* `options` Object
  * `offline` boolean (optional) - Whether to emulate network outage. Defaults
    to false.
  * `latency` Double (optional) - RTT in ms. Defaults to 0 which will disable
    latency throttling.
  * `downloadThroughput` Double (optional) - Download rate in Bps. Defaults to 0
    which will disable download throttling.
  * `uploadThroughput` Double (optional) - Upload rate in Bps. Defaults to 0
    which will disable upload throttling.

Emulates network with the given configuration for the `session`.

```javascript
// To emulate a GPRS connection with 50kbps throughput and 500 ms latency.
window.webContents.session.enableNetworkEmulation({
  latency: 500,
  downloadThroughput: 6400,
  uploadThroughput: 6400
})

// To emulate a network outage.
window.webContents.session.enableNetworkEmulation({ offline: true })
```

#### `ses.preconnect(options)`

* `options` Object
  * `url` string - URL for preconnect. Only the origin is relevant for opening the socket.
  * `numSockets` number (optional) - number of sockets to preconnect. Must be between 1 and 6. Defaults to 1.

Preconnects the given number of sockets to an origin.

#### `ses.closeAllConnections()`

Returns `Promise<void>` - Resolves when all connections are closed.

**Note:** It will terminate / fail all requests currently in flight.

#### `ses.fetch(input[, init])`

* `input` string | [GlobalRequest](https://nodejs.org/api/globals.html#request)
* `init` [RequestInit](https://developer.mozilla.org/en-US/docs/Web/API/fetch#options) (optional)

Returns `Promise<GlobalResponse>` - see [Response](https://developer.mozilla.org/en-US/docs/Web/API/Response).

Sends a request, similarly to how `fetch()` works in the renderer, using
Chrome's network stack. This differs from Node's `fetch()`, which uses
Node.js's HTTP stack.

Example:

```js
async function example () {
  const response = await net.fetch('https://my.app')
  if (response.ok) {
    const body = await response.json()
    // ... use the result.
  }
}
```

See also [`net.fetch()`](net.md#netfetchinput-init), a convenience method which
issues requests from the [default session](#sessiondefaultsession).

See the MDN documentation for
[`fetch()`](https://developer.mozilla.org/en-US/docs/Web/API/fetch) for more
details.

Limitations:

* `net.fetch()` does not support the `data:` or `blob:` schemes.
* The value of the `integrity` option is ignored.
* The `.type` and `.url` values of the returned `Response` object are
  incorrect.

By default, requests made with `net.fetch` can be made to [custom
protocols](protocol.md) as well as `file:`, and will trigger
[webRequest](web-request.md) handlers if present. When the non-standard
`bypassCustomProtocolHandlers` option is set in RequestInit, custom protocol
handlers will not be called for this request. This allows forwarding an
intercepted request to the built-in handler. [webRequest](web-request.md)
handlers will still be triggered when bypassing custom protocols.

```js
protocol.handle('https', (req) => {
  if (req.url === 'https://my-app.com') {
    return new Response('<body>my app</body>')
  } else {
    return net.fetch(req, { bypassCustomProtocolHandlers: true })
  }
})
```

#### `ses.disableNetworkEmulation()`

Disables any network emulation already active for the `session`. Resets to
the original network configuration.

#### `ses.setCertificateVerifyProc(proc)`

* `proc` Function | null
  * `request` Object
    * `hostname` string
    * `certificate` [Certificate](structures/certificate.md)
    * `validatedCertificate` [Certificate](structures/certificate.md)
    * `isIssuedByKnownRoot` boolean - `true` if Chromium recognises the root CA as a standard root. If it isn't then it's probably the case that this certificate was generated by a MITM proxy whose root has been installed locally (for example, by a corporate proxy). This should not be trusted if the `verificationResult` is not `OK`.
    * `verificationResult` string - `OK` if the certificate is trusted, otherwise an error like `CERT_REVOKED`.
    * `errorCode` Integer - Error code.
  * `callback` Function
    * `verificationResult` Integer - Value can be one of certificate error codes
    from [here](https://source.chromium.org/chromium/chromium/src/+/main:net/base/net_error_list.h).
    Apart from the certificate error codes, the following special codes can be used.
      * `0` - Indicates success and disables Certificate Transparency verification.
      * `-2` - Indicates failure.
      * `-3` - Uses the verification result from chromium.

Sets the certificate verify proc for `session`, the `proc` will be called with
`proc(request, callback)` whenever a server certificate
verification is requested. Calling `callback(0)` accepts the certificate,
calling `callback(-2)` rejects it.

Calling `setCertificateVerifyProc(null)` will revert back to default certificate
verify proc.

```javascript
const { BrowserWindow } = require('electron')
const win = new BrowserWindow()

win.webContents.session.setCertificateVerifyProc((request, callback) => {
  const { hostname } = request
  if (hostname === 'github.com') {
    callback(0)
  } else {
    callback(-2)
  }
})
```

> **NOTE:** The result of this procedure is cached by the network service.

#### `ses.setPermissionRequestHandler(handler)`

* `handler` Function | null
  * `webContents` [WebContents](web-contents.md) - WebContents requesting the permission.  Please note that if the request comes from a subframe you should use `requestingUrl` to check the request origin.
  * `permission` string - The type of requested permission.
    * `clipboard-read` - Request access to read from the clipboard.
    * `clipboard-sanitized-write` - Request access to write to the clipboard.
    * `media` -  Request access to media devices such as camera, microphone and speakers.
    * `display-capture` - Request access to capture the screen.
    * `mediaKeySystem` - Request access to DRM protected content.
    * `geolocation` - Request access to user's current location.
    * `notifications` - Request notification creation and the ability to display them in the user's system tray.
    * `midi` - Request MIDI access in the `webmidi` API.
    * `midiSysex` - Request the use of system exclusive messages in the `webmidi` API.
    * `pointerLock` - Request to directly interpret mouse movements as an input method. Click [here](https://developer.mozilla.org/en-US/docs/Web/API/Pointer_Lock_API) to know more. These requests always appear to originate from the main frame.
    * `fullscreen` - Request for the app to enter fullscreen mode.
    * `openExternal` - Request to open links in external applications.
    * `window-management` - Request access to enumerate screens using the [`getScreenDetails`](https://developer.chrome.com/en/articles/multi-screen-window-placement/) API.
    * `unknown` - An unrecognized permission request
  * `callback` Function
    * `permissionGranted` boolean - Allow or deny the permission.
  * `details` Object - Some properties are only available on certain permission types.
    * `externalURL` string (optional) - The url of the `openExternal` request.
    * `securityOrigin` string (optional) - The security origin of the `media` request.
    * `mediaTypes` string[] (optional) - The types of media access being requested, elements can be `video`
      or `audio`
    * `requestingUrl` string - The last URL the requesting frame loaded
    * `isMainFrame` boolean - Whether the frame making the request is the main frame

Sets the handler which can be used to respond to permission requests for the `session`.
Calling `callback(true)` will allow the permission and `callback(false)` will reject it.
To clear the handler, call `setPermissionRequestHandler(null)`.  Please note that
you must also implement `setPermissionCheckHandler` to get complete permission handling.
Most web APIs do a permission check and then make a permission request if the check is denied.

```javascript
const { session } = require('electron')
session.fromPartition('some-partition').setPermissionRequestHandler((webContents, permission, callback) => {
  if (webContents.getURL() === 'some-host' && permission === 'notifications') {
    return callback(false) // denied.
  }

  callback(true)
})
```

#### `ses.setPermissionCheckHandler(handler)`

* `handler` Function\<boolean> | null
  * `webContents` ([WebContents](web-contents.md) | null) - WebContents checking the permission.  Please note that if the request comes from a subframe you should use `requestingUrl` to check the request origin.  All cross origin sub frames making permission checks will pass a `null` webContents to this handler, while certain other permission checks such as `notifications` checks will always pass `null`.  You should use `embeddingOrigin` and `requestingOrigin` to determine what origin the owning frame and the requesting frame are on respectively.
  * `permission` string - Type of permission check.  Valid values are `midiSysex`, `notifications`, `geolocation`, `media`,`mediaKeySystem`,`midi`, `pointerLock`, `fullscreen`, `openExternal`, `hid`, `serial`, or `usb`.
  * `requestingOrigin` string - The origin URL of the permission check
  * `details` Object - Some properties are only available on certain permission types.
    * `embeddingOrigin` string (optional) - The origin of the frame embedding the frame that made the permission check.  Only set for cross-origin sub frames making permission checks.
    * `securityOrigin` string (optional) - The security origin of the `media` check.
    * `mediaType` string (optional) - The type of media access being requested, can be `video`,
      `audio` or `unknown`
    * `requestingUrl` string (optional) - The last URL the requesting frame loaded.  This is not provided for cross-origin sub frames making permission checks.
    * `isMainFrame` boolean - Whether the frame making the request is the main frame

Sets the handler which can be used to respond to permission checks for the `session`.
Returning `true` will allow the permission and `false` will reject it.  Please note that
you must also implement `setPermissionRequestHandler` to get complete permission handling.
Most web APIs do a permission check and then make a permission request if the check is denied.
To clear the handler, call `setPermissionCheckHandler(null)`.

```javascript
const { session } = require('electron')
const url = require('url')
session.fromPartition('some-partition').setPermissionCheckHandler((webContents, permission, requestingOrigin) => {
  if (new URL(requestingOrigin).hostname === 'some-host' && permission === 'notifications') {
    return true // granted
  }

  return false // denied
})
```

#### `ses.setDisplayMediaRequestHandler(handler)`

* `handler` Function | null
  * `request` Object
    * `frame` [WebFrameMain](web-frame-main.md) - Frame that is requesting access to media.
    * `securityOrigin` String - Origin of the page making the request.
    * `videoRequested` Boolean - true if the web content requested a video stream.
    * `audioRequested` Boolean - true if the web content requested an audio stream.
    * `userGesture` Boolean - Whether a user gesture was active when this request was triggered.
  * `callback` Function
    * `streams` Object
      * `video` Object | [WebFrameMain](web-frame-main.md) (optional)
        * `id` String - The id of the stream being granted. This will usually
          come from a [DesktopCapturerSource](structures/desktop-capturer-source.md)
          object.
        * `name` String - The name of the stream being granted. This will
          usually come from a [DesktopCapturerSource](structures/desktop-capturer-source.md)
          object.
      * `audio` String | [WebFrameMain](web-frame-main.md) (optional) - If
        a string is specified, can be `loopback` or `loopbackWithMute`.
        Specifying a loopback device will capture system audio, and is
        currently only supported on Windows. If a WebFrameMain is specified,
        will capture audio from that frame.
      * `enableLocalEcho` Boolean (optional) - If `audio` is a [WebFrameMain](web-frame-main.md)
         and this is set to `true`, then local playback of audio will not be muted (e.g. using `MediaRecorder`
         to record `WebFrameMain` with this flag set to `true` will allow audio to pass through to the speakers
         while recording). Default is `false`.

This handler will be called when web content requests access to display media
via the `navigator.mediaDevices.getDisplayMedia` API. Use the
[desktopCapturer](desktop-capturer.md) API to choose which stream(s) to grant
access to.

```javascript
const { session, desktopCapturer } = require('electron')

session.defaultSession.setDisplayMediaRequestHandler((request, callback) => {
  desktopCapturer.getSources({ types: ['screen'] }).then((sources) => {
    // Grant access to the first screen found.
    callback({ video: sources[0] })
  })
})
```

Passing a [WebFrameMain](web-frame-main.md) object as a video or audio stream
will capture the video or audio stream from that frame.

```javascript
const { session } = require('electron')

session.defaultSession.setDisplayMediaRequestHandler((request, callback) => {
  // Allow the tab to capture itself.
  callback({ video: request.frame })
})
```

Passing `null` instead of a function resets the handler to its default state.

#### `ses.setDevicePermissionHandler(handler)`

* `handler` Function\<boolean> | null
  * `details` Object
    * `deviceType` string - The type of device that permission is being requested on, can be `hid`, `serial`, or `usb`.
    * `origin` string - The origin URL of the device permission check.
    * `device` [HIDDevice](structures/hid-device.md) | [SerialPort](structures/serial-port.md) | [USBDevice](structures/usb-device.md) - the device that permission is being requested for.

Sets the handler which can be used to respond to device permission checks for the `session`.
Returning `true` will allow the device to be permitted and `false` will reject it.
To clear the handler, call `setDevicePermissionHandler(null)`.
This handler can be used to provide default permissioning to devices without first calling for permission
to devices (eg via `navigator.hid.requestDevice`).  If this handler is not defined, the default device
permissions as granted through device selection (eg via `navigator.hid.requestDevice`) will be used.
Additionally, the default behavior of Electron is to store granted device permision in memory.
If longer term storage is needed, a developer can store granted device
permissions (eg when handling the `select-hid-device` event) and then read from that storage with `setDevicePermissionHandler`.

```javascript
const { app, BrowserWindow } = require('electron')

let win = null

app.whenReady().then(() => {
  win = new BrowserWindow()

  win.webContents.session.setPermissionCheckHandler((webContents, permission, requestingOrigin, details) => {
    if (permission === 'hid') {
      // Add logic here to determine if permission should be given to allow HID selection
      return true
    } else if (permission === 'serial') {
      // Add logic here to determine if permission should be given to allow serial port selection
    } else if (permission === 'usb') {
      // Add logic here to determine if permission should be given to allow USB device selection
    }
    return false
  })

  // Optionally, retrieve previously persisted devices from a persistent store
  const grantedDevices = fetchGrantedDevices()

  win.webContents.session.setDevicePermissionHandler((details) => {
    if (new URL(details.origin).hostname === 'some-host' && details.deviceType === 'hid') {
      if (details.device.vendorId === 123 && details.device.productId === 345) {
        // Always allow this type of device (this allows skipping the call to `navigator.hid.requestDevice` first)
        return true
      }

      // Search through the list of devices that have previously been granted permission
      return grantedDevices.some((grantedDevice) => {
        return grantedDevice.vendorId === details.device.vendorId &&
              grantedDevice.productId === details.device.productId &&
              grantedDevice.serialNumber && grantedDevice.serialNumber === details.device.serialNumber
      })
    } else if (details.deviceType === 'serial') {
      if (details.device.vendorId === 123 && details.device.productId === 345) {
        // Always allow this type of device (this allows skipping the call to `navigator.hid.requestDevice` first)
        return true
      }
    }
    return false
  })

  win.webContents.session.on('select-hid-device', (event, details, callback) => {
    event.preventDefault()
    const selectedDevice = details.deviceList.find((device) => {
      return device.vendorId === '9025' && device.productId === '67'
    })
    callback(selectedPort?.deviceId)
  })
})
```

#### `ses.setBluetoothPairingHandler(handler)` _Windows_ _Linux_

* `handler` Function | null
  * `details` Object
    * `deviceId` string
    * `pairingKind` string - The type of pairing prompt being requested.
      One of the following values:
      * `confirm`
        This prompt is requesting confirmation that the Bluetooth device should
        be paired.
      * `confirmPin`
        This prompt is requesting confirmation that the provided PIN matches the
        pin displayed on the device.
      * `providePin`
        This prompt is requesting that a pin be provided for the device.
    * `frame` [WebFrameMain](web-frame-main.md)
    * `pin` string (optional) - The pin value to verify if `pairingKind` is `confirmPin`.
  * `callback` Function
    * `response` Object
      * `confirmed` boolean - `false` should be passed in if the dialog is canceled.
        If the `pairingKind` is `confirm` or `confirmPin`, this value should indicate
        if the pairing is confirmed.  If the `pairingKind` is `providePin` the value
        should be `true` when a value is provided.
      * `pin` string | null (optional) - When the `pairingKind` is `providePin`
        this value should be the required pin for the Bluetooth device.

Sets a handler to respond to Bluetooth pairing requests. This handler
allows developers to handle devices that require additional validation
before pairing.  When a handler is not defined, any pairing on Linux or Windows
that requires additional validation will be automatically cancelled.
macOS does not require a handler because macOS handles the pairing
automatically.  To clear the handler, call `setBluetoothPairingHandler(null)`.

```javascript

const { app, BrowserWindow, ipcMain, session } = require('electron')

let bluetoothPinCallback = null

function createWindow () {
  const mainWindow = new BrowserWindow({
    webPreferences: {
      preload: path.join(__dirname, 'preload.js')
    }
  })
}

// Listen for an IPC message from the renderer to get the response for the Bluetooth pairing.
ipcMain.on('bluetooth-pairing-response', (event, response) => {
  bluetoothPinCallback(response)
})

mainWindow.webContents.session.setBluetoothPairingHandler((details, callback) => {
  bluetoothPinCallback = callback
  // Send a IPC message to the renderer to prompt the user to confirm the pairing.
  // Note that this will require logic in the renderer to handle this message and
  // display a prompt to the user.
  mainWindow.webContents.send('bluetooth-pairing-request', details)
})

app.whenReady().then(() => {
  createWindow()
})
```

#### `ses.clearHostResolverCache()`

Returns `Promise<void>` - Resolves when the operation is complete.

Clears the host resolver cache.

#### `ses.allowNTLMCredentialsForDomains(domains)`

* `domains` string - A comma-separated list of servers for which
  integrated authentication is enabled.

Dynamically sets whether to always send credentials for HTTP NTLM or Negotiate
authentication.

```javascript
const { session } = require('electron')
// consider any url ending with `example.com`, `foobar.com`, `baz`
// for integrated authentication.
session.defaultSession.allowNTLMCredentialsForDomains('*example.com, *foobar.com, *baz')

// consider all urls for integrated authentication.
session.defaultSession.allowNTLMCredentialsForDomains('*')
```

#### `ses.setUserAgent(userAgent[, acceptLanguages])`

* `userAgent` string
* `acceptLanguages` string (optional)

Overrides the `userAgent` and `acceptLanguages` for this session.

The `acceptLanguages` must a comma separated ordered list of language codes, for
example `"en-US,fr,de,ko,zh-CN,ja"`.

This doesn't affect existing `WebContents`, and each `WebContents` can use
`webContents.setUserAgent` to override the session-wide user agent.

#### `ses.isPersistent()`

Returns `boolean` - Whether or not this session is a persistent one. The default
`webContents` session of a `BrowserWindow` is persistent. When creating a session
from a partition, session prefixed with `persist:` will be persistent, while others
will be temporary.

#### `ses.getUserAgent()`

Returns `string` - The user agent for this session.

#### `ses.setSSLConfig(config)`

* `config` Object
  * `minVersion` string (optional) - Can be `tls1`, `tls1.1`, `tls1.2` or `tls1.3`. The
    minimum SSL version to allow when connecting to remote servers. Defaults to
    `tls1`.
  * `maxVersion` string (optional) - Can be `tls1.2` or `tls1.3`. The maximum SSL version
    to allow when connecting to remote servers. Defaults to `tls1.3`.
  * `disabledCipherSuites` Integer[] (optional) - List of cipher suites which
    should be explicitly prevented from being used in addition to those
    disabled by the net built-in policy.
    Supported literal forms: 0xAABB, where AA is `cipher_suite[0]` and BB is
    `cipher_suite[1]`, as defined in RFC 2246, Section 7.4.1.2. Unrecognized but
    parsable cipher suites in this form will not return an error.
    Ex: To disable TLS_RSA_WITH_RC4_128_MD5, specify 0x0004, while to
    disable TLS_ECDH_ECDSA_WITH_RC4_128_SHA, specify 0xC002.
    Note that TLSv1.3 ciphers cannot be disabled using this mechanism.

Sets the SSL configuration for the session. All subsequent network requests
will use the new configuration. Existing network connections (such as WebSocket
connections) will not be terminated, but old sockets in the pool will not be
reused for new connections.

#### `ses.getBlobData(identifier)`

* `identifier` string - Valid UUID.

Returns `Promise<Buffer>` - resolves with blob data.

#### `ses.downloadURL(url)`

* `url` string

Initiates a download of the resource at `url`.
The API will generate a [DownloadItem](download-item.md) that can be accessed
with the [will-download](#event-will-download) event.

**Note:** This does not perform any security checks that relate to a page's origin,
unlike [`webContents.downloadURL`](web-contents.md#contentsdownloadurlurl).

#### `ses.createInterruptedDownload(options)`

* `options` Object
  * `path` string - Absolute path of the download.
  * `urlChain` string[] - Complete URL chain for the download.
  * `mimeType` string (optional)
  * `offset` Integer - Start range for the download.
  * `length` Integer - Total length of the download.
  * `lastModified` string (optional) - Last-Modified header value.
  * `eTag` string (optional) - ETag header value.
  * `startTime` Double (optional) - Time when download was started in
    number of seconds since UNIX epoch.

Allows resuming `cancelled` or `interrupted` downloads from previous `Session`.
The API will generate a [DownloadItem](download-item.md) that can be accessed with the [will-download](#event-will-download)
event. The [DownloadItem](download-item.md) will not have any `WebContents` associated with it and
the initial state will be `interrupted`. The download will start only when the
`resume` API is called on the [DownloadItem](download-item.md).

#### `ses.clearAuthCache()`

Returns `Promise<void>` - resolves when the session’s HTTP authentication cache has been cleared.

#### `ses.setPreloads(preloads)`

* `preloads` string[] - An array of absolute path to preload scripts

Adds scripts that will be executed on ALL web contents that are associated with
this session just before normal `preload` scripts run.

#### `ses.getPreloads()`

Returns `string[]` an array of paths to preload scripts that have been
registered.

#### `ses.setCodeCachePath(path)`

* `path` String - Absolute path to store the v8 generated JS code cache from the renderer.

Sets the directory to store the generated JS [code cache](https://v8.dev/blog/code-caching-for-devs) for this session. The directory is not required to be created by the user before this call, the runtime will create if it does not exist otherwise will use the existing directory. If directory cannot be created, then code cache will not be used and all operations related to code cache will fail silently inside the runtime. By default, the directory will be `Code Cache` under the
respective user data folder.

#### `ses.clearCodeCaches(options)`

* `options` Object
  * `urls` String[] (optional) - An array of url corresponding to the resource whose generated code cache needs to be removed. If the list is empty then all entries in the cache directory will be removed.

Returns `Promise<void>` - resolves when the code cache clear operation is complete.

#### `ses.setSpellCheckerEnabled(enable)`

* `enable` boolean

Sets whether to enable the builtin spell checker.

#### `ses.isSpellCheckerEnabled()`

Returns `boolean` - Whether the builtin spell checker is enabled.

#### `ses.setSpellCheckerLanguages(languages)`

* `languages` string[] - An array of language codes to enable the spellchecker for.

The built in spellchecker does not automatically detect what language a user is typing in.  In order for the
spell checker to correctly check their words you must call this API with an array of language codes.  You can
get the list of supported language codes with the `ses.availableSpellCheckerLanguages` property.

**Note:** On macOS the OS spellchecker is used and will detect your language automatically.  This API is a no-op on macOS.

#### `ses.getSpellCheckerLanguages()`

Returns `string[]` - An array of language codes the spellchecker is enabled for.  If this list is empty the spellchecker
will fallback to using `en-US`.  By default on launch if this setting is an empty list Electron will try to populate this
setting with the current OS locale.  This setting is persisted across restarts.

**Note:** On macOS the OS spellchecker is used and has its own list of languages. On macOS, this API will return whichever languages have been configured by the OS.

#### `ses.setSpellCheckerDictionaryDownloadURL(url)`

* `url` string - A base URL for Electron to download hunspell dictionaries from.

By default Electron will download hunspell dictionaries from the Chromium CDN.  If you want to override this
behavior you can use this API to point the dictionary downloader at your own hosted version of the hunspell
dictionaries.  We publish a `hunspell_dictionaries.zip` file with each release which contains the files you need
to host here.

The file server must be **case insensitive**. If you cannot do this, you must upload each file twice: once with
the case it has in the ZIP file and once with the filename as all lowercase.

If the files present in `hunspell_dictionaries.zip` are available at `https://example.com/dictionaries/language-code.bdic`
then you should call this api with `ses.setSpellCheckerDictionaryDownloadURL('https://example.com/dictionaries/')`.  Please
note the trailing slash.  The URL to the dictionaries is formed as `${url}${filename}`.

**Note:** On macOS the OS spellchecker is used and therefore we do not download any dictionary files.  This API is a no-op on macOS.

#### `ses.listWordsInSpellCheckerDictionary()`

Returns `Promise<string[]>` - An array of all words in app's custom dictionary.
Resolves when the full dictionary is loaded from disk.

#### `ses.addWordToSpellCheckerDictionary(word)`

* `word` string - The word you want to add to the dictionary

Returns `boolean` - Whether the word was successfully written to the custom dictionary. This API
will not work on non-persistent (in-memory) sessions.

**Note:** On macOS and Windows 10 this word will be written to the OS custom dictionary as well

#### `ses.removeWordFromSpellCheckerDictionary(word)`

* `word` string - The word you want to remove from the dictionary

Returns `boolean` - Whether the word was successfully removed from the custom dictionary. This API
will not work on non-persistent (in-memory) sessions.

**Note:** On macOS and Windows 10 this word will be removed from the OS custom dictionary as well

#### `ses.loadExtension(path[, options])`

* `path` string - Path to a directory containing an unpacked Chrome extension
* `options` Object (optional)
  * `allowFileAccess` boolean - Whether to allow the extension to read local files over `file://`
    protocol and inject content scripts into `file://` pages. This is required e.g. for loading
    devtools extensions on `file://` URLs. Defaults to false.

Returns `Promise<Extension>` - resolves when the extension is loaded.

This method will raise an exception if the extension could not be loaded. If
there are warnings when installing the extension (e.g. if the extension
requests an API that Electron does not support) then they will be logged to the
console.

Note that Electron does not support the full range of Chrome extensions APIs.
See [Supported Extensions APIs](extensions.md#supported-extensions-apis) for
more details on what is supported.

Note that in previous versions of Electron, extensions that were loaded would
be remembered for future runs of the application. This is no longer the case:
`loadExtension` must be called on every boot of your app if you want the
extension to be loaded.

```js
const { app, session } = require('electron')
const path = require('path')

app.on('ready', async () => {
  await session.defaultSession.loadExtension(
    path.join(__dirname, 'react-devtools'),
    // allowFileAccess is required to load the devtools extension on file:// URLs.
    { allowFileAccess: true }
  )
  // Note that in order to use the React DevTools extension, you'll need to
  // download and unzip a copy of the extension.
})
```

This API does not support loading packed (.crx) extensions.

**Note:** This API cannot be called before the `ready` event of the `app` module
is emitted.

**Note:** Loading extensions into in-memory (non-persistent) sessions is not
supported and will throw an error.

#### `ses.removeExtension(extensionId)`

* `extensionId` string - ID of extension to remove

Unloads an extension.

**Note:** This API cannot be called before the `ready` event of the `app` module
is emitted.

#### `ses.getExtension(extensionId)`

* `extensionId` string - ID of extension to query

Returns `Extension` | `null` - The loaded extension with the given ID.

**Note:** This API cannot be called before the `ready` event of the `app` module
is emitted.

#### `ses.getAllExtensions()`

Returns `Extension[]` - A list of all loaded extensions.

**Note:** This API cannot be called before the `ready` event of the `app` module
is emitted.

#### `ses.getStoragePath()`

Returns `string | null` - The absolute file system path where data for this
session is persisted on disk.  For in memory sessions this returns `null`.

### Instance Properties

The following properties are available on instances of `Session`:

#### `ses.availableSpellCheckerLanguages` _Readonly_

A `string[]` array which consists of all the known available spell checker languages.  Providing a language
code to the `setSpellCheckerLanguages` API that isn't in this array will result in an error.

#### `ses.spellCheckerEnabled`

A `boolean` indicating whether builtin spell checker is enabled.

#### `ses.storagePath` _Readonly_

A `string | null` indicating the absolute file system path where data for this
session is persisted on disk.  For in memory sessions this returns `null`.

#### `ses.cookies` _Readonly_

A [`Cookies`](cookies.md) object for this session.

#### `ses.serviceWorkers` _Readonly_

A [`ServiceWorkers`](service-workers.md) object for this session.

#### `ses.webRequest` _Readonly_

A [`WebRequest`](web-request.md) object for this session.

#### `ses.protocol` _Readonly_

A [`Protocol`](protocol.md) object for this session.

```javascript
const { app, session } = require('electron')
const path = require('path')

app.whenReady().then(() => {
  const protocol = session.fromPartition('some-partition').protocol
  if (!protocol.registerFileProtocol('atom', (request, callback) => {
    const url = request.url.substr(7)
    callback({ path: path.normalize(`${__dirname}/${url}`) })
  })) {
    console.error('Failed to register protocol')
  }
})
```

#### `ses.netLog` _Readonly_

A [`NetLog`](net-log.md) object for this session.

```javascript
const { app, session } = require('electron')

app.whenReady().then(async () => {
  const netLog = session.fromPartition('some-partition').netLog
  netLog.startLogging('/path/to/net-log')
  // After some network events
  const path = await netLog.stopLogging()
  console.log('Net-logs written to', path)
})
```
