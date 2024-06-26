import { expect } from 'chai';
import * as cp from 'node:child_process';
import { BrowserWindow } from 'electron';
import * as fs from 'node:fs';
import * as os from 'node:os';
import * as path from 'node:path';
import { pathToFileURL } from 'node:url';

const runFixture = async (appPath: string, args: string[] = []) => {
  const result = cp.spawn(process.execPath, [appPath, ...args], {
    stdio: 'pipe'
  });

  const stdout: Buffer[] = [];
  const stderr: Buffer[] = [];
  result.stdout.on('data', (chunk) => stdout.push(chunk));
  result.stderr.on('data', (chunk) => stderr.push(chunk));

  const [code, signal] = await new Promise<[number | null, NodeJS.Signals | null]>((resolve) => {
    result.on('close', (code, signal) => {
      resolve([code, signal]);
    });
  });

  return {
    code,
    signal,
    stdout: Buffer.concat(stdout).toString().trim(),
    stderr: Buffer.concat(stderr).toString().trim()
  };
};

const fixturePath = path.resolve(__dirname, 'fixtures', 'esm');

describe('esm', () => {
  describe('main process', () => {
    it('should load an esm entrypoint', async () => {
      const result = await runFixture(path.resolve(fixturePath, 'entrypoint.mjs'));
      expect(result.code).to.equal(0);
      expect(result.stdout).to.equal('ESM Launch, ready: false');
    });

    it('should load an esm entrypoint based on type=module', async () => {
      const result = await runFixture(path.resolve(fixturePath, 'package'));
      expect(result.code).to.equal(0);
      expect(result.stdout).to.equal('ESM Package Launch, ready: false');
    });

    it('should wait for a top-level await before declaring the app ready', async () => {
      const result = await runFixture(path.resolve(fixturePath, 'top-level-await.mjs'));
      expect(result.code).to.equal(0);
      expect(result.stdout).to.equal('Top level await, ready: false');
    });

    it('should allow usage of pre-app-ready apis in top-level await', async () => {
      const result = await runFixture(path.resolve(fixturePath, 'pre-app-ready-apis.mjs'));
      expect(result.code).to.equal(0);
    });

    it('should allow use of dynamic import', async () => {
      const result = await runFixture(path.resolve(fixturePath, 'dynamic.mjs'));
      expect(result.code).to.equal(0);
      expect(result.stdout).to.equal('Exit with app, ready: false');
    });
  });

  describe('renderer process', () => {
    let w: BrowserWindow | null = null;
    const tempDirs: string[] = [];

    afterEach(async () => {
      if (w) w.close();
      w = null;
      while (tempDirs.length) {
        await fs.promises.rm(tempDirs.pop()!, { force: true, recursive: true });
      }
    });

    async function loadWindowWithPreload (preload: string, webPreferences: Electron.WebPreferences) {
      const tmpDir = await fs.promises.mkdtemp(path.resolve(os.tmpdir(), 'e-spec-preload-'));
      tempDirs.push(tmpDir);
      const preloadPath = path.resolve(tmpDir, 'preload.mjs');
      await fs.promises.writeFile(preloadPath, preload);

      w = new BrowserWindow({
        show: false,
        webPreferences: {
          ...webPreferences,
          preload: preloadPath
        }
      });

      let error: Error | null = null;
      w.webContents.on('preload-error', (_, __, err) => {
        error = err;
      });

      await w.loadFile(path.resolve(fixturePath, 'empty.html'));

      return [w.webContents, error] as [Electron.WebContents, Error | null];
    }

    describe('nodeIntegration', () => {
      it('should support an esm entrypoint', async () => {
        const [webContents] = await loadWindowWithPreload('import { resolve } from "path"; window.resolvePath = resolve;', {
          nodeIntegration: true,
          sandbox: false,
          contextIsolation: false
        });

        const exposedType = await webContents.executeJavaScript('typeof window.resolvePath');
        expect(exposedType).to.equal('function');
      });

      it('should delay load until the ESM import chain is complete', async () => {
        const [webContents] = await loadWindowWithPreload(`import { resolve } from "path";
        await new Promise(r => setTimeout(r, 500));
        window.resolvePath = resolve;`, {
          nodeIntegration: true,
          sandbox: false,
          contextIsolation: false
        });

        const exposedType = await webContents.executeJavaScript('typeof window.resolvePath');
        expect(exposedType).to.equal('function');
      });

      it('should support a top-level await fetch blocking the page load', async () => {
        const [webContents] = await loadWindowWithPreload(`
        const r = await fetch("package/package.json");
        window.packageJson = await r.json();`, {
          nodeIntegration: true,
          sandbox: false,
          contextIsolation: false
        });

        const packageJson = await webContents.executeJavaScript('window.packageJson');
        expect(packageJson).to.deep.equal(require('./fixtures/esm/package/package.json'));
      });

      const hostsUrl = pathToFileURL(process.platform === 'win32' ? 'C:\\Windows\\System32\\drivers\\etc\\hosts' : '/etc/hosts');

      describe('without context isolation', () => {
        it('should use Blinks dynamic loader in the main world', async () => {
          const [webContents] = await loadWindowWithPreload('', {
            nodeIntegration: true,
            sandbox: false,
            contextIsolation: false
          });

          let error: Error | null = null;
          try {
            await webContents.executeJavaScript(`import(${JSON.stringify(hostsUrl)})`);
          } catch (err) {
            error = err as Error;
          }

          expect(error).to.not.equal(null);
          // This is a blink specific error message
          expect(error?.message).to.include('Failed to fetch dynamically imported module');
        });

        it('should use import.meta callback handling from Node.js for Node.js modules', async () => {
          const result = await runFixture(path.resolve(fixturePath, 'import-meta'));
          expect(result.code).to.equal(0);
        });
      });

      describe('with context isolation', () => {
        let badFilePath = '';

        beforeEach(async () => {
          badFilePath = path.resolve(path.resolve(os.tmpdir(), 'bad-file.badjs'));
          await fs.promises.writeFile(badFilePath, 'const foo = "bar";');
        });

        afterEach(async () => {
          await fs.promises.unlink(badFilePath);
        });

        it('should use Node.js ESM dynamic loader in the isolated context', async () => {
          const [, preloadError] = await loadWindowWithPreload(`await import(${JSON.stringify((pathToFileURL(badFilePath)))})`, {
            nodeIntegration: true,
            sandbox: false,
            contextIsolation: true
          });

          expect(preloadError).to.not.equal(null);
          // This is a node.js specific error message
          expect(preloadError!.toString()).to.include('Unknown file extension');
        });

        it('should use Blinks dynamic loader in the main world', async () => {
          const [webContents] = await loadWindowWithPreload('', {
            nodeIntegration: true,
            sandbox: false,
            contextIsolation: true
          });

          let error: Error | null = null;
          try {
            await webContents.executeJavaScript(`import(${JSON.stringify(hostsUrl)})`);
          } catch (err) {
            error = err as Error;
          }

          expect(error).to.not.equal(null);
          // This is a blink specific error message
          expect(error?.message).to.include('Failed to fetch dynamically imported module');
        });
      });
    });
  });
});
