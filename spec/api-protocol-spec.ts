import { expect } from 'chai';
import { v4 } from 'uuid';
import { protocol, webContents, WebContents, session, BrowserWindow, ipcMain, net } from 'electron/main';
import * as ChildProcess from 'node:child_process';
import * as path from 'node:path';
import * as url from 'node:url';
import * as http from 'node:http';
import * as fs from 'node:fs';
import * as qs from 'node:querystring';
import * as stream from 'node:stream';
import * as streamConsumers from 'node:stream/consumers';
import * as webStream from 'node:stream/web';
import { EventEmitter, once } from 'node:events';
import { closeAllWindows, closeWindow } from './lib/window-helpers';
import { WebmGenerator } from './lib/video-helpers';
import { listen, defer, ifit } from './lib/spec-helpers';
import { setTimeout } from 'node:timers/promises';

const fixturesPath = path.resolve(__dirname, 'fixtures');

const registerStringProtocol = protocol.registerStringProtocol;
const registerBufferProtocol = protocol.registerBufferProtocol;
const registerFileProtocol = protocol.registerFileProtocol;
const registerStreamProtocol = protocol.registerStreamProtocol;
const interceptStringProtocol = protocol.interceptStringProtocol;
const interceptBufferProtocol = protocol.interceptBufferProtocol;
const interceptHttpProtocol = protocol.interceptHttpProtocol;
const interceptStreamProtocol = protocol.interceptStreamProtocol;
const unregisterProtocol = protocol.unregisterProtocol;
const uninterceptProtocol = protocol.uninterceptProtocol;

const text = 'valar morghulis';
const protocolName = 'no-cors';
const postData = {
  name: 'post test',
  type: 'string'
};

function getStream (chunkSize = text.length, data: Buffer | string = text) {
  // allowHalfOpen required, otherwise Readable.toWeb gets confused and thinks
  // the stream isn't done when the readable half ends.
  const body = new stream.PassThrough({ allowHalfOpen: false });

  async function sendChunks () {
    await setTimeout(0); // the stream protocol API breaks if you send data immediately.
    let buf = Buffer.from(data as any); // nodejs typings are wrong, Buffer.from can take a Buffer
    for (;;) {
      body.push(buf.slice(0, chunkSize));
      buf = buf.slice(chunkSize);
      if (!buf.length) {
        break;
      }
      // emulate some network delay
      await setTimeout(10);
    }
    body.push(null);
  }

  sendChunks();
  return body;
}
function getWebStream (chunkSize = text.length, data: Buffer | string = text): ReadableStream<ArrayBufferView> {
  return stream.Readable.toWeb(getStream(chunkSize, data)) as ReadableStream<ArrayBufferView>;
}

// A promise that can be resolved externally.
function deferPromise (): Promise<any> & {resolve: Function, reject: Function} {
  let promiseResolve: Function = null as unknown as Function;
  let promiseReject: Function = null as unknown as Function;
  const promise: any = new Promise((resolve, reject) => {
    promiseResolve = resolve;
    promiseReject = reject;
  });
  promise.resolve = promiseResolve;
  promise.reject = promiseReject;
  return promise;
}

