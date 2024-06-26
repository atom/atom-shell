import * as fs from 'node:fs';
import * as path from 'node:path';
import { expect } from 'chai';
import { BrowserWindow } from 'electron';
import { defer } from './lib/spec-helpers';
import { app } from 'electron/main';
import { closeAllWindows } from './lib/window-helpers';

describe('process module', () => {
  describe('renderer process', () => {
    let w: BrowserWindow;
    before(async () => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      await w.loadURL('about:blank');
    });
    after(closeAllWindows);

    describe('process.getCreationTime()', () => {
      it('returns a creation time', async () => {
        const creationTime = await w.webContents.executeJavaScript('process.getCreationTime()');
        expect(creationTime).to.be.a('number').and.be.at.least(0);
      });
    });

    describe('process.getCPUUsage()', () => {
      it('returns a cpu usage object', async () => {
        const cpuUsage = await w.webContents.executeJavaScript('process.getCPUUsage()');
        expect(cpuUsage.percentCPUUsage).to.be.a('number');
        expect(cpuUsage.cumulativeCPUUsage).to.be.a('number');
        expect(cpuUsage.idleWakeupsPerSecond).to.be.a('number');
      });
    });

    describe('process.getBlinkMemoryInfo()', () => {
      it('returns blink memory information object', async () => {
        const heapStats = await w.webContents.executeJavaScript('process.getBlinkMemoryInfo()');
        expect(heapStats.allocated).to.be.a('number');
        expect(heapStats.total).to.be.a('number');
      });
    });

    describe('process.getProcessMemoryInfo()', () => {
      it('resolves promise successfully with valid data', async () => {
        const memoryInfo = await w.webContents.executeJavaScript('process.getProcessMemoryInfo()');
        expect(memoryInfo).to.be.an('object');
        if (process.platform === 'linux' || process.platform === 'win32') {
          expect(memoryInfo.residentSet).to.be.a('number').greaterThan(0);
        }
        expect(memoryInfo.private).to.be.a('number').greaterThan(0);
        // Shared bytes can be zero
        expect(memoryInfo.shared).to.be.a('number').greaterThan(-1);
      });
    });

    describe('process.getSystemMemoryInfo()', () => {
      it('returns system memory info object', async () => {
        const systemMemoryInfo = await w.webContents.executeJavaScript('process.getSystemMemoryInfo()');
        expect(systemMemoryInfo.free).to.be.a('number');
        expect(systemMemoryInfo.total).to.be.a('number');
      });
    });

    describe('process.getSystemVersion()', () => {
      it('returns a string', async () => {
        const systemVersion = await w.webContents.executeJavaScript('process.getSystemVersion()');
        expect(systemVersion).to.be.a('string');
      });
    });

    describe('process.getHeapStatistics()', () => {
      it('returns heap statistics object', async () => {
        const heapStats = await w.webContents.executeJavaScript('process.getHeapStatistics()');
        expect(heapStats.totalHeapSize).to.be.a('number');
        expect(heapStats.totalHeapSizeExecutable).to.be.a('number');
        expect(heapStats.totalPhysicalSize).to.be.a('number');
        expect(heapStats.totalAvailableSize).to.be.a('number');
        expect(heapStats.usedHeapSize).to.be.a('number');
        expect(heapStats.heapSizeLimit).to.be.a('number');
        expect(heapStats.mallocedMemory).to.be.a('number');
        expect(heapStats.peakMallocedMemory).to.be.a('number');
        expect(heapStats.doesZapGarbage).to.be.a('boolean');
      });
    });

    describe('process.takeHeapSnapshot()', () => {
      it('returns true on success', async () => {
        const filePath = path.join(app.getPath('temp'), 'test.heapsnapshot');
        defer(() => {
          try {
            fs.unlinkSync(filePath);
          } catch {
            // ignore error
          }
        });

        const success = await w.webContents.executeJavaScript(`process.takeHeapSnapshot(${JSON.stringify(filePath)})`);
        expect(success).to.be.true();
        const stats = fs.statSync(filePath);
        expect(stats.size).not.to.be.equal(0);
      });

      it('returns false on failure', async () => {
        const success = await w.webContents.executeJavaScript('process.takeHeapSnapshot("")');
        expect(success).to.be.false();
      });
    });

    describe('process.contextId', () => {
      it('is a string', async () => {
        const contextId = await w.webContents.executeJavaScript('process.contextId');
        expect(contextId).to.be.a('string');
      });
    });
  });

  describe('main process', () => {
    describe('process.getCreationTime()', () => {
      it('returns a creation time', () => {
        const creationTime = process.getCreationTime();
        expect(creationTime).to.be.a('number').and.be.at.least(0);
      });
    });

    describe('process.getCPUUsage()', () => {
      it('returns a cpu usage object', () => {
        const cpuUsage = process.getCPUUsage();
        expect(cpuUsage.percentCPUUsage).to.be.a('number');
        expect(cpuUsage.cumulativeCPUUsage).to.be.a('number');
        expect(cpuUsage.idleWakeupsPerSecond).to.be.a('number');
      });
    });

    describe('process.getBlinkMemoryInfo()', () => {
      it('returns blink memory information object', () => {
        const heapStats = process.getBlinkMemoryInfo();
        expect(heapStats.allocated).to.be.a('number');
        expect(heapStats.total).to.be.a('number');
      });
    });

    describe('process.getProcessMemoryInfo()', () => {
      it('resolves promise successfully with valid data', async () => {
        const memoryInfo = await process.getProcessMemoryInfo();
        expect(memoryInfo).to.be.an('object');
        if (process.platform === 'linux' || process.platform === 'win32') {
          expect(memoryInfo.residentSet).to.be.a('number').greaterThan(0);
        }
        expect(memoryInfo.private).to.be.a('number').greaterThan(0);
        // Shared bytes can be zero
        expect(memoryInfo.shared).to.be.a('number').greaterThan(-1);
      });
    });

    describe('process.getSystemMemoryInfo()', () => {
      it('returns system memory info object', () => {
        const systemMemoryInfo = process.getSystemMemoryInfo();
        expect(systemMemoryInfo.free).to.be.a('number');
        expect(systemMemoryInfo.total).to.be.a('number');
      });
    });

    describe('process.getSystemVersion()', () => {
      it('returns a string', () => {
        const systemVersion = process.getSystemVersion();
        expect(systemVersion).to.be.a('string');
      });
    });

    describe('process.getHeapStatistics()', () => {
      it('returns heap statistics object', async () => {
        const heapStats = process.getHeapStatistics();
        expect(heapStats.totalHeapSize).to.be.a('number');
        expect(heapStats.totalHeapSizeExecutable).to.be.a('number');
        expect(heapStats.totalPhysicalSize).to.be.a('number');
        expect(heapStats.totalAvailableSize).to.be.a('number');
        expect(heapStats.usedHeapSize).to.be.a('number');
        expect(heapStats.heapSizeLimit).to.be.a('number');
        expect(heapStats.mallocedMemory).to.be.a('number');
        expect(heapStats.peakMallocedMemory).to.be.a('number');
        expect(heapStats.doesZapGarbage).to.be.a('boolean');
      });
    });

    describe('process.takeHeapSnapshot()', () => {
      // DISABLED-FIXME(nornagon): this seems to take a really long time when run in the
      // main process, for unknown reasons.
      it('returns true on success', () => {
        const filePath = path.join(app.getPath('temp'), 'test.heapsnapshot');
        defer(() => {
          try {
            fs.unlinkSync(filePath);
          } catch {
            // ignore error
          }
        });

        const success = process.takeHeapSnapshot(filePath);
        expect(success).to.be.true();
        const stats = fs.statSync(filePath);
        expect(stats.size).not.to.be.equal(0);
      });

      it('returns false on failure', async () => {
        const success = process.takeHeapSnapshot('');
        expect(success).to.be.false();
      });
    });
  });
});
