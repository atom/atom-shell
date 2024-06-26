import { expect } from 'chai';
import * as path from 'node:path';
import * as url from 'node:url';
import { Worker } from 'node:worker_threads';
import { BrowserWindow, ipcMain } from 'electron/main';
import { closeAllWindows } from './lib/window-helpers';
import { getRemoteContext, ifdescribe, ifit, itremote, useRemoteContext } from './lib/spec-helpers';
import * as importedFs from 'node:fs';
import { once } from 'node:events';

describe('asar package', () => {
  const fixtures = path.join(__dirname, 'fixtures');
  const asarDir = path.join(fixtures, 'test.asar');

  afterEach(closeAllWindows);

  describe('asar protocol', () => {
    it('sets __dirname correctly', async function () {
      after(function () {
        ipcMain.removeAllListeners('dirname');
      });

      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      const p = path.resolve(asarDir, 'web.asar', 'index.html');
      const dirnameEvent = once(ipcMain, 'dirname');
      w.loadFile(p);
      const [, dirname] = await dirnameEvent;
      expect(dirname).to.equal(path.dirname(p));
    });

    it('loads script tag in html', async function () {
      after(function () {
        ipcMain.removeAllListeners('ping');
      });

      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      const p = path.resolve(asarDir, 'script.asar', 'index.html');
      const ping = once(ipcMain, 'ping');
      w.loadFile(p);
      const [, message] = await ping;
      expect(message).to.equal('pong');
    });

    it('loads video tag in html', async function () {
      this.timeout(60000);

      after(function () {
        ipcMain.removeAllListeners('asar-video');
      });

      const w = new BrowserWindow({
        show: false,
        width: 400,
        height: 400,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      const p = path.resolve(asarDir, 'video.asar', 'index.html');
      w.loadFile(p);
      const [, message, error] = await once(ipcMain, 'asar-video');
      if (message === 'ended') {
        expect(error).to.be.null();
      } else if (message === 'error') {
        throw new Error(error);
      }
    });
  });

  describe('worker', () => {
    it('Worker can load asar file', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixtures, 'workers', 'load_worker.html'));

      const workerUrl = url.format({
        pathname: path.resolve(fixtures, 'workers', 'workers.asar', 'worker.js').replaceAll('\\', '/'),
        protocol: 'file',
        slashes: true
      });
      const result = await w.webContents.executeJavaScript(`loadWorker('${workerUrl}')`);
      expect(result).to.equal('success');
    });

    it('SharedWorker can load asar file', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadFile(path.join(fixtures, 'workers', 'load_shared_worker.html'));

      const workerUrl = url.format({
        pathname: path.resolve(fixtures, 'workers', 'workers.asar', 'shared_worker.js').replaceAll('\\', '/'),
        protocol: 'file',
        slashes: true
      });
      const result = await w.webContents.executeJavaScript(`loadSharedWorker('${workerUrl}')`);
      expect(result).to.equal('success');
    });
  });

  describe('worker threads', function () {
    // DISABLED-FIXME(#38192): only disabled for ASan.
    ifit(!process.env.IS_ASAN)('should start worker thread from asar file', function (callback) {
      const p = path.join(asarDir, 'worker_threads.asar', 'worker.js');
      const w = new Worker(p);

      w.on('error', (err) => callback(err));
      w.on('message', (message) => {
        expect(message).to.equal('ping');
        w.terminate();

        callback(null);
      });
    });
  });
});

// eslint-disable-next-line @typescript-eslint/no-unused-vars
async function expectToThrowErrorWithCode (_func: Function, _code: string) {
  /* dummy for typescript */
}

// eslint-disable-next-line @typescript-eslint/no-unused-vars
function promisify (_f: Function): any {
  /* dummy for typescript */
}

