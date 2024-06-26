/* global chrome */

chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
  sendResponse(request);
});

const map = {
  executeScript () {
    chrome.runtime.sendMessage({ method: 'executeScript' }, response => {
      console.log(JSON.stringify(response));
    });
  },
  registerContentScripts () {
    chrome.runtime.sendMessage({ method: 'registerContentScripts' }, response => {
      console.log(JSON.stringify(response));
    });
  },
  insertCSS () {
    chrome.runtime.sendMessage({ method: 'insertCSS' }, response => {
      console.log(JSON.stringify(response));
    });
  },
  globalParams () {
    chrome.runtime.sendMessage({ method: 'globalParams' }, response => {
      console.log(JSON.stringify(response));
    });
  }
};

const dispatchTest = (event) => {
  const { method, args = [] } = JSON.parse(event.data);
  map[method](...args);
};

window.addEventListener('message', dispatchTest, false);