describe('protocol module', () => {
  let contents: WebContents;
  // NB. sandbox: true is used because it makes navigations much (~8x) faster.
  before(() => { contents = (webContents as typeof ElectronInternal.WebContents).create({ sandbox: true }); });
  after(() => contents.destroy());

  async function ajax (url: string, options = {}) {
    // Note that we need to do navigation every time after a protocol is
    // registered or unregistered, otherwise the new protocol won't be
    // recognized by current page when NetworkService is used.
    await contents.loadFile(path.join(__dirname, 'fixtures', 'pages', 'fetch.html'));
    return contents.executeJavaScript(`ajax("${url}", ${JSON.stringify(options)})`);
  }

  afterEach(() => {
    protocol.unregisterProtocol(protocolName);
    protocol.uninterceptProtocol('http');
  });

  describe('protocol.register(Any)Protocol', () => {
    it('fails when scheme is already registered', () => {
      expect(registerStringProtocol(protocolName, (req, cb) => cb(''))).to.equal(true);
      expect(registerBufferProtocol(protocolName, (req, cb) => cb(Buffer.from('')))).to.equal(false);
    });

    it('does not crash when handler is called twice', async () => {
      registerStringProtocol(protocolName, (request, callback) => {
        try {
          callback(text);
          callback('');
        } catch {
          // Ignore error
        }
      });
      const r = await ajax(protocolName + '://fake-host');
      expect(r.data).to.equal(text);
    });

    it('sends error when callback is called with nothing', async () => {
      registerBufferProtocol(protocolName, (req, cb: any) => cb());
      await expect(ajax(protocolName + '://fake-host')).to.eventually.be.rejected();
    });

    it('does not crash when callback is called in next tick', async () => {
      registerStringProtocol(protocolName, (request, callback) => {
        setImmediate(() => callback(text));
      });
      const r = await ajax(protocolName + '://fake-host');
      expect(r.data).to.equal(text);
    });

    it('can redirect to the same scheme', async () => {
      registerStringProtocol(protocolName, (request, callback) => {
        if (request.url === `${protocolName}://fake-host/redirect`) {
          callback({
            statusCode: 302,
            headers: {
              Location: `${protocolName}://fake-host`
            }
          });
        } else {
          expect(request.url).to.equal(`${protocolName}://fake-host`);
          callback('redirected');
        }
      });
      const r = await ajax(`${protocolName}://fake-host/redirect`);
      expect(r.data).to.equal('redirected');
    });
  });

  describe('protocol.unregisterProtocol', () => {
    it('returns false when scheme does not exist', () => {
      expect(unregisterProtocol('not-exist')).to.equal(false);
    });
  });

  for (const [registerStringProtocol, name] of [
    [protocol.registerStringProtocol, 'protocol.registerStringProtocol'] as const,
    [(protocol as any).registerProtocol as typeof protocol.registerStringProtocol, 'protocol.registerProtocol'] as const
  ]) {
    describe(name, () => {
      it('sends string as response', async () => {
        registerStringProtocol(protocolName, (request, callback) => callback(text));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
      });

      it('sets Access-Control-Allow-Origin', async () => {
        registerStringProtocol(protocolName, (request, callback) => callback(text));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
        expect(r.headers).to.have.property('access-control-allow-origin', '*');
      });

      it('sends object as response', async () => {
        registerStringProtocol(protocolName, (request, callback) => {
          callback({
            data: text,
            mimeType: 'text/html'
          });
        });
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
      });

      it('fails when sending object other than string', async () => {
        const notAString = () => {};
        registerStringProtocol(protocolName, (request, callback) => callback(notAString as any));
        await expect(ajax(protocolName + '://fake-host')).to.be.eventually.rejected();
      });
    });
  }

  for (const [registerBufferProtocol, name] of [
    [protocol.registerBufferProtocol, 'protocol.registerBufferProtocol'] as const,
    [(protocol as any).registerProtocol as typeof protocol.registerBufferProtocol, 'protocol.registerProtocol'] as const
  ]) {
    describe(name, () => {
      const buffer = Buffer.from(text);
      it('sends Buffer as response', async () => {
        registerBufferProtocol(protocolName, (request, callback) => callback(buffer));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
      });

      it('sets Access-Control-Allow-Origin', async () => {
        registerBufferProtocol(protocolName, (request, callback) => callback(buffer));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
        expect(r.headers).to.have.property('access-control-allow-origin', '*');
      });

      it('sends object as response', async () => {
        registerBufferProtocol(protocolName, (request, callback) => {
          callback({
            data: buffer,
            mimeType: 'text/html'
          });
        });
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
      });

      if (name !== 'protocol.registerProtocol') {
        it('fails when sending string', async () => {
          registerBufferProtocol(protocolName, (request, callback) => callback(text as any));
          await expect(ajax(protocolName + '://fake-host')).to.be.eventually.rejected();
        });
      }
    });
  }

  for (const [registerFileProtocol, name] of [
    [protocol.registerFileProtocol, 'protocol.registerFileProtocol'] as const,
    [(protocol as any).registerProtocol as typeof protocol.registerFileProtocol, 'protocol.registerProtocol'] as const
  ]) {
    describe(name, () => {
      const filePath = path.join(fixturesPath, 'test.asar', 'a.asar', 'file1');
      const fileContent = fs.readFileSync(filePath);
      const normalPath = path.join(fixturesPath, 'pages', 'a.html');
      const normalContent = fs.readFileSync(normalPath);

      afterEach(closeAllWindows);

      if (name === 'protocol.registerFileProtocol') {
        it('sends file path as response', async () => {
          registerFileProtocol(protocolName, (request, callback) => callback(filePath));
          const r = await ajax(protocolName + '://fake-host');
          expect(r.data).to.equal(String(fileContent));
        });
      }

      it('sets Access-Control-Allow-Origin', async () => {
        registerFileProtocol(protocolName, (request, callback) => callback({ path: filePath }));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(String(fileContent));
        expect(r.headers).to.have.property('access-control-allow-origin', '*');
      });

      it('sets custom headers', async () => {
        registerFileProtocol(protocolName, (request, callback) => callback({
          path: filePath,
          headers: { 'X-Great-Header': 'sogreat' }
        }));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(String(fileContent));
        expect(r.headers).to.have.property('x-great-header', 'sogreat');
      });

      it('can load iframes with custom protocols', async () => {
        registerFileProtocol('custom', (request, callback) => {
          const filename = request.url.substring(9);
          const p = path.join(__dirname, 'fixtures', 'pages', filename);
          callback({ path: p });
        });

        const w = new BrowserWindow({
          show: false,
          webPreferences: {
            nodeIntegration: true,
            contextIsolation: false
          }
        });

        const loaded = once(ipcMain, 'loaded-iframe-custom-protocol');
        w.loadFile(path.join(__dirname, 'fixtures', 'pages', 'iframe-protocol.html'));
        await loaded;
      });

      it('sends object as response', async () => {
        registerFileProtocol(protocolName, (request, callback) => callback({ path: filePath }));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(String(fileContent));
      });

      it('can send normal file', async () => {
        registerFileProtocol(protocolName, (request, callback) => callback({ path: normalPath }));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(String(normalContent));
      });

      it('fails when sending unexist-file', async () => {
        const fakeFilePath = path.join(fixturesPath, 'test.asar', 'a.asar', 'not-exist');
        registerFileProtocol(protocolName, (request, callback) => callback({ path: fakeFilePath }));
        await expect(ajax(protocolName + '://fake-host')).to.be.eventually.rejected();
      });

      it('fails when sending unsupported content', async () => {
        registerFileProtocol(protocolName, (request, callback) => callback(new Date() as any));
        await expect(ajax(protocolName + '://fake-host')).to.be.eventually.rejected();
      });
    });
  }

  for (const [registerHttpProtocol, name] of [
    [protocol.registerHttpProtocol, 'protocol.registerHttpProtocol'] as const,
    [(protocol as any).registerProtocol as typeof protocol.registerHttpProtocol, 'protocol.registerProtocol'] as const
  ]) {
    describe(name, () => {
      it('sends url as response', async () => {
        const server = http.createServer((req, res) => {
          expect(req.headers.accept).to.not.equal('');
          res.end(text);
          server.close();
        });
        const { url } = await listen(server);

        registerHttpProtocol(protocolName, (request, callback) => callback({ url }));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
      });

      it('fails when sending invalid url', async () => {
        registerHttpProtocol(protocolName, (request, callback) => callback({ url: 'url' }));
        await expect(ajax(protocolName + '://fake-host')).to.be.eventually.rejected();
      });

      it('fails when sending unsupported content', async () => {
        registerHttpProtocol(protocolName, (request, callback) => callback(new Date() as any));
        await expect(ajax(protocolName + '://fake-host')).to.be.eventually.rejected();
      });

      it('works when target URL redirects', async () => {
        const server = http.createServer((req, res) => {
          if (req.url === '/serverRedirect') {
            res.statusCode = 301;
            res.setHeader('Location', `http://${req.rawHeaders[1]}`);
            res.end();
          } else {
            res.end(text);
          }
        });
        after(() => server.close());
        const { port } = await listen(server);
        const url = `${protocolName}://fake-host`;
        const redirectURL = `http://127.0.0.1:${port}/serverRedirect`;
        registerHttpProtocol(protocolName, (request, callback) => callback({ url: redirectURL }));

        const r = await ajax(url);
        expect(r.data).to.equal(text);
      });

      it('can access request headers', (done) => {
        protocol.registerHttpProtocol(protocolName, (request) => {
          try {
            expect(request).to.have.property('headers');
            done();
          } catch (e) {
            done(e);
          }
        });
        ajax(protocolName + '://fake-host').catch(() => {});
      });
    });
  }

  for (const [registerStreamProtocol, name] of [
    [protocol.registerStreamProtocol, 'protocol.registerStreamProtocol'] as const,
    [(protocol as any).registerProtocol as typeof protocol.registerStreamProtocol, 'protocol.registerProtocol'] as const
  ]) {
    describe(name, () => {
      it('sends Stream as response', async () => {
        registerStreamProtocol(protocolName, (request, callback) => callback(getStream()));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
      });

      it('sends object as response', async () => {
        registerStreamProtocol(protocolName, (request, callback) => callback({ data: getStream() }));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
        expect(r.status).to.equal(200);
      });

      it('sends custom response headers', async () => {
        registerStreamProtocol(protocolName, (request, callback) => callback({
          data: getStream(3),
          headers: {
            'x-electron': ['a', 'b']
          }
        }));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.equal(text);
        expect(r.status).to.equal(200);
        expect(r.headers).to.have.property('x-electron', 'a, b');
      });

      it('sends custom status code', async () => {
        registerStreamProtocol(protocolName, (request, callback) => callback({
          statusCode: 204,
          data: null as any
        }));
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.be.empty('data');
        expect(r.status).to.equal(204);
      });

      it('receives request headers', async () => {
        registerStreamProtocol(protocolName, (request, callback) => {
          callback({
            headers: {
              'content-type': 'application/json'
            },
            data: getStream(5, JSON.stringify(Object.assign({}, request.headers)))
          });
        });
        const r = await ajax(protocolName + '://fake-host', { headers: { 'x-return-headers': 'yes' } });
        expect(JSON.parse(r.data)['x-return-headers']).to.equal('yes');
      });

      it('returns response multiple response headers with the same name', async () => {
        registerStreamProtocol(protocolName, (request, callback) => {
          callback({
            headers: {
              header1: ['value1', 'value2'],
              header2: 'value3'
            },
            data: getStream()
          });
        });
        const r = await ajax(protocolName + '://fake-host');
        // SUBTLE: when the response headers have multiple values it
        // separates values by ", ". When the response headers are incorrectly
        // converting an array to a string it separates values by ",".
        expect(r.headers).to.have.property('header1', 'value1, value2');
        expect(r.headers).to.have.property('header2', 'value3');
      });

      it('can handle large responses', async () => {
        const data = Buffer.alloc(128 * 1024);
        registerStreamProtocol(protocolName, (request, callback) => {
          callback(getStream(data.length, data));
        });
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.have.lengthOf(data.length);
      });

      it('can handle a stream completing while writing', async () => {
        function dumbPassthrough () {
          return new stream.Transform({
            async transform (chunk, encoding, cb) {
              cb(null, chunk);
            }
          });
        }
        registerStreamProtocol(protocolName, (request, callback) => {
          callback({
            statusCode: 200,
            headers: { 'Content-Type': 'text/plain' },
            data: getStream(1024 * 1024, Buffer.alloc(1024 * 1024 * 2)).pipe(dumbPassthrough())
          });
        });
        const r = await ajax(protocolName + '://fake-host');
        expect(r.data).to.have.lengthOf(1024 * 1024 * 2);
      });

      it('can handle next-tick scheduling during read calls', async () => {
        const events = new EventEmitter();
        function createStream () {
          const buffers = [
            Buffer.alloc(65536),
            Buffer.alloc(65537),
            Buffer.alloc(39156)
          ];
          const e = new stream.Readable({ highWaterMark: 0 });
          e.push(buffers.shift());
          e._read = function () {
            process.nextTick(() => this.push(buffers.shift() || null));
          };
          e.on('end', function () {
            events.emit('end');
          });
          return e;
        }
        registerStreamProtocol(protocolName, (request, callback) => {
          callback({
            statusCode: 200,
            headers: { 'Content-Type': 'text/plain' },
            data: createStream()
          });
        });
        const hasEndedPromise = once(events, 'end');
        ajax(protocolName + '://fake-host').catch(() => {});
        await hasEndedPromise;
      });

      it('destroys response streams when aborted before completion', async () => {
        const events = new EventEmitter();
        registerStreamProtocol(protocolName, (request, callback) => {
          const responseStream = new stream.PassThrough();
          responseStream.push('data\r\n');
          responseStream.on('close', () => {
            events.emit('close');
          });
          callback({
            statusCode: 200,
            headers: { 'Content-Type': 'text/plain' },
            data: responseStream
          });
          events.emit('respond');
        });

        const hasRespondedPromise = once(events, 'respond');
        const hasClosedPromise = once(events, 'close');
        ajax(protocolName + '://fake-host').catch(() => {});
        await hasRespondedPromise;
        await contents.loadFile(path.join(__dirname, 'fixtures', 'pages', 'fetch.html'));
        await hasClosedPromise;
      });
    });
  }

  describe('protocol.isProtocolRegistered', () => {
    it('returns false when scheme is not registered', () => {
      const result = protocol.isProtocolRegistered('no-exist');
      expect(result).to.be.false('no-exist: is handled');
    });

    it('returns true for custom protocol', () => {
      registerStringProtocol(protocolName, (request, callback) => callback(''));
      const result = protocol.isProtocolRegistered(protocolName);
      expect(result).to.be.true('custom protocol is handled');
    });
  });

  describe('protocol.isProtocolIntercepted', () => {
    it('returns true for intercepted protocol', () => {
      interceptStringProtocol('http', (request, callback) => callback(''));
      const result = protocol.isProtocolIntercepted('http');
      expect(result).to.be.true('intercepted protocol is handled');
    });
  });

  describe('protocol.intercept(Any)Protocol', () => {
    it('returns false when scheme is already intercepted', () => {
      expect(protocol.interceptStringProtocol('http', (request, callback) => callback(''))).to.equal(true);
      expect(protocol.interceptBufferProtocol('http', (request, callback) => callback(Buffer.from('')))).to.equal(false);
    });

    it('does not crash when handler is called twice', async () => {
      interceptStringProtocol('http', (request, callback) => {
        try {
          callback(text);
          callback('');
        } catch {
          // Ignore error
        }
      });
      const r = await ajax('http://fake-host');
      expect(r.data).to.be.equal(text);
    });

    it('sends error when callback is called with nothing', async () => {
      interceptStringProtocol('http', (request, callback: any) => callback());
      await expect(ajax('http://fake-host')).to.be.eventually.rejected();
    });
  });

  describe('protocol.interceptStringProtocol', () => {
    it('can intercept http protocol', async () => {
      interceptStringProtocol('http', (request, callback) => callback(text));
      const r = await ajax('http://fake-host');
      expect(r.data).to.equal(text);
    });

    it('can set content-type', async () => {
      interceptStringProtocol('http', (request, callback) => {
        callback({
          mimeType: 'application/json',
          data: '{"value": 1}'
        });
      });
      const r = await ajax('http://fake-host');
      expect(JSON.parse(r.data)).to.have.property('value').that.is.equal(1);
    });

    it('can set content-type with charset', async () => {
      interceptStringProtocol('http', (request, callback) => {
        callback({
          mimeType: 'application/json; charset=UTF-8',
          data: '{"value": 1}'
        });
      });
      const r = await ajax('http://fake-host');
      expect(JSON.parse(r.data)).to.have.property('value').that.is.equal(1);
    });

    it('can receive post data', async () => {
      interceptStringProtocol('http', (request, callback) => {
        const uploadData = request.uploadData![0].bytes.toString();
        callback({ data: uploadData });
      });
      const r = await ajax('http://fake-host', { method: 'POST', body: qs.stringify(postData) });
      expect({ ...qs.parse(r.data) }).to.deep.equal(postData);
    });
  });

  describe('protocol.interceptBufferProtocol', () => {
    it('can intercept http protocol', async () => {
      interceptBufferProtocol('http', (request, callback) => callback(Buffer.from(text)));
      const r = await ajax('http://fake-host');
      expect(r.data).to.equal(text);
    });

    it('can receive post data', async () => {
      interceptBufferProtocol('http', (request, callback) => {
        const uploadData = request.uploadData![0].bytes;
        callback(uploadData);
      });
      const r = await ajax('http://fake-host', { method: 'POST', body: qs.stringify(postData) });
      expect(qs.parse(r.data)).to.deep.equal({ name: 'post test', type: 'string' });
    });
  });

  describe('protocol.interceptHttpProtocol', () => {
    // FIXME(zcbenz): This test was passing because the test itself was wrong,
    // I don't know whether it ever passed before and we should take a look at
    // it in future.
    xit('can send POST request', async () => {
      const server = http.createServer((req, res) => {
        let body = '';
        req.on('data', (chunk) => {
          body += chunk;
        });
        req.on('end', () => {
          res.end(body);
        });
        server.close();
      });
      after(() => server.close());
      const { url } = await listen(server);
      interceptHttpProtocol('http', (request, callback) => {
        const data: Electron.ProtocolResponse = {
          url: url,
          method: 'POST',
          uploadData: {
            contentType: 'application/x-www-form-urlencoded',
            data: request.uploadData![0].bytes
          },
          session: undefined
        };
        callback(data);
      });
      const r = await ajax('http://fake-host', { type: 'POST', data: postData });
      expect({ ...qs.parse(r.data) }).to.deep.equal(postData);
    });

    it('can use custom session', async () => {
      const customSession = session.fromPartition('custom-ses', { cache: false });
      customSession.webRequest.onBeforeRequest((details, callback) => {
        expect(details.url).to.equal('http://fake-host/');
        callback({ cancel: true });
      });
      after(() => customSession.webRequest.onBeforeRequest(null));

      interceptHttpProtocol('http', (request, callback) => {
        callback({
          url: request.url,
          session: customSession
        });
      });
      await expect(ajax('http://fake-host')).to.be.eventually.rejectedWith(Error);
    });

    it('can access request headers', (done) => {
      protocol.interceptHttpProtocol('http', (request) => {
        try {
          expect(request).to.have.property('headers');
          done();
        } catch (e) {
          done(e);
        }
      });
      ajax('http://fake-host').catch(() => {});
    });
  });

  describe('protocol.interceptStreamProtocol', () => {
    it('can intercept http protocol', async () => {
      interceptStreamProtocol('http', (request, callback) => callback(getStream()));
      const r = await ajax('http://fake-host');
      expect(r.data).to.equal(text);
    });

    it('can receive post data', async () => {
      interceptStreamProtocol('http', (request, callback) => {
        callback(getStream(3, request.uploadData![0].bytes.toString()));
      });
      const r = await ajax('http://fake-host', { method: 'POST', body: qs.stringify(postData) });
      expect({ ...qs.parse(r.data) }).to.deep.equal(postData);
    });

    it('can execute redirects', async () => {
      interceptStreamProtocol('http', (request, callback) => {
        if (request.url.indexOf('http://fake-host') === 0) {
          setTimeout(300).then(() => {
            callback({
              data: '',
              statusCode: 302,
              headers: {
                Location: 'http://fake-redirect'
              }
            });
          });
        } else {
          expect(request.url.indexOf('http://fake-redirect')).to.equal(0);
          callback(getStream(1, 'redirect'));
        }
      });
      const r = await ajax('http://fake-host');
      expect(r.data).to.equal('redirect');
    });

    it('should discard post data after redirection', async () => {
      interceptStreamProtocol('http', (request, callback) => {
        if (request.url.indexOf('http://fake-host') === 0) {
          setTimeout(300).then(() => {
            callback({
              statusCode: 302,
              headers: {
                Location: 'http://fake-redirect'
              }
            });
          });
        } else {
          expect(request.url.indexOf('http://fake-redirect')).to.equal(0);
          callback(getStream(3, request.method));
        }
      });
      const r = await ajax('http://fake-host', { type: 'POST', data: postData });
      expect(r.data).to.equal('GET');
    });
  });

  describe('protocol.uninterceptProtocol', () => {
    it('returns false when scheme does not exist', () => {
      expect(uninterceptProtocol('not-exist')).to.equal(false);
    });

    it('returns false when scheme is not intercepted', () => {
      expect(uninterceptProtocol('http')).to.equal(false);
    });
  });

  describe('protocol.registerSchemeAsPrivileged', () => {
    it('does not crash on exit', async () => {
      const appPath = path.join(__dirname, 'fixtures', 'api', 'custom-protocol-shutdown.js');
      const appProcess = ChildProcess.spawn(process.execPath, ['--enable-logging', appPath]);
      let stdout = '';
      let stderr = '';
      appProcess.stdout.on('data', data => { process.stdout.write(data); stdout += data; });
      appProcess.stderr.on('data', data => { process.stderr.write(data); stderr += data; });
      const [code] = await once(appProcess, 'exit');
      if (code !== 0) {
        console.log('Exit code : ', code);
        console.log('stdout : ', stdout);
        console.log('stderr : ', stderr);
      }
      expect(code).to.equal(0);
      expect(stdout).to.not.contain('VALIDATION_ERROR_DESERIALIZATION_FAILED');
      expect(stderr).to.not.contain('VALIDATION_ERROR_DESERIALIZATION_FAILED');
    });
  });

  describe('protocol.registerSchemesAsPrivileged allowServiceWorkers', () => {
    protocol.registerStringProtocol(serviceWorkerScheme, (request, cb) => {
      if (request.url.endsWith('.js')) {
        cb({
          mimeType: 'text/javascript',
          charset: 'utf-8',
          data: 'console.log("Loaded")'
        });
      } else {
        cb({
          mimeType: 'text/html',
          charset: 'utf-8',
          data: '<!DOCTYPE html>'
        });
      }
    });
    after(() => protocol.unregisterProtocol(serviceWorkerScheme));

    it('should fail when registering invalid service worker', async () => {
      await contents.loadURL(`${serviceWorkerScheme}://${v4()}.com`);
      await expect(contents.executeJavaScript(`navigator.serviceWorker.register('${v4()}.notjs', {scope: './'})`)).to.be.rejected();
    });

    it('should be able to register service worker for custom scheme', async () => {
      await contents.loadURL(`${serviceWorkerScheme}://${v4()}.com`);
      await contents.executeJavaScript(`navigator.serviceWorker.register('${v4()}.js', {scope: './'})`);
    });
  });

  describe('protocol.registerSchemesAsPrivileged standard', () => {
    const origin = `${standardScheme}://fake-host`;
    const imageURL = `${origin}/test.png`;
    const filePath = path.join(fixturesPath, 'pages', 'b.html');
    const fileContent = '<img src="/test.png" />';
    let w: BrowserWindow;

    beforeEach(() => {
      w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
    });

    afterEach(async () => {
      await closeWindow(w);
      unregisterProtocol(standardScheme);
      w = null as unknown as BrowserWindow;
    });

    it('resolves relative resources', async () => {
      registerFileProtocol(standardScheme, (request, callback) => {
        if (request.url === imageURL) {
          callback('');
        } else {
          callback(filePath);
        }
      });
      await w.loadURL(origin);
    });

    it('resolves absolute resources', async () => {
      registerStringProtocol(standardScheme, (request, callback) => {
        if (request.url === imageURL) {
          callback('');
        } else {
          callback({
            data: fileContent,
            mimeType: 'text/html'
          });
        }
      });
      await w.loadURL(origin);
    });

    it('can have fetch working in it', async () => {
      const requestReceived = deferPromise();
      const server = http.createServer((req, res) => {
        res.end();
        server.close();
        requestReceived.resolve();
      });
      const { url } = await listen(server);
      const content = `<script>fetch(${JSON.stringify(url)})</script>`;
      registerStringProtocol(standardScheme, (request, callback) => callback({ data: content, mimeType: 'text/html' }));
      await w.loadURL(origin);
      await requestReceived;
    });

    it('can access files through the FileSystem API', (done) => {
      const filePath = path.join(fixturesPath, 'pages', 'filesystem.html');
      protocol.registerFileProtocol(standardScheme, (request, callback) => callback({ path: filePath }));
      w.loadURL(origin);
      ipcMain.once('file-system-error', (event, err) => done(err));
      ipcMain.once('file-system-write-end', () => done());
    });

    it('registers secure, when {secure: true}', (done) => {
      const filePath = path.join(fixturesPath, 'pages', 'cache-storage.html');
      ipcMain.once('success', () => done());
      ipcMain.once('failure', (event, err) => done(err));
      protocol.registerFileProtocol(standardScheme, (request, callback) => callback({ path: filePath }));
      w.loadURL(origin);
    });
  });

  describe('protocol.registerSchemesAsPrivileged cors-fetch', function () {
    let w: BrowserWindow;
    beforeEach(async () => {
      w = new BrowserWindow({ show: false });
    });

    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
      for (const scheme of [standardScheme, 'cors', 'no-cors', 'no-fetch']) {
        protocol.unregisterProtocol(scheme);
      }
    });

    it('supports fetch api by default', async () => {
      const url = `file://${fixturesPath}/assets/logo.png`;
      await w.loadURL(`file://${fixturesPath}/pages/blank.html`);
      const ok = await w.webContents.executeJavaScript(`fetch(${JSON.stringify(url)}).then(r => r.ok)`);
      expect(ok).to.be.true('response ok');
    });

    it('allows CORS requests by default', async () => {
      await allowsCORSRequests('cors', 200, new RegExp(''), () => {
        const { ipcRenderer } = require('electron');
        fetch('cors://myhost').then(function (response) {
          ipcRenderer.send('response', response.status);
        }).catch(function () {
          ipcRenderer.send('response', 'failed');
        });
      });
    });

    // DISABLED-FIXME: Figure out why this test is failing
    it('disallows CORS and fetch requests when only supportFetchAPI is specified', async () => {
      await allowsCORSRequests('no-cors', ['failed xhr', 'failed fetch'], /has been blocked by CORS policy/, () => {
        const { ipcRenderer } = require('electron');
        Promise.all([
          new Promise(resolve => {
            const req = new XMLHttpRequest();
            req.onload = () => resolve('loaded xhr');
            req.onerror = () => resolve('failed xhr');
            req.open('GET', 'no-cors://myhost');
            req.send();
          }),
          fetch('no-cors://myhost')
            .then(() => 'loaded fetch')
            .catch(() => 'failed fetch')
        ]).then(([xhr, fetch]) => {
          ipcRenderer.send('response', [xhr, fetch]);
        });
      });
    });

    it('allows CORS, but disallows fetch requests, when specified', async () => {
      await allowsCORSRequests('no-fetch', ['loaded xhr', 'failed fetch'], /Fetch API cannot load/, () => {
        const { ipcRenderer } = require('electron');
        Promise.all([
          new Promise(resolve => {
            const req = new XMLHttpRequest();
            req.onload = () => resolve('loaded xhr');
            req.onerror = () => resolve('failed xhr');
            req.open('GET', 'no-fetch://myhost');
            req.send();
          }),
          fetch('no-fetch://myhost')
            .then(() => 'loaded fetch')
            .catch(() => 'failed fetch')
        ]).then(([xhr, fetch]) => {
          ipcRenderer.send('response', [xhr, fetch]);
        });
      });
    });

    async function allowsCORSRequests (corsScheme: string, expected: any, expectedConsole: RegExp, content: Function) {
      registerStringProtocol(standardScheme, (request, callback) => {
        callback({ data: `<script>(${content})()</script>`, mimeType: 'text/html' });
      });
      registerStringProtocol(corsScheme, (request, callback) => {
        callback('');
      });

      const newContents = (webContents as typeof ElectronInternal.WebContents).create({
        nodeIntegration: true,
        contextIsolation: false
      });
      const consoleMessages: string[] = [];
      newContents.on('console-message', (e, level, message) => consoleMessages.push(message));
      try {
        newContents.loadURL(standardScheme + '://fake-host');
        const [, response] = await once(ipcMain, 'response');
        expect(response).to.deep.equal(expected);
        expect(consoleMessages.join('\n')).to.match(expectedConsole);
      } finally {
        // This is called in a timeout to avoid a crash that happens when
        // calling destroy() in a microtask.
        setTimeout().then(() => {
          newContents.destroy();
        });
      }
    }
  });

  describe('protocol.registerSchemesAsPrivileged stream', async function () {
    const pagePath = path.join(fixturesPath, 'pages', 'video.html');
    const videoSourceImagePath = path.join(fixturesPath, 'video-source-image.webp');
    const videoPath = path.join(fixturesPath, 'video.webm');
    let w: BrowserWindow;

    before(async () => {
      // generate test video
      const imageBase64 = await fs.promises.readFile(videoSourceImagePath, 'base64');
      const imageDataUrl = `data:image/webp;base64,${imageBase64}`;
      const encoder = new WebmGenerator(15);
      for (let i = 0; i < 30; i++) {
        encoder.add(imageDataUrl);
      }
      await new Promise((resolve, reject) => {
        encoder.compile((output:Uint8Array) => {
          fs.promises.writeFile(videoPath, output).then(resolve, reject);
        });
      });
    });

    after(async () => {
      await fs.promises.unlink(videoPath);
    });

    beforeEach(async function () {
      w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');
      if (!await w.webContents.executeJavaScript('document.createElement(\'video\').canPlayType(\'video/webm; codecs="vp8.0"\')')) {
        this.skip();
      }
    });

    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
      await protocol.unregisterProtocol(standardScheme);
      await protocol.unregisterProtocol('stream');
    });

    it('successfully plays videos when content is buffered (stream: false)', async () => {
      await streamsResponses(standardScheme, 'play');
    });

    it('successfully plays videos when streaming content (stream: true)', async () => {
      await streamsResponses('stream', 'play');
    });

    async function streamsResponses (testingScheme: string, expected: any) {
      const protocolHandler = (request: any, callback: Function) => {
        if (request.url.includes('/video.webm')) {
          const stat = fs.statSync(videoPath);
          const fileSize = stat.size;
          const range = request.headers.Range;
          if (range) {
            const parts = range.replace(/bytes=/, '').split('-');
            const start = parseInt(parts[0], 10);
            const end = parts[1] ? parseInt(parts[1], 10) : fileSize - 1;
            const chunksize = (end - start) + 1;
            const headers = {
              'Content-Range': `bytes ${start}-${end}/${fileSize}`,
              'Accept-Ranges': 'bytes',
              'Content-Length': String(chunksize),
              'Content-Type': 'video/webm'
            };
            callback({ statusCode: 206, headers, data: fs.createReadStream(videoPath, { start, end }) });
          } else {
            callback({
              statusCode: 200,
              headers: {
                'Content-Length': String(fileSize),
                'Content-Type': 'video/webm'
              },
              data: fs.createReadStream(videoPath)
            });
          }
        } else {
          callback({ data: fs.createReadStream(pagePath), headers: { 'Content-Type': 'text/html' }, statusCode: 200 });
        }
      };
      await registerStreamProtocol(standardScheme, protocolHandler);
      await registerStreamProtocol('stream', protocolHandler);

      const newContents = (webContents as typeof ElectronInternal.WebContents).create({
        nodeIntegration: true,
        contextIsolation: false
      });

      try {
        newContents.loadURL(testingScheme + '://fake-host');
        const [, response] = await once(ipcMain, 'result');
        expect(response).to.deep.equal(expected);
      } finally {
        // This is called in a timeout to avoid a crash that happens when
        // calling destroy() in a microtask.
        setTimeout().then(() => {
          newContents.destroy();
        });
      }
    }
  });

  describe('protocol.registerSchemesAsPrivileged codeCache', function () {
    const temp = require('temp').track();
    const appPath = path.join(fixturesPath, 'apps', 'refresh-page');

    let w: BrowserWindow;
    let codeCachePath: string;
    beforeEach(async () => {
      w = new BrowserWindow({ show: false });
      codeCachePath = temp.path();
    });

    afterEach(async () => {
      await closeWindow(w);
      w = null as unknown as BrowserWindow;
    });

    it('code cache in custom protocol is disabled by default', async () => {
      ChildProcess.spawnSync(process.execPath, [appPath, 'false', codeCachePath]);
      expect(fs.readdirSync(path.join(codeCachePath, 'js')).length).to.equal(2);
    });

    it('codeCache:true enables codeCache in custom protocol', async () => {
      ChildProcess.spawnSync(process.execPath, [appPath, 'true', codeCachePath]);
      expect(fs.readdirSync(path.join(codeCachePath, 'js')).length).to.above(2);
    });
  });

  describe('handle', () => {
    afterEach(closeAllWindows);

    it('receives requests to a custom scheme', async () => {
      protocol.handle('test-scheme', (req) => new Response('hello ' + req.url));
      defer(() => { protocol.unhandle('test-scheme'); });
      const resp = await net.fetch('test-scheme://foo');
      expect(resp.status).to.equal(200);
    });

    it('can be unhandled', async () => {
      protocol.handle('test-scheme', (req) => new Response('hello ' + req.url));
      defer(() => {
        try {
          // In case of failure, make sure we unhandle. But we should succeed
          // :)
          protocol.unhandle('test-scheme');
        } catch { /* ignore */ }
      });
      const resp1 = await net.fetch('test-scheme://foo');
      expect(resp1.status).to.equal(200);
      protocol.unhandle('test-scheme');
      await expect(net.fetch('test-scheme://foo')).to.eventually.be.rejectedWith(/ERR_UNKNOWN_URL_SCHEME/);
    });

    it('receives requests to the existing https scheme', async () => {
      protocol.handle('https', (req) => new Response('hello ' + req.url));
      defer(() => { protocol.unhandle('https'); });
      const body = await net.fetch('https://foo').then(r => r.text());
      expect(body).to.equal('hello https://foo/');
    });

    it('receives requests to the existing file scheme', (done) => {
      const filePath = path.join(__dirname, 'fixtures', 'pages', 'a.html');

      protocol.handle('file', (req) => {
        let file;
        if (process.platform === 'win32') {
          file = `file:///${filePath.replaceAll('\\', '/')}`;
        } else {
          file = `file://${filePath}`;
        }

        if (req.url === file) done();
        return new Response(req.url);
      });

      defer(() => { protocol.unhandle('file'); });

      const w = new BrowserWindow();
      w.loadFile(filePath);
    });

    it('receives requests to an existing scheme when navigating', async () => {
      protocol.handle('https', (req) => new Response('hello ' + req.url));
      defer(() => { protocol.unhandle('https'); });
      const w = new BrowserWindow({ show: false });
      await w.loadURL('https://localhost');
      expect(await w.webContents.executeJavaScript('document.body.textContent')).to.equal('hello https://localhost/');
    });

    it('can send buffer body', async () => {
      protocol.handle('test-scheme', (req) => new Response(Buffer.from('hello ' + req.url)));
      defer(() => { protocol.unhandle('test-scheme'); });
      const body = await net.fetch('test-scheme://foo').then(r => r.text());
      expect(body).to.equal('hello test-scheme://foo');
    });

    it('can send stream body', async () => {
      protocol.handle('test-scheme', () => new Response(getWebStream()));
      defer(() => { protocol.unhandle('test-scheme'); });
      const body = await net.fetch('test-scheme://foo').then(r => r.text());
      expect(body).to.equal(text);
    });

    it('accepts urls with no hostname in non-standard schemes', async () => {
      protocol.handle('test-scheme', (req) => new Response(req.url));
      defer(() => { protocol.unhandle('test-scheme'); });
      {
        const body = await net.fetch('test-scheme://foo').then(r => r.text());
        expect(body).to.equal('test-scheme://foo');
      }
      {
        const body = await net.fetch('test-scheme:///foo').then(r => r.text());
        expect(body).to.equal('test-scheme:///foo');
      }
      {
        const body = await net.fetch('test-scheme://').then(r => r.text());
        expect(body).to.equal('test-scheme://');
      }
    });

    it('accepts urls with a port-like component in non-standard schemes', async () => {
      protocol.handle('test-scheme', (req) => new Response(req.url));
      defer(() => { protocol.unhandle('test-scheme'); });
      {
        const body = await net.fetch('test-scheme://foo:30').then(r => r.text());
        expect(body).to.equal('test-scheme://foo:30');
      }
    });

    it('normalizes urls in standard schemes', async () => {
      // NB. 'app' is registered as a standard scheme in test setup.
      protocol.handle('app', (req) => new Response(req.url));
      defer(() => { protocol.unhandle('app'); });
      {
        const body = await net.fetch('app://foo').then(r => r.text());
        expect(body).to.equal('app://foo/');
      }
      {
        const body = await net.fetch('app:///foo').then(r => r.text());
        expect(body).to.equal('app://foo/');
      }
      // NB. 'app' is registered with the default scheme type of 'host'.
      {
        const body = await net.fetch('app://foo:1234').then(r => r.text());
        expect(body).to.equal('app://foo/');
      }
      await expect(net.fetch('app://')).to.be.rejectedWith('Invalid URL');
    });

    it('fails on URLs with a username', async () => {
      // NB. 'app' is registered as a standard scheme in test setup.
      protocol.handle('http', (req) => new Response(req.url));
      defer(() => { protocol.unhandle('http'); });
      await expect(contents.loadURL('http://x@foo:1234')).to.be.rejectedWith(/ERR_UNEXPECTED/);
    });

    it('normalizes http urls', async () => {
      protocol.handle('http', (req) => new Response(req.url));
      defer(() => { protocol.unhandle('http'); });
      {
        const body = await net.fetch('http://foo').then(r => r.text());
        expect(body).to.equal('http://foo/');
      }
    });

    it('can send errors', async () => {
      protocol.handle('test-scheme', () => Response.error());
      defer(() => { protocol.unhandle('test-scheme'); });
      await expect(net.fetch('test-scheme://foo')).to.eventually.be.rejectedWith('net::ERR_FAILED');
    });

    it('handles invalid protocol response status', async () => {
      protocol.handle('test-scheme', () => {
        return { status: [] } as any;
      });

      defer(() => { protocol.unhandle('test-scheme'); });
      await expect(net.fetch('test-scheme://foo')).to.be.rejectedWith('net::ERR_UNEXPECTED');
    });

    it('handles invalid protocol response statusText', async () => {
      protocol.handle('test-scheme', () => {
        return { statusText: false } as any;
      });

      defer(() => { protocol.unhandle('test-scheme'); });
      await expect(net.fetch('test-scheme://foo')).to.be.rejectedWith('net::ERR_UNEXPECTED');
    });

    it('handles invalid protocol response header parameters', async () => {
      protocol.handle('test-scheme', () => {
        return { headers: false } as any;
      });

      defer(() => { protocol.unhandle('test-scheme'); });
      await expect(net.fetch('test-scheme://foo')).to.be.rejectedWith('net::ERR_UNEXPECTED');
    });

    it('handles invalid protocol response body parameters', async () => {
      protocol.handle('test-scheme', () => {
        return { body: false } as any;
      });

      defer(() => { protocol.unhandle('test-scheme'); });
      await expect(net.fetch('test-scheme://foo')).to.be.rejectedWith('net::ERR_UNEXPECTED');
    });

    it('handles a synchronous error in the handler', async () => {
      protocol.handle('test-scheme', () => { throw new Error('test'); });
      defer(() => { protocol.unhandle('test-scheme'); });
      await expect(net.fetch('test-scheme://foo')).to.be.rejectedWith('net::ERR_UNEXPECTED');
    });

    it('handles an asynchronous error in the handler', async () => {
      protocol.handle('test-scheme', () => Promise.reject(new Error('rejected promise')));
      defer(() => { protocol.unhandle('test-scheme'); });
      await expect(net.fetch('test-scheme://foo')).to.be.rejectedWith('net::ERR_UNEXPECTED');
    });

    it('correctly sets statusCode', async () => {
      protocol.handle('test-scheme', () => new Response(null, { status: 201 }));
      defer(() => { protocol.unhandle('test-scheme'); });
      const resp = await net.fetch('test-scheme://foo');
      expect(resp.status).to.equal(201);
    });

    it('correctly sets content-type and charset', async () => {
      protocol.handle('test-scheme', () => new Response(null, { headers: { 'content-type': 'text/html; charset=testcharset' } }));
      defer(() => { protocol.unhandle('test-scheme'); });
      const resp = await net.fetch('test-scheme://foo');
      expect(resp.headers.get('content-type')).to.equal('text/html; charset=testcharset');
    });

    it('can forward to http', async () => {
      const server = http.createServer((req, res) => {
        res.end(text);
      });
      defer(() => { server.close(); });
      const { url } = await listen(server);

      protocol.handle('test-scheme', () => net.fetch(url));
      defer(() => { protocol.unhandle('test-scheme'); });
      const body = await net.fetch('test-scheme://foo').then(r => r.text());
      expect(body).to.equal(text);
    });

    it('can forward an http request with headers', async () => {
      const server = http.createServer((req, res) => {
        res.setHeader('foo', 'bar');
        res.end(text);
      });
      defer(() => { server.close(); });
      const { url } = await listen(server);

      protocol.handle('test-scheme', (req) => net.fetch(url, { headers: req.headers }));
      defer(() => { protocol.unhandle('test-scheme'); });

      const resp = await net.fetch('test-scheme://foo');
      expect(resp.headers.get('foo')).to.equal('bar');
    });

    it('can forward to file', async () => {
      protocol.handle('test-scheme', () => net.fetch(url.pathToFileURL(path.join(__dirname, 'fixtures', 'hello.txt')).toString()));
      defer(() => { protocol.unhandle('test-scheme'); });

      const body = await net.fetch('test-scheme://foo').then(r => r.text());
      expect(body.trimEnd()).to.equal('hello world');
    });

    it('can receive simple request body', async () => {
      protocol.handle('test-scheme', (req) => new Response(req.body));
      defer(() => { protocol.unhandle('test-scheme'); });
      const body = await net.fetch('test-scheme://foo', {
        method: 'POST',
        body: 'foobar'
      }).then(r => r.text());
      expect(body).to.equal('foobar');
    });

    it('can receive stream request body', async () => {
      protocol.handle('test-scheme', (req) => new Response(req.body));
      defer(() => { protocol.unhandle('test-scheme'); });
      const body = await net.fetch('test-scheme://foo', {
        method: 'POST',
        body: getWebStream(),
        duplex: 'half' // https://github.com/microsoft/TypeScript/issues/53157
      } as any).then(r => r.text());
      expect(body).to.equal(text);
    });

    it('can receive stream request body asynchronously', async () => {
      let done: any;
      const requestReceived: Promise<Buffer[]> = new Promise(resolve => { done = resolve; });
      protocol.handle('http-like', async (req) => {
        const chunks = [];
        for await (const chunk of (req.body as any)) {
          chunks.push(chunk);
        }
        done(chunks);
        return new Response('ok');
      });
      defer(() => { protocol.unhandle('http-like'); });
      const w = new BrowserWindow({ show: false });
      w.loadURL('about:blank');
      const expectedHashChunks = await w.webContents.executeJavaScript(`
        const dataStream = () =>
          new ReadableStream({
            async start(controller) {
              for (let i = 0; i < 10; i++) { controller.enqueue(Array(1024 * 128).fill(+i).join("\\n")); }
              controller.close();
            },
          }).pipeThrough(new TextEncoderStream());
        fetch(
          new Request("http-like://host", {
            method: "POST",
            body: dataStream(),
            duplex: "half",
          })
        );
        (async () => {
          const chunks = []
          for await (const chunk of dataStream()) {
            chunks.push(chunk);
          }
          return chunks;
        })()
      `);
      const expectedHash = Buffer.from(await crypto.subtle.digest('SHA-256', Buffer.concat(expectedHashChunks))).toString('hex');
      const body = Buffer.concat(await requestReceived);
      const actualHash = Buffer.from(await crypto.subtle.digest('SHA-256', Buffer.from(body))).toString('hex');
      expect(actualHash).to.equal(expectedHash);
    });

    it('can receive multi-part postData from loadURL', async () => {
      protocol.handle('test-scheme', (req) => new Response(req.body));
      defer(() => { protocol.unhandle('test-scheme'); });
      await contents.loadURL('test-scheme://foo', { postData: [{ type: 'rawData', bytes: Buffer.from('a') }, { type: 'rawData', bytes: Buffer.from('b') }] });
      expect(await contents.executeJavaScript('document.documentElement.textContent')).to.equal('ab');
    });

    it('can receive file postData from loadURL', async () => {
      protocol.handle('test-scheme', (req) => new Response(req.body));
      defer(() => { protocol.unhandle('test-scheme'); });
      await contents.loadURL('test-scheme://foo', { postData: [{ type: 'file', filePath: path.join(fixturesPath, 'hello.txt'), length: 'hello world\n'.length, offset: 0, modificationTime: 0 }] });
      expect(await contents.executeJavaScript('document.documentElement.textContent')).to.equal('hello world\n');
    });

    it('can receive file postData from a form', async () => {
      protocol.handle('test-scheme', (req) => new Response(req.body));
      defer(() => { protocol.unhandle('test-scheme'); });
      await contents.loadURL('data:text/html,<form action="test-scheme://foo" method=POST enctype="multipart/form-data"><input name=foo type=file>');
      const { debugger: dbg } = contents;
      dbg.attach();
      const { root } = await dbg.sendCommand('DOM.getDocument');
      const { nodeId: fileInputNodeId } = await dbg.sendCommand('DOM.querySelector', { nodeId: root.nodeId, selector: 'input' });
      await dbg.sendCommand('DOM.setFileInputFiles', {
        nodeId: fileInputNodeId,
        files: [
          path.join(fixturesPath, 'hello.txt')
        ]
      });
      const navigated = once(contents, 'did-finish-load');
      await contents.executeJavaScript('document.querySelector("form").submit()');
      await navigated;
      expect(await contents.executeJavaScript('document.documentElement.textContent')).to.match(/------WebKitFormBoundary.*\nContent-Disposition: form-data; name="foo"; filename="hello.txt"\nContent-Type: text\/plain\n\nhello world\n\n------WebKitFormBoundary.*--\n/);
    });

    it('can receive streaming fetch upload', async () => {
      protocol.handle('no-cors', (req) => new Response(req.body));
      defer(() => { protocol.unhandle('no-cors'); });
      await contents.loadURL('no-cors://foo');
      const fetchBodyResult = await contents.executeJavaScript(`
        const stream = new ReadableStream({
          async start(controller) {
            controller.enqueue('hello world');
            controller.close();
          },
        }).pipeThrough(new TextEncoderStream());
        fetch(location.href, {method: 'POST', body: stream, duplex: 'half'}).then(x => x.text())
      `);
      expect(fetchBodyResult).to.equal('hello world');
    });

    it('can receive streaming fetch upload when a webRequest handler is present', async () => {
      session.defaultSession.webRequest.onBeforeRequest((details, cb) => {
        console.log('webRequest', details.url, details.method);
        cb({});
      });
      defer(() => {
        session.defaultSession.webRequest.onBeforeRequest(null);
      });

      protocol.handle('no-cors', (req) => {
        console.log('handle', req.url, req.method);
        return new Response(req.body);
      });
      defer(() => { protocol.unhandle('no-cors'); });
      await contents.loadURL('no-cors://foo');
      const fetchBodyResult = await contents.executeJavaScript(`
        const stream = new ReadableStream({
          async start(controller) {
            controller.enqueue('hello world');
            controller.close();
          },
        }).pipeThrough(new TextEncoderStream());
        fetch(location.href, {method: 'POST', body: stream, duplex: 'half'}).then(x => x.text())
      `);
      expect(fetchBodyResult).to.equal('hello world');
    });

    it('can receive an error from streaming fetch upload', async () => {
      protocol.handle('no-cors', (req) => new Response(req.body));
      defer(() => { protocol.unhandle('no-cors'); });
      await contents.loadURL('no-cors://foo');
      const fetchBodyResult = await contents.executeJavaScript(`
        const stream = new ReadableStream({
          async start(controller) {
            controller.error('test')
          },
        });
        fetch(location.href, {method: 'POST', body: stream, duplex: 'half'}).then(x => x.text()).catch(err => err)
      `);
      expect(fetchBodyResult).to.be.an.instanceOf(Error);
    });

    it('gets an error from streaming fetch upload when the renderer dies', async () => {
      let gotRequest: Function;
      const receivedRequest = new Promise<Request>(resolve => { gotRequest = resolve; });
      protocol.handle('no-cors', (req) => {
        if (/fetch/.test(req.url)) gotRequest(req);
        return new Response();
      });
      defer(() => { protocol.unhandle('no-cors'); });
      await contents.loadURL('no-cors://foo');
      contents.executeJavaScript(`
        const stream = new ReadableStream({
          async start(controller) {
            window.controller = controller // no GC
          },
        });
        fetch(location.href + '/fetch', {method: 'POST', body: stream, duplex: 'half'}).then(x => x.text()).catch(err => err)
      `);
      const req = await receivedRequest;
      contents.destroy();
      // Undo .destroy() for the next test
      contents = (webContents as typeof ElectronInternal.WebContents).create({ sandbox: true });
      await expect(req.body!.getReader().read()).to.eventually.be.rejectedWith('net::ERR_FAILED');
    });

    it('can bypass intercepeted protocol handlers', async () => {
      protocol.handle('http', () => new Response('custom'));
      defer(() => { protocol.unhandle('http'); });
      const server = http.createServer((req, res) => {
        res.end('default');
      });
      defer(() => server.close());
      const { url } = await listen(server);
      expect(await net.fetch(url, { bypassCustomProtocolHandlers: true }).then(r => r.text())).to.equal('default');
    });

    it('bypassing custom protocol handlers also bypasses new protocols', async () => {
      protocol.handle('app', () => new Response('custom'));
      defer(() => { protocol.unhandle('app'); });
      await expect(net.fetch('app://foo', { bypassCustomProtocolHandlers: true })).to.be.rejectedWith('net::ERR_UNKNOWN_URL_SCHEME');
    });

    it('can forward to the original handler', async () => {
      protocol.handle('http', (req) => net.fetch(req, { bypassCustomProtocolHandlers: true }));
      defer(() => { protocol.unhandle('http'); });
      const server = http.createServer((req, res) => {
        res.end('hello');
        server.close();
      });
      const { url } = await listen(server);
      await contents.loadURL(url);
      expect(await contents.executeJavaScript('document.documentElement.textContent')).to.equal('hello');
    });

    it('supports sniffing mime type', async () => {
      protocol.handle('http', async (req) => {
        return net.fetch(req, { bypassCustomProtocolHandlers: true });
      });
      defer(() => { protocol.unhandle('http'); });

      const server = http.createServer((req, res) => {
        if (/html/.test(req.url ?? '')) { res.end('<!doctype html><body>hi'); } else { res.end('hi'); }
      });
      const { url } = await listen(server);
      defer(() => server.close());

      {
        await contents.loadURL(url);
        const doc = await contents.executeJavaScript('document.documentElement.outerHTML');
        expect(doc).to.match(/white-space: pre-wrap/);
      }
      {
        await contents.loadURL(url + '?html');
        const doc = await contents.executeJavaScript('document.documentElement.outerHTML');
        expect(doc).to.equal('<html><head></head><body>hi</body></html>');
      }
    });

    it('does not emit undefined chunks into the request body stream when uploading a stream', async () => {
      protocol.handle('cors', async (request) => {
        expect(request.body).to.be.an.instanceOf(webStream.ReadableStream);
        for await (const value of request.body as webStream.ReadableStream<Uint8Array>) {
          expect(value).to.not.be.undefined();
        }
        return new Response(undefined, { status: 200 });
      });
      defer(() => { protocol.unhandle('cors'); });

      await contents.loadFile(path.resolve(fixturesPath, 'pages', 'base-page.html'));
      contents.on('console-message', (e, level, message) => console.log(message));
      const ok = await contents.executeJavaScript(`(async () => {
        function wait(milliseconds) {
          return new Promise((resolve) => setTimeout(resolve, milliseconds));
        }

        const stream = new ReadableStream({
          async start(controller) {
            await wait(4);
            controller.enqueue('This ');
            await wait(4);
            controller.enqueue('is ');
            await wait(4);
            controller.enqueue('a ');
            await wait(4);
            controller.enqueue('slow ');
            await wait(4);
            controller.enqueue('request.');
            controller.close();
          }
        }).pipeThrough(new TextEncoderStream());
        return (await fetch('cors://url.invalid', { method: 'POST', body: stream, duplex: 'half' })).ok;
      })()`);
      expect(ok).to.be.true();
    });

    it('does not emit undefined chunks into the request body stream when uploading a file', async () => {
      protocol.handle('cors', async (request) => {
        expect(request.body).to.be.an.instanceOf(webStream.ReadableStream);
        for await (const value of request.body as webStream.ReadableStream<Uint8Array>) {
          expect(value).to.not.be.undefined();
        }
        return new Response(undefined, { status: 200 });
      });
      defer(() => { protocol.unhandle('cors'); });

      await contents.loadFile(path.resolve(fixturesPath, 'pages', 'file-input.html'));
      const { debugger: debug } = contents;
      debug.attach();
      try {
        const { root: { nodeId } } = await debug.sendCommand('DOM.getDocument');
        const { nodeId: inputNodeId } = await debug.sendCommand('DOM.querySelector', { nodeId, selector: 'input' });
        await debug.sendCommand('DOM.setFileInputFiles', {
          files: [path.join(fixturesPath, 'cat-spin.mp4')],
          nodeId: inputNodeId
        });
        const ok = await contents.executeJavaScript(`(async () => {
          const formData = new FormData();
          formData.append("data", document.getElementById("file").files[0]);
          return (await fetch('cors://url.invalid', { method: 'POST', body: formData })).ok;
        })()`);
        expect(ok).to.be.true();
      } finally {
        debug.detach();
      }
    });

    it('filters an illegal "origin: null" header', async () => {
      protocol.handle('http', (req) => {
        expect(new Headers(req.headers).get('origin')).to.not.equal('null');
        return new Response();
      });
      defer(() => { protocol.unhandle('http'); });

      const filePath = path.join(fixturesPath, 'pages', 'form-with-data.html');
      await contents.loadFile(filePath);

      const loadPromise = new Promise((resolve, reject) => {
        contents.once('did-finish-load', resolve);
        contents.once('did-fail-load', (_, errorCode, errorDescription) =>
          reject(new Error(`did-fail-load: ${errorCode} ${errorDescription}. See AssertionError for details.`))
        );
      });
      await contents.executeJavaScript(`
        const form = document.querySelector('form');
        form.action = 'http://cors.invalid';
        form.method = 'POST';
        form.submit();
      `);
      await loadPromise;
    });

    it('does forward Blob chunks', async () => {
      // we register the protocol on a separate session to validate the assumption
      // that `getBlobData()` indeed returns the blob data from a global variable
      const s = session.fromPartition('protocol-handle-forwards-blob-chunks');

      s.protocol.handle('cors', async (request) => {
        expect(request.body).to.be.an.instanceOf(webStream.ReadableStream);
        return new Response(
          `hello to ${await streamConsumers.text(request.body as webStream.ReadableStream<Uint8Array>)}`,
          { status: 200 }
        );
      });
      defer(() => { s.protocol.unhandle('cors'); });

      const w = new BrowserWindow({ show: false, webPreferences: { session: s } });
      await w.webContents.loadFile(path.resolve(fixturesPath, 'pages', 'base-page.html'));
      const response = await w.webContents.executeJavaScript(`(async () => {
        const body = new Blob(["it's-a ", 'me! ', 'Mario!'], { type: 'text/plain' });
        return await (await fetch('cors://url.invalid', { method: 'POST', body })).text();
      })()`);
      expect(response).to.be.string('hello to it\'s-a me! Mario!');
    });

    // TODO(nornagon): this test doesn't pass on Linux currently, investigate.
    ifit(process.platform !== 'linux')('is fast', async () => {
      // 128 MB of spaces.
      const chunk = new Uint8Array(128 * 1024 * 1024);
      chunk.fill(' '.charCodeAt(0));

      const server = http.createServer((req, res) => {
        // The sniffed mime type for the space-filled chunk will be
        // text/plain, which chews up all its performance in the renderer
        // trying to wrap lines. Setting content-type to text/html measures
        // something closer to just the raw cost of getting the bytes over
        // the wire.
        res.setHeader('content-type', 'text/html');
        res.end(chunk);
      });
      defer(() => server.close());
      const { url } = await listen(server);

      const rawTime = await (async () => {
        await contents.loadURL(url); // warm
        const begin = Date.now();
        await contents.loadURL(url);
        const end = Date.now();
        return end - begin;
      })();

      // Fetching through an intercepted handler should not be too much slower
      // than it would be if the protocol hadn't been intercepted.

      protocol.handle('http', async (req) => {
        return net.fetch(req, { bypassCustomProtocolHandlers: true });
      });
      defer(() => { protocol.unhandle('http'); });

      const interceptedTime = await (async () => {
        const begin = Date.now();
        await contents.loadURL(url);
        const end = Date.now();
        return end - begin;
      })();
      expect(interceptedTime).to.be.lessThan(rawTime * 1.6);
    });
  });
});
