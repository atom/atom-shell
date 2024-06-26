import { BrowserWindow, ipcMain } from 'electron/main';
import { contextBridge } from 'electron/renderer';
import { expect } from 'chai';
import * as fs from 'node:fs';
import * as http from 'node:http';
import * as os from 'node:os';
import * as path from 'node:path';
import * as cp from 'node:child_process';

import { closeWindow } from './lib/window-helpers';
import { listen } from './lib/spec-helpers';
import { once } from 'node:events';

const fixturesPath = path.resolve(__dirname, 'fixtures', 'api', 'context-bridge');

describe('contextBridge', () => {
  let w: BrowserWindow;
  let dir: string;
  let server: http.Server;
  let serverUrl: string;

  before(async () => {
    server = http.createServer((req, res) => {
      res.setHeader('Content-Type', 'text/html');
      res.end('');
    });
    serverUrl = (await listen(server)).url;
  });

  after(async () => {
    if (server) await new Promise(resolve => server.close(resolve));
    server = null as any;
  });

  afterEach(async () => {
    await closeWindow(w);
    if (dir) await fs.promises.rm(dir, { force: true, recursive: true });
  });

  it('should not be accessible when contextIsolation is disabled', async () => {
    w = new BrowserWindow({
      show: false,
      webPreferences: {
        contextIsolation: false,
        preload: path.resolve(fixturesPath, 'can-bind-preload.js')
      }
    });
    w.loadFile(path.resolve(fixturesPath, 'empty.html'));
    const [, bound] = await once(ipcMain, 'context-bridge-bound');
    expect(bound).to.equal(false);
  });

  it('should be accessible when contextIsolation is enabled', async () => {
    w = new BrowserWindow({
      show: false,
      webPreferences: {
        contextIsolation: true,
        preload: path.resolve(fixturesPath, 'can-bind-preload.js')
      }
    });
    w.loadFile(path.resolve(fixturesPath, 'empty.html'));
    const [, bound] = await once(ipcMain, 'context-bridge-bound');
    expect(bound).to.equal(true);
  });

  const generateTests = (useSandbox: boolean) => {
    describe(`with sandbox=${useSandbox}`, () => {
      const makeBindingWindow = async (bindingCreator: Function, worldId: number = 0) => {
        const preloadContentForMainWorld = `const renderer_1 = require('electron');
        ${useSandbox ? '' : `require('node:v8').setFlagsFromString('--expose_gc');
        const gc=require('node:vm').runInNewContext('gc');
        renderer_1.contextBridge.exposeInMainWorld('GCRunner', {
          run: () => gc()
        });`}
        (${bindingCreator.toString()})();`;

        const preloadContentForIsolatedWorld = `const renderer_1 = require('electron');
        ${useSandbox ? '' : `require('node:v8').setFlagsFromString('--expose_gc');
        const gc=require('node:vm').runInNewContext('gc');
        renderer_1.webFrame.setIsolatedWorldInfo(${worldId}, {
          name: "Isolated World"
        });
        renderer_1.contextBridge.exposeInIsolatedWorld(${worldId}, 'GCRunner', {
          run: () => gc()
        });`}
        (${bindingCreator.toString()})();`;

        const tmpDir = await fs.promises.mkdtemp(path.resolve(os.tmpdir(), 'electron-spec-preload-'));
        dir = tmpDir;
        await fs.promises.writeFile(path.resolve(tmpDir, 'preload.js'), worldId === 0 ? preloadContentForMainWorld : preloadContentForIsolatedWorld);
        w = new BrowserWindow({
          show: false,
          webPreferences: {
            contextIsolation: true,
            nodeIntegration: true,
            sandbox: useSandbox,
            preload: path.resolve(tmpDir, 'preload.js'),
            additionalArguments: ['--unsafely-expose-electron-internals-for-testing']
          }
        });
        await w.loadURL(serverUrl);
      };

      const callWithBindings = (fn: Function, worldId: number = 0) =>
        worldId === 0 ? w.webContents.executeJavaScript(`(${fn.toString()})(window)`) : w.webContents.executeJavaScriptInIsolatedWorld(worldId, [{ code: `(${fn.toString()})(window)` }]); ;

      const getGCInfo = async (): Promise<{
        trackedValues: number;
      }> => {
        w.webContents.send('get-gc-info');
        const [, info] = await once(ipcMain, 'gc-info');
        return info;
      };

      const forceGCOnWindow = async () => {
        w.webContents.debugger.attach();
        await w.webContents.debugger.sendCommand('HeapProfiler.enable');
        await w.webContents.debugger.sendCommand('HeapProfiler.collectGarbage');
        await w.webContents.debugger.sendCommand('HeapProfiler.disable');
        w.webContents.debugger.detach();
      };

      it('should proxy numbers', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', 123);
        });
        const result = await callWithBindings((root: any) => {
          return root.example;
        });
        expect(result).to.equal(123);
      });

      it('should proxy numbers when exposed in isolated world', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInIsolatedWorld(1004, 'example', 123);
        }, 1004);
        const result = await callWithBindings((root: any) => {
          return root.example;
        }, 1004);
        expect(result).to.equal(123);
      });

      it('should make global properties read-only', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', 123);
        });
        const result = await callWithBindings((root: any) => {
          root.example = 456;
          return root.example;
        });
        expect(result).to.equal(123);
      });

      it('should proxy nested numbers', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myNumber: 123
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.myNumber;
        });
        expect(result).to.equal(123);
      });

      it('should make properties unwriteable', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myNumber: 123
          });
        });
        const result = await callWithBindings((root: any) => {
          root.example.myNumber = 456;
          return root.example.myNumber;
        });
        expect(result).to.equal(123);
      });

      it('should proxy strings', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', 'my-words');
        });
        const result = await callWithBindings((root: any) => {
          return root.example;
        });
        expect(result).to.equal('my-words');
      });

      it('should proxy nested strings', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myString: 'my-words'
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.myString;
        });
        expect(result).to.equal('my-words');
      });

      it('should proxy nested strings when exposed in isolated world', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInIsolatedWorld(1004, 'example', {
            myString: 'my-words'
          });
        }, 1004);
        const result = await callWithBindings((root: any) => {
          return root.example.myString;
        }, 1004);
        expect(result).to.equal('my-words');
      });

      it('should proxy arrays', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', [123, 'my-words']);
        });
        const result = await callWithBindings((root: any) => {
          return [root.example, Array.isArray(root.example)];
        });
        expect(result).to.deep.equal([[123, 'my-words'], true]);
      });

      it('should proxy nested arrays', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myArr: [123, 'my-words']
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.myArr;
        });
        expect(result).to.deep.equal([123, 'my-words']);
      });

      it('should make arrays immutable', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', [123, 'my-words']);
        });
        const immutable = await callWithBindings((root: any) => {
          try {
            root.example.push(456);
            return false;
          } catch {
            return true;
          }
        });
        expect(immutable).to.equal(true);
      });

      it('should make nested arrays immutable', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myArr: [123, 'my-words']
          });
        });
        const immutable = await callWithBindings((root: any) => {
          try {
            root.example.myArr.push(456);
            return false;
          } catch {
            return true;
          }
        });
        expect(immutable).to.equal(true);
      });

      it('should proxy booleans', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', true);
        });
        const result = await callWithBindings((root: any) => {
          return root.example;
        });
        expect(result).to.equal(true);
      });

      it('should proxy nested booleans', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myBool: true
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.myBool;
        });
        expect(result).to.equal(true);
      });

      it('should proxy promises and resolve with the correct value', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example',
            Promise.resolve('i-resolved')
          );
        });
        const result = await callWithBindings((root: any) => {
          return root.example;
        });
        expect(result).to.equal('i-resolved');
      });

      it('should proxy nested promises and resolve with the correct value', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myPromise: Promise.resolve('i-resolved')
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.myPromise;
        });
        expect(result).to.equal('i-resolved');
      });

      it('should proxy promises and reject with the correct value', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', Promise.reject(new Error('i-rejected')));
        });
        const result = await callWithBindings(async (root: any) => {
          try {
            await root.example;
            return null;
          } catch (err) {
            return err;
          }
        });
        expect(result).to.be.an.instanceOf(Error).with.property('message', 'i-rejected');
      });

      it('should proxy nested promises and reject with the correct value', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myPromise: Promise.reject(new Error('i-rejected'))
          });
        });
        const result = await callWithBindings(async (root: any) => {
          try {
            await root.example.myPromise;
            return null;
          } catch (err) {
            return err;
          }
        });
        expect(result).to.be.an.instanceOf(Error).with.property('message', 'i-rejected');
      });

      it('should proxy promises and resolve with the correct value if it resolves later', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myPromise: () => new Promise(resolve => setTimeout(() => resolve('delayed'), 20))
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.myPromise();
        });
        expect(result).to.equal('delayed');
      });

      it('should proxy nested promises correctly', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            myPromise: () => new Promise(resolve => setTimeout(() => resolve(Promise.resolve(123)), 20))
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.myPromise();
        });
        expect(result).to.equal(123);
      });

      it('should proxy methods', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            getNumber: () => 123,
            getString: () => 'help',
            getBoolean: () => false,
            getPromise: async () => 'promise'
          });
        });
        const result = await callWithBindings(async (root: any) => {
          return [root.example.getNumber(), root.example.getString(), root.example.getBoolean(), await root.example.getPromise()];
        });
        expect(result).to.deep.equal([123, 'help', false, 'promise']);
      });

      it('should proxy functions', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', () => 'return-value');
        });
        const result = await callWithBindings(async (root: any) => {
          return root.example();
        });
        expect(result).equal('return-value');
      });

      it('should not double-proxy functions when they are returned to their origin side of the bridge', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', (fn: any) => fn);
        });
        const result = await callWithBindings(async (root: any) => {
          const fn = () => null;
          return root.example(fn) === fn;
        });
        expect(result).equal(true);
      });

      it('should properly handle errors thrown in proxied functions', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', () => { throw new Error('oh no'); });
        });
        const result = await callWithBindings(async (root: any) => {
          try {
            root.example();
          } catch (e) {
            return (e as Error).message;
          }
        });
        expect(result).equal('oh no');
      });

      it('should proxy methods that are callable multiple times', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            doThing: () => 123
          });
        });
        const result = await callWithBindings(async (root: any) => {
          return [root.example.doThing(), root.example.doThing(), root.example.doThing()];
        });
        expect(result).to.deep.equal([123, 123, 123]);
      });

      it('should proxy methods in the reverse direction', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            callWithNumber: (fn: any) => fn(123)
          });
        });
        const result = await callWithBindings(async (root: any) => {
          return root.example.callWithNumber((n: number) => n + 1);
        });
        expect(result).to.equal(124);
      });

      it('should proxy promises in the reverse direction', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            getPromiseValue: (p: Promise<any>) => p
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.getPromiseValue(Promise.resolve('my-proxied-value'));
        });
        expect(result).to.equal('my-proxied-value');
      });

      it('should proxy objects with number keys', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            1: 123,
            2: 456,
            3: 789
          });
        });
        const result = await callWithBindings(async (root: any) => {
          return [root.example[1], root.example[2], root.example[3], Array.isArray(root.example)];
        });
        expect(result).to.deep.equal([123, 456, 789, false]);
      });

      it('it should proxy null', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', null);
        });
        const result = await callWithBindings((root: any) => {
          // Convert to strings as although the context bridge keeps the right value
          // IPC does not
          return `${root.example}`;
        });
        expect(result).to.deep.equal('null');
      });

      it('it should proxy undefined', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', undefined);
        });
        const result = await callWithBindings((root: any) => {
          // Convert to strings as although the context bridge keeps the right value
          // IPC does not
          return `${root.example}`;
        });
        expect(result).to.deep.equal('undefined');
      });

      it('it should proxy nested null and undefined correctly', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            values: [null, undefined]
          });
        });
        const result = await callWithBindings((root: any) => {
          // Convert to strings as although the context bridge keeps the right value
          // IPC does not
          return root.example.values.map((val: any) => `${val}`);
        });
        expect(result).to.deep.equal(['null', 'undefined']);
      });

      it('should proxy symbols', async () => {
        await makeBindingWindow(() => {
          const mySymbol = Symbol('unique');
          const isSymbol = (s: Symbol) => s === mySymbol;
          contextBridge.exposeInMainWorld('symbol', mySymbol);
          contextBridge.exposeInMainWorld('isSymbol', isSymbol);
        });
        const result = await callWithBindings((root: any) => {
          return root.isSymbol(root.symbol);
        });
        expect(result).to.equal(true, 'symbols should be equal across contexts');
      });

      it('should proxy symbols such that symbol equality works', async () => {
        await makeBindingWindow(() => {
          const mySymbol = Symbol('unique');
          contextBridge.exposeInMainWorld('example', {
            getSymbol: () => mySymbol,
            isSymbol: (s: Symbol) => s === mySymbol
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.isSymbol(root.example.getSymbol());
        });
        expect(result).to.equal(true, 'symbols should be equal across contexts');
      });

      it('should proxy symbols such that symbol key lookup works', async () => {
        await makeBindingWindow(() => {
          const mySymbol = Symbol('unique');
          contextBridge.exposeInMainWorld('example', {
            getSymbol: () => mySymbol,
            getObject: () => ({ [mySymbol]: 123 })
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.getObject()[root.example.getSymbol()];
        });
        expect(result).to.equal(123, 'symbols key lookup should work across contexts');
      });

      it('should proxy typed arrays', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', new Uint8Array(100));
        });
        const result = await callWithBindings((root: any) => {
          return Object.getPrototypeOf(root.example) === Uint8Array.prototype;
        });
        expect(result).equal(true);
      });

      it('should proxy regexps', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', /a/g);
        });
        const result = await callWithBindings((root: any) => {
          return Object.getPrototypeOf(root.example) === RegExp.prototype;
        });
        expect(result).equal(true);
      });

      it('should proxy typed arrays and regexps through the serializer', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            arr: new Uint8Array(100),
            regexp: /a/g
          });
        });
        const result = await callWithBindings((root: any) => {
          return [
            Object.getPrototypeOf(root.example.arr) === Uint8Array.prototype,
            Object.getPrototypeOf(root.example.regexp) === RegExp.prototype
          ];
        });
        expect(result).to.deep.equal([true, true]);
      });

      it('should handle recursive objects', async () => {
        await makeBindingWindow(() => {
          const o: any = { value: 135 };
          o.o = o;
          contextBridge.exposeInMainWorld('example', {
            o
          });
        });
        const result = await callWithBindings((root: any) => {
          return [root.example.o.value, root.example.o.o.value, root.example.o.o.o.value];
        });
        expect(result).to.deep.equal([135, 135, 135]);
      });

      it('should handle DOM elements', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            getElem: () => document.body
          });
        });
        const result = await callWithBindings((root: any) => {
          return [root.example.getElem().tagName, root.example.getElem().constructor.name, typeof root.example.getElem().querySelector];
        });
        expect(result).to.deep.equal(['BODY', 'HTMLBodyElement', 'function']);
      });

      it('should handle DOM elements going backwards over the bridge', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            getElemInfo: (fn: Function) => {
              const elem = fn();
              return [elem.tagName, elem.constructor.name, typeof elem.querySelector];
            }
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.getElemInfo(() => document.body);
        });
        expect(result).to.deep.equal(['BODY', 'HTMLBodyElement', 'function']);
      });

      it('should handle Blobs', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            getBlob: () => new Blob(['ab', 'cd'])
          });
        });
        const result = await callWithBindings(async (root: any) => {
          return [await root.example.getBlob().text()];
        });
        expect(result).to.deep.equal(['abcd']);
      });

      it('should handle Blobs going backwards over the bridge', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            getBlobText: async (fn: Function) => {
              const blob = fn();
              return [await blob.text()];
            }
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.getBlobText(() => new Blob(['12', '45']));
        });
        expect(result).to.deep.equal(['1245']);
      });

      // Can only run tests which use the GCRunner in non-sandboxed environments
      if (!useSandbox) {
        it('should release the global hold on methods sent across contexts', async () => {
          await makeBindingWindow(() => {
            const trackedValues: WeakRef<object>[] = [];
            require('electron').ipcRenderer.on('get-gc-info', e => e.sender.send('gc-info', { trackedValues: trackedValues.filter(value => value.deref()).length }));
            contextBridge.exposeInMainWorld('example', {
              getFunction: () => () => 123,
              track: (value: object) => { trackedValues.push(new WeakRef(value)); }
            });
          });
          await callWithBindings(async (root: any) => {
            root.GCRunner.run();
          });
          expect((await getGCInfo()).trackedValues).to.equal(0);
          await callWithBindings(async (root: any) => {
            const fn = root.example.getFunction();
            root.example.track(fn);
            root.x = [fn];
          });
          expect((await getGCInfo()).trackedValues).to.equal(1);
          await callWithBindings(async (root: any) => {
            root.x = [];
            root.GCRunner.run();
          });
          expect((await getGCInfo()).trackedValues).to.equal(0);
        });
      }

      if (useSandbox) {
        it('should not leak the global hold on methods sent across contexts when reloading a sandboxed renderer', async () => {
          await makeBindingWindow(() => {
            const trackedValues: WeakRef<object>[] = [];
            require('electron').ipcRenderer.on('get-gc-info', e => e.sender.send('gc-info', { trackedValues: trackedValues.filter(value => value.deref()).length }));
            contextBridge.exposeInMainWorld('example', {
              getFunction: () => () => 123,
              track: (value: object) => { trackedValues.push(new WeakRef(value)); }
            });
            require('electron').ipcRenderer.send('window-ready-for-tasking');
          });
          const loadPromise = once(ipcMain, 'window-ready-for-tasking');
          expect((await getGCInfo()).trackedValues).to.equal(0);
          await callWithBindings((root: any) => {
            root.example.track(root.example.getFunction());
          });
          expect((await getGCInfo()).trackedValues).to.equal(1);
          await callWithBindings((root: any) => {
            root.location.reload();
          });
          await loadPromise;
          await forceGCOnWindow();
          // If this is ever "2" it means we leaked the exposed function and
          // therefore the entire context after a reload
          expect((await getGCInfo()).trackedValues).to.equal(0);
        });
      }

      it('it should not let you overwrite existing exposed things', async () => {
        await makeBindingWindow(() => {
          let threw = false;
          contextBridge.exposeInMainWorld('example', {
            attempt: 1,
            getThrew: () => threw
          });
          try {
            contextBridge.exposeInMainWorld('example', {
              attempt: 2,
              getThrew: () => threw
            });
          } catch {
            threw = true;
          }
        });
        const result = await callWithBindings((root: any) => {
          return [root.example.attempt, root.example.getThrew()];
        });
        expect(result).to.deep.equal([1, true]);
      });

      it('should work with complex nested methods and promises', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            first: (second: Function) => second((fourth: Function) => {
              return fourth();
            })
          });
        });
        const result = await callWithBindings((root: any) => {
          return root.example.first((third: Function) => {
            return third(() => Promise.resolve('final value'));
          });
        });
        expect(result).to.equal('final value');
      });

      it('should work with complex nested methods and promises attached directly to the global', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example',
            (second: Function) => second((fourth: Function) => {
              return fourth();
            })
          );
        });
        const result = await callWithBindings((root: any) => {
          return root.example((third: Function) => {
            return third(() => Promise.resolve('final value'));
          });
        });
        expect(result).to.equal('final value');
      });

      it('should throw an error when recursion depth is exceeded', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            doThing: (a: any) => console.log(a)
          });
        });
        let threw = await callWithBindings((root: any) => {
          try {
            let a: any = [];
            for (let i = 0; i < 999; i++) {
              a = [a];
            }
            root.example.doThing(a);
            return false;
          } catch {
            return true;
          }
        });
        expect(threw).to.equal(false);
        threw = await callWithBindings((root: any) => {
          try {
            let a: any = [];
            for (let i = 0; i < 1000; i++) {
              a = [a];
            }
            root.example.doThing(a);
            return false;
          } catch {
            return true;
          }
        });
        expect(threw).to.equal(true);
      });

      it('should copy thrown errors into the other context', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            throwNormal: () => {
              throw new Error('whoops');
            },
            throwWeird: () => {
              throw 'this is no error...'; // eslint-disable-line no-throw-literal
            },
            throwNotClonable: () => {
              return Object(Symbol('foo'));
            },
            throwNotClonableNestedArray: () => {
              return [Object(Symbol('foo'))];
            },
            throwNotClonableNestedObject: () => {
              return {
                bad: Object(Symbol('foo'))
              };
            },
            throwDynamic: () => {
              return {
                get bad () {
                  throw new Error('damm');
                }
              };
            },
            argumentConvert: () => {},
            rejectNotClonable: async () => {
              throw Object(Symbol('foo'));
            },
            resolveNotClonable: async () => Object(Symbol('foo'))
          });
        });
        const result = await callWithBindings(async (root: any) => {
          const getError = (fn: Function) => {
            try {
              fn();
            } catch (e) {
              return e;
            }
            return null;
          };
          const getAsyncError = async (fn: Function) => {
            try {
              await fn();
            } catch (e) {
              return e;
            }
            return null;
          };
          const normalIsError = Object.getPrototypeOf(getError(root.example.throwNormal)) === Error.prototype;
          const weirdIsError = Object.getPrototypeOf(getError(root.example.throwWeird)) === Error.prototype;
          const notClonableIsError = Object.getPrototypeOf(getError(root.example.throwNotClonable)) === Error.prototype;
          const notClonableNestedArrayIsError = Object.getPrototypeOf(getError(root.example.throwNotClonableNestedArray)) === Error.prototype;
          const notClonableNestedObjectIsError = Object.getPrototypeOf(getError(root.example.throwNotClonableNestedObject)) === Error.prototype;
          const dynamicIsError = Object.getPrototypeOf(getError(root.example.throwDynamic)) === Error.prototype;
          const argumentConvertIsError = Object.getPrototypeOf(getError(() => root.example.argumentConvert(Object(Symbol('test'))))) === Error.prototype;
          const rejectNotClonableIsError = Object.getPrototypeOf(await getAsyncError(root.example.rejectNotClonable)) === Error.prototype;
          const resolveNotClonableIsError = Object.getPrototypeOf(await getAsyncError(root.example.resolveNotClonable)) === Error.prototype;
          return [normalIsError, weirdIsError, notClonableIsError, notClonableNestedArrayIsError, notClonableNestedObjectIsError, dynamicIsError, argumentConvertIsError, rejectNotClonableIsError, resolveNotClonableIsError];
        });
        expect(result).to.deep.equal([true, true, true, true, true, true, true, true, true], 'should all be errors in the current context');
      });

      it('should not leak prototypes', async () => {
        await makeBindingWindow(() => {
          contextBridge.exposeInMainWorld('example', {
            number: 123,
            string: 'string',
            boolean: true,
            arr: [123, 'string', true, ['foo']],
            symbol: Symbol('foo'),
            bigInt: 10n,
            getObject: () => ({ thing: 123 }),
            getNumber: () => 123,
            getString: () => 'string',
            getBoolean: () => true,
            getArr: () => [123, 'string', true, ['foo']],
            getPromise: async () => ({ number: 123, string: 'string', boolean: true, fn: () => 'string', arr: [123, 'string', true, ['foo']] }),
            getFunctionFromFunction: async () => () => null,
            object: {
              number: 123,
              string: 'string',
              boolean: true,
              arr: [123, 'string', true, ['foo']],
              getPromise: async () => ({ number: 123, string: 'string', boolean: true, fn: () => 'string', arr: [123, 'string', true, ['foo']] })
            },
            receiveArguments: (fn: any) => fn({ key: 'value' }),
            symbolKeyed: {
              [Symbol('foo')]: 123
            },
            getBody: () => document.body,
            getBlob: () => new Blob(['ab', 'cd'])
          });
        });
        const result = await callWithBindings(async (root: any) => {
          const { example } = root;
          let arg: any;
          example.receiveArguments((o: any) => { arg = o; });
          const protoChecks = [
            ...Object.keys(example).map(key => [key, String]),
            ...Object.getOwnPropertySymbols(example.symbolKeyed).map(key => [key, Symbol]),
            [example, Object],
            [example.number, Number],
            [example.string, String],
            [example.boolean, Boolean],
            [example.arr, Array],
            [example.arr[0], Number],
            [example.arr[1], String],
            [example.arr[2], Boolean],
            [example.arr[3], Array],
            [example.arr[3][0], String],
            [example.symbol, Symbol],
            [example.bigInt, BigInt],
            [example.getNumber, Function],
            [example.getNumber(), Number],
            [example.getObject(), Object],
            [example.getString(), String],
            [example.getBoolean(), Boolean],
            [example.getArr(), Array],
            [example.getArr()[0], Number],
            [example.getArr()[1], String],
            [example.getArr()[2], Boolean],
            [example.getArr()[3], Array],
            [example.getArr()[3][0], String],
            [example.getFunctionFromFunction, Function],
            [example.getFunctionFromFunction(), Promise],
            [await example.getFunctionFromFunction(), Function],
            [example.getPromise(), Promise],
            [await example.getPromise(), Object],
            [(await example.getPromise()).number, Number],
            [(await example.getPromise()).string, String],
            [(await example.getPromise()).boolean, Boolean],
            [(await example.getPromise()).fn, Function],
            [(await example.getPromise()).fn(), String],
            [(await example.getPromise()).arr, Array],
            [(await example.getPromise()).arr[0], Number],
            [(await example.getPromise()).arr[1], String],
            [(await example.getPromise()).arr[2], Boolean],
            [(await example.getPromise()).arr[3], Array],
            [(await example.getPromise()).arr[3][0], String],
            [example.object, Object],
            [example.object.number, Number],
            [example.object.string, String],
            [example.object.boolean, Boolean],
            [example.object.arr, Array],
            [example.object.arr[0], Number],
            [example.object.arr[1], String],
            [example.object.arr[2], Boolean],
            [example.object.arr[3], Array],
            [example.object.arr[3][0], String],
            [await example.object.getPromise(), Object],
            [(await example.object.getPromise()).number, Number],
            [(await example.object.getPromise()).string, String],
            [(await example.object.getPromise()).boolean, Boolean],
            [(await example.object.getPromise()).fn, Function],
            [(await example.object.getPromise()).fn(), String],
            [(await example.object.getPromise()).arr, Array],
            [(await example.object.getPromise()).arr[0], Number],
            [(await example.object.getPromise()).arr[1], String],
            [(await example.object.getPromise()).arr[2], Boolean],
            [(await example.object.getPromise()).arr[3], Array],
            [(await example.object.getPromise()).arr[3][0], String],
            [arg, Object],
            [arg.key, String],
            [example.getBody(), HTMLBodyElement],
            [example.getBlob(), Blob]
          ];
          return {
            protoMatches: protoChecks.map(([a, Constructor]) => Object.getPrototypeOf(a) === Constructor.prototype)
          };
        });
        // Every protomatch should be true
        expect(result.protoMatches).to.deep.equal(result.protoMatches.map(() => true));
      });

      it('should not leak prototypes when attaching directly to the global', async () => {
        await makeBindingWindow(() => {
          const toExpose = {
            number: 123,
            string: 'string',
            boolean: true,
            arr: [123, 'string', true, ['foo']],
            symbol: Symbol('foo'),
            bigInt: 10n,
            getObject: () => ({ thing: 123 }),
            getNumber: () => 123,
            getString: () => 'string',
            getBoolean: () => true,
            getArr: () => [123, 'string', true, ['foo']],
            getPromise: async () => ({ number: 123, string: 'string', boolean: true, fn: () => 'string', arr: [123, 'string', true, ['foo']] }),
            getFunctionFromFunction: async () => () => null,
            getError: () => new Error('foo'),
            getWeirdError: () => {
              const e = new Error('foo');
              e.message = { garbage: true } as any;
              return e;
            },
            object: {
              number: 123,
              string: 'string',
              boolean: true,
              arr: [123, 'string', true, ['foo']],
              getPromise: async () => ({ number: 123, string: 'string', boolean: true, fn: () => 'string', arr: [123, 'string', true, ['foo']] })
            },
            receiveArguments: (fn: any) => fn({ key: 'value' }),
            symbolKeyed: {
              [Symbol('foo')]: 123
            }
          };
          const keys: string[] = [];
          for (const [key, value] of Object.entries(toExpose)) {
            keys.push(key);
            contextBridge.exposeInMainWorld(key, value);
          }
          contextBridge.exposeInMainWorld('keys', keys);
        });
        const result = await callWithBindings(async (root: any) => {
          const { keys } = root;
          const cleanedRoot: any = {};
          for (const [key, value] of Object.entries(root)) {
            if (keys.includes(key)) {
              cleanedRoot[key] = value;
            }
          }

          let arg: any;
          cleanedRoot.receiveArguments((o: any) => { arg = o; });
          const protoChecks = [
            ...Object.keys(cleanedRoot).map(key => [key, String]),
            ...Object.getOwnPropertySymbols(cleanedRoot.symbolKeyed).map(key => [key, Symbol]),
            [cleanedRoot, Object],
            [cleanedRoot.number, Number],
            [cleanedRoot.string, String],
            [cleanedRoot.boolean, Boolean],
            [cleanedRoot.arr, Array],
            [cleanedRoot.arr[0], Number],
            [cleanedRoot.arr[1], String],
            [cleanedRoot.arr[2], Boolean],
            [cleanedRoot.arr[3], Array],
            [cleanedRoot.arr[3][0], String],
            [cleanedRoot.symbol, Symbol],
            [cleanedRoot.bigInt, BigInt],
            [cleanedRoot.getNumber, Function],
            [cleanedRoot.getNumber(), Number],
            [cleanedRoot.getObject(), Object],
            [cleanedRoot.getString(), String],
            [cleanedRoot.getBoolean(), Boolean],
            [cleanedRoot.getArr(), Array],
            [cleanedRoot.getArr()[0], Number],
            [cleanedRoot.getArr()[1], String],
            [cleanedRoot.getArr()[2], Boolean],
            [cleanedRoot.getArr()[3], Array],
            [cleanedRoot.getArr()[3][0], String],
            [cleanedRoot.getFunctionFromFunction, Function],
            [cleanedRoot.getFunctionFromFunction(), Promise],
            [await cleanedRoot.getFunctionFromFunction(), Function],
            [cleanedRoot.getError(), Error],
            [cleanedRoot.getError().message, String],
            [cleanedRoot.getWeirdError(), Error],
            [cleanedRoot.getWeirdError().message, String],
            [cleanedRoot.getPromise(), Promise],
            [await cleanedRoot.getPromise(), Object],
            [(await cleanedRoot.getPromise()).number, Number],
            [(await cleanedRoot.getPromise()).string, String],
            [(await cleanedRoot.getPromise()).boolean, Boolean],
            [(await cleanedRoot.getPromise()).fn, Function],
            [(await cleanedRoot.getPromise()).fn(), String],
            [(await cleanedRoot.getPromise()).arr, Array],
            [(await cleanedRoot.getPromise()).arr[0], Number],
            [(await cleanedRoot.getPromise()).arr[1], String],
            [(await cleanedRoot.getPromise()).arr[2], Boolean],
            [(await cleanedRoot.getPromise()).arr[3], Array],
            [(await cleanedRoot.getPromise()).arr[3][0], String],
            [cleanedRoot.object, Object],
            [cleanedRoot.object.number, Number],
            [cleanedRoot.object.string, String],
            [cleanedRoot.object.boolean, Boolean],
            [cleanedRoot.object.arr, Array],
            [cleanedRoot.object.arr[0], Number],
            [cleanedRoot.object.arr[1], String],
            [cleanedRoot.object.arr[2], Boolean],
            [cleanedRoot.object.arr[3], Array],
            [cleanedRoot.object.arr[3][0], String],
            [await cleanedRoot.object.getPromise(), Object],
            [(await cleanedRoot.object.getPromise()).number, Number],
            [(await cleanedRoot.object.getPromise()).string, String],
            [(await cleanedRoot.object.getPromise()).boolean, Boolean],
            [(await cleanedRoot.object.getPromise()).fn, Function],
            [(await cleanedRoot.object.getPromise()).fn(), String],
            [(await cleanedRoot.object.getPromise()).arr, Array],
            [(await cleanedRoot.object.getPromise()).arr[0], Number],
            [(await cleanedRoot.object.getPromise()).arr[1], String],
            [(await cleanedRoot.object.getPromise()).arr[2], Boolean],
            [(await cleanedRoot.object.getPromise()).arr[3], Array],
            [(await cleanedRoot.object.getPromise()).arr[3][0], String],
            [arg, Object],
            [arg.key, String]
          ];
          return {
            protoMatches: protoChecks.map(([a, Constructor]) => Object.getPrototypeOf(a) === Constructor.prototype)
          };
        });
        // Every protomatch should be true
        expect(result.protoMatches).to.deep.equal(result.protoMatches.map(() => true));
      });

      describe('internalContextBridge', () => {
        describe('overrideGlobalValueFromIsolatedWorld', () => {
          it('should override top level properties', async () => {
            await makeBindingWindow(() => {
              contextBridge.internalContextBridge!.overrideGlobalValueFromIsolatedWorld(['open'], () => ({ you: 'are a wizard' }));
            });
            const result = await callWithBindings(async (root: any) => {
              return root.open();
            });
            expect(result).to.deep.equal({ you: 'are a wizard' });
          });

          it('should override deep properties', async () => {
            await makeBindingWindow(() => {
              contextBridge.internalContextBridge!.overrideGlobalValueFromIsolatedWorld(['document', 'foo'], () => 'I am foo');
            });
            const result = await callWithBindings(async (root: any) => {
              return root.document.foo();
            });
            expect(result).to.equal('I am foo');
          });
        });

        describe('overrideGlobalPropertyFromIsolatedWorld', () => {
          it('should call the getter correctly', async () => {
            await makeBindingWindow(() => {
              let callCount = 0;
              const getter = () => {
                callCount++;
                return true;
              };
              contextBridge.internalContextBridge!.overrideGlobalPropertyFromIsolatedWorld(['isFun'], getter);
              contextBridge.exposeInMainWorld('foo', {
                callCount: () => callCount
              });
            });
            const result = await callWithBindings(async (root: any) => {
              return [root.isFun, root.foo.callCount()];
            });
            expect(result[0]).to.equal(true);
            expect(result[1]).to.equal(1);
          });

          it('should not make a setter if none is provided', async () => {
            await makeBindingWindow(() => {
              contextBridge.internalContextBridge!.overrideGlobalPropertyFromIsolatedWorld(['isFun'], () => true);
            });
            const result = await callWithBindings(async (root: any) => {
              root.isFun = 123;
              return root.isFun;
            });
            expect(result).to.equal(true);
          });

          it('should call the setter correctly', async () => {
            await makeBindingWindow(() => {
              const callArgs: any[] = [];
              const setter = (...args: any[]) => {
                callArgs.push(args);
                return true;
              };
              contextBridge.internalContextBridge!.overrideGlobalPropertyFromIsolatedWorld(['isFun'], () => true, setter);
              contextBridge.exposeInMainWorld('foo', {
                callArgs: () => callArgs
              });
            });
            const result = await callWithBindings(async (root: any) => {
              root.isFun = 123;
              return root.foo.callArgs();
            });
            expect(result).to.have.lengthOf(1);
            expect(result[0]).to.have.lengthOf(1);
            expect(result[0][0]).to.equal(123);
          });
        });

        describe('overrideGlobalValueWithDynamicPropsFromIsolatedWorld', () => {
          it('should not affect normal values', async () => {
            await makeBindingWindow(() => {
              contextBridge.internalContextBridge!.overrideGlobalValueWithDynamicPropsFromIsolatedWorld(['thing'], {
                a: 123,
                b: () => 2,
                c: () => ({ d: 3 })
              });
            });
            const result = await callWithBindings(async (root: any) => {
              return [root.thing.a, root.thing.b(), root.thing.c()];
            });
            expect(result).to.deep.equal([123, 2, { d: 3 }]);
          });

          it('should work with getters', async () => {
            await makeBindingWindow(() => {
              contextBridge.internalContextBridge!.overrideGlobalValueWithDynamicPropsFromIsolatedWorld(['thing'], {
                get foo () {
                  return 'hi there';
                }
              });
            });
            const result = await callWithBindings(async (root: any) => {
              return root.thing.foo;
            });
            expect(result).to.equal('hi there');
          });

          it('should work with nested getters', async () => {
            await makeBindingWindow(() => {
              contextBridge.internalContextBridge!.overrideGlobalValueWithDynamicPropsFromIsolatedWorld(['thing'], {
                get foo () {
                  return {
                    get bar () {
                      return 'hi there';
                    }
                  };
                }
              });
            });
            const result = await callWithBindings(async (root: any) => {
              return root.thing.foo.bar;
            });
            expect(result).to.equal('hi there');
          });

          it('should work with setters', async () => {
            await makeBindingWindow(() => {
              let a: any = null;
              contextBridge.internalContextBridge!.overrideGlobalValueWithDynamicPropsFromIsolatedWorld(['thing'], {
                get foo () {
                  return a;
                },
                set foo (arg: any) {
                  a = arg + 1;
                }
              });
            });
            const result = await callWithBindings(async (root: any) => {
              root.thing.foo = 123;
              return root.thing.foo;
            });
            expect(result).to.equal(124);
          });

          it('should work with nested getter / setter combos', async () => {
            await makeBindingWindow(() => {
              let a: any = null;
              contextBridge.internalContextBridge!.overrideGlobalValueWithDynamicPropsFromIsolatedWorld(['thing'], {
                get thingy () {
                  return {
                    get foo () {
                      return a;
                    },
                    set foo (arg: any) {
                      a = arg + 1;
                    }
                  };
                }
              });
            });
            const result = await callWithBindings(async (root: any) => {
              root.thing.thingy.foo = 123;
              return root.thing.thingy.foo;
            });
            expect(result).to.equal(124);
          });

          it('should work with deep properties', async () => {
            await makeBindingWindow(() => {
              contextBridge.internalContextBridge!.overrideGlobalValueWithDynamicPropsFromIsolatedWorld(['thing'], {
                a: () => ({
                  get foo () {
                    return 'still here';
                  }
                })
              });
            });
            const result = await callWithBindings(async (root: any) => {
              return root.thing.a().foo;
            });
            expect(result).to.equal('still here');
          });
        });
      });
    });
  };

  generateTests(true);
  generateTests(false);
});

describe('ContextBridgeMutability', () => {
  it('should not make properties unwriteable and read-only if ContextBridgeMutability is on', async () => {
    const appPath = path.join(fixturesPath, 'context-bridge-mutability');
    const appProcess = cp.spawn(process.execPath, ['--enable-logging', '--enable-features=ContextBridgeMutability', appPath]);

    let output = '';
    appProcess.stdout.on('data', data => { output += data; });
    await once(appProcess, 'exit');

    expect(output).to.include('some-modified-text');
    expect(output).to.include('obj-modified-prop');
    expect(output).to.include('1,2,5,3,4');
  });

  it('should make properties unwriteable and read-only if ContextBridgeMutability is off', async () => {
    const appPath = path.join(fixturesPath, 'context-bridge-mutability');
    const appProcess = cp.spawn(process.execPath, ['--enable-logging', appPath]);

    let output = '';
    appProcess.stdout.on('data', data => { output += data; });
    await once(appProcess, 'exit');

    expect(output).to.include('some-text');
    expect(output).to.include('obj-prop');
    expect(output).to.include('1,2,3,4');
  });
});
