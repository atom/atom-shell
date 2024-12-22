# DevTools Extension

Electron supports [Chrome DevTools extensions][devtools-extension], which can
be used to extend the ability of Chrome's developer tools for debugging
popular web frameworks.

## Loading a DevTools extension with tooling

The easiest way to load a DevTools extension is to use third-party tooling to automate the
process for you. [electron-devtools-installer][electron-devtools-installer] is a popular
NPM package that does just that.

## Manually loading a DevTools extension

If you don't want to use the tooling approach, you can also do all of the necessary
operations by hand. To load an extension in Electron, you need to download it via Chrome,
locate its filesystem path, and then load it into your [Session][session] by calling the
[`ses.loadExtension`][load-extension] API.

Using the [React Developer Tools][react-devtools] as an example:

1. Install the extension in Google Chrome.
1. Navigate to `chrome://extensions`, and find its extension ID, which is a hash
   string like `fmkadmapgofadopljbjfkapdkoienihi`.
1. Find out the filesystem location used by Chrome for storing extensions:
   * on Windows it is `%LOCALAPPDATA%\Google\Chrome\User Data\Default\Extensions`;
   * on Linux it could be:
     * `~/.config/google-chrome/Default/Extensions/`
     * `~/.config/google-chrome-beta/Default/Extensions/`
     * `~/.config/google-chrome-canary/Default/Extensions/`
     * `~/.config/chromium/Default/Extensions/`
   * on macOS it is `~/Library/Application Support/Google/Chrome/Default/Extensions`.
1. Pass the location of the extension to the [`ses.loadExtension`][load-extension]
   API. For React Developer Tools `v4.9.0`, it looks something like:

   ```js
   const { app, session } = require('electron')

   const os = require('node:os')
   const path = require('node:path')

   // on macOS
   const reactDevToolsPath = path.join(
     os.homedir(),
     '/Library/Application Support/Google/Chrome/Default/Extensions/fmkadmapgofadopljbjfkapdkoienihi/4.9.0_0'
   )

   app.whenReady().then(async () => {
     await session.defaultSession.loadExtension(reactDevToolsPath)
   })
   ```

**Notes:**

* `loadExtension` returns a Promise with an [Extension object][extension-structure],
which contains metadata about the extension that was loaded. This promise needs to
resolve (e.g. with an `await` expression) before loading a page. Otherwise, the
extension won't be guaranteed to load.
* `loadExtension` cannot be called before the `ready` event of the `app` module
is emitted, nor can it be called on in-memory (non-persistent) sessions.
* `loadExtension` must be called on every boot of your app if you want the
extension to be loaded.

### Removing a DevTools extension

You can pass the extension's ID to the [`ses.removeExtension`][remove-extension] API to
remove it from your Session. Loaded extensions are not persisted between
app launches.

## DevTools extension support

Electron only supports
[a limited set of `chrome.*` APIs][supported-extension-apis],
so extensions using unsupported `chrome.*` APIs under the hood may not work.

The following Devtools extensions have been tested to work in Electron:

* [Ember Inspector](https://chrome.google.com/webstore/detail/ember-inspector/bmdblncegkenkacieihfhpjfppoconhi)
* [React Developer Tools](https://chrome.google.com/webstore/detail/react-developer-tools/fmkadmapgofadopljbjfkapdkoienihi)
* [Backbone Debugger](https://chrome.google.com/webstore/detail/backbone-debugger/bhljhndlimiafopmmhjlgfpnnchjjbhd)
* [jQuery Debugger](https://chrome.google.com/webstore/detail/jquery-debugger/dbhhnnnpaeobfddmlalhnehgclcmjimi)
* [Vue.js devtools](https://chrome.google.com/webstore/detail/vuejs-devtools/nhdogjmejiglipccpnnnanhbledajbpd)
* [Cerebral Debugger](https://cerebraljs.com/docs/introduction/devtools.html)
* [Redux DevTools Extension](https://chrome.google.com/webstore/detail/redux-devtools/lmhkpmbekcpmknklioeibfkpmmfibljd)
* [MobX Developer Tools](https://chrome.google.com/webstore/detail/mobx-developer-tools/pfgnfdagidkfgccljigdamigbcnndkod)

### What should I do if a DevTools extension is not working?

First, please make sure the extension is still being maintained and is compatible
with the latest version of Google Chrome. We cannot provide additional support for
unsupported extensions.

If the extension works on Chrome but not on Electron, file a bug in Electron's
[issue tracker][issue-tracker] and describe which part
of the extension is not working as expected.

[devtools-extension]: https://developer.chrome.com/extensions/devtools
[session]: ../api/session.md
[react-devtools]: https://chrome.google.com/webstore/detail/react-developer-tools/fmkadmapgofadopljbjfkapdkoienihi
[load-extension]: ../api/session.md#sesloadextensionpath-options
[extension-structure]: ../api/structures/extension.md
[remove-extension]: ../api/session.md#sesremoveextensionextensionid
[electron-devtools-installer]: https://github.com/MarshallOfSound/electron-devtools-installer
[supported-extension-apis]: ../api/extensions.md
[issue-tracker]: https://github.com/electron/electron/issues
