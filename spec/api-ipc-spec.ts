import { EventEmitter, once } from 'node:events';
import { expect } from 'chai';
import { BrowserWindow, ipcMain, IpcMainInvokeEvent, MessageChannelMain, WebContents } from 'electron/main';
import { closeAllWindows } from './lib/window-helpers';
import { defer, listen } from './lib/spec-helpers';
import * as path from 'node:path';
import * as http from 'node:http';

const v8Util = process._linkedBinding('electron_common_v8_util');
const fixturesPath = path.resolve(__dirname, 'fixtures');

describe('ipc module', () => {
  describe('invoke', () => {
    let w: BrowserWindow;

    before(async () => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      await w.loadURL('about:blank');
    });
    after(async () => {
      w.destroy();
    });

    async function rendererInvoke (...args: any[]) {
      const { ipcRenderer } = require('electron');
      try {
        const result = await ipcRenderer.invoke('test', ...args);
        ipcRenderer.send('result', { result });
      } catch (e) {
        ipcRenderer.send('result', { error: (e as Error).message });
      }
    }

    it('receives a response from a synchronous handler', async () => {
      ipcMain.handleOnce('test', (e: IpcMainInvokeEvent, arg: number) => {
        expect(arg).to.equal(123);
        return 3;
      });
      const done = new Promise<void>(resolve => ipcMain.once('result', (e, arg) => {
        expect(arg).to.deep.equal({ result: 3 });
        resolve();
      }));
      await w.webContents.executeJavaScript(`(${rendererInvoke})(123)`);
      await done;
    });

    it('receives a response from an asynchronous handler', async () => {
      ipcMain.handleOnce('test', async (e: IpcMainInvokeEvent, arg: number) => {
        expect(arg).to.equal(123);
        await new Promise(setImmediate);
        return 3;
      });
      const done = new Promise<void>(resolve => ipcMain.once('result', (e, arg) => {
        expect(arg).to.deep.equal({ result: 3 });
        resolve();
      }));
      await w.webContents.executeJavaScript(`(${rendererInvoke})(123)`);
      await done;
    });

    it('receives an error from a synchronous handler', async () => {
      ipcMain.handleOnce('test', () => {
        throw new Error('some error');
      });
      const done = new Promise<void>(resolve => ipcMain.once('result', (e, arg) => {
        expect(arg.error).to.match(/some error/);
        resolve();
      }));
      await w.webContents.executeJavaScript(`(${rendererInvoke})()`);
      await done;
    });

    it('receives an error from an asynchronous handler', async () => {
      ipcMain.handleOnce('test', async () => {
        await new Promise(setImmediate);
        throw new Error('some error');
      });
      const done = new Promise<void>(resolve => ipcMain.once('result', (e, arg) => {
        expect(arg.error).to.match(/some error/);
        resolve();
      }));
      await w.webContents.executeJavaScript(`(${rendererInvoke})()`);
      await done;
    });

    it('throws an error if no handler is registered', async () => {
      const done = new Promise<void>(resolve => ipcMain.once('result', (e, arg) => {
        expect(arg.error).to.match(/No handler registered/);
        resolve();
      }));
      await w.webContents.executeJavaScript(`(${rendererInvoke})()`);
      await done;
    });

    it('throws an error when invoking a handler that was removed', async () => {
      ipcMain.handle('test', () => { });
      ipcMain.removeHandler('test');
      const done = new Promise<void>(resolve => ipcMain.once('result', (e, arg) => {
        expect(arg.error).to.match(/No handler registered/);
        resolve();
      }));
      await w.webContents.executeJavaScript(`(${rendererInvoke})()`);
      await done;
    });

    it('forbids multiple handlers', async () => {
      ipcMain.handle('test', () => { });
      try {
        expect(() => { ipcMain.handle('test', () => { }); }).to.throw(/second handler/);
      } finally {
        ipcMain.removeHandler('test');
      }
    });

    it('throws an error in the renderer if the reply callback is dropped', async () => {
      ipcMain.handleOnce('test', () => new Promise(() => {
        setTimeout(() => v8Util.requestGarbageCollectionForTesting());
        /* never resolve */
      }));
      w.webContents.executeJavaScript(`(${rendererInvoke})()`);
      const [, { error }] = await once(ipcMain, 'result');
      expect(error).to.match(/reply was never sent/);
    });
  });

  describe('ordering', () => {
    let w: BrowserWindow;

    before(async () => {
      w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      await w.loadURL('about:blank');
    });
    after(async () => {
      w.destroy();
    });

    it('between send and sendSync is consistent', async () => {
      const received: number[] = [];
      ipcMain.on('test-async', (e, i) => { received.push(i); });
      ipcMain.on('test-sync', (e, i) => { received.push(i); e.returnValue = null; });
      const done = new Promise<void>(resolve => ipcMain.once('done', () => { resolve(); }));
      function rendererStressTest () {
        const { ipcRenderer } = require('electron');
        for (let i = 0; i < 1000; i++) {
          switch ((Math.random() * 2) | 0) {
            case 0:
              ipcRenderer.send('test-async', i);
              break;
            case 1:
              ipcRenderer.sendSync('test-sync', i);
              break;
          }
        }
        ipcRenderer.send('done');
      }
      try {
        w.webContents.executeJavaScript(`(${rendererStressTest})()`);
        await done;
      } finally {
        ipcMain.removeAllListeners('test-async');
        ipcMain.removeAllListeners('test-sync');
      }
      expect(received).to.have.lengthOf(1000);
      expect(received).to.deep.equal([...received].sort((a, b) => a - b));
    });

    it('between send, sendSync, and invoke is consistent', async () => {
      const received: number[] = [];
      ipcMain.handle('test-invoke', (e, i) => { received.push(i); });
      ipcMain.on('test-async', (e, i) => { received.push(i); });
      ipcMain.on('test-sync', (e, i) => { received.push(i); e.returnValue = null; });
      const done = new Promise<void>(resolve => ipcMain.once('done', () => { resolve(); }));
      function rendererStressTest () {
        const { ipcRenderer } = require('electron');
        for (let i = 0; i < 1000; i++) {
          switch ((Math.random() * 3) | 0) {
            case 0:
              ipcRenderer.send('test-async', i);
              break;
            case 1:
              ipcRenderer.sendSync('test-sync', i);
              break;
            case 2:
              ipcRenderer.invoke('test-invoke', i);
              break;
          }
        }
        ipcRenderer.send('done');
      }
      try {
        w.webContents.executeJavaScript(`(${rendererStressTest})()`);
        await done;
      } finally {
        ipcMain.removeHandler('test-invoke');
        ipcMain.removeAllListeners('test-async');
        ipcMain.removeAllListeners('test-sync');
      }
      expect(received).to.have.lengthOf(1000);
      expect(received).to.deep.equal([...received].sort((a, b) => a - b));
    });
  });

  describe('MessagePort', () => {
    afterEach(closeAllWindows);

    it('can send a port to the main process', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      const p = once(ipcMain, 'port');
      await w.webContents.executeJavaScript(`(${function () {
        const channel = new MessageChannel();
        require('electron').ipcRenderer.postMessage('port', 'hi', [channel.port1]);
      }})()`);
      const [ev, msg] = await p;
      expect(msg).to.equal('hi');
      expect(ev.ports).to.have.length(1);
      expect(ev.senderFrame.parent).to.be.null();
      expect(ev.senderFrame.routingId).to.equal(w.webContents.mainFrame.routingId);
      const [port] = ev.ports;
      expect(port).to.be.an.instanceOf(EventEmitter);
    });

    it('can sent a message without a transfer', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      const p = once(ipcMain, 'port');
      await w.webContents.executeJavaScript(`(${function () {
        require('electron').ipcRenderer.postMessage('port', 'hi');
      }})()`);
      const [ev, msg] = await p;
      expect(msg).to.equal('hi');
      expect(ev.ports).to.deep.equal([]);
      expect(ev.senderFrame.routingId).to.equal(w.webContents.mainFrame.routingId);
    });

    it('can communicate between main and renderer', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      const p = once(ipcMain, 'port');
      await w.webContents.executeJavaScript(`(${function () {
        const channel = new MessageChannel();
        channel.port2.onmessage = (ev: any) => {
          channel.port2.postMessage(ev.data * 2);
        };
        require('electron').ipcRenderer.postMessage('port', '', [channel.port1]);
      }})()`);
      const [ev] = await p;
      expect(ev.ports).to.have.length(1);
      expect(ev.senderFrame.routingId).to.equal(w.webContents.mainFrame.routingId);
      const [port] = ev.ports;
      port.start();
      port.postMessage(42);
      const [ev2] = await once(port, 'message');
      expect(ev2.data).to.equal(84);
    });

    it('can receive a port from a renderer over a MessagePort connection', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      function fn () {
        const channel1 = new MessageChannel();
        const channel2 = new MessageChannel();
        channel1.port2.postMessage('', [channel2.port1]);
        channel2.port2.postMessage('matryoshka');
        require('electron').ipcRenderer.postMessage('port', '', [channel1.port1]);
      }
      w.webContents.executeJavaScript(`(${fn})()`);
      const [{ ports: [port1] }] = await once(ipcMain, 'port');
      port1.start();
      const [{ ports: [port2] }] = await once(port1, 'message');
      port2.start();
      const [{ data }] = await once(port2, 'message');
      expect(data).to.equal('matryoshka');
    });

    it('can forward a port from one renderer to another renderer', async () => {
      const w1 = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      const w2 = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w1.loadURL('about:blank');
      w2.loadURL('about:blank');
      w1.webContents.executeJavaScript(`(${function () {
        const channel = new MessageChannel();
        channel.port2.onmessage = (ev: any) => {
          require('electron').ipcRenderer.send('message received', ev.data);
        };
        require('electron').ipcRenderer.postMessage('port', '', [channel.port1]);
      }})()`);
      const [{ ports: [port] }] = await once(ipcMain, 'port');
      await w2.webContents.executeJavaScript(`(${function () {
        require('electron').ipcRenderer.on('port', ({ ports: [port] }: any) => {
          port.postMessage('a message');
        });
      }})()`);
      w2.webContents.postMessage('port', '', [port]);
      const [, data] = await once(ipcMain, 'message received');
      expect(data).to.equal('a message');
    });

    describe('close event', () => {
      describe('in renderer', () => {
        it('is emitted when the main process closes its end of the port', async () => {
          const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
          w.loadURL('about:blank');
          await w.webContents.executeJavaScript(`(${function () {
            const { ipcRenderer } = require('electron');
            ipcRenderer.on('port', e => {
              const [port] = e.ports;
              port.start();
              port.onclose = () => {
                ipcRenderer.send('closed');
              };
            });
          }})()`);
          const { port1, port2 } = new MessageChannelMain();
          w.webContents.postMessage('port', null, [port2]);
          port1.close();
          await once(ipcMain, 'closed');
        });

        it('is emitted when the other end of a port is garbage-collected', async () => {
          const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
          w.loadURL('about:blank');
          await w.webContents.executeJavaScript(`(${async function () {
            const { port2 } = new MessageChannel();
            await new Promise<void>(resolve => {
              port2.start();
              port2.onclose = resolve;
              // @ts-ignore --expose-gc is enabled.
              gc({ type: 'major', execution: 'async' });
            });
          }})()`);
        });

        it('is emitted when the other end of a port is sent to nowhere', async () => {
          const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
          w.loadURL('about:blank');
          ipcMain.once('do-a-gc', () => v8Util.requestGarbageCollectionForTesting());
          await w.webContents.executeJavaScript(`(${async function () {
            const { port1, port2 } = new MessageChannel();
            await new Promise<void>(resolve => {
              port2.start();
              port2.onclose = resolve;
              require('electron').ipcRenderer.postMessage('nobody-listening', null, [port1]);
              require('electron').ipcRenderer.send('do-a-gc');
            });
          }})()`);
        });
      });

      describe('when context destroyed', () => {
        it('does not crash', async () => {
          let count = 0;
          const server = http.createServer((req, res) => {
            switch (req.url) {
              case '/index.html':
                res.setHeader('content-type', 'text/html');
                res.statusCode = 200;
                count = count + 1;
                res.end(`
                  <title>Hello${count}</title>
                  <script>
                  var sharedWorker = new SharedWorker('worker.js');

                  sharedWorker.port.addEventListener('close', function(event) {
                     console.log('close event', event.data);
                  });
                  </script>`);

                break;
              case '/worker.js':
                res.setHeader('content-type', 'application/javascript; charset=UTF-8');
                res.statusCode = 200;
                res.end(`
                  self.addEventListener('connect', function(event) {
                    var port = event.ports[0];

                    port.addEventListener('message', function(event) {
                      console.log('Message from main:', event.data);
                      port.postMessage('Hello from SharedWorker!');
                    });

                  });`);
                break;
              default:
                throw new Error(`unsupported endpoint: ${req.url}`);
            }
          });
          const { port } = await listen(server);
          defer(() => {
            server.close();
          });
          const w = new BrowserWindow({ show: false });
          await w.loadURL(`http://localhost:${port}/index.html`);
          expect(w.webContents.getTitle()).to.equal('Hello1');
          // Before the fix, it would crash if reloaded, but now it doesn't
          await w.loadURL(`http://localhost:${port}/index.html`);
          expect(w.webContents.getTitle()).to.equal('Hello2');
          // const crashEvent = emittedOnce(w.webContents, 'render-process-gone');
          // await crashEvent;
        });
      });
    });

    describe('MessageChannelMain', () => {
      it('can be created', () => {
        const { port1, port2 } = new MessageChannelMain();
        expect(port1).not.to.be.null();
        expect(port2).not.to.be.null();
      });

      it('throws an error when an invalid parameter is sent to postMessage', () => {
        const { port1 } = new MessageChannelMain();

        expect(() => {
          const buffer = new ArrayBuffer(10) as any;
          port1.postMessage(null, [buffer]);
        }).to.throw(/Port at index 0 is not a valid port/);

        expect(() => {
          port1.postMessage(null, ['1' as any]);
        }).to.throw(/Port at index 0 is not a valid port/);

        expect(() => {
          port1.postMessage(null, [new Date() as any]);
        }).to.throw(/Port at index 0 is not a valid port/);
      });

      it('throws when postMessage transferables contains the source port', () => {
        const { port1 } = new MessageChannelMain();

        expect(() => {
          port1.postMessage(null, [port1]);
        }).to.throw(/Port at index 0 contains the source port./);
      });

      it('can send messages within the process', async () => {
        const { port1, port2 } = new MessageChannelMain();
        port2.postMessage('hello');
        port1.start();
        const [ev] = await once(port1, 'message');
        expect(ev.data).to.equal('hello');
      });

      it('can pass one end to a WebContents', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
        w.loadURL('about:blank');
        await w.webContents.executeJavaScript(`(${function () {
          const { ipcRenderer } = require('electron');
          ipcRenderer.on('port', ev => {
            const [port] = ev.ports;
            port.onmessage = () => {
              ipcRenderer.send('done');
            };
          });
        }})()`);
        const { port1, port2 } = new MessageChannelMain();
        port1.postMessage('hello');
        w.webContents.postMessage('port', null, [port2]);
        await once(ipcMain, 'done');
      });

      it('can be passed over another channel', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
        w.loadURL('about:blank');
        await w.webContents.executeJavaScript(`(${function () {
          const { ipcRenderer } = require('electron');
          ipcRenderer.on('port', e1 => {
            e1.ports[0].onmessage = e2 => {
              e2.ports[0].onmessage = e3 => {
                ipcRenderer.send('done', e3.data);
              };
            };
          });
        }})()`);
        const { port1, port2 } = new MessageChannelMain();
        const { port1: port3, port2: port4 } = new MessageChannelMain();
        port1.postMessage(null, [port4]);
        port3.postMessage('hello');
        w.webContents.postMessage('port', null, [port2]);
        const [, message] = await once(ipcMain, 'done');
        expect(message).to.equal('hello');
      });

      it('can send messages to a closed port', () => {
        const { port1, port2 } = new MessageChannelMain();
        port2.start();
        port2.on('message', () => { throw new Error('unexpected message received'); });
        port1.close();
        port1.postMessage('hello');
      });

      it('can send messages to a port whose remote end is closed', () => {
        const { port1, port2 } = new MessageChannelMain();
        port2.start();
        port2.on('message', () => { throw new Error('unexpected message received'); });
        port2.close();
        port1.postMessage('hello');
      });

      it('throws when passing null ports', () => {
        const { port1 } = new MessageChannelMain();
        expect(() => {
          port1.postMessage(null, [null] as any);
        }).to.throw(/Port at index 0 is not a valid port/);
      });

      it('throws when passing duplicate ports', () => {
        const { port1 } = new MessageChannelMain();
        const { port1: port3 } = new MessageChannelMain();
        expect(() => {
          port1.postMessage(null, [port3, port3]);
        }).to.throw(/duplicate/);
      });

      it('throws when passing ports that have already been neutered', () => {
        const { port1 } = new MessageChannelMain();
        const { port1: port3 } = new MessageChannelMain();
        port1.postMessage(null, [port3]);
        expect(() => {
          port1.postMessage(null, [port3]);
        }).to.throw(/already neutered/);
      });

      it('throws when passing itself', () => {
        const { port1 } = new MessageChannelMain();
        expect(() => {
          port1.postMessage(null, [port1]);
        }).to.throw(/contains the source port/);
      });

      describe('GC behavior', () => {
        it('is not collected while it could still receive messages', async () => {
          let trigger: Function;
          const promise = new Promise(resolve => { trigger = resolve; });
          const port1 = (() => {
            const { port1, port2 } = new MessageChannelMain();

            port2.on('message', (e) => { trigger(e.data); });
            port2.start();
            return port1;
          })();
          v8Util.requestGarbageCollectionForTesting();
          port1.postMessage('hello');
          expect(await promise).to.equal('hello');
        });
      });
    });

    const generateTests = (title: string, postMessage: (contents: WebContents) => WebContents['postMessage']) => {
      describe(title, () => {
        it('sends a message', async () => {
          const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
          w.loadURL('about:blank');
          await w.webContents.executeJavaScript(`(${function () {
            const { ipcRenderer } = require('electron');
            ipcRenderer.on('foo', (_e, msg) => {
              ipcRenderer.send('bar', msg);
            });
          }})()`);
          postMessage(w.webContents)('foo', { some: 'message' });
          const [, msg] = await once(ipcMain, 'bar');
          expect(msg).to.deep.equal({ some: 'message' });
        });

        describe('error handling', () => {
          it('throws on missing channel', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            expect(() => {
              (postMessage(w.webContents) as any)();
            }).to.throw(/Insufficient number of arguments/);
          });

          it('throws on invalid channel', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            expect(() => {
              postMessage(w.webContents)(null as any, '', []);
            }).to.throw(/Error processing argument at index 0/);
          });

          it('throws on missing message', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            expect(() => {
              (postMessage(w.webContents) as any)('channel');
            }).to.throw(/Insufficient number of arguments/);
          });

          it('throws on non-serializable message', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            expect(() => {
              postMessage(w.webContents)('channel', w);
            }).to.throw(/An object could not be cloned/);
          });

          it('throws on invalid transferable list', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            expect(() => {
              postMessage(w.webContents)('', '', null as any);
            }).to.throw(/Invalid value for transfer/);
          });

          it('throws on transferring non-transferable', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            expect(() => {
              (postMessage(w.webContents) as any)('channel', '', [123]);
            }).to.throw(/Invalid value for transfer/);
          });

          it('throws when passing null ports', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            expect(() => {
              postMessage(w.webContents)('foo', null, [null] as any);
            }).to.throw(/Invalid value for transfer/);
          });

          it('throws when passing duplicate ports', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            const { port1 } = new MessageChannelMain();
            expect(() => {
              postMessage(w.webContents)('foo', null, [port1, port1]);
            }).to.throw(/duplicate/);
          });

          it('throws when passing ports that have already been neutered', async () => {
            const w = new BrowserWindow({ show: false });
            await w.loadURL('about:blank');
            const { port1 } = new MessageChannelMain();
            postMessage(w.webContents)('foo', null, [port1]);
            expect(() => {
              postMessage(w.webContents)('foo', null, [port1]);
            }).to.throw(/already neutered/);
          });
        });
      });
    };

    generateTests('WebContents.postMessage', contents => contents.postMessage.bind(contents));
    generateTests('WebFrameMain.postMessage', contents => contents.mainFrame.postMessage.bind(contents.mainFrame));
  });

  describe('WebContents.ipc', () => {
    afterEach(closeAllWindows);

    it('receives ipc messages sent from the WebContents', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.send(\'test\', 42)');
      const [, num] = await once(w.webContents.ipc, 'test');
      expect(num).to.equal(42);
    });

    it('receives sync-ipc messages sent from the WebContents', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.ipc.on('test', (event, arg) => {
        event.returnValue = arg * 2;
      });
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.sendSync(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('receives postMessage messages sent from the WebContents, w/ MessagePorts', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.postMessage(\'test\', null, [(new MessageChannel).port1])');
      const [event] = await once(w.webContents.ipc, 'test');
      expect(event.ports.length).to.equal(1);
    });

    it('handles invoke messages sent from the WebContents', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.ipc.handle('test', (_event, arg) => arg * 2);
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.invoke(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('cascades to ipcMain', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      let gotFromIpcMain = false;
      const ipcMainReceived = new Promise<void>(resolve => ipcMain.on('test', () => { gotFromIpcMain = true; resolve(); }));
      const ipcReceived = new Promise<boolean>(resolve => w.webContents.ipc.on('test', () => { resolve(gotFromIpcMain); }));
      defer(() => ipcMain.removeAllListeners('test'));
      w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.send(\'test\', 42)');

      // assert that they are delivered in the correct order
      expect(await ipcReceived).to.be.false();
      await ipcMainReceived;
    });

    it('overrides ipcMain handlers', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.ipc.handle('test', (_event, arg) => arg * 2);
      ipcMain.handle('test', () => { throw new Error('should not be called'); });
      defer(() => ipcMain.removeHandler('test'));
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.invoke(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('falls back to ipcMain handlers', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      ipcMain.handle('test', (_event, arg) => { return arg * 2; });
      defer(() => ipcMain.removeHandler('test'));
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.invoke(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('receives ipcs from child frames', async () => {
      const server = http.createServer((req, res) => {
        res.setHeader('content-type', 'text/html');
        res.end('');
      });
      const { port } = await listen(server);
      defer(() => {
        server.close();
      });
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegrationInSubFrames: true, preload: path.resolve(fixturesPath, 'preload-expose-ipc.js') } });
      // Preloads don't run in about:blank windows, and file:// urls can't be loaded in iframes, so use a blank http page.
      await w.loadURL(`data:text/html,<iframe src="http://localhost:${port}"></iframe>`);
      w.webContents.mainFrame.frames[0].executeJavaScript('ipc.send(\'test\', 42)');
      const [, arg] = await once(w.webContents.ipc, 'test');
      expect(arg).to.equal(42);
    });
  });

  describe('WebFrameMain.ipc', () => {
    afterEach(closeAllWindows);
    it('responds to ipc messages in the main frame', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.send(\'test\', 42)');
      const [, arg] = await once(w.webContents.mainFrame.ipc, 'test');
      expect(arg).to.equal(42);
    });

    it('responds to sync ipc messages in the main frame', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.mainFrame.ipc.on('test', (event, arg) => {
        event.returnValue = arg * 2;
      });
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.sendSync(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('receives postMessage messages sent from the WebContents, w/ MessagePorts', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.postMessage(\'test\', null, [(new MessageChannel).port1])');
      const [event] = await once(w.webContents.mainFrame.ipc, 'test');
      expect(event.ports.length).to.equal(1);
    });

    it('handles invoke messages sent from the WebContents', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.mainFrame.ipc.handle('test', (_event, arg) => arg * 2);
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.invoke(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('cascades to WebContents and ipcMain', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      let gotFromIpcMain = false;
      let gotFromWebContents = false;
      const ipcMainReceived = new Promise<void>(resolve => ipcMain.on('test', () => { gotFromIpcMain = true; resolve(); }));
      const ipcWebContentsReceived = new Promise<boolean>(resolve => w.webContents.ipc.on('test', () => { gotFromWebContents = true; resolve(gotFromIpcMain); }));
      const ipcReceived = new Promise<boolean>(resolve => w.webContents.mainFrame.ipc.on('test', () => { resolve(gotFromWebContents); }));
      defer(() => ipcMain.removeAllListeners('test'));
      w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.send(\'test\', 42)');

      // assert that they are delivered in the correct order
      expect(await ipcReceived).to.be.false();
      expect(await ipcWebContentsReceived).to.be.false();
      await ipcMainReceived;
    });

    it('overrides ipcMain handlers', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.mainFrame.ipc.handle('test', (_event, arg) => arg * 2);
      ipcMain.handle('test', () => { throw new Error('should not be called'); });
      defer(() => ipcMain.removeHandler('test'));
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.invoke(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('overrides WebContents handlers', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.ipc.handle('test', () => { throw new Error('should not be called'); });
      w.webContents.mainFrame.ipc.handle('test', (_event, arg) => arg * 2);
      ipcMain.handle('test', () => { throw new Error('should not be called'); });
      defer(() => ipcMain.removeHandler('test'));
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.invoke(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('falls back to WebContents handlers', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false } });
      w.loadURL('about:blank');
      w.webContents.ipc.handle('test', (_event, arg) => { return arg * 2; });
      const result = await w.webContents.executeJavaScript('require(\'electron\').ipcRenderer.invoke(\'test\', 42)');
      expect(result).to.equal(42 * 2);
    });

    it('receives ipcs from child frames', async () => {
      const server = http.createServer((req, res) => {
        res.setHeader('content-type', 'text/html');
        res.end('');
      });
      const { port } = await listen(server);
      defer(() => {
        server.close();
      });
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegrationInSubFrames: true, preload: path.resolve(fixturesPath, 'preload-expose-ipc.js') } });
      // Preloads don't run in about:blank windows, and file:// urls can't be loaded in iframes, so use a blank http page.
      await w.loadURL(`data:text/html,<iframe src="http://localhost:${port}"></iframe>`);
      w.webContents.mainFrame.frames[0].executeJavaScript('ipc.send(\'test\', 42)');
      w.webContents.mainFrame.ipc.on('test', () => { throw new Error('should not be called'); });
      const [, arg] = await once(w.webContents.mainFrame.frames[0].ipc, 'test');
      expect(arg).to.equal(42);
    });
  });
});
