
import { ipcRenderer, webFrame } from 'electron/renderer';
import { clipboard, crashReporter, shell } from 'electron/common';

// In renderer process (web page).
// https://github.com/electron/electron/blob/main/docs/api/ipc-renderer.md

(async () => {
  console.log(await ipcRenderer.invoke('ping-pong')); // prints "pong"
})();

console.log(ipcRenderer.sendSync('synchronous-message', 'ping')); // prints "pong"

ipcRenderer.on('test', () => {});
ipcRenderer.off('test', () => {});
ipcRenderer.once('test', () => {});
ipcRenderer.addListener('test', () => {});
ipcRenderer.removeListener('test', () => {});
ipcRenderer.removeAllListeners('test');

ipcRenderer.on('asynchronous-reply', (event, arg: any) => {
  console.log(arg); // prints "pong"
  event.sender.send('another-message', 'Hello World!');
});

ipcRenderer.send('asynchronous-message', 'ping');

// @ts-expect-error Removed API
ipcRenderer.sendTo(1, 'test', 'Hello World!');

// web-frame
// https://github.com/electron/electron/blob/main/docs/api/web-frame.md

webFrame.setZoomFactor(2);
console.log(webFrame.getZoomFactor());

webFrame.setZoomLevel(200);
console.log(webFrame.getZoomLevel());

webFrame.setVisualZoomLevelLimits(50, 200);

webFrame.setSpellCheckProvider('en-US', {
  spellCheck (words, callback) {
    setTimeout(() => {
      const spellchecker = require('spellchecker');
      const misspelled = words.filter(x => spellchecker.isMisspelled(x));
      callback(misspelled);
    }, 0);
  }
});

webFrame.insertText('text');

webFrame.executeJavaScript('return true;').then((v: boolean) => console.log(v));
webFrame.executeJavaScript('return true;', true).then((v: boolean) => console.log(v));
webFrame.executeJavaScript('return true;', true);
webFrame.executeJavaScript('return true;', true).then((result: boolean) => console.log(result));

console.log(webFrame.getResourceUsage());
webFrame.clearCache();

// clipboard
// https://github.com/electron/electron/blob/main/docs/api/clipboard.md

clipboard.writeText('Example String');
clipboard.writeText('Example String', 'selection');
console.log(clipboard.readText('selection'));
console.log(clipboard.availableFormats());
clipboard.clear();

clipboard.write({
  html: '<html></html>',
  text: 'Hello World!',
  bookmark: 'Bookmark name',
  image: clipboard.readImage()
});

// crash-reporter
// https://github.com/electron/electron/blob/main/docs/api/crash-reporter.md

crashReporter.start({
  productName: 'YourName',
  companyName: 'YourCompany',
  submitURL: 'https://your-domain.com/url-to-submit',
  uploadToServer: true
});

// desktopCapturer
// https://github.com/electron/electron/blob/main/docs/api/desktop-capturer.md

getSources({ types: ['window', 'screen'] }).then(sources => {
  for (const source of sources) {
    if (source.name === 'Electron') {
      (navigator as any).webkitGetUserMedia({
        audio: false,
        video: {
          mandatory: {
            chromeMediaSource: 'desktop',
            chromeMediaSourceId: source.id,
            minWidth: 1280,
            maxWidth: 1280,
            minHeight: 720,
            maxHeight: 720
          }
        }
      }, gotStream, getUserMediaError);
      return;
    }
  }
});

function getSources (options: Electron.SourcesOptions) {
  return ipcRenderer.invoke('get-sources', options) as Promise<Electron.DesktopCapturerSource[]>;
}

function gotStream (stream: any) {
  (document.querySelector('video') as HTMLVideoElement).src = URL.createObjectURL(stream);
}

function getUserMediaError (error: Error) {
  console.log('getUserMediaError', error);
}

// nativeImage
// https://github.com/electron/electron/blob/main/docs/api/native-image.md

const image = clipboard.readImage();
console.log(image.getSize());

// https://github.com/electron/electron/blob/main/docs/api/process.md

// preload.js
const _setImmediate = setImmediate;
const _clearImmediate = clearImmediate;
process.once('loaded', function () {
  global.setImmediate = _setImmediate;
  global.clearImmediate = _clearImmediate;
});

// shell
// https://github.com/electron/electron/blob/main/docs/api/shell.md

shell.openExternal('https://github.com').then(() => {});

// <webview>
// https://github.com/electron/electron/blob/main/docs/api/webview-tag.md

const webview = document.createElement('webview');
webview.loadURL('https://github.com');

webview.addEventListener('console-message', function (e) {
  console.log('Guest page logged a message:', e.message);
});

webview.addEventListener('found-in-page', function (e) {
  if (e.result.finalUpdate) {
    webview.stopFindInPage('keepSelection');
  }
});

const requestId = webview.findInPage('test');
console.log(requestId);

webview.addEventListener('close', function () {
  webview.src = 'about:blank';
});

// In embedder page.
webview.addEventListener('ipc-message', function (event) {
  console.log(event.channel); // Prints "pong"
});
webview.send('ping');
webview.capturePage().then(image => { console.log(image); });

const opened = webview.isDevToolsOpened();
console.log('isDevToolsOpened', opened);

const focused = webview.isDevToolsFocused();
console.log('isDevToolsFocused', focused);

// In guest page.
ipcRenderer.on('ping', function () {
  ipcRenderer.sendToHost('pong');
});
