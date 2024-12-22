# MessageChannelMain

`MessageChannelMain` is the main-process-side equivalent of the DOM
[`MessageChannel`][] object. Its singular function is to create a pair of
connected [`MessagePortMain`](message-port-main.md) objects.

See the [Channel Messaging API][] documentation for more information on using
channel messaging.

## Class: MessageChannelMain

> Channel interface for channel messaging in the main process.

Process: [Main](../glossary.md#main-process)

Example:

<!-- eslint-disable import/order -->

```js
// Main process
const { BrowserWindow, MessageChannelMain } = require('electron')

const w = new BrowserWindow()
const { port1, port2 } = new MessageChannelMain()
w.webContents.postMessage('port', null, [port2])
port1.postMessage({ some: 'message' })

// Renderer process
const { ipcRenderer } = require('electron')

ipcRenderer.on('port', (e) => {
  // e.ports is a list of ports sent along with this message
  e.ports[0].onmessage = (messageEvent) => {
    console.log(messageEvent.data)
  }
})
```

### Instance Properties

#### `channel.port1`

A [`MessagePortMain`](message-port-main.md) property.

#### `channel.port2`

A [`MessagePortMain`](message-port-main.md) property.

[`MessageChannel`]: https://developer.mozilla.org/en-US/docs/Web/API/MessageChannel
[Channel Messaging API]: https://developer.mozilla.org/en-US/docs/Web/API/Channel_Messaging_API
