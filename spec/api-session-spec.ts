import { expect } from 'chai';
import * as crypto from 'node:crypto';
import * as http from 'node:http';
import * as https from 'node:https';
import * as path from 'node:path';
import * as fs from 'node:fs';
import * as ChildProcess from 'node:child_process';
import { app, session, BrowserWindow, net, ipcMain, Session, webFrameMain, WebFrameMain } from 'electron/main';
import * as send from 'send';
import * as auth from 'basic-auth';
import { closeAllWindows } from './lib/window-helpers';
import { defer, listen } from './lib/spec-helpers';
import { once } from 'node:events';
import { setTimeout } from 'node:timers/promises';

describe('session module', () => {
  const fixtures = path.resolve(__dirname, 'fixtures');
  const url = 'http://127.0.0.1';

  describe('session.defaultSession', () => {
    it('returns the default session', () => {
      expect(session.defaultSession).to.equal(session.fromPartition(''));
    });
  });

  describe('session.fromPartition(partition, options)', () => {
    it('returns existing session with same partition', () => {
      expect(session.fromPartition('test')).to.equal(session.fromPartition('test'));
    });
  });

  describe('session.fromPath(path)', () => {
    it('returns storage path of a session which was created with an absolute path', () => {
      const tmppath = require('electron').app.getPath('temp');
      const ses = session.fromPath(tmppath);
      expect(ses.storagePath).to.equal(tmppath);
    });
  });

  describe('ses.cookies', () => {
    const name = '0';
    const value = '0';
    afterEach(closeAllWindows);

    // Clear cookie of defaultSession after each test.
    afterEach(async () => {
      const { cookies } = session.defaultSession;
      const cs = await cookies.get({ url });
      for (const c of cs) {
        await cookies.remove(url, c.name);
      }
    });

    it('should get cookies', async () => {
      const server = http.createServer((req, res) => {
        res.setHeader('Set-Cookie', [`${name}=${value}`]);
        res.end('finished');
        server.close();
      });
      const { port } = await listen(server);
      const w = new BrowserWindow({ show: false });
      await w.loadURL(`${url}:${port}`);
      const list = await w.webContents.session.cookies.get({ url });
      const cookie = list.find(cookie => cookie.name === name);
      expect(cookie).to.exist.and.to.have.property('value', value);
    });

    it('sets cookies', async () => {
      const { cookies } = session.defaultSession;
      const name = '1';
      const value = '1';

      await cookies.set({ url, name, value, expirationDate: (Date.now()) / 1000 + 120 });
      const c = (await cookies.get({ url }))[0];
      expect(c.name).to.equal(name);
      expect(c.value).to.equal(value);
      expect(c.session).to.equal(false);
    });

    it('sets session cookies', async () => {
      const { cookies } = session.defaultSession;
      const name = '2';
      const value = '1';

      await cookies.set({ url, name, value });
      const c = (await cookies.get({ url }))[0];
      expect(c.name).to.equal(name);
      expect(c.value).to.equal(value);
      expect(c.session).to.equal(true);
    });

    it('sets cookies without name', async () => {
      const { cookies } = session.defaultSession;
      const value = '3';

      await cookies.set({ url, value });
      const c = (await cookies.get({ url }))[0];
      expect(c.name).to.be.empty();
      expect(c.value).to.equal(value);
    });

    for (const sameSite of <const>['unspecified', 'no_restriction', 'lax', 'strict']) {
      it(`sets cookies with samesite=${sameSite}`, async () => {
        const { cookies } = session.defaultSession;
        const value = 'hithere';
        await cookies.set({ url, value, sameSite });
        const c = (await cookies.get({ url }))[0];
        expect(c.name).to.be.empty();
        expect(c.value).to.equal(value);
        expect(c.sameSite).to.equal(sameSite);
      });
    }

    it('fails to set cookies with samesite=garbage', async () => {
      const { cookies } = session.defaultSession;
      const value = 'hithere';
      await expect(cookies.set({ url, value, sameSite: 'garbage' as any })).to.eventually.be.rejectedWith('Failed to convert \'garbage\' to an appropriate cookie same site value');
    });

    it('gets cookies without url', async () => {
      const { cookies } = session.defaultSession;
      const name = '1';
      const value = '1';

      await cookies.set({ url, name, value, expirationDate: (Date.now()) / 1000 + 120 });
      const cs = await cookies.get({ domain: '127.0.0.1' });
      expect(cs.some(c => c.name === name && c.value === value)).to.equal(true);
    });

    it('does not match on empty domain filter strings', async () => {
      const { cookies } = session.defaultSession;
      const name = crypto.randomBytes(20).toString('hex');
      const value = '1';
      const url = 'https://microsoft.com/';

      await cookies.set({ url, name, value });
      const cs = await cookies.get({ domain: '' });
      expect(cs.some(c => c.name === name && c.value === value)).to.equal(false);
      cookies.remove(url, name);
    });

    it('gets domain-equal cookies', async () => {
      const { cookies } = session.defaultSession;
      const name = crypto.randomBytes(20).toString('hex');
      const value = '1';
      const url = 'https://microsoft.com/';

      await cookies.set({ url, name, value });
      const cs = await cookies.get({ domain: 'microsoft.com' });
      expect(cs.some(c => c.name === name && c.value === value)).to.equal(true);
      cookies.remove(url, name);
    });

    it('gets domain-inclusive cookies', async () => {
      const { cookies } = session.defaultSession;
      const name = crypto.randomBytes(20).toString('hex');
      const value = '1';
      const url = 'https://subdomain.microsoft.com/';

      await cookies.set({ url, name, value });
      const cs = await cookies.get({ domain: 'microsoft.com' });
      expect(cs.some(c => c.name === name && c.value === value)).to.equal(true);
      cookies.remove(url, name);
    });

    it('omits domain-exclusive cookies', async () => {
      const { cookies } = session.defaultSession;
      const name = crypto.randomBytes(20).toString('hex');
      const value = '1';
      const url = 'https://microsoft.com';

      await cookies.set({ url, name, value });
      const cs = await cookies.get({ domain: 'subdomain.microsoft.com' });
      expect(cs.some(c => c.name === name && c.value === value)).to.equal(false);
      cookies.remove(url, name);
    });

    it('rejects when setting a cookie with missing required fields', async () => {
      const { cookies } = session.defaultSession;
      const name = '1';
      const value = '1';

      await expect(
        cookies.set({ url: '', name, value })
      ).to.eventually.be.rejectedWith('Failed to set cookie - The cookie was set with an invalid Domain attribute.');
    });

    it('rejects when setting a cookie with an invalid URL', async () => {
      const { cookies } = session.defaultSession;
      const name = '1';
      const value = '1';

      await expect(
        cookies.set({ url: 'asdf', name, value })
      ).to.eventually.be.rejectedWith('Failed to set cookie - The cookie was set with an invalid Domain attribute.');
    });

    it('rejects when setting a cookie with an invalid ASCII control character', async () => {
      const { cookies } = session.defaultSession;
      const name = 'BadCookie';
      const value = 'test;test';

      await expect(
        cookies.set({ url, name, value })
      ).to.eventually.be.rejectedWith('Failed to set cookie - The cookie contains ASCII control characters');
    });

    it('should overwrite previous cookies', async () => {
      const { cookies } = session.defaultSession;
      const name = 'DidOverwrite';
      for (const value of ['No', 'Yes']) {
        await cookies.set({ url, name, value, expirationDate: (Date.now()) / 1000 + 120 });
        const list = await cookies.get({ url });

        expect(list.some(cookie => cookie.name === name && cookie.value === value)).to.equal(true);
      }
    });

    it('should remove cookies', async () => {
      const { cookies } = session.defaultSession;
      const name = '2';
      const value = '2';

      await cookies.set({ url, name, value, expirationDate: (Date.now()) / 1000 + 120 });
      await cookies.remove(url, name);
      const list = await cookies.get({ url });

      expect(list.some(cookie => cookie.name === name && cookie.value === value)).to.equal(false);
    });

    // DISABLED-FIXME
    it('should set cookie for standard scheme', async () => {
      const { cookies } = session.defaultSession;
      const domain = 'fake-host';
      const url = `${standardScheme}://${domain}`;
      const name = 'custom';
      const value = '1';

      await cookies.set({ url, name, value, expirationDate: (Date.now()) / 1000 + 120 });
      const list = await cookies.get({ url });

      expect(list).to.have.lengthOf(1);
      expect(list[0]).to.have.property('name', name);
      expect(list[0]).to.have.property('value', value);
      expect(list[0]).to.have.property('domain', domain);
    });

    it('emits a changed event when a cookie is added or removed', async () => {
      const { cookies } = session.fromPartition('cookies-changed');
      const name = 'foo';
      const value = 'bar';

      const a = once(cookies, 'changed');
      await cookies.set({ url, name, value, expirationDate: (Date.now()) / 1000 + 120 });
      const [, setEventCookie, setEventCause, setEventRemoved] = await a;

      const b = once(cookies, 'changed');
      await cookies.remove(url, name);
      const [, removeEventCookie, removeEventCause, removeEventRemoved] = await b;

      expect(setEventCookie.name).to.equal(name);
      expect(setEventCookie.value).to.equal(value);
      expect(setEventCause).to.equal('explicit');
      expect(setEventRemoved).to.equal(false);

      expect(removeEventCookie.name).to.equal(name);
      expect(removeEventCookie.value).to.equal(value);
      expect(removeEventCause).to.equal('explicit');
      expect(removeEventRemoved).to.equal(true);
    });

    describe('ses.cookies.flushStore()', async () => {
      it('flushes the cookies to disk', async () => {
        const name = 'foo';
        const value = 'bar';
        const { cookies } = session.defaultSession;

        await cookies.set({ url, name, value });
        await cookies.flushStore();
      });
    });

    it('should survive an app restart for persistent partition', async function () {
      this.timeout(60000);
      const appPath = path.join(fixtures, 'api', 'cookie-app');

      const runAppWithPhase = (phase: string) => {
        return new Promise((resolve) => {
          let output = '';

          const appProcess = ChildProcess.spawn(
            process.execPath,
            [appPath],
            { env: { PHASE: phase, ...process.env } }
          );

          appProcess.stdout.on('data', data => { output += data; });
          appProcess.on('exit', () => {
            resolve(output.replaceAll(/(\r\n|\n|\r)/gm, ''));
          });
        });
      };

      expect(await runAppWithPhase('one')).to.equal('011');
      expect(await runAppWithPhase('two')).to.equal('110');
    });
  });

  describe('ses.clearStorageData(options)', () => {
    afterEach(closeAllWindows);
    it('clears localstorage data', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
      await w.loadFile(path.join(fixtures, 'api', 'localstorage.html'));
      await w.webContents.session.clearStorageData({
        origin: 'file://',
        storages: ['localstorage'],
        quotas: ['temporary']
      });
      while (await w.webContents.executeJavaScript('localStorage.length') !== 0) {
        // The storage clear isn't instantly visible to the renderer, so keep
        // trying until it is.
      }
    });
  });

  describe('will-download event', () => {
    afterEach(closeAllWindows);
    it('can cancel default download behavior', async () => {
      const w = new BrowserWindow({ show: false });
      const mockFile = Buffer.alloc(1024);
      const contentDisposition = 'inline; filename="mockFile.txt"';
      const downloadServer = http.createServer((req, res) => {
        res.writeHead(200, {
          'Content-Length': mockFile.length,
          'Content-Type': 'application/plain',
          'Content-Disposition': contentDisposition
        });
        res.end(mockFile);
        downloadServer.close();
      });
      const url = (await listen(downloadServer)).url;

      const downloadPrevented: Promise<{itemUrl: string, itemFilename: string, item: Electron.DownloadItem}> = new Promise(resolve => {
        w.webContents.session.once('will-download', function (e, item) {
          e.preventDefault();
          resolve({ itemUrl: item.getURL(), itemFilename: item.getFilename(), item });
        });
      });
      w.loadURL(url);
      const { item, itemUrl, itemFilename } = await downloadPrevented;
      expect(itemUrl).to.equal(url + '/');
      expect(itemFilename).to.equal('mockFile.txt');
      // Delay till the next tick.
      await new Promise(setImmediate);
      expect(() => item.getURL()).to.throw('DownloadItem used after being destroyed');
    });
  });

  describe('ses.protocol', () => {
    const partitionName = 'temp';
    const protocolName = 'sp';
    let customSession: Session;
    const protocol = session.defaultSession.protocol;
    const handler = (ignoredError: any, callback: Function) => {
      callback({ data: '<script>require(\'electron\').ipcRenderer.send(\'hello\')</script>', mimeType: 'text/html' });
    };

    beforeEach(async () => {
      customSession = session.fromPartition(partitionName);
      await customSession.protocol.registerStringProtocol(protocolName, handler);
    });

    afterEach(async () => {
      await customSession.protocol.unregisterProtocol(protocolName);
      customSession = null as any;
    });
    afterEach(closeAllWindows);

    it('does not affect defaultSession', () => {
      const result1 = protocol.isProtocolRegistered(protocolName);
      expect(result1).to.equal(false);

      const result2 = customSession.protocol.isProtocolRegistered(protocolName);
      expect(result2).to.equal(true);
    });

    it('handles requests from partition', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          partition: partitionName,
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      customSession = session.fromPartition(partitionName);
      await customSession.protocol.registerStringProtocol(protocolName, handler);
      w.loadURL(`${protocolName}://fake-host`);
      await once(ipcMain, 'hello');
    });
  });

  describe('ses.setProxy(options)', () => {
    let server: http.Server;
    let customSession: Electron.Session;
    let created = false;

    beforeEach(async () => {
      customSession = session.fromPartition('proxyconfig');
      if (!created) {
        // Work around for https://github.com/electron/electron/issues/26166 to
        // reduce flake
        await setTimeout(100);
        created = true;
      }
    });

    afterEach(() => {
      if (server) {
        server.close();
      }
      customSession = null as any;
    });

    it('allows configuring proxy settings', async () => {
      const config = { proxyRules: 'http=myproxy:80' };
      await customSession.setProxy(config);
      const proxy = await customSession.resolveProxy('http://example.com/');
      expect(proxy).to.equal('PROXY myproxy:80');
    });

    it('allows removing the implicit bypass rules for localhost', async () => {
      const config = {
        proxyRules: 'http=myproxy:80',
        proxyBypassRules: '<-loopback>'
      };

      await customSession.setProxy(config);
      const proxy = await customSession.resolveProxy('http://localhost');
      expect(proxy).to.equal('PROXY myproxy:80');
    });

    it('allows configuring proxy settings with pacScript', async () => {
      server = http.createServer((req, res) => {
        const pac = `
          function FindProxyForURL(url, host) {
            return "PROXY myproxy:8132";
          }
        `;
        res.writeHead(200, {
          'Content-Type': 'application/x-ns-proxy-autoconfig'
        });
        res.end(pac);
      });
      const { url } = await listen(server);
      {
        const config = { pacScript: url };
        await customSession.setProxy(config);
        const proxy = await customSession.resolveProxy('https://google.com');
        expect(proxy).to.equal('PROXY myproxy:8132');
      }
      {
        const config = { mode: 'pac_script' as any, pacScript: url };
        await customSession.setProxy(config);
        const proxy = await customSession.resolveProxy('https://google.com');
        expect(proxy).to.equal('PROXY myproxy:8132');
      }
    });

    it('allows bypassing proxy settings', async () => {
      const config = {
        proxyRules: 'http=myproxy:80',
        proxyBypassRules: '<local>'
      };
      await customSession.setProxy(config);
      const proxy = await customSession.resolveProxy('http://example/');
      expect(proxy).to.equal('DIRECT');
    });

    it('allows configuring proxy settings with mode `direct`', async () => {
      const config = { mode: 'direct' as const, proxyRules: 'http=myproxy:80' };
      await customSession.setProxy(config);
      const proxy = await customSession.resolveProxy('http://example.com/');
      expect(proxy).to.equal('DIRECT');
    });

    it('allows configuring proxy settings with mode `auto_detect`', async () => {
      const config = { mode: 'auto_detect' as const };
      await customSession.setProxy(config);
    });

    it('allows configuring proxy settings with mode `pac_script`', async () => {
      const config = { mode: 'pac_script' as const };
      await customSession.setProxy(config);
      const proxy = await customSession.resolveProxy('http://example.com/');
      expect(proxy).to.equal('DIRECT');
    });

    it('allows configuring proxy settings with mode `fixed_servers`', async () => {
      const config = { mode: 'fixed_servers' as const, proxyRules: 'http=myproxy:80' };
      await customSession.setProxy(config);
      const proxy = await customSession.resolveProxy('http://example.com/');
      expect(proxy).to.equal('PROXY myproxy:80');
    });

    it('allows configuring proxy settings with mode `system`', async () => {
      const config = { mode: 'system' as const };
      await customSession.setProxy(config);
    });

    it('disallows configuring proxy settings with mode `invalid`', async () => {
      const config = { mode: 'invalid' as any };
      await expect(customSession.setProxy(config)).to.eventually.be.rejectedWith(/Invalid mode/);
    });

    it('reload proxy configuration', async () => {
      let proxyPort = 8132;
      server = http.createServer((req, res) => {
        const pac = `
          function FindProxyForURL(url, host) {
            return "PROXY myproxy:${proxyPort}";
          }
        `;
        res.writeHead(200, {
          'Content-Type': 'application/x-ns-proxy-autoconfig'
        });
        res.end(pac);
      });
      const { url } = await listen(server);
      const config = { mode: 'pac_script' as const, pacScript: url };
      await customSession.setProxy(config);
      {
        const proxy = await customSession.resolveProxy('https://google.com');
        expect(proxy).to.equal(`PROXY myproxy:${proxyPort}`);
      }
      {
        proxyPort = 8133;
        await customSession.forceReloadProxyConfig();
        const proxy = await customSession.resolveProxy('https://google.com');
        expect(proxy).to.equal(`PROXY myproxy:${proxyPort}`);
      }
    });
  });

  describe('ses.resolveHost(host)', () => {
    let customSession: Electron.Session;

    beforeEach(async () => {
      customSession = session.fromPartition('resolvehost');
    });

    afterEach(() => {
      customSession = null as any;
    });

    it('resolves ipv4.localhost2', async () => {
      const { endpoints } = await customSession.resolveHost('ipv4.localhost2');
      expect(endpoints).to.be.a('array');
      expect(endpoints).to.have.lengthOf(1);
      expect(endpoints[0].family).to.equal('ipv4');
      expect(endpoints[0].address).to.equal('10.0.0.1');
    });

    it('fails to resolve AAAA record for ipv4.localhost2', async () => {
      await expect(customSession.resolveHost('ipv4.localhost2', {
        queryType: 'AAAA'
      }))
        .to.eventually.be.rejectedWith(/net::ERR_NAME_NOT_RESOLVED/);
    });

    it('resolves ipv6.localhost2', async () => {
      const { endpoints } = await customSession.resolveHost('ipv6.localhost2');
      expect(endpoints).to.be.a('array');
      expect(endpoints).to.have.lengthOf(1);
      expect(endpoints[0].family).to.equal('ipv6');
      expect(endpoints[0].address).to.equal('::1');
    });

    it('fails to resolve A record for ipv6.localhost2', async () => {
      await expect(customSession.resolveHost('notfound.localhost2', {
        queryType: 'A'
      }))
        .to.eventually.be.rejectedWith(/net::ERR_NAME_NOT_RESOLVED/);
    });

    it('fails to resolve notfound.localhost2', async () => {
      await expect(customSession.resolveHost('notfound.localhost2'))
        .to.eventually.be.rejectedWith(/net::ERR_NAME_NOT_RESOLVED/);
    });
  });

  describe('ses.getBlobData()', () => {
    const scheme = 'cors-blob';
    const protocol = session.defaultSession.protocol;
    const url = `${scheme}://host`;
    after(async () => {
      await protocol.unregisterProtocol(scheme);
    });
    afterEach(closeAllWindows);

    it('returns blob data for uuid', (done) => {
      const postData = JSON.stringify({
        type: 'blob',
        value: 'hello'
      });
      const content = `<html>
                       <script>
                       let fd = new FormData();
                       fd.append('file', new Blob(['${postData}'], {type:'application/json'}));
                       fetch('${url}', {method:'POST', body: fd });
                       </script>
                       </html>`;

      protocol.registerStringProtocol(scheme, (request, callback) => {
        try {
          if (request.method === 'GET') {
            callback({ data: content, mimeType: 'text/html' });
          } else if (request.method === 'POST') {
            const uuid = request.uploadData![1].blobUUID;
            expect(uuid).to.be.a('string');
            session.defaultSession.getBlobData(uuid!).then(result => {
              try {
                expect(result.toString()).to.equal(postData);
                done();
              } catch (e) {
                done(e);
              }
            });
          }
        } catch (e) {
          done(e);
        }
      });
      const w = new BrowserWindow({ show: false });
      w.loadURL(url);
    });
  });

  describe('ses.getBlobData2()', () => {
    const scheme = 'cors-blob';
    const protocol = session.defaultSession.protocol;
    const url = `${scheme}://host`;
    after(async () => {
      await protocol.unregisterProtocol(scheme);
    });
    afterEach(closeAllWindows);

    it('returns blob data for uuid', (done) => {
      const content = `<html>
                       <script>
                       let fd = new FormData();
                       fd.append("data", new Blob(new Array(65_537).fill('a')));
                       fetch('${url}', {method:'POST', body: fd });
                       </script>
                       </html>`;

      protocol.registerStringProtocol(scheme, (request, callback) => {
        try {
          if (request.method === 'GET') {
            callback({ data: content, mimeType: 'text/html' });
          } else if (request.method === 'POST') {
            const uuid = request.uploadData![1].blobUUID;
            expect(uuid).to.be.a('string');
            session.defaultSession.getBlobData(uuid!).then(result => {
              try {
                const data = new Array(65_537).fill('a');
                expect(result.toString()).to.equal(data.join(''));
                done();
              } catch (e) {
                done(e);
              }
            });
          }
        } catch (e) {
          done(e);
        }
      });
      const w = new BrowserWindow({ show: false });
      w.loadURL(url);
    });
  });

  describe('ses.setCertificateVerifyProc(callback)', () => {
    let server: http.Server;
    let serverUrl: string;

    beforeEach(async () => {
      const certPath = path.join(fixtures, 'certificates');
      const options = {
        key: fs.readFileSync(path.join(certPath, 'server.key')),
        cert: fs.readFileSync(path.join(certPath, 'server.pem')),
        ca: [
          fs.readFileSync(path.join(certPath, 'rootCA.pem')),
          fs.readFileSync(path.join(certPath, 'intermediateCA.pem'))
        ],
        rejectUnauthorized: false
      };

      server = https.createServer(options, (req, res) => {
        res.writeHead(200);
        res.end('<title>hello</title>');
      });
      serverUrl = (await listen(server)).url;
    });

    afterEach((done) => {
      server.close(done);
    });
    afterEach(closeAllWindows);

    it('accepts the request when the callback is called with 0', async () => {
      const ses = session.fromPartition(`${Math.random()}`);
      let validate: () => void;
      ses.setCertificateVerifyProc(({ hostname, verificationResult, errorCode }, callback) => {
        if (hostname !== '127.0.0.1') return callback(-3);
        validate = () => {
          expect(verificationResult).to.be.oneOf(['net::ERR_CERT_AUTHORITY_INVALID', 'net::ERR_CERT_COMMON_NAME_INVALID']);
          expect(errorCode).to.be.oneOf([-202, -200]);
        };
        callback(0);
      });

      const w = new BrowserWindow({ show: false, webPreferences: { session: ses } });
      await w.loadURL(serverUrl);
      expect(w.webContents.getTitle()).to.equal('hello');
      expect(validate!).not.to.be.undefined();
      validate!();
    });

    it('rejects the request when the callback is called with -2', async () => {
      const ses = session.fromPartition(`${Math.random()}`);
      let validate: () => void;
      ses.setCertificateVerifyProc(({ hostname, certificate, verificationResult, isIssuedByKnownRoot }, callback) => {
        if (hostname !== '127.0.0.1') return callback(-3);
        validate = () => {
          expect(certificate.issuerName).to.equal('Intermediate CA');
          expect(certificate.subjectName).to.equal('localhost');
          expect(certificate.issuer.commonName).to.equal('Intermediate CA');
          expect(certificate.subject.commonName).to.equal('localhost');
          expect(certificate.issuerCert.issuer.commonName).to.equal('Root CA');
          expect(certificate.issuerCert.subject.commonName).to.equal('Intermediate CA');
          expect(certificate.issuerCert.issuerCert.issuer.commonName).to.equal('Root CA');
          expect(certificate.issuerCert.issuerCert.subject.commonName).to.equal('Root CA');
          expect(certificate.issuerCert.issuerCert.issuerCert).to.equal(undefined);
          expect(verificationResult).to.be.oneOf(['net::ERR_CERT_AUTHORITY_INVALID', 'net::ERR_CERT_COMMON_NAME_INVALID']);
          expect(isIssuedByKnownRoot).to.be.false();
        };
        callback(-2);
      });

      const w = new BrowserWindow({ show: false, webPreferences: { session: ses } });
      await expect(w.loadURL(serverUrl)).to.eventually.be.rejectedWith(/ERR_FAILED/);
      expect(validate!).not.to.be.undefined();
      validate!();
    });

    it('saves cached results', async () => {
      const ses = session.fromPartition(`${Math.random()}`);
      let numVerificationRequests = 0;
      ses.setCertificateVerifyProc((e, callback) => {
        if (e.hostname !== '127.0.0.1') return callback(-3);
        numVerificationRequests++;
        callback(-2);
      });

      const w = new BrowserWindow({ show: false, webPreferences: { session: ses } });
      await expect(w.loadURL(serverUrl), 'first load').to.eventually.be.rejectedWith(/ERR_FAILED/);
      await once(w.webContents, 'did-stop-loading');
      await expect(w.loadURL(serverUrl + '/test'), 'second load').to.eventually.be.rejectedWith(/ERR_FAILED/);
      expect(numVerificationRequests).to.equal(1);
    });

    it('does not cancel requests in other sessions', async () => {
      const ses1 = session.fromPartition(`${Math.random()}`);
      ses1.setCertificateVerifyProc((opts, cb) => cb(0));
      const ses2 = session.fromPartition(`${Math.random()}`);

      const req = net.request({ url: serverUrl, session: ses1, credentials: 'include' });
      req.end();
      setTimeout().then(() => {
        ses2.setCertificateVerifyProc((opts, callback) => callback(0));
      });
      await expect(new Promise<void>((resolve, reject) => {
        req.on('error', (err) => {
          reject(err);
        });
        req.on('response', () => {
          resolve();
        });
      })).to.eventually.be.fulfilled();
    });
  });

  describe('ses.clearAuthCache()', () => {
    it('can clear http auth info from cache', async () => {
      const ses = session.fromPartition('auth-cache');
      const server = http.createServer((req, res) => {
        const credentials = auth(req);
        if (!credentials || credentials.name !== 'test' || credentials.pass !== 'test') {
          res.statusCode = 401;
          res.setHeader('WWW-Authenticate', 'Basic realm="Restricted"');
          res.end();
        } else {
          res.end('authenticated');
        }
      });
      const { port } = await listen(server);
      const fetch = (url: string) => new Promise((resolve, reject) => {
        const request = net.request({ url, session: ses });
        request.on('response', (response) => {
          let data: string | null = null;
          response.on('data', (chunk) => {
            if (!data) {
              data = '';
            }
            data += chunk;
          });
          response.on('end', () => {
            if (!data) {
              reject(new Error('Empty response'));
            } else {
              resolve(data);
            }
          });
          response.on('error', (error: any) => { reject(new Error(error)); });
        });
        request.on('error', (error: any) => { reject(new Error(error)); });
        request.end();
      });
      // the first time should throw due to unauthenticated
      await expect(fetch(`http://127.0.0.1:${port}`)).to.eventually.be.rejected();
      // passing the password should let us in
      expect(await fetch(`http://test:test@127.0.0.1:${port}`)).to.equal('authenticated');
      // subsequently, the credentials are cached
      expect(await fetch(`http://127.0.0.1:${port}`)).to.equal('authenticated');
      await ses.clearAuthCache();
      // once the cache is cleared, we should get an error again
      await expect(fetch(`http://127.0.0.1:${port}`)).to.eventually.be.rejected();
    });
  });

  describe('DownloadItem', () => {
    const mockPDF = Buffer.alloc(1024 * 1024 * 5);
    const downloadFilePath = path.join(__dirname, '..', 'fixtures', 'mock.pdf');
    const protocolName = 'custom-dl';
    const contentDisposition = 'inline; filename="mock.pdf"';
    let port: number;
    let downloadServer: http.Server;

    before(async () => {
      downloadServer = http.createServer((req, res) => {
        res.writeHead(200, {
          'Content-Length': mockPDF.length,
          'Content-Type': 'application/pdf',
          'Content-Disposition': req.url === '/?testFilename' ? 'inline' : contentDisposition
        });
        res.end(mockPDF);
      });
      port = (await listen(downloadServer)).port;
    });

    after(async () => {
      await new Promise(resolve => downloadServer.close(resolve));
    });

    afterEach(closeAllWindows);

    const isPathEqual = (path1: string, path2: string) => {
      return path.relative(path1, path2) === '';
    };
    const assertDownload = (state: string, item: Electron.DownloadItem, isCustom = false) => {
      expect(state).to.equal('completed');
      expect(item.getFilename()).to.equal('mock.pdf');
      expect(path.isAbsolute(item.savePath)).to.equal(true);
      expect(isPathEqual(item.savePath, downloadFilePath)).to.equal(true);
      if (isCustom) {
        expect(item.getURL()).to.equal(`${protocolName}://item`);
      } else {
        expect(item.getURL()).to.be.equal(`${url}:${port}/`);
      }
      expect(item.getMimeType()).to.equal('application/pdf');
      expect(item.getReceivedBytes()).to.equal(mockPDF.length);
      expect(item.getTotalBytes()).to.equal(mockPDF.length);
      expect(item.getContentDisposition()).to.equal(contentDisposition);
      expect(fs.existsSync(downloadFilePath)).to.equal(true);
      fs.unlinkSync(downloadFilePath);
    };

    describe('session.downloadURL', () => {
      it('can perform a download', (done) => {
        session.defaultSession.once('will-download', function (e, item) {
          item.savePath = downloadFilePath;
          item.on('done', function (e, state) {
            try {
              assertDownload(state, item);
              done();
            } catch (e) {
              done(e);
            }
          });
        });
        session.defaultSession.downloadURL(`${url}:${port}`);
      });

      it('can perform a download with a valid auth header', async () => {
        const server = http.createServer((req, res) => {
          const { authorization } = req.headers;
          if (!authorization || authorization !== 'Basic i-am-an-auth-header') {
            res.statusCode = 401;
            res.setHeader('WWW-Authenticate', 'Basic realm="Restricted"');
            res.end();
          } else {
            res.writeHead(200, {
              'Content-Length': mockPDF.length,
              'Content-Type': 'application/pdf',
              'Content-Disposition': req.url === '/?testFilename' ? 'inline' : contentDisposition
            });
            res.end(mockPDF);
          }
        });

        const { port } = await listen(server);

        const downloadDone: Promise<Electron.DownloadItem> = new Promise((resolve) => {
          session.defaultSession.once('will-download', (e, item) => {
            item.savePath = downloadFilePath;
            item.on('done', () => {
              try {
                resolve(item);
              } catch { }
            });
          });
        });

        session.defaultSession.downloadURL(`${url}:${port}`, {
          headers: {
            Authorization: 'Basic i-am-an-auth-header'
          }
        });

        const today = Math.floor(Date.now() / 1000);
        const item = await downloadDone;
        expect(item.getState()).to.equal('completed');
        expect(item.getFilename()).to.equal('mock.pdf');
        expect(item.getMimeType()).to.equal('application/pdf');
        expect(item.getReceivedBytes()).to.equal(mockPDF.length);
        expect(item.getTotalBytes()).to.equal(mockPDF.length);
        expect(item.getPercentComplete()).to.equal(100);
        expect(item.getCurrentBytesPerSecond()).to.equal(0);
        expect(item.getContentDisposition()).to.equal(contentDisposition);

        const start = item.getStartTime();
        const end = item.getEndTime();
        expect(start).to.be.greaterThan(today);
        expect(end).to.be.greaterThan(start);
      });

      it('throws when called with invalid headers', () => {
        expect(() => {
          session.defaultSession.downloadURL(`${url}:${port}`, {
            // @ts-ignore this line is intentionally incorrect
            headers: 'i-am-a-bad-header'
          });
        }).to.throw(/Invalid value for headers - must be an object/);
      });

      it('correctly handles a download with an invalid auth header', async () => {
        const server = http.createServer((req, res) => {
          const { authorization } = req.headers;
          if (!authorization || authorization !== 'Basic i-am-an-auth-header') {
            res.statusCode = 401;
            res.setHeader('WWW-Authenticate', 'Basic realm="Restricted"');
            res.end();
          } else {
            res.writeHead(200, {
              'Content-Length': mockPDF.length,
              'Content-Type': 'application/pdf',
              'Content-Disposition': req.url === '/?testFilename' ? 'inline' : contentDisposition
            });
            res.end(mockPDF);
          }
        });

        const { port } = await listen(server);

        const downloadFailed: Promise<Electron.DownloadItem> = new Promise((resolve) => {
          session.defaultSession.once('will-download', (_, item) => {
            item.savePath = downloadFilePath;
            item.on('done', (e, state) => {
              console.log(state);
              try {
                resolve(item);
              } catch { }
            });
          });
        });

        session.defaultSession.downloadURL(`${url}:${port}`, {
          headers: {
            Authorization: 'wtf-is-this'
          }
        });

        const item = await downloadFailed;
        expect(item.getState()).to.equal('interrupted');
        expect(item.getReceivedBytes()).to.equal(0);
        expect(item.getTotalBytes()).to.equal(0);
      });
    });

    describe('webContents.downloadURL', () => {
      it('can perform a download', (done) => {
        const w = new BrowserWindow({ show: false });
        w.webContents.session.once('will-download', function (e, item) {
          item.savePath = downloadFilePath;
          item.on('done', function (e, state) {
            try {
              assertDownload(state, item);
              done();
            } catch (e) {
              done(e);
            }
          });
        });
        w.webContents.downloadURL(`${url}:${port}`);
      });

      it('can perform a download with a valid auth header', async () => {
        const server = http.createServer((req, res) => {
          const { authorization } = req.headers;
          if (!authorization || authorization !== 'Basic i-am-an-auth-header') {
            res.statusCode = 401;
            res.setHeader('WWW-Authenticate', 'Basic realm="Restricted"');
            res.end();
          } else {
            res.writeHead(200, {
              'Content-Length': mockPDF.length,
              'Content-Type': 'application/pdf',
              'Content-Disposition': req.url === '/?testFilename' ? 'inline' : contentDisposition
            });
            res.end(mockPDF);
          }
        });

        const { port } = await listen(server);

        const w = new BrowserWindow({ show: false });
        const downloadDone: Promise<Electron.DownloadItem> = new Promise((resolve) => {
          w.webContents.session.once('will-download', (e, item) => {
            item.savePath = downloadFilePath;
            item.on('done', () => {
              try {
                resolve(item);
              } catch { }
            });
          });
        });

        w.webContents.downloadURL(`${url}:${port}`, {
          headers: {
            Authorization: 'Basic i-am-an-auth-header'
          }
        });

        const item = await downloadDone;
        expect(item.getState()).to.equal('completed');
        expect(item.getFilename()).to.equal('mock.pdf');
        expect(item.getMimeType()).to.equal('application/pdf');
        expect(item.getReceivedBytes()).to.equal(mockPDF.length);
        expect(item.getTotalBytes()).to.equal(mockPDF.length);
        expect(item.getContentDisposition()).to.equal(contentDisposition);
      });

      it('throws when called with invalid headers', () => {
        const w = new BrowserWindow({ show: false });
        expect(() => {
          w.webContents.downloadURL(`${url}:${port}`, {
            // @ts-ignore this line is intentionally incorrect
            headers: 'i-am-a-bad-header'
          });
        }).to.throw(/Invalid value for headers - must be an object/);
      });

      it('correctly handles a download and an invalid auth header', async () => {
        const server = http.createServer((req, res) => {
          const { authorization } = req.headers;
          if (!authorization || authorization !== 'Basic i-am-an-auth-header') {
            res.statusCode = 401;
            res.setHeader('WWW-Authenticate', 'Basic realm="Restricted"');
            res.end();
          } else {
            res.writeHead(200, {
              'Content-Length': mockPDF.length,
              'Content-Type': 'application/pdf',
              'Content-Disposition': req.url === '/?testFilename' ? 'inline' : contentDisposition
            });
            res.end(mockPDF);
          }
        });

        const { port } = await listen(server);

        const w = new BrowserWindow({ show: false });
        const downloadFailed: Promise<Electron.DownloadItem> = new Promise((resolve) => {
          w.webContents.session.once('will-download', (_, item) => {
            item.savePath = downloadFilePath;
            item.on('done', (e, state) => {
              console.log(state);
              try {
                resolve(item);
              } catch { }
            });
          });
        });

        w.webContents.downloadURL(`${url}:${port}`, {
          headers: {
            Authorization: 'wtf-is-this'
          }
        });

        const item = await downloadFailed;
        expect(item.getState()).to.equal('interrupted');
        expect(item.getReceivedBytes()).to.equal(0);
        expect(item.getTotalBytes()).to.equal(0);
      });

      it('can download from custom protocols', (done) => {
        const protocol = session.defaultSession.protocol;
        const handler = (ignoredError: any, callback: Function) => {
          callback({ url: `${url}:${port}` });
        };
        protocol.registerHttpProtocol(protocolName, handler);
        const w = new BrowserWindow({ show: false });
        w.webContents.session.once('will-download', function (e, item) {
          item.savePath = downloadFilePath;
          item.on('done', function (e, state) {
            try {
              assertDownload(state, item, true);
              done();
            } catch (e) {
              done(e);
            }
          });
        });
        w.webContents.downloadURL(`${protocolName}://item`);
      });

      it('can cancel download', (done) => {
        const w = new BrowserWindow({ show: false });
        w.webContents.session.once('will-download', function (e, item) {
          item.savePath = downloadFilePath;
          item.on('done', function (e, state) {
            try {
              expect(state).to.equal('cancelled');
              expect(item.getFilename()).to.equal('mock.pdf');
              expect(item.getMimeType()).to.equal('application/pdf');
              expect(item.getReceivedBytes()).to.equal(0);
              expect(item.getTotalBytes()).to.equal(mockPDF.length);
              expect(item.getContentDisposition()).to.equal(contentDisposition);
              done();
            } catch (e) {
              done(e);
            }
          });
          item.cancel();
        });
        w.webContents.downloadURL(`${url}:${port}/`);
      });

      it('can generate a default filename', function (done) {
        if (process.env.APPVEYOR === 'True') {
          // FIXME(alexeykuzmin): Skip the test.
          // this.skip()
          return done();
        }

        const w = new BrowserWindow({ show: false });
        w.webContents.session.once('will-download', function (e, item) {
          item.savePath = downloadFilePath;
          item.on('done', function () {
            try {
              expect(item.getFilename()).to.equal('download.pdf');
              done();
            } catch (e) {
              done(e);
            }
          });
          item.cancel();
        });
        w.webContents.downloadURL(`${url}:${port}/?testFilename`);
      });

      it('can set options for the save dialog', (done) => {
        const filePath = path.join(__dirname, 'fixtures', 'mock.pdf');
        const options = {
          window: null,
          title: 'title',
          message: 'message',
          buttonLabel: 'buttonLabel',
          nameFieldLabel: 'nameFieldLabel',
          defaultPath: '/',
          filters: [{
            name: '1', extensions: ['.1', '.2']
          }, {
            name: '2', extensions: ['.3', '.4', '.5']
          }],
          showsTagField: true,
          securityScopedBookmarks: true
        };

        const w = new BrowserWindow({ show: false });
        w.webContents.session.once('will-download', function (e, item) {
          item.setSavePath(filePath);
          item.setSaveDialogOptions(options);
          item.on('done', function () {
            try {
              expect(item.getSaveDialogOptions()).to.deep.equal(options);
              done();
            } catch (e) {
              done(e);
            }
          });
          item.cancel();
        });
        w.webContents.downloadURL(`${url}:${port}`);
      });

      describe('when a save path is specified and the URL is unavailable', () => {
        it('does not display a save dialog and reports the done state as interrupted', (done) => {
          const w = new BrowserWindow({ show: false });
          w.webContents.session.once('will-download', function (e, item) {
            item.savePath = downloadFilePath;
            if (item.getState() === 'interrupted') {
              item.resume();
            }
            item.on('done', function (e, state) {
              try {
                expect(state).to.equal('interrupted');
                done();
              } catch (e) {
                done(e);
              }
            });
          });
          w.webContents.downloadURL(`file://${path.join(__dirname, 'does-not-exist.txt')}`);
        });
      });
    });

    describe('WebView.downloadURL', () => {
      it('can perform a download', async () => {
        const w = new BrowserWindow({ show: false, webPreferences: { webviewTag: true } });
        await w.loadURL('about:blank');
        function webviewDownload ({ fixtures, url, port }: { fixtures: string, url: string, port: string }) {
          const webview = new (window as any).WebView();
          webview.addEventListener('did-finish-load', () => {
            webview.downloadURL(`${url}:${port}/`);
          });
          webview.src = `file://${fixtures}/api/blank.html`;
          document.body.appendChild(webview);
        }
        const done: Promise<[string, Electron.DownloadItem]> = new Promise(resolve => {
          w.webContents.session.once('will-download', function (e, item) {
            item.savePath = downloadFilePath;
            item.on('done', function (e, state) {
              resolve([state, item]);
            });
          });
        });
        await w.webContents.executeJavaScript(`(${webviewDownload})(${JSON.stringify({ fixtures, url, port })})`);
        const [state, item] = await done;
        assertDownload(state, item);
      });
    });
  });

  describe('ses.createInterruptedDownload(options)', () => {
    afterEach(closeAllWindows);
    it('can create an interrupted download item', async () => {
      const downloadFilePath = path.join(__dirname, '..', 'fixtures', 'mock.pdf');
      const options = {
        path: downloadFilePath,
        urlChain: ['http://127.0.0.1/'],
        mimeType: 'application/pdf',
        offset: 0,
        length: 5242880
      };
      const w = new BrowserWindow({ show: false });
      const p = once(w.webContents.session, 'will-download');
      w.webContents.session.createInterruptedDownload(options);
      const [, item] = await p;
      expect(item.getState()).to.equal('interrupted');
      item.cancel();
      expect(item.getURLChain()).to.deep.equal(options.urlChain);
      expect(item.getMimeType()).to.equal(options.mimeType);
      expect(item.getReceivedBytes()).to.equal(options.offset);
      expect(item.getTotalBytes()).to.equal(options.length);
      expect(item.savePath).to.equal(downloadFilePath);
    });

    it('can be resumed', async () => {
      const downloadFilePath = path.join(fixtures, 'logo.png');
      const rangeServer = http.createServer((req, res) => {
        const options = { root: fixtures };
        send(req, req.url!, options)
          .on('error', (error: any) => { throw error; }).pipe(res);
      });
      try {
        const { url } = await listen(rangeServer);
        const w = new BrowserWindow({ show: false });
        const downloadCancelled: Promise<Electron.DownloadItem> = new Promise((resolve) => {
          w.webContents.session.once('will-download', function (e, item) {
            item.setSavePath(downloadFilePath);
            item.on('done', function () {
              resolve(item);
            });
            item.cancel();
          });
        });
        const downloadUrl = `${url}/assets/logo.png`;
        w.webContents.downloadURL(downloadUrl);
        const item = await downloadCancelled;
        expect(item.getState()).to.equal('cancelled');

        const options = {
          path: item.savePath,
          urlChain: item.getURLChain(),
          mimeType: item.getMimeType(),
          offset: item.getReceivedBytes(),
          length: item.getTotalBytes(),
          lastModified: item.getLastModifiedTime(),
          eTag: item.getETag()
        };
        const downloadResumed: Promise<Electron.DownloadItem> = new Promise((resolve) => {
          w.webContents.session.once('will-download', function (e, item) {
            expect(item.getState()).to.equal('interrupted');
            item.setSavePath(downloadFilePath);
            item.resume();
            item.on('done', function () {
              resolve(item);
            });
          });
        });
        w.webContents.session.createInterruptedDownload(options);
        const completedItem = await downloadResumed;
        expect(completedItem.getState()).to.equal('completed');
        expect(completedItem.getFilename()).to.equal('logo.png');
        expect(completedItem.savePath).to.equal(downloadFilePath);
        expect(completedItem.getURL()).to.equal(downloadUrl);
        expect(completedItem.getMimeType()).to.equal('image/png');
        expect(completedItem.getReceivedBytes()).to.equal(14022);
        expect(completedItem.getTotalBytes()).to.equal(14022);
        expect(fs.existsSync(downloadFilePath)).to.equal(true);
      } finally {
        rangeServer.close();
      }
    });
  });

  describe('ses.setPermissionRequestHandler(handler)', () => {
    afterEach(closeAllWindows);
    // These tests are done on an http server because navigator.userAgentData
    // requires a secure context.
    let server: http.Server;
    let serverUrl: string;
    before(async () => {
      server = http.createServer((req, res) => {
        res.setHeader('Content-Type', 'text/html');
        res.end('');
      });
      serverUrl = (await listen(server)).url;
    });
    after(() => {
      server.close();
    });

    it('cancels any pending requests when cleared', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          partition: 'very-temp-permission-handler',
          nodeIntegration: true,
          contextIsolation: false
        }
      });

      const ses = w.webContents.session;
      ses.setPermissionRequestHandler(() => {
        ses.setPermissionRequestHandler(null);
      });

      ses.protocol.interceptStringProtocol('https', (req, cb) => {
        cb(`<html><script>(${remote})()</script></html>`);
      });

      const result = once(require('electron').ipcMain, 'message');

      function remote () {
        (navigator as any).requestMIDIAccess({ sysex: true }).then(() => {}, (err: any) => {
          require('electron').ipcRenderer.send('message', err.name);
        });
      }

      await w.loadURL('https://myfakesite');

      const [, name] = await result;
      expect(name).to.deep.equal('SecurityError');
    });

    it('successfully resolves when calling legacy getUserMedia', async () => {
      const ses = session.fromPartition('' + Math.random());
      ses.setPermissionRequestHandler(
        (_webContents, _permission, callback) => {
          callback(true);
        }
      );

      const w = new BrowserWindow({ show: false, webPreferences: { session: ses } });
      await w.loadURL(serverUrl);
      const { ok, message } = await w.webContents.executeJavaScript(`
        new Promise((resolve, reject) => navigator.getUserMedia({
          video: true,
          audio: true,
        }, x => resolve({ok: x instanceof MediaStream}), e => reject({ok: false, message: e.message})))
      `);
      expect(ok).to.be.true(message);
    });

    it('successfully rejects when calling legacy getUserMedia', async () => {
      const ses = session.fromPartition('' + Math.random());
      ses.setPermissionRequestHandler(
        (_webContents, _permission, callback) => {
          callback(false);
        }
      );

      const w = new BrowserWindow({ show: false, webPreferences: { session: ses } });
      await w.loadURL(serverUrl);
      await expect(w.webContents.executeJavaScript(`
        new Promise((resolve, reject) => navigator.getUserMedia({
          video: true,
          audio: true,
        }, x => resolve({ok: x instanceof MediaStream}), e => reject({ok: false, message: e.message})))
      `)).to.eventually.be.rejectedWith('Permission denied');
    });
  });

  describe('ses.setPermissionCheckHandler(handler)', () => {
    afterEach(closeAllWindows);
    it('details provides requestingURL for mainFrame', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          partition: 'very-temp-permission-handler'
        }
      });
      const ses = w.webContents.session;
      const loadUrl = 'https://myfakesite/';
      let handlerDetails : Electron.PermissionCheckHandlerHandlerDetails;

      ses.protocol.interceptStringProtocol('https', (req, cb) => {
        cb('<html><script>console.log(\'test\');</script></html>');
      });

      ses.setPermissionCheckHandler((wc, permission, requestingOrigin, details) => {
        if (permission === 'clipboard-read') {
          handlerDetails = details;
          return true;
        }
        return false;
      });

      const readClipboardPermission: any = () => {
        return w.webContents.executeJavaScript(`
          navigator.permissions.query({name: 'clipboard-read'})
              .then(permission => permission.state).catch(err => err.message);
        `, true);
      };

      await w.loadURL(loadUrl);
      const state = await readClipboardPermission();
      expect(state).to.equal('granted');
      expect(handlerDetails!.requestingUrl).to.equal(loadUrl);
    });

    it('details provides requestingURL for cross origin subFrame', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          partition: 'very-temp-permission-handler'
        }
      });
      const ses = w.webContents.session;
      const loadUrl = 'https://myfakesite/';
      let handlerDetails : Electron.PermissionCheckHandlerHandlerDetails;

      ses.protocol.interceptStringProtocol('https', (req, cb) => {
        cb('<html><script>console.log(\'test\');</script></html>');
      });

      ses.setPermissionCheckHandler((wc, permission, requestingOrigin, details) => {
        if (permission === 'clipboard-read') {
          handlerDetails = details;
          return true;
        }
        return false;
      });

      const readClipboardPermission: any = (frame: WebFrameMain) => {
        return frame.executeJavaScript(`
          navigator.permissions.query({name: 'clipboard-read'})
              .then(permission => permission.state).catch(err => err.message);
        `, true);
      };

      await w.loadFile(path.join(fixtures, 'api', 'blank.html'));
      w.webContents.executeJavaScript(`
        var iframe = document.createElement('iframe');
        iframe.src = '${loadUrl}';
        iframe.allow = 'clipboard-read';
        document.body.appendChild(iframe);
        null;
      `);
      const [,, frameProcessId, frameRoutingId] = await once(w.webContents, 'did-frame-finish-load');
      const state = await readClipboardPermission(webFrameMain.fromId(frameProcessId, frameRoutingId));
      expect(state).to.equal('granted');
      expect(handlerDetails!.requestingUrl).to.equal(loadUrl);
      expect(handlerDetails!.isMainFrame).to.be.false();
      expect(handlerDetails!.embeddingOrigin).to.equal('file:///');
    });
  });

  describe('ses.isPersistent()', () => {
    afterEach(closeAllWindows);

    it('returns default session as persistent', () => {
      const w = new BrowserWindow({
        show: false
      });

      const ses = w.webContents.session;

      expect(ses.isPersistent()).to.be.true();
    });

    it('returns persist: session as persistent', () => {
      const ses = session.fromPartition(`persist:${Math.random()}`);
      expect(ses.isPersistent()).to.be.true();
    });

    it('returns temporary session as not persistent', () => {
      const ses = session.fromPartition(`${Math.random()}`);
      expect(ses.isPersistent()).to.be.false();
    });
  });

  describe('ses.setUserAgent()', () => {
    afterEach(closeAllWindows);

    it('can be retrieved with getUserAgent()', () => {
      const userAgent = 'test-agent';
      const ses = session.fromPartition('' + Math.random());
      ses.setUserAgent(userAgent);
      expect(ses.getUserAgent()).to.equal(userAgent);
    });

    it('sets the User-Agent header for web requests made from renderers', async () => {
      const userAgent = 'test-agent';
      const ses = session.fromPartition('' + Math.random());
      ses.setUserAgent(userAgent, 'en-US,fr,de');
      const w = new BrowserWindow({ show: false, webPreferences: { session: ses } });
      let headers: http.IncomingHttpHeaders | null = null;
      const server = http.createServer((req, res) => {
        headers = req.headers;
        res.end();
        server.close();
      });
      const { url } = await listen(server);
      await w.loadURL(url);
      expect(headers!['user-agent']).to.equal(userAgent);
      expect(headers!['accept-language']).to.equal('en-US,fr;q=0.9,de;q=0.8');
    });
  });

  describe('session-created event', () => {
    it('is emitted when a session is created', async () => {
      const sessionCreated = once(app, 'session-created') as Promise<[any, Session]>;
      const session1 = session.fromPartition('' + Math.random());
      const [session2] = await sessionCreated;
      expect(session1).to.equal(session2);
    });
  });

  describe('session.storagePage', () => {
    it('returns a string', () => {
      expect(session.defaultSession.storagePath).to.be.a('string');
    });

    it('returns null for in memory sessions', () => {
      expect(session.fromPartition('in-memory').storagePath).to.equal(null);
    });

    it('returns different paths for partitions and the default session', () => {
      expect(session.defaultSession.storagePath).to.not.equal(session.fromPartition('persist:two').storagePath);
    });

    it('returns different paths for different partitions', () => {
      expect(session.fromPartition('persist:one').storagePath).to.not.equal(session.fromPartition('persist:two').storagePath);
    });
  });

  describe('session.setCodeCachePath()', () => {
    it('throws when relative or empty path is provided', () => {
      expect(() => {
        session.defaultSession.setCodeCachePath('../fixtures');
      }).to.throw('Absolute path must be provided to store code cache.');
      expect(() => {
        session.defaultSession.setCodeCachePath('');
      }).to.throw('Absolute path must be provided to store code cache.');
      expect(() => {
        session.defaultSession.setCodeCachePath(path.join(app.getPath('userData'), 'electron-test-code-cache'));
      }).to.not.throw();
    });
  });

  describe('ses.setSSLConfig()', () => {
    it('can disable cipher suites', async () => {
      const ses = session.fromPartition('' + Math.random());
      const fixturesPath = path.resolve(__dirname, 'fixtures');
      const certPath = path.join(fixturesPath, 'certificates');
      const server = https.createServer({
        key: fs.readFileSync(path.join(certPath, 'server.key')),
        cert: fs.readFileSync(path.join(certPath, 'server.pem')),
        ca: [
          fs.readFileSync(path.join(certPath, 'rootCA.pem')),
          fs.readFileSync(path.join(certPath, 'intermediateCA.pem'))
        ],
        minVersion: 'TLSv1.2',
        maxVersion: 'TLSv1.2',
        ciphers: 'AES128-GCM-SHA256'
      }, (req, res) => {
        res.end('hi');
      });
      const { port } = await listen(server);
      defer(() => server.close());

      function request () {
        return new Promise((resolve, reject) => {
          const r = net.request({
            url: `https://127.0.0.1:${port}`,
            session: ses
          });
          r.on('response', (res) => {
            let data = '';
            res.on('data', (chunk) => {
              data += chunk.toString('utf8');
            });
            res.on('end', () => {
              resolve(data);
            });
          });
          r.on('error', (err) => {
            reject(err);
          });
          r.end();
        });
      }

      await expect(request()).to.be.rejectedWith(/ERR_CERT_AUTHORITY_INVALID/);
      ses.setSSLConfig({
        disabledCipherSuites: [0x009C]
      });
      await expect(request()).to.be.rejectedWith(/ERR_SSL_VERSION_OR_CIPHER_MISMATCH/);
    });
  });

  describe('ses.clearData()', () => {
    afterEach(closeAllWindows);

    // NOTE: This API clears more than localStorage, but localStorage is a
    // convenient test target for this API
    it('clears all data when no options supplied', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
      await w.loadFile(path.join(fixtures, 'api', 'localstorage.html'));

      expect(await w.webContents.executeJavaScript('localStorage.length')).to.be.greaterThan(0);

      await w.webContents.session.clearData();

      expect(await w.webContents.executeJavaScript('localStorage.length')).to.equal(0);
    });

    it('clears all data when no options supplied, called twice in parallel', async () => {
      const w = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true } });
      await w.loadFile(path.join(fixtures, 'api', 'localstorage.html'));

      expect(await w.webContents.executeJavaScript('localStorage.length')).to.be.greaterThan(0);

      // This first call is not awaited immediately
      const clearDataPromise = w.webContents.session.clearData();
      await w.webContents.session.clearData();

      expect(await w.webContents.executeJavaScript('localStorage.length')).to.equal(0);

      // Await the first promise so it doesn't creep into another test
      await clearDataPromise;
    });

    it('only clears specified data categories', async () => {
      const w = new BrowserWindow({
        show: false,
        webPreferences: { nodeIntegration: true, contextIsolation: false }
      });
      await w.loadFile(
        path.join(fixtures, 'api', 'localstorage-and-indexeddb.html')
      );

      const { webContents } = w;
      const { session } = webContents;

      await once(ipcMain, 'indexeddb-ready');

      async function queryData (channel: string): Promise<string> {
        const event = once(ipcMain, `result-${channel}`);
        webContents.send(`get-${channel}`);
        return (await event)[1];
      }

      // Data is in localStorage
      await expect(queryData('localstorage')).to.eventually.equal('hello localstorage');
      // Data is in indexedDB
      await expect(queryData('indexeddb')).to.eventually.equal('hello indexeddb');

      // Clear only indexedDB, not localStorage
      await session.clearData({ dataTypes: ['indexedDB'] });

      // The localStorage data should still be there
      await expect(queryData('localstorage')).to.eventually.equal('hello localstorage');

      // The indexedDB data should be gone
      await expect(queryData('indexeddb')).to.eventually.be.undefined();
    });

    it('only clears the specified origins', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');

      const { session } = w.webContents;
      const { cookies } = session;

      await Promise.all([
        cookies.set({
          url: 'https://example.com/',
          name: 'testdotcom',
          value: 'testdotcom'
        }),
        cookies.set({
          url: 'https://example.org/',
          name: 'testdotorg',
          value: 'testdotorg'
        })
      ]);

      await session.clearData({ origins: ['https://example.com'] });

      expect((await cookies.get({ url: 'https://example.com/', name: 'testdotcom' })).length).to.equal(0);
      expect((await cookies.get({ url: 'https://example.org/', name: 'testdotorg' })).length).to.be.greaterThan(0);
    });

    it('clears all except the specified origins', async () => {
      const w = new BrowserWindow({ show: false });
      await w.loadURL('about:blank');

      const { session } = w.webContents;
      const { cookies } = session;

      await Promise.all([
        cookies.set({
          url: 'https://example.com/',
          name: 'testdotcom',
          value: 'testdotcom'
        }),
        cookies.set({
          url: 'https://example.org/',
          name: 'testdotorg',
          value: 'testdotorg'
        })
      ]);

      await session.clearData({ excludeOrigins: ['https://example.com'] });

      expect((await cookies.get({ url: 'https://example.com/', name: 'testdotcom' })).length).to.be.greaterThan(0);
      expect((await cookies.get({ url: 'https://example.org/', name: 'testdotorg' })).length).to.equal(0);
    });
  });
});