describe('asar package', function () {
  const fixtures = path.join(__dirname, 'fixtures');
  const asarDir = path.join(fixtures, 'test.asar');
  const fs = require('node:fs') as typeof importedFs; // dummy, to fool typescript

  useRemoteContext({
    url: url.pathToFileURL(path.join(fixtures, 'pages', 'blank.html')),
    setup: `
      async function expectToThrowErrorWithCode (func, code) {
        let error;
        try {
          await func();
        } catch (e) {
          error = e;
        }

        const chai = require('chai')
        chai.expect(error).to.have.property('code').which.equals(code);
      }

      fs = require('node:fs')
      path = require('node:path')
      fixtures = ${JSON.stringify(fixtures)}
      asarDir = ${JSON.stringify(asarDir)}

      // This is used instead of util.promisify for some tests to dodge the
      // util.promisify.custom behavior.
      promisify = (f) => {
        return (...args) => new Promise((resolve, reject) => {
          f(...args, (err, result) => {
            if (err) reject(err)
            else resolve(result)
          })
        })
      }

      null
    `
  });

  describe('node api', function () {
    itremote('supports paths specified as a Buffer', function () {
      const file = Buffer.from(path.join(asarDir, 'a.asar', 'file1'));
      expect(fs.existsSync(file)).to.be.true();
    });

    describe('fs.readFileSync', function () {
      itremote('does not leak fd', function () {
        let readCalls = 1;
        while (readCalls <= 10000) {
          fs.readFileSync(path.join(process.resourcesPath, 'default_app.asar', 'main.js'));
          readCalls++;
        }
      });

      itremote('reads a normal file', function () {
        const file1 = path.join(asarDir, 'a.asar', 'file1');
        expect(fs.readFileSync(file1).toString().trim()).to.equal('file1');
        const file2 = path.join(asarDir, 'a.asar', 'file2');
        expect(fs.readFileSync(file2).toString().trim()).to.equal('file2');
        const file3 = path.join(asarDir, 'a.asar', 'file3');
        expect(fs.readFileSync(file3).toString().trim()).to.equal('file3');
      });

      itremote('reads from a empty file', function () {
        const file = path.join(asarDir, 'empty.asar', 'file1');
        const buffer = fs.readFileSync(file);
        expect(buffer).to.be.empty();
        expect(buffer.toString()).to.equal('');
      });

      itremote('reads a linked file', function () {
        const p = path.join(asarDir, 'a.asar', 'link1');
        expect(fs.readFileSync(p).toString().trim()).to.equal('file1');
      });

      itremote('reads a file from linked directory', function () {
        const p1 = path.join(asarDir, 'a.asar', 'link2', 'file1');
        expect(fs.readFileSync(p1).toString().trim()).to.equal('file1');
        const p2 = path.join(asarDir, 'a.asar', 'link2', 'link2', 'file1');
        expect(fs.readFileSync(p2).toString().trim()).to.equal('file1');
      });

      itremote('throws ENOENT error when can not find file', function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        expect(() => {
          fs.readFileSync(p);
        }).to.throw(/ENOENT/);
      });

      itremote('passes ENOENT error to callback when can not find file', function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        let async = false;
        fs.readFile(p, function (error) {
          expect(async).to.be.true();
          expect(error).to.match(/ENOENT/);
        });
        async = true;
      });

      itremote('reads a normal file with unpacked files', function () {
        const p = path.join(asarDir, 'unpack.asar', 'a.txt');
        expect(fs.readFileSync(p).toString().trim()).to.equal('a');
      });

      itremote('reads a file in filesystem', function () {
        const p = path.resolve(asarDir, 'file');
        expect(fs.readFileSync(p).toString().trim()).to.equal('file');
      });
    });

    describe('fs.readFile', function () {
      itremote('reads a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const content = await new Promise((resolve, reject) => fs.readFile(p, (err, content) => {
          if (err) return reject(err);
          resolve(content);
        }));
        expect(String(content).trim()).to.equal('file1');
      });

      itremote('reads from a empty file', async function () {
        const p = path.join(asarDir, 'empty.asar', 'file1');
        const content = await new Promise((resolve, reject) => fs.readFile(p, (err, content) => {
          if (err) return reject(err);
          resolve(content);
        }));
        expect(String(content)).to.equal('');
      });

      itremote('reads from a empty file with encoding', async function () {
        const p = path.join(asarDir, 'empty.asar', 'file1');
        const content = await new Promise((resolve, reject) => fs.readFile(p, (err, content) => {
          if (err) return reject(err);
          resolve(content);
        }));
        expect(String(content)).to.equal('');
      });

      itremote('reads a linked file', async function () {
        const p = path.join(asarDir, 'a.asar', 'link1');
        const content = await new Promise((resolve, reject) => fs.readFile(p, (err, content) => {
          if (err) return reject(err);
          resolve(content);
        }));
        expect(String(content).trim()).to.equal('file1');
      });

      itremote('reads a file from linked directory', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2', 'file1');
        const content = await new Promise((resolve, reject) => fs.readFile(p, (err, content) => {
          if (err) return reject(err);
          resolve(content);
        }));
        expect(String(content).trim()).to.equal('file1');
      });

      itremote('throws ENOENT error when can not find file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        const err = await new Promise<any>((resolve) => fs.readFile(p, resolve));
        expect(err.code).to.equal('ENOENT');
      });
    });

    describe('fs.promises.readFile', function () {
      itremote('reads a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const content = await fs.promises.readFile(p);
        expect(String(content).trim()).to.equal('file1');
      });

      itremote('reads from a empty file', async function () {
        const p = path.join(asarDir, 'empty.asar', 'file1');
        const content = await fs.promises.readFile(p);
        expect(String(content)).to.equal('');
      });

      itremote('reads from a empty file with encoding', async function () {
        const p = path.join(asarDir, 'empty.asar', 'file1');
        const content = await fs.promises.readFile(p, 'utf8');
        expect(content).to.equal('');
      });

      itremote('reads a linked file', async function () {
        const p = path.join(asarDir, 'a.asar', 'link1');
        const content = await fs.promises.readFile(p);
        expect(String(content).trim()).to.equal('file1');
      });

      itremote('reads a file from linked directory', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2', 'file1');
        const content = await fs.promises.readFile(p);
        expect(String(content).trim()).to.equal('file1');
      });

      itremote('throws ENOENT error when can not find file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        await expectToThrowErrorWithCode(() => fs.promises.readFile(p), 'ENOENT');
      });
    });

    describe('fs.copyFile', function () {
      itremote('copies a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const temp = require('temp').track();
        const dest = temp.path();
        await new Promise<void>((resolve, reject) => {
          fs.copyFile(p, dest, (err) => {
            if (err) reject(err);
            else resolve();
          });
        });
        expect(fs.readFileSync(p).equals(fs.readFileSync(dest))).to.be.true();
      });

      itremote('copies a unpacked file', async function () {
        const p = path.join(asarDir, 'unpack.asar', 'a.txt');
        const temp = require('temp').track();
        const dest = temp.path();
        await new Promise<void>((resolve, reject) => {
          fs.copyFile(p, dest, (err) => {
            if (err) reject(err);
            else resolve();
          });
        });
        expect(fs.readFileSync(p).equals(fs.readFileSync(dest))).to.be.true();
      });
    });

    describe('fs.promises.copyFile', function () {
      itremote('copies a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const temp = require('temp').track();
        const dest = temp.path();
        await fs.promises.copyFile(p, dest);
        expect(fs.readFileSync(p).equals(fs.readFileSync(dest))).to.be.true();
      });

      itremote('copies a unpacked file', async function () {
        const p = path.join(asarDir, 'unpack.asar', 'a.txt');
        const temp = require('temp').track();
        const dest = temp.path();
        await fs.promises.copyFile(p, dest);
        expect(fs.readFileSync(p).equals(fs.readFileSync(dest))).to.be.true();
      });
    });

    describe('fs.copyFileSync', function () {
      itremote('copies a normal file', function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const temp = require('temp').track();
        const dest = temp.path();
        fs.copyFileSync(p, dest);
        expect(fs.readFileSync(p).equals(fs.readFileSync(dest))).to.be.true();
      });

      itremote('copies a unpacked file', function () {
        const p = path.join(asarDir, 'unpack.asar', 'a.txt');
        const temp = require('temp').track();
        const dest = temp.path();
        fs.copyFileSync(p, dest);
        expect(fs.readFileSync(p).equals(fs.readFileSync(dest))).to.be.true();
      });
    });

    describe('fs.lstatSync', function () {
      itremote('handles path with trailing slash correctly', function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2', 'file1');
        fs.lstatSync(p);
        fs.lstatSync(p + '/');
      });

      itremote('returns information of root', function () {
        const p = path.join(asarDir, 'a.asar');
        const stats = fs.lstatSync(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.true();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of root with stats as bigint', function () {
        const p = path.join(asarDir, 'a.asar');
        const stats = fs.lstatSync(p, { bigint: false });
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.true();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of a normal file', function () {
        const ref2 = ['file1', 'file2', 'file3', path.join('dir1', 'file1'), path.join('link2', 'file1')];
        for (let j = 0, len = ref2.length; j < len; j++) {
          const file = ref2[j];
          const p = path.join(asarDir, 'a.asar', file);
          const stats = fs.lstatSync(p);
          expect(stats.isFile()).to.be.true();
          expect(stats.isDirectory()).to.be.false();
          expect(stats.isSymbolicLink()).to.be.false();
          expect(stats.size).to.equal(6);
        }
      });

      itremote('returns information of a normal directory', function () {
        const ref2 = ['dir1', 'dir2', 'dir3'];
        for (let j = 0, len = ref2.length; j < len; j++) {
          const file = ref2[j];
          const p = path.join(asarDir, 'a.asar', file);
          const stats = fs.lstatSync(p);
          expect(stats.isFile()).to.be.false();
          expect(stats.isDirectory()).to.be.true();
          expect(stats.isSymbolicLink()).to.be.false();
          expect(stats.size).to.equal(0);
        }
      });

      itremote('returns information of a linked file', function () {
        const ref2 = ['link1', path.join('dir1', 'link1'), path.join('link2', 'link2')];
        for (let j = 0, len = ref2.length; j < len; j++) {
          const file = ref2[j];
          const p = path.join(asarDir, 'a.asar', file);
          const stats = fs.lstatSync(p);
          expect(stats.isFile()).to.be.false();
          expect(stats.isDirectory()).to.be.false();
          expect(stats.isSymbolicLink()).to.be.true();
          expect(stats.size).to.equal(0);
        }
      });

      itremote('returns information of a linked directory', function () {
        const ref2 = ['link2', path.join('dir1', 'link2'), path.join('link2', 'link2')];
        for (let j = 0, len = ref2.length; j < len; j++) {
          const file = ref2[j];
          const p = path.join(asarDir, 'a.asar', file);
          const stats = fs.lstatSync(p);
          expect(stats.isFile()).to.be.false();
          expect(stats.isDirectory()).to.be.false();
          expect(stats.isSymbolicLink()).to.be.true();
          expect(stats.size).to.equal(0);
        }
      });

      itremote('throws ENOENT error when can not find file', function () {
        const ref2 = ['file4', 'file5', path.join('dir1', 'file4')];
        for (let j = 0, len = ref2.length; j < len; j++) {
          const file = ref2[j];
          const p = path.join(asarDir, 'a.asar', file);
          expect(() => {
            fs.lstatSync(p);
          }).to.throw(/ENOENT/);
        }
      });

      itremote('returns null when can not find file with throwIfNoEntry === false', function () {
        const ref2 = ['file4', 'file5', path.join('dir1', 'file4')];
        for (let j = 0, len = ref2.length; j < len; j++) {
          const file = ref2[j];
          const p = path.join(asarDir, 'a.asar', file);
          expect(fs.lstatSync(p, { throwIfNoEntry: false })).to.equal(null);
        }
      });
    });

    describe('fs.lstat', function () {
      itremote('handles path with trailing slash correctly', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2', 'file1');
        await promisify(fs.lstat)(p + '/');
      });

      itremote('returns information of root', async function () {
        const p = path.join(asarDir, 'a.asar');
        const stats = await promisify(fs.lstat)(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.true();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of root with stats as bigint', async function () {
        const p = path.join(asarDir, 'a.asar');
        const stats = await promisify(fs.lstat)(p, { bigint: false });
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.true();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'file1');
        const stats = await promisify(fs.lstat)(p);
        expect(stats.isFile()).to.be.true();
        expect(stats.isDirectory()).to.be.false();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(6);
      });

      itremote('returns information of a normal directory', async function () {
        const p = path.join(asarDir, 'a.asar', 'dir1');
        const stats = await promisify(fs.lstat)(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.true();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of a linked file', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link1');
        const stats = await promisify(fs.lstat)(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.false();
        expect(stats.isSymbolicLink()).to.be.true();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of a linked directory', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2');
        const stats = await promisify(fs.lstat)(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.false();
        expect(stats.isSymbolicLink()).to.be.true();
        expect(stats.size).to.equal(0);
      });

      itremote('throws ENOENT error when can not find file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file4');
        const err = await new Promise<any>(resolve => fs.lstat(p, resolve));
        expect(err.code).to.equal('ENOENT');
      });
    });

    describe('fs.promises.lstat', function () {
      itremote('handles path with trailing slash correctly', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2', 'file1');
        await fs.promises.lstat(p + '/');
      });

      itremote('returns information of root', async function () {
        const p = path.join(asarDir, 'a.asar');
        const stats = await fs.promises.lstat(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.true();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of root with stats as bigint', async function () {
        const p = path.join(asarDir, 'a.asar');
        const stats = await fs.promises.lstat(p, { bigint: false });
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.true();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'file1');
        const stats = await fs.promises.lstat(p);
        expect(stats.isFile()).to.be.true();
        expect(stats.isDirectory()).to.be.false();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(6);
      });

      itremote('returns information of a normal directory', async function () {
        const p = path.join(asarDir, 'a.asar', 'dir1');
        const stats = await fs.promises.lstat(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.true();
        expect(stats.isSymbolicLink()).to.be.false();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of a linked file', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link1');
        const stats = await fs.promises.lstat(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.false();
        expect(stats.isSymbolicLink()).to.be.true();
        expect(stats.size).to.equal(0);
      });

      itremote('returns information of a linked directory', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2');
        const stats = await fs.promises.lstat(p);
        expect(stats.isFile()).to.be.false();
        expect(stats.isDirectory()).to.be.false();
        expect(stats.isSymbolicLink()).to.be.true();
        expect(stats.size).to.equal(0);
      });

      itremote('throws ENOENT error when can not find file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file4');
        await expectToThrowErrorWithCode(() => fs.promises.lstat(p), 'ENOENT');
      });
    });

    describe('fs.realpathSync', () => {
      itremote('returns real path root', () => {
        const parent = fs.realpathSync(asarDir);
        const p = 'a.asar';
        const r = fs.realpathSync(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal file', () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'file1');
        const r = fs.realpathSync(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal directory', () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'dir1');
        const r = fs.realpathSync(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a linked file', () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'link2', 'link1');
        const r = fs.realpathSync(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'file1'));
      });

      itremote('returns real path of a linked directory', () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'link2', 'link2');
        const r = fs.realpathSync(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'dir1'));
      });

      itremote('returns real path of an unpacked file', () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('unpack.asar', 'a.txt');
        const r = fs.realpathSync(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('throws ENOENT error when can not find file', () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'not-exist');
        expect(() => {
          fs.realpathSync(path.join(parent, p));
        }).to.throw(/ENOENT/);
      });
    });

    describe('fs.realpathSync.native', () => {
      itremote('returns real path root', () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = 'a.asar';
        const r = fs.realpathSync.native(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal file', () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'file1');
        const r = fs.realpathSync.native(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal directory', () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'dir1');
        const r = fs.realpathSync.native(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a linked file', () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'link2', 'link1');
        const r = fs.realpathSync.native(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'file1'));
      });

      itremote('returns real path of a linked directory', () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'link2', 'link2');
        const r = fs.realpathSync.native(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'dir1'));
      });

      itremote('returns real path of an unpacked file', () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('unpack.asar', 'a.txt');
        const r = fs.realpathSync.native(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('throws ENOENT error when can not find file', () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'not-exist');
        expect(() => {
          fs.realpathSync.native(path.join(parent, p));
        }).to.throw(/ENOENT/);
      });
    });

    describe('fs.realpath', () => {
      itremote('returns real path root', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = 'a.asar';
        const r = await promisify(fs.realpath)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal file', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'file1');
        const r = await promisify(fs.realpath)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal directory', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'dir1');
        const r = await promisify(fs.realpath)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a linked file', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'link2', 'link1');
        const r = await promisify(fs.realpath)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'file1'));
      });

      itremote('returns real path of a linked directory', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'link2', 'link2');
        const r = await promisify(fs.realpath)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'dir1'));
      });

      itremote('returns real path of an unpacked file', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('unpack.asar', 'a.txt');
        const r = await promisify(fs.realpath)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('throws ENOENT error when can not find file', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'not-exist');
        const err = await new Promise<any>(resolve => fs.realpath(path.join(parent, p), resolve));
        expect(err.code).to.equal('ENOENT');
      });
    });

    describe('fs.promises.realpath', () => {
      itremote('returns real path root', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = 'a.asar';
        const r = await fs.promises.realpath(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal file', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'file1');
        const r = await fs.promises.realpath(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal directory', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'dir1');
        const r = await fs.promises.realpath(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a linked file', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'link2', 'link1');
        const r = await fs.promises.realpath(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'file1'));
      });

      itremote('returns real path of a linked directory', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'link2', 'link2');
        const r = await fs.promises.realpath(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'dir1'));
      });

      itremote('returns real path of an unpacked file', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('unpack.asar', 'a.txt');
        const r = await fs.promises.realpath(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('throws ENOENT error when can not find file', async () => {
        const parent = fs.realpathSync(asarDir);
        const p = path.join('a.asar', 'not-exist');
        await expectToThrowErrorWithCode(() => fs.promises.realpath(path.join(parent, p)), 'ENOENT');
      });
    });

    describe('fs.realpath.native', () => {
      itremote('returns real path root', async () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = 'a.asar';
        const r = await promisify(fs.realpath.native)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal file', async () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'file1');
        const r = await promisify(fs.realpath.native)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a normal directory', async () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'dir1');
        const r = await promisify(fs.realpath.native)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('returns real path of a linked file', async () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'link2', 'link1');
        const r = await promisify(fs.realpath.native)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'file1'));
      });

      itremote('returns real path of a linked directory', async () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'link2', 'link2');
        const r = await promisify(fs.realpath.native)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, 'a.asar', 'dir1'));
      });

      itremote('returns real path of an unpacked file', async () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('unpack.asar', 'a.txt');
        const r = await promisify(fs.realpath.native)(path.join(parent, p));
        expect(r).to.equal(path.join(parent, p));
      });

      itremote('throws ENOENT error when can not find file', async () => {
        const parent = fs.realpathSync.native(asarDir);
        const p = path.join('a.asar', 'not-exist');
        const err = await new Promise<any>(resolve => fs.realpath.native(path.join(parent, p), resolve));
        expect(err.code).to.equal('ENOENT');
      });
    });

    describe('fs.readdirSync', function () {
      itremote('reads dirs from root', function () {
        const p = path.join(asarDir, 'a.asar');
        const dirs = fs.readdirSync(p);
        expect(dirs).to.deep.equal(['dir1', 'dir2', 'dir3', 'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js']);
      });

      itremote('supports recursive readdirSync withFileTypes', () => {
        const dir = path.join(fixtures, 'recursive-asar');
        const files = fs.readdirSync(dir, { recursive: true, withFileTypes: true });

        expect(files).to.have.length(24);

        for (const file of files) {
          expect(file).to.be.an.instanceOf(fs.Dirent);
        }

        const paths = files.map((a: any) => a.name);
        expect(paths).to.have.members([
          'a.asar', 'nested', 'test.txt', 'dir1', 'dir2', 'dir3',
          'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js',
          'hello.txt', 'file1', 'file2', 'file3', 'link1', 'link2',
          'file1', 'file2', 'file3', 'file1', 'file2', 'file3'
        ]);
      });

      itremote('supports recursive readdirSync', () => {
        const dir = path.join(fixtures, 'recursive-asar');
        const files = fs.readdirSync(dir, { recursive: true });
        expect(files).to.have.members([
          'a.asar',
          'nested',
          'test.txt',
          path.join('a.asar', 'dir1'),
          path.join('a.asar', 'dir2'),
          path.join('a.asar', 'dir3'),
          path.join('a.asar', 'file1'),
          path.join('a.asar', 'file2'),
          path.join('a.asar', 'file3'),
          path.join('a.asar', 'link1'),
          path.join('a.asar', 'link2'),
          path.join('a.asar', 'ping.js'),
          path.join('nested', 'hello.txt'),
          path.join('a.asar', 'dir1', 'file1'),
          path.join('a.asar', 'dir1', 'file2'),
          path.join('a.asar', 'dir1', 'file3'),
          path.join('a.asar', 'dir1', 'link1'),
          path.join('a.asar', 'dir1', 'link2'),
          path.join('a.asar', 'dir2', 'file1'),
          path.join('a.asar', 'dir2', 'file2'),
          path.join('a.asar', 'dir2', 'file3'),
          path.join('a.asar', 'dir3', 'file1'),
          path.join('a.asar', 'dir3', 'file2'),
          path.join('a.asar', 'dir3', 'file3')
        ]);
      });

      itremote('reads dirs from a normal dir', function () {
        const p = path.join(asarDir, 'a.asar', 'dir1');
        const dirs = fs.readdirSync(p);
        expect(dirs).to.deep.equal(['file1', 'file2', 'file3', 'link1', 'link2']);
      });

      itremote('supports withFileTypes', function () {
        const p = path.join(asarDir, 'a.asar');
        const dirs = fs.readdirSync(p, { withFileTypes: true });
        for (const dir of dirs) {
          expect(dir).to.be.an.instanceof(fs.Dirent);
        }
        const names = dirs.map(a => a.name);
        expect(names).to.deep.equal(['dir1', 'dir2', 'dir3', 'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js']);
      });

      itremote('supports withFileTypes for a deep directory', function () {
        const p = path.join(asarDir, 'a.asar', 'dir3');
        const dirs = fs.readdirSync(p, { withFileTypes: true });
        for (const dir of dirs) {
          expect(dir).to.be.an.instanceof(fs.Dirent);
        }
        const names = dirs.map(a => a.name);
        expect(names).to.deep.equal(['file1', 'file2', 'file3']);
      });

      itremote('reads dirs from a linked dir', function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2');
        const dirs = fs.readdirSync(p);
        expect(dirs).to.deep.equal(['file1', 'file2', 'file3', 'link1', 'link2']);
      });

      itremote('throws ENOENT error when can not find file', function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        expect(() => {
          fs.readdirSync(p);
        }).to.throw(/ENOENT/);
      });
    });

    describe('fs.readdir', function () {
      itremote('reads dirs from root', async () => {
        const p = path.join(asarDir, 'a.asar');
        const dirs = await promisify(fs.readdir)(p);
        expect(dirs).to.deep.equal(['dir1', 'dir2', 'dir3', 'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js']);
      });

      itremote('supports recursive readdirSync', async () => {
        const dir = path.join(fixtures, 'recursive-asar');
        const files = await promisify(fs.readdir)(dir, { recursive: true });
        expect(files).to.have.members([
          'a.asar',
          'nested',
          'test.txt',
          path.join('a.asar', 'dir1'),
          path.join('a.asar', 'dir2'),
          path.join('a.asar', 'dir3'),
          path.join('a.asar', 'file1'),
          path.join('a.asar', 'file2'),
          path.join('a.asar', 'file3'),
          path.join('a.asar', 'link1'),
          path.join('a.asar', 'link2'),
          path.join('a.asar', 'ping.js'),
          path.join('nested', 'hello.txt'),
          path.join('a.asar', 'dir1', 'file1'),
          path.join('a.asar', 'dir1', 'file2'),
          path.join('a.asar', 'dir1', 'file3'),
          path.join('a.asar', 'dir1', 'link1'),
          path.join('a.asar', 'dir1', 'link2'),
          path.join('a.asar', 'dir2', 'file1'),
          path.join('a.asar', 'dir2', 'file2'),
          path.join('a.asar', 'dir2', 'file3'),
          path.join('a.asar', 'dir3', 'file1'),
          path.join('a.asar', 'dir3', 'file2'),
          path.join('a.asar', 'dir3', 'file3')
        ]);
      });

      itremote('supports readdir withFileTypes', async () => {
        const dir = path.join(fixtures, 'recursive-asar');
        const files = await promisify(fs.readdir)(dir, { recursive: true, withFileTypes: true });

        expect(files).to.have.length(24);

        for (const file of files) {
          expect(file).to.be.an.instanceOf(fs.Dirent);
        }

        const paths = files.map((a: any) => a.name);
        expect(paths).to.have.members([
          'a.asar', 'nested', 'test.txt', 'dir1', 'dir2', 'dir3',
          'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js',
          'hello.txt', 'file1', 'file2', 'file3', 'link1', 'link2',
          'file1', 'file2', 'file3', 'file1', 'file2', 'file3'
        ]);
      });

      itremote('supports withFileTypes', async () => {
        const p = path.join(asarDir, 'a.asar');

        const dirs = await promisify(fs.readdir)(p, { withFileTypes: true });
        for (const dir of dirs) {
          expect(dir).to.be.an.instanceof(fs.Dirent);
        }

        const names = dirs.map((a: any) => a.name);
        expect(names).to.deep.equal(['dir1', 'dir2', 'dir3', 'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js']);
      });

      itremote('reads dirs from a normal dir', async () => {
        const p = path.join(asarDir, 'a.asar', 'dir1');
        const dirs = await promisify(fs.readdir)(p);
        expect(dirs).to.deep.equal(['file1', 'file2', 'file3', 'link1', 'link2']);
      });

      itremote('reads dirs from a linked dir', async () => {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2');
        const dirs = await promisify(fs.readdir)(p);
        expect(dirs).to.deep.equal(['file1', 'file2', 'file3', 'link1', 'link2']);
      });

      itremote('throws ENOENT error when can not find file', async () => {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        const err = await new Promise<any>(resolve => fs.readdir(p, resolve));
        expect(err.code).to.equal('ENOENT');
      });

      it('handles null for options', function (done) {
        const p = path.join(asarDir, 'a.asar', 'dir1');
        fs.readdir(p, null, function (err, dirs) {
          try {
            expect(err).to.be.null();
            expect(dirs).to.deep.equal(['file1', 'file2', 'file3', 'link1', 'link2']);
            done();
          } catch (e) {
            done(e);
          }
        });
      });

      it('handles undefined for options', function (done) {
        const p = path.join(asarDir, 'a.asar', 'dir1');
        fs.readdir(p, undefined, function (err, dirs) {
          try {
            expect(err).to.be.null();
            expect(dirs).to.deep.equal(['file1', 'file2', 'file3', 'link1', 'link2']);
            done();
          } catch (e) {
            done(e);
          }
        });
      });
    });

    describe('fs.promises.readdir', function () {
      itremote('reads dirs from root', async function () {
        const p = path.join(asarDir, 'a.asar');
        const dirs = await fs.promises.readdir(p);
        expect(dirs).to.deep.equal(['dir1', 'dir2', 'dir3', 'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js']);
      });

      itremote('supports recursive readdir', async () => {
        const dir = path.join(fixtures, 'recursive-asar');
        const files = await fs.promises.readdir(dir, { recursive: true });
        expect(files).to.have.members([
          'a.asar',
          'nested',
          'test.txt',
          path.join('a.asar', 'dir1'),
          path.join('a.asar', 'dir2'),
          path.join('a.asar', 'dir3'),
          path.join('a.asar', 'file1'),
          path.join('a.asar', 'file2'),
          path.join('a.asar', 'file3'),
          path.join('a.asar', 'link1'),
          path.join('a.asar', 'link2'),
          path.join('a.asar', 'ping.js'),
          path.join('nested', 'hello.txt'),
          path.join('a.asar', 'dir1', 'file1'),
          path.join('a.asar', 'dir1', 'file2'),
          path.join('a.asar', 'dir1', 'file3'),
          path.join('a.asar', 'dir1', 'link1'),
          path.join('a.asar', 'dir1', 'link2'),
          path.join('a.asar', 'dir2', 'file1'),
          path.join('a.asar', 'dir2', 'file2'),
          path.join('a.asar', 'dir2', 'file3'),
          path.join('a.asar', 'dir3', 'file1'),
          path.join('a.asar', 'dir3', 'file2'),
          path.join('a.asar', 'dir3', 'file3')
        ]);
      });

      itremote('supports readdir withFileTypes', async () => {
        const dir = path.join(fixtures, 'recursive-asar');
        const files = await fs.promises.readdir(dir, { recursive: true, withFileTypes: true });

        expect(files).to.have.length(24);

        for (const file of files) {
          expect(file).to.be.an.instanceOf(fs.Dirent);
        }

        const paths = files.map((a: any) => a.name);
        expect(paths).to.have.members([
          'a.asar', 'nested', 'test.txt', 'dir1', 'dir2', 'dir3',
          'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js',
          'hello.txt', 'file1', 'file2', 'file3', 'link1', 'link2',
          'file1', 'file2', 'file3', 'file1', 'file2', 'file3'
        ]);
      });

      itremote('supports withFileTypes', async function () {
        const p = path.join(asarDir, 'a.asar');
        const dirs = await fs.promises.readdir(p, { withFileTypes: true });
        for (const dir of dirs) {
          expect(dir).to.be.an.instanceof(fs.Dirent);
        }
        const names = dirs.map(a => a.name);
        expect(names).to.deep.equal(['dir1', 'dir2', 'dir3', 'file1', 'file2', 'file3', 'link1', 'link2', 'ping.js']);
      });

      itremote('reads dirs from a normal dir', async function () {
        const p = path.join(asarDir, 'a.asar', 'dir1');
        const dirs = await fs.promises.readdir(p);
        expect(dirs).to.deep.equal(['file1', 'file2', 'file3', 'link1', 'link2']);
      });

      itremote('reads dirs from a linked dir', async function () {
        const p = path.join(asarDir, 'a.asar', 'link2', 'link2');
        const dirs = await fs.promises.readdir(p);
        expect(dirs).to.deep.equal(['file1', 'file2', 'file3', 'link1', 'link2']);
      });

      itremote('throws ENOENT error when can not find file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        await expectToThrowErrorWithCode(() => fs.promises.readdir(p), 'ENOENT');
      });
    });

    describe('fs.openSync', function () {
      itremote('opens a normal/linked/under-linked-directory file', function () {
        const ref2 = ['file1', 'link1', path.join('link2', 'file1')];
        for (let j = 0, len = ref2.length; j < len; j++) {
          const file = ref2[j];
          const p = path.join(asarDir, 'a.asar', file);
          const fd = fs.openSync(p, 'r');
          const buffer = Buffer.alloc(6);
          fs.readSync(fd, buffer, 0, 6, 0);
          expect(String(buffer).trim()).to.equal('file1');
          fs.closeSync(fd);
        }
      });

      itremote('throws ENOENT error when can not find file', function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        expect(() => {
          (fs.openSync as any)(p);
        }).to.throw(/ENOENT/);
      });
    });

    describe('fs.open', function () {
      itremote('opens a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const fd = await promisify(fs.open)(p, 'r');
        const buffer = Buffer.alloc(6);
        await promisify(fs.read)(fd, buffer, 0, 6, 0);
        expect(String(buffer).trim()).to.equal('file1');
        await promisify(fs.close)(fd);
      });

      itremote('throws ENOENT error when can not find file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        const err = await new Promise<any>(resolve => fs.open(p, 'r', resolve));
        expect(err.code).to.equal('ENOENT');
      });
    });

    describe('fs.promises.open', function () {
      itremote('opens a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const fh = await fs.promises.open(p, 'r');
        const buffer = Buffer.alloc(6);
        await fh.read(buffer, 0, 6, 0);
        expect(String(buffer).trim()).to.equal('file1');
        await fh.close();
      });

      itremote('throws ENOENT error when can not find file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        await expectToThrowErrorWithCode(() => fs.promises.open(p, 'r'), 'ENOENT');
      });
    });

    describe('fs.mkdir', function () {
      itremote('throws error when calling inside asar archive', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        const err = await new Promise<any>(resolve => fs.mkdir(p, resolve));
        expect(err.code).to.equal('ENOTDIR');
      });
    });

    describe('fs.promises.mkdir', function () {
      itremote('throws error when calling inside asar archive', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        await expectToThrowErrorWithCode(() => fs.promises.mkdir(p), 'ENOTDIR');
      });
    });

    describe('fs.mkdirSync', function () {
      itremote('throws error when calling inside asar archive', function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        expect(() => {
          fs.mkdirSync(p);
        }).to.throw(/ENOTDIR/);
      });
    });

    describe('fs.exists', function () {
      itremote('handles an existing file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const exists = await new Promise(resolve => fs.exists(p, resolve));
        expect(exists).to.be.true();
      });

      itremote('handles a non-existent file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        const exists = await new Promise(resolve => fs.exists(p, resolve));
        expect(exists).to.be.false();
      });

      itremote('promisified version handles an existing file', async () => {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const exists = await require('node:util').promisify(fs.exists)(p);
        expect(exists).to.be.true();
      });

      itremote('promisified version handles a non-existent file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        const exists = await require('node:util').promisify(fs.exists)(p);
        expect(exists).to.be.false();
      });
    });

    describe('fs.existsSync', function () {
      itremote('handles an existing file', function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        expect(fs.existsSync(p)).to.be.true();
      });

      itremote('handles a non-existent file', function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        expect(fs.existsSync(p)).to.be.false();
      });
    });

    describe('fs.access', function () {
      itremote('accesses a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        await promisify(fs.access)(p);
      });

      itremote('throws an error when called with write mode', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        const err = await new Promise<any>(resolve => fs.access(p, fs.constants.R_OK | fs.constants.W_OK, resolve));
        expect(err.code).to.equal('EACCES');
      });

      itremote('throws an error when called on non-existent file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        const err = await new Promise<any>(resolve => fs.access(p, fs.constants.R_OK | fs.constants.W_OK, resolve));
        expect(err.code).to.equal('ENOENT');
      });

      itremote('allows write mode for unpacked files', async function () {
        const p = path.join(asarDir, 'unpack.asar', 'a.txt');
        await promisify(fs.access)(p, fs.constants.R_OK | fs.constants.W_OK);
      });
    });

    describe('fs.promises.access', function () {
      itremote('accesses a normal file', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        await fs.promises.access(p);
      });

      itremote('throws an error when called with write mode', async function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        await expectToThrowErrorWithCode(() => fs.promises.access(p, fs.constants.R_OK | fs.constants.W_OK), 'EACCES');
      });

      itremote('throws an error when called on non-existent file', async function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        await expectToThrowErrorWithCode(() => fs.promises.access(p), 'ENOENT');
      });

      itremote('allows write mode for unpacked files', async function () {
        const p = path.join(asarDir, 'unpack.asar', 'a.txt');
        await fs.promises.access(p, fs.constants.R_OK | fs.constants.W_OK);
      });
    });

    describe('fs.accessSync', function () {
      itremote('accesses a normal file', function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        expect(() => {
          fs.accessSync(p);
        }).to.not.throw();
      });

      itremote('throws an error when called with write mode', function () {
        const p = path.join(asarDir, 'a.asar', 'file1');
        expect(() => {
          fs.accessSync(p, fs.constants.R_OK | fs.constants.W_OK);
        }).to.throw(/EACCES/);
      });

      itremote('throws an error when called on non-existent file', function () {
        const p = path.join(asarDir, 'a.asar', 'not-exist');
        expect(() => {
          fs.accessSync(p);
        }).to.throw(/ENOENT/);
      });

      itremote('allows write mode for unpacked files', function () {
        const p = path.join(asarDir, 'unpack.asar', 'a.txt');
        expect(() => {
          fs.accessSync(p, fs.constants.R_OK | fs.constants.W_OK);
        }).to.not.throw();
      });
    });

    function generateSpecs (childProcess: string) {
      describe(`${childProcess}.fork`, function () {
        itremote('opens a normal js file', async function (childProcess: string) {
          const child = require(childProcess).fork(path.join(asarDir, 'a.asar', 'ping.js'));
          child.send('message');
          const msg = await new Promise(resolve => child.once('message', resolve));
          expect(msg).to.equal('message');
        }, [childProcess]);

        itremote('supports asar in the forked js', async function (childProcess: string, fixtures: string) {
          const file = path.join(asarDir, 'a.asar', 'file1');
          const child = require(childProcess).fork(path.join(fixtures, 'module', 'asar.js'));
          child.send(file);
          const content = await new Promise(resolve => child.once('message', resolve));
          expect(content).to.equal(fs.readFileSync(file).toString());
        }, [childProcess, fixtures]);
      });

      describe(`${childProcess}.exec`, function () {
        itremote('should not try to extract the command if there is a reference to a file inside an .asar', async function (childProcess: string) {
          const echo = path.join(asarDir, 'echo.asar', 'echo');

          const stdout = await promisify(require(childProcess).exec)('echo ' + echo + ' foo bar');
          expect(stdout.toString().replaceAll('\r', '')).to.equal(echo + ' foo bar\n');
        }, [childProcess]);
      });

      describe(`${childProcess}.execSync`, function () {
        itremote('should not try to extract the command if there is a reference to a file inside an .asar', async function (childProcess: string) {
          const echo = path.join(asarDir, 'echo.asar', 'echo');

          const stdout = require(childProcess).execSync('echo ' + echo + ' foo bar');
          expect(stdout.toString().replaceAll('\r', '')).to.equal(echo + ' foo bar\n');
        }, [childProcess]);
      });

      ifdescribe(process.platform === 'darwin' && process.arch !== 'arm64')(`${childProcess}.execFile`, function () {
        itremote('executes binaries', async function (childProcess: string) {
          const echo = path.join(asarDir, 'echo.asar', 'echo');
          const stdout = await promisify(require(childProcess).execFile)(echo, ['test']);
          expect(stdout).to.equal('test\n');
        }, [childProcess]);

        itremote('executes binaries without callback', async function (childProcess: string) {
          const echo = path.join(asarDir, 'echo.asar', 'echo');
          const process = require(childProcess).execFile(echo, ['test']);
          const code = await new Promise(resolve => process.once('close', resolve));
          expect(code).to.equal(0);
          process.on('error', function () {
            throw new Error('error');
          });
        }, [childProcess]);

        itremote('execFileSync executes binaries', function (childProcess: string) {
          const echo = path.join(asarDir, 'echo.asar', 'echo');
          const output = require(childProcess).execFileSync(echo, ['test']);
          expect(String(output)).to.equal('test\n');
        }, [childProcess]);
      });
    }

    generateSpecs('child_process');
    generateSpecs('node:child_process');

    describe('internalModuleReadJSON', function () {
      itremote('reads a normal file', function () {
        const { internalModuleReadJSON } = (process as any).binding('fs');
        const file1 = path.join(asarDir, 'a.asar', 'file1');
        const [s1, c1] = internalModuleReadJSON(file1);
        expect([s1.toString().trim(), c1]).to.eql(['file1', true]);

        const file2 = path.join(asarDir, 'a.asar', 'file2');
        const [s2, c2] = internalModuleReadJSON(file2);
        expect([s2.toString().trim(), c2]).to.eql(['file2', true]);

        const file3 = path.join(asarDir, 'a.asar', 'file3');
        const [s3, c3] = internalModuleReadJSON(file3);
        expect([s3.toString().trim(), c3]).to.eql(['file3', true]);
      });

      itremote('reads a normal file with unpacked files', function () {
        const { internalModuleReadJSON } = (process as any).binding('fs');
        const p = path.join(asarDir, 'unpack.asar', 'a.txt');
        const [s, c] = internalModuleReadJSON(p);
        expect([s.toString().trim(), c]).to.eql(['a', true]);
      });
    });

    describe('util.promisify', function () {
      itremote('can promisify all fs functions', function () {
        const originalFs = require('original-fs');
        const util = require('node:util');

        for (const [propertyName, originalValue] of Object.entries(originalFs)) {
          // Some properties exist but have a value of `undefined` on some platforms.
          // E.g. `fs.lchmod`, which in only available on MacOS, see
          // https://nodejs.org/docs/latest-v10.x/api/fs.html#fs_fs_lchmod_path_mode_callback
          // Also check for `null`s, `hasOwnProperty()` can't handle them.
          if (typeof originalValue === 'undefined' || originalValue === null) continue;

          if (Object.hasOwn(originalValue, util.promisify.custom)) {
            expect(fs).to.have.own.property(propertyName)
              .that.has.own.property(util.promisify.custom);
          }
        }
      });
    });

    describe('process.noAsar', function () {
      const errorName = process.platform === 'win32' ? 'ENOENT' : 'ENOTDIR';

      beforeEach(async function () {
        return (await getRemoteContext()).webContents.executeJavaScript(`
          process.noAsar = true;
        `);
      });

      afterEach(async function () {
        return (await getRemoteContext()).webContents.executeJavaScript(`
          process.noAsar = false;
        `);
      });

      itremote('disables asar support in sync API', function (errorName: string) {
        const file = path.join(asarDir, 'a.asar', 'file1');
        const dir = path.join(asarDir, 'a.asar', 'dir1');
        console.log(1);
        expect(() => {
          fs.readFileSync(file);
        }).to.throw(new RegExp(errorName));
        expect(() => {
          fs.lstatSync(file);
        }).to.throw(new RegExp(errorName));
        expect(() => {
          fs.realpathSync(file);
        }).to.throw(new RegExp(errorName));
        expect(() => {
          fs.readdirSync(dir);
        }).to.throw(new RegExp(errorName));
      }, [errorName]);

      itremote('disables asar support in async API', async function (errorName: string) {
        const file = path.join(asarDir, 'a.asar', 'file1');
        const dir = path.join(asarDir, 'a.asar', 'dir1');
        await new Promise<void>(resolve => {
          fs.readFile(file, function (error) {
            expect(error?.code).to.equal(errorName);
            fs.lstat(file, function (error) {
              expect(error?.code).to.equal(errorName);
              fs.realpath(file, function (error) {
                expect(error?.code).to.equal(errorName);
                fs.readdir(dir, function (error) {
                  expect(error?.code).to.equal(errorName);
                  resolve();
                });
              });
            });
          });
        });
      }, [errorName]);

      itremote('disables asar support in promises API', async function (errorName: string) {
        const file = path.join(asarDir, 'a.asar', 'file1');
        const dir = path.join(asarDir, 'a.asar', 'dir1');
        await expect(fs.promises.readFile(file)).to.be.eventually.rejectedWith(Error, new RegExp(errorName));
        await expect(fs.promises.lstat(file)).to.be.eventually.rejectedWith(Error, new RegExp(errorName));
        await expect(fs.promises.realpath(file)).to.be.eventually.rejectedWith(Error, new RegExp(errorName));
        await expect(fs.promises.readdir(dir)).to.be.eventually.rejectedWith(Error, new RegExp(errorName));
      }, [errorName]);

      itremote('treats *.asar as normal file', function () {
        const originalFs = require('original-fs');
        const asar = path.join(asarDir, 'a.asar');
        const content1 = fs.readFileSync(asar);
        const content2 = originalFs.readFileSync(asar);
        expect(content1.compare(content2)).to.equal(0);
        expect(() => {
          fs.readdirSync(asar);
        }).to.throw(/ENOTDIR/);
      });

      itremote('is reset to its original value when execSync throws an error', function () {
        process.noAsar = false;
        expect(() => {
          require('node:child_process').execSync(path.join(__dirname, 'does-not-exist.txt'));
        }).to.throw();
        expect(process.noAsar).to.be.false();
      });
    });

    /*
    describe('process.env.ELECTRON_NO_ASAR', function () {
      itremote('disables asar support in forked processes', function (done) {
        const forked = ChildProcess.fork(path.join(__dirname, 'fixtures', 'module', 'no-asar.js'), [], {
          env: {
            ELECTRON_NO_ASAR: true
          }
        });
        forked.on('message', function (stats) {
          try {
            expect(stats.isFile).to.be.true();
            expect(stats.size).to.equal(3458);
            done();
          } catch (e) {
            done(e);
          }
        });
      });

      itremote('disables asar support in spawned processes', function (done) {
        const spawned = ChildProcess.spawn(process.execPath, [path.join(__dirname, 'fixtures', 'module', 'no-asar.js')], {
          env: {
            ELECTRON_NO_ASAR: true,
            ELECTRON_RUN_AS_NODE: true
          }
        });

        let output = '';
        spawned.stdout.on('data', function (data) {
          output += data;
        });
        spawned.stdout.on('close', function () {
          try {
            const stats = JSON.parse(output);
            expect(stats.isFile).to.be.true();
            expect(stats.size).to.equal(3458);
            done();
          } catch (e) {
            done(e);
          }
        });
      });
    });
    */
  });

  describe('asar protocol', function () {
    itremote('can request a file in package', async function () {
      const p = path.resolve(asarDir, 'a.asar', 'file1');
      const response = await fetch('file://' + p);
      const data = await response.text();
      expect(data.trim()).to.equal('file1');
    });

    itremote('can request a file in package with unpacked files', async function () {
      const p = path.resolve(asarDir, 'unpack.asar', 'a.txt');
      const response = await fetch('file://' + p);
      const data = await response.text();
      expect(data.trim()).to.equal('a');
    });

    itremote('can request a linked file in package', async function () {
      const p = path.resolve(asarDir, 'a.asar', 'link2', 'link1');
      const response = await fetch('file://' + p);
      const data = await response.text();
      expect(data.trim()).to.equal('file1');
    });

    itremote('can request a file in filesystem', async function () {
      const p = path.resolve(asarDir, 'file');
      const response = await fetch('file://' + p);
      const data = await response.text();
      expect(data.trim()).to.equal('file');
    });

    itremote('gets error when file is not found', async function () {
      const p = path.resolve(asarDir, 'a.asar', 'no-exist');
      try {
        const response = await fetch('file://' + p);
        expect(response.status).to.equal(404);
      } catch (error: any) {
        expect(error.message).to.equal('Failed to fetch');
      }
    });
  });

  describe('original-fs module', function () {
    itremote('treats .asar as file', function () {
      const file = path.join(asarDir, 'a.asar');
      const originalFs = require('original-fs');
      const stats = originalFs.statSync(file);
      expect(stats.isFile()).to.be.true();
    });

    /*
    it('is available in forked scripts', async function () {
      const child = ChildProcess.fork(path.join(fixtures, 'module', 'original-fs.js'));
      const message = once(child, 'message');
      child.send('message');
      const [msg] = await message;
      expect(msg).to.equal('object');
    });
    */

    itremote('can be used with streams', () => {
      const originalFs = require('original-fs');
      originalFs.createReadStream(path.join(asarDir, 'a.asar'));
    });

    itremote('can recursively delete a directory with an asar file in itremote using rmdirSync', () => {
      const deleteDir = path.join(asarDir, 'deleteme');
      fs.mkdirSync(deleteDir);

      const originalFs = require('original-fs');
      originalFs.rmdirSync(deleteDir, { recursive: true });

      expect(fs.existsSync(deleteDir)).to.be.false();
    });

    itremote('can recursively delete a directory with an asar file in itremote using promises.rmdir', async () => {
      const deleteDir = path.join(asarDir, 'deleteme');
      fs.mkdirSync(deleteDir);

      const originalFs = require('original-fs');
      await originalFs.promises.rmdir(deleteDir, { recursive: true });

      expect(fs.existsSync(deleteDir)).to.be.false();
    });

    itremote('has the same APIs as fs', function () {
      expect(Object.keys(require('node:fs'))).to.deep.equal(Object.keys(require('original-fs')));
      expect(Object.keys(require('node:fs').promises)).to.deep.equal(Object.keys(require('original-fs').promises));
    });
  });

  describe('graceful-fs module', function () {
    itremote('recognize asar archives', function () {
      const gfs = require('graceful-fs');

      const p = path.join(asarDir, 'a.asar', 'link1');
      expect(gfs.readFileSync(p).toString().trim()).to.equal('file1');
    });
    itremote('does not touch global fs object', function () {
      const gfs = require('graceful-fs');
      expect(fs.readdir).to.not.equal(gfs.readdir);
    });
  });

  describe('mkdirp module', function () {
    itremote('throws error when calling inside asar archive', function () {
      const mkdirp = require('mkdirp');

      const p = path.join(asarDir, 'a.asar', 'not-exist');
      expect(() => {
        mkdirp.sync(p);
      }).to.throw(/ENOTDIR/);
    });
  });

  describe('native-image', function () {
    itremote('reads image from asar archive', function () {
      const p = path.join(asarDir, 'logo.asar', 'logo.png');
      const logo = require('electron').nativeImage.createFromPath(p);
      expect(logo.getSize()).to.deep.equal({
        width: 55,
        height: 55
      });
    });

    itremote('reads image from asar archive with unpacked files', function () {
      const p = path.join(asarDir, 'unpack.asar', 'atom.png');
      const logo = require('electron').nativeImage.createFromPath(p);
      expect(logo.getSize()).to.deep.equal({
        width: 1024,
        height: 1024
      });
    });
  });
});
