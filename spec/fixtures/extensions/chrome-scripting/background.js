/* global chrome */

const handleRequest = async (request, sender, sendResponse) => {
  const { method } = request;
  const tabId = sender.tab.id;

  switch (method) {
    case 'executeScript': {
      chrome.scripting.executeScript({
        target: { tabId },
        function: () => {
          document.title = 'HEY HEY HEY';
          return document.title;
        }
      }).then(() => {
        console.log('success');
      }).catch((err) => {
        console.log('error', err);
      });
      break;
    }

    case 'globalParams' : {
      await chrome.scripting.executeScript({
        target: { tabId },
        func: () => {
          chrome.scripting.globalParams.changed = true;
        },
        world: 'ISOLATED'
      });

      const results = await chrome.scripting.executeScript({
        target: { tabId },
        func: () => JSON.stringify(chrome.scripting.globalParams),
        world: 'ISOLATED'
      });

      const result = JSON.parse(results[0].result);

      sendResponse(result);
      break;
    }

    case 'registerContentScripts': {
      await chrome.scripting.registerContentScripts([{
        id: 'session-script',
        js: ['content.js'],
        persistAcrossSessions: false,
        matches: ['<all_urls>'],
        runAt: 'document_start'
      }]);

      chrome.scripting.getRegisteredContentScripts().then(sendResponse);
      break;
    }

    case 'insertCSS': {
      chrome.scripting.insertCSS({
        target: { tabId },
        css: 'body { background-color: red; }'
      }).then(() => {
        sendResponse({ success: true });
      });
      break;
    }
  }
};

chrome.runtime.onMessage.addListener((request, sender, sendResponse) => {
  handleRequest(request, sender, sendResponse);
  return true;
});
