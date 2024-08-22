# protocol

> Register a custom protocol and intercept existing protocol requests.

Process: [Main](../glossary.md#main-process)

An example of implementing a protocol that has the same effect as the
`file://` protocol:

```js
const { app, protocol, net } = require('electron')
const path = require('node:path')
const url = require('node:url')

app.whenReady().then(() => {
  protocol.handle('atom', (request) => {
    const filePath = request.url.slice('atom://'.length)
    return net.fetch(url.pathToFileURL(path.join(__dirname, filePath)).toString())
  })
})
```

**Note:** All methods unless specified can only be used after the `ready` event
of the `app` module gets emitted.

## Using `protocol` with a custom `partition` or `session`

A protocol is registered to a specific Electron [`session`](./session.md)
object. If you don't specify a session, then your `protocol` will be applied to
the default session that Electron uses. However, if you define a `partition` or
`session` on your `browserWindow`'s `webPreferences`, then that window will use
a different session and your custom protocol will not work if you just use
`electron.protocol.XXX`.

To have your custom protocol work in combination with a custom session, you need
to register it to that session explicitly.

```js
const { app, BrowserWindow, net, protocol, session } = require('electron')
const path = require('node:path')
const url = require('url')

app.whenReady().then(() => {
  const partition = 'persist:example'
  const ses = session.fromPartition(partition)

  ses.protocol.handle('atom', (request) => {
    const filePath = request.url.slice('atom://'.length)
    return net.fetch(url.pathToFileURL(path.resolve(__dirname, filePath)).toString())
  })

  const mainWindow = new BrowserWindow({ webPreferences: { partition } })
})
```

## Methods

The `protocol` module has the following methods:

### `protocol.registerSchemesAsPrivileged(customSchemes)`

* `customSchemes` [CustomScheme[]](structures/custom-scheme.md)

**Note:** This method can only be used before the `ready` event of the `app`
module gets emitted and can be called only once.

Registers the `scheme` as standard, secure, bypasses content security policy for
resources, allows registering ServiceWorker, supports fetch API, streaming
video/audio, and V8 code cache. Specify a privilege with the value of `true` to
enable the capability.

An example of registering a privileged scheme, that bypasses Content Security
Policy:

```js
const { protocol } = require('electron')
protocol.registerSchemesAsPrivileged([
  { scheme: 'foo', privileges: { bypassCSP: true } }
])
```

A standard scheme adheres to what RFC 3986 calls [generic URI syntax](https://tools.ietf.org/html/rfc3986#section-3).
For example `http` and `https` are standard schemes, while `file` is not.

Registering a scheme as standard allows relative and absolute resources to
be resolved correctly when served. Otherwise the scheme will behave like the
`file` protocol, but without the ability to resolve relative URLs.

For example when you load following page with custom protocol without
registering it as standard scheme, the image will not be loaded because
non-standard schemes can not recognize relative URLs:

```html
<body>
  <img src='test.png'>
</body>
```

Registering a scheme as standard will allow access to files through the
[FileSystem API][file-system-api]. Otherwise the renderer will throw a security
error for the scheme.

By default web storage apis (localStorage, sessionStorage, webSQL, indexedDB,
cookies) are disabled for non standard schemes. So in general if you want to
register a custom protocol to replace the `http` protocol, you have to register
it as a standard scheme.

Protocols that use streams (http and stream protocols) should set `stream: true`.
The `<video>` and `<audio>` HTML elements expect protocols to buffer their
responses by default. The `stream` flag configures those elements to correctly
expect streaming responses.

### `protocol.handle(scheme, handler)`

* `scheme` string - scheme to handle, for example `https` or `my-app`. This is
  the bit before the `:` in a URL.
* `handler` Function\<[GlobalResponse](https://nodejs.org/api/globals.html#response) | Promise\<GlobalResponse\>\>
  * `request` [GlobalRequest](https://nodejs.org/api/globals.html#request)

Register a protocol handler for `scheme`. Requests made to URLs with this
scheme will delegate to this handler to determine what response should be sent.

Either a `Response` or a `Promise<Response>` can be returned.

Example:

```js
const { app, net, protocol } = require('electron')
const path = require('node:path')
const { pathToFileURL } = require('url')

protocol.registerSchemesAsPrivileged([
  {
    scheme: 'app',
    privileges: {
      standard: true,
      secure: true,
      supportFetchAPI: true
    }
  }
])

app.whenReady().then(() => {
  protocol.handle('app', (req) => {
    const { host, pathname } = new URL(req.url)
    if (host === 'bundle') {
      if (pathname === '/') {
        return new Response('<h1>hello, world</h1>', {
          headers: { 'content-type': 'text/html' }
        })
      }
      // NB, this checks for paths that escape the bundle, e.g.
      // app://bundle/../../secret_file.txt
      const pathToServe = path.resolve(__dirname, pathname)
      const relativePath = path.relative(__dirname, pathToServe)
      const isSafe = relativePath && !relativePath.startsWith('..') && !path.isAbsolute(relativePath)
      if (!isSafe) {
        return new Response('bad', {
          status: 400,
          headers: { 'content-type': 'text/html' }
        })
      }

      return net.fetch(pathToFileURL(pathToServe).toString())
    } else if (host === 'api') {
      return net.fetch('https://api.my-server.com/' + pathname, {
        method: req.method,
        headers: req.headers,
        body: req.body
      })
    }
  })
})
```

See the MDN docs for [`Request`](https://developer.mozilla.org/en-US/docs/Web/API/Request) and [`Response`](https://developer.mozilla.org/en-US/docs/Web/API/Response) for more details.

### `protocol.unhandle(scheme)`

* `scheme` string - scheme for which to remove the handler.

Removes a protocol handler registered with `protocol.handle`.

### `protocol.isProtocolHandled(scheme)`

* `scheme` string

Returns `boolean` - Whether `scheme` is already handled.

### `protocol.registerFileProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` (string | [ProtocolResponse](structures/protocol-response.md))

Returns `boolean` - Whether the protocol was successfully registered

Registers a protocol of `scheme` that will send a file as the response. The
`handler` will be called with `request` and `callback` where `request` is
an incoming request for the `scheme`.

To handle the `request`, the `callback` should be called with either the file's
path or an object that has a `path` property, e.g. `callback(filePath)` or
`callback({ path: filePath })`. The `filePath` must be an absolute path.

By default the `scheme` is treated like `http:`, which is parsed differently
from protocols that follow the "generic URI syntax" like `file:`.

### `protocol.registerBufferProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` (Buffer | [ProtocolResponse](structures/protocol-response.md))

Returns `boolean` - Whether the protocol was successfully registered

Registers a protocol of `scheme` that will send a `Buffer` as a response.

The usage is the same with `registerFileProtocol`, except that the `callback`
should be called with either a `Buffer` object or an object that has the `data`
property.

Example:

```js
protocol.registerBufferProtocol('atom', (request, callback) => {
  callback({ mimeType: 'text/html', data: Buffer.from('<h5>Response</h5>') })
})
```

### `protocol.registerStringProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` (string | [ProtocolResponse](structures/protocol-response.md))

Returns `boolean` - Whether the protocol was successfully registered

Registers a protocol of `scheme` that will send a `string` as a response.

The usage is the same with `registerFileProtocol`, except that the `callback`
should be called with either a `string` or an object that has the `data`
property.

### `protocol.registerHttpProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` [ProtocolResponse](structures/protocol-response.md)

Returns `boolean` - Whether the protocol was successfully registered

Registers a protocol of `scheme` that will send an HTTP request as a response.

The usage is the same with `registerFileProtocol`, except that the `callback`
should be called with an object that has the `url` property.

### `protocol.registerStreamProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` (ReadableStream | [ProtocolResponse](structures/protocol-response.md))

Returns `boolean` - Whether the protocol was successfully registered

Registers a protocol of `scheme` that will send a stream as a response.

The usage is the same with `registerFileProtocol`, except that the
`callback` should be called with either a [`ReadableStream`](https://nodejs.org/api/stream.html#stream_class_stream_readable) object or an object that
has the `data` property.

Example:

```js
const { protocol } = require('electron')
const { PassThrough } = require('stream')

function createStream (text) {
  const rv = new PassThrough() // PassThrough is also a Readable stream
  rv.push(text)
  rv.push(null)
  return rv
}

protocol.registerStreamProtocol('atom', (request, callback) => {
  callback({
    statusCode: 200,
    headers: {
      'content-type': 'text/html'
    },
    data: createStream('<h5>Response</h5>')
  })
})
```

It is possible to pass any object that implements the readable stream API (emits
`data`/`end`/`error` events). For example, here's how a file could be returned:

```js
protocol.registerStreamProtocol('atom', (request, callback) => {
  callback(fs.createReadStream('index.html'))
})
```

### `protocol.unregisterProtocol(scheme)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string

Returns `boolean` - Whether the protocol was successfully unregistered

Unregisters the custom protocol of `scheme`.

### `protocol.isProtocolRegistered(scheme)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string

Returns `boolean` - Whether `scheme` is already registered.

### `protocol.interceptFileProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` (string | [ProtocolResponse](structures/protocol-response.md))

Returns `boolean` - Whether the protocol was successfully intercepted

Intercepts `scheme` protocol and uses `handler` as the protocol's new handler
which sends a file as a response.

### `protocol.interceptStringProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` (string | [ProtocolResponse](structures/protocol-response.md))

Returns `boolean` - Whether the protocol was successfully intercepted

Intercepts `scheme` protocol and uses `handler` as the protocol's new handler
which sends a `string` as a response.

### `protocol.interceptBufferProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` (Buffer | [ProtocolResponse](structures/protocol-response.md))

Returns `boolean` - Whether the protocol was successfully intercepted

Intercepts `scheme` protocol and uses `handler` as the protocol's new handler
which sends a `Buffer` as a response.

### `protocol.interceptHttpProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` [ProtocolResponse](structures/protocol-response.md)

Returns `boolean` - Whether the protocol was successfully intercepted

Intercepts `scheme` protocol and uses `handler` as the protocol's new handler
which sends a new HTTP request as a response.

### `protocol.interceptStreamProtocol(scheme, handler)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string
* `handler` Function
  * `request` [ProtocolRequest](structures/protocol-request.md)
  * `callback` Function
    * `response` (ReadableStream | [ProtocolResponse](structures/protocol-response.md))

Returns `boolean` - Whether the protocol was successfully intercepted

Same as `protocol.registerStreamProtocol`, except that it replaces an existing
protocol handler.

### `protocol.uninterceptProtocol(scheme)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string

Returns `boolean` - Whether the protocol was successfully unintercepted

Remove the interceptor installed for `scheme` and restore its original handler.

### `protocol.isProtocolIntercepted(scheme)` _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/36674
    description: "`protocol.register*Protocol` and `protocol.intercept*Protocol` methods have been replaced with `protocol.handle`"
    breaking-changes-header: deprecated-protocolunregisterinterceptbufferstringstreamfilehttpprotocol-and-protocolisprotocolregisteredintercepted
```
-->

* `scheme` string

Returns `boolean` - Whether `scheme` is already intercepted.

[file-system-api]: https://developer.mozilla.org/en-US/docs/Web/API/LocalFileSystem
