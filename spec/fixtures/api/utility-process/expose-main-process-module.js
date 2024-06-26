const { systemPreferences } = require('electron');

const status = systemPreferences.getMediaAccessStatus('screen');
process.parentPort.on('message', () => {
  process.parentPort.postMessage(status);
});
