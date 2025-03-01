/* global chrome */

chrome.webRequest.onBeforeRequest.addListener(
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  (details) => {
    console.log(details);
  },
  { urls: ['<all_urls>'] }
);
