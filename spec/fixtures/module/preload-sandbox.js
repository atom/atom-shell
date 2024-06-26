(function () {
  const { setImmediate } = require('node:timers');
  const { ipcRenderer } = require('electron');
  window.ipcRenderer = ipcRenderer;
  window.setImmediate = setImmediate;
  window.require = require;

  function invoke (code) {
    try {
      return code();
    } catch {
      return null;
    }
  }

  process.once('loaded', () => {
    ipcRenderer.send('process-loaded');
  });

  if (location.protocol === 'file:') {
    window.test = 'preload';
    window.process = process;
    if (process.env.sandboxmain) {
      window.test = {
        osSandbox: !process.argv.includes('--no-sandbox'),
        hasCrash: typeof process.crash === 'function',
        hasHang: typeof process.hang === 'function',
        creationTime: invoke(() => process.getCreationTime()),
        heapStatistics: invoke(() => process.getHeapStatistics()),
        blinkMemoryInfo: invoke(() => process.getBlinkMemoryInfo()),
        processMemoryInfo: invoke(() => process.getProcessMemoryInfo() ? {} : null),
        systemMemoryInfo: invoke(() => process.getSystemMemoryInfo()),
        systemVersion: invoke(() => process.getSystemVersion()),
        cpuUsage: invoke(() => process.getCPUUsage()),
        uptime: invoke(() => process.uptime()),
        // eslint-disable-next-line unicorn/prefer-node-protocol
        nodeEvents: invoke(() => require('events') === require('node:events')),
        // eslint-disable-next-line unicorn/prefer-node-protocol
        nodeTimers: invoke(() => require('timers') === require('node:timers')),
        // eslint-disable-next-line unicorn/prefer-node-protocol
        nodeUrl: invoke(() => require('url') === require('node:url')),
        env: process.env,
        execPath: process.execPath,
        pid: process.pid,
        arch: process.arch,
        platform: process.platform,
        sandboxed: process.sandboxed,
        contextIsolated: process.contextIsolated,
        type: process.type,
        version: process.version,
        versions: process.versions,
        contextId: process.contextId
      };
    }
  } else if (location.href !== 'about:blank') {
    addEventListener('DOMContentLoaded', () => {
      ipcRenderer.on('touch-the-opener', () => {
        let errorMessage = null;
        try {
          // eslint-disable-next-line @typescript-eslint/no-unused-vars
          const openerDoc = opener.document;
        } catch (error) {
          errorMessage = error.message;
        }
        ipcRenderer.send('answer', errorMessage);
      });
      ipcRenderer.send('child-loaded', window.opener == null, document.body.innerHTML, location.href);
    });
  }
})();
