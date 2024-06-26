import { assert, expect } from 'chai';
import * as cp from 'node:child_process';
import * as https from 'node:https';
import * as http from 'node:http';
import * as net from 'node:net';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { promisify } from 'node:util';
import { app, BrowserWindow, Menu, session, net as electronNet, WebContents, utilityProcess } from 'electron/main';
import { closeWindow, closeAllWindows } from './lib/window-helpers';
import { ifdescribe, ifit, listen, waitUntil } from './lib/spec-helpers';
import { collectStreamBody, getResponse } from './lib/net-helpers';
import { once } from 'node:events';
import split = require('split')
import * as semver from 'semver';

const fixturesPath = path.resolve(__dirname, 'fixtures');

describe('electron module', () => {
  it('does not expose internal modules to require', () => {
    expect(() => {
      require('clipboard');
    }).to.throw(/Cannot find module 'clipboard'/);
  });

  describe('require("electron")', () => {
    it('always returns the internal electron module', () => {
      require('electron');
    });
  });
});

describe('app module', () => {
  let server: https.Server;
  let secureUrl: string;
  const certPath = path.join(fixturesPath, 'certificates');

  before(async () => {
    const options = {
      key: fs.readFileSync(path.join(certPath, 'server.key')),
      cert: fs.readFileSync(path.join(certPath, 'server.pem')),
      ca: [
        fs.readFileSync(path.join(certPath, 'rootCA.pem')),
        fs.readFileSync(path.join(certPath, 'intermediateCA.pem'))
      ],
      requestCert: true,
      rejectUnauthorized: false
    };

    server = https.createServer(options, (req, res) => {
      if ((req as any).client.authorized) {
        res.writeHead(200);
        res.end('<title>authorized</title>');
      } else {
        res.writeHead(401);
        res.end('<title>denied</title>');
      }
    });

    secureUrl = (await listen(server)).url;
  });

  after(done => {
    server.close(() => done());
  });

  describe('app.getVersion()', () => {
    it('returns the version field of package.json', () => {
      expect(app.getVersion()).to.equal('0.1.0');
    });
  });

  describe('app.setVersion(version)', () => {
    it('overrides the version', () => {
      expect(app.getVersion()).to.equal('0.1.0');
      app.setVersion('test-version');

      expect(app.getVersion()).to.equal('test-version');
      app.setVersion('0.1.0');
    });
  });

  describe('app name APIs', () => {
    it('with properties', () => {
      it('returns the name field of package.json', () => {
        expect(app.name).to.equal('Electron Test Main');
      });

      it('overrides the name', () => {
        expect(app.name).to.equal('Electron Test Main');
        app.name = 'electron-test-name';

        expect(app.name).to.equal('electron-test-name');
        app.name = 'Electron Test Main';
      });
    });

    it('with functions', () => {
      it('returns the name field of package.json', () => {
        expect(app.getName()).to.equal('Electron Test Main');
      });

      it('overrides the name', () => {
        expect(app.getName()).to.equal('Electron Test Main');
        app.setName('electron-test-name');

        expect(app.getName()).to.equal('electron-test-name');
        app.setName('Electron Test Main');
      });
    });
  });

  describe('app.getLocale()', () => {
    it('should not be empty', () => {
      expect(app.getLocale()).to.not.equal('');
    });
  });

  describe('app.getSystemLocale()', () => {
    it('should not be empty', () => {
      expect(app.getSystemLocale()).to.not.equal('');
    });
  });

  describe('app.getPreferredSystemLanguages()', () => {
    ifit(process.platform !== 'linux')('should not be empty', () => {
      expect(app.getPreferredSystemLanguages().length).to.not.equal(0);
    });

    ifit(process.platform === 'linux')('should be empty or contain C entry', () => {
      const languages = app.getPreferredSystemLanguages();
      if (languages.length) {
        expect(languages).to.not.include('C');
      }
    });
  });

  describe('app.getLocaleCountryCode()', () => {
    it('should be empty or have length of two', () => {
      const localeCountryCode = app.getLocaleCountryCode();
      expect(localeCountryCode).to.be.a('string');
      expect(localeCountryCode.length).to.be.oneOf([0, 2]);
    });
  });

  describe('app.isPackaged', () => {
    it('should be false during tests', () => {
      expect(app.isPackaged).to.equal(false);
    });
  });

  ifdescribe(process.platform === 'darwin')('app.isInApplicationsFolder()', () => {
    it('should be false during tests', () => {
      expect(app.isInApplicationsFolder()).to.equal(false);
    });
  });

  describe('app.exit(exitCode)', () => {
    let appProcess: cp.ChildProcess | null = null;

    afterEach(() => {
      if (appProcess) appProcess.kill();
    });

    it('emits a process exit event with the code', async () => {
      const appPath = path.join(fixturesPath, 'api', 'quit-app');
      const electronPath = process.execPath;
      let output = '';

      appProcess = cp.spawn(electronPath, [appPath]);
      if (appProcess && appProcess.stdout) {
        appProcess.stdout.on('data', data => { output += data; });
      }
      const [code] = await once(appProcess, 'exit');

      if (process.platform !== 'win32') {
        expect(output).to.include('Exit event with code: 123');
      }
      expect(code).to.equal(123);
    });

    it('closes all windows', async function () {
      const appPath = path.join(fixturesPath, 'api', 'exit-closes-all-windows-app');
      const electronPath = process.execPath;

      appProcess = cp.spawn(electronPath, [appPath]);
      const [code, signal] = await once(appProcess, 'exit');

      expect(signal).to.equal(null, 'exit signal should be null, if you see this please tag @MarshallOfSound');
      expect(code).to.equal(123, 'exit code should be 123, if you see this please tag @MarshallOfSound');
    });

    ifit(['darwin', 'linux'].includes(process.platform))('exits gracefully', async function () {
      const electronPath = process.execPath;
      const appPath = path.join(fixturesPath, 'api', 'singleton');
      appProcess = cp.spawn(electronPath, [appPath]);

      // Singleton will send us greeting data to let us know it's running.
      // After that, ask it to exit gracefully and confirm that it does.
      if (appProcess && appProcess.stdout) {
        appProcess.stdout.on('data', () => appProcess!.kill());
      }
      const [code, signal] = await once(appProcess, 'exit');

      const message = `code:\n${code}\nsignal:\n${signal}`;
      expect(code).to.equal(0, message);
      expect(signal).to.equal(null, message);
    });
  });

  ifdescribe(process.platform === 'darwin')('app.setActivationPolicy', () => {
    it('throws an error on invalid application policies', () => {
      expect(() => {
        app.setActivationPolicy('terrible' as any);
      }).to.throw(/Invalid activation policy: must be one of 'regular', 'accessory', or 'prohibited'/);
    });
  });

  describe('app.requestSingleInstanceLock', () => {
    interface SingleInstanceLockTestArgs {
      args: string[];
      expectedAdditionalData: unknown;
    }

    it('prevents the second launch of app', async function () {
      this.timeout(120000);
      const appPath = path.join(fixturesPath, 'api', 'singleton-data');
      const first = cp.spawn(process.execPath, [appPath]);
      await once(first.stdout, 'data');
      // Start second app when received output.
      const second = cp.spawn(process.execPath, [appPath]);
      const [code2] = await once(second, 'exit');
      expect(code2).to.equal(1);
      const [code1] = await once(first, 'exit');
      expect(code1).to.equal(0);
    });

    it('returns true when setting non-existent user data folder', async function () {
      const appPath = path.join(fixturesPath, 'api', 'singleton-userdata');
      const instance = cp.spawn(process.execPath, [appPath]);
      const [code] = await once(instance, 'exit');
      expect(code).to.equal(0);
    });

    async function testArgumentPassing (testArgs: SingleInstanceLockTestArgs) {
      const appPath = path.join(fixturesPath, 'api', 'singleton-data');
      const first = cp.spawn(process.execPath, [appPath, ...testArgs.args]);
      const firstExited = once(first, 'exit');

      // Wait for the first app to boot.
      const firstStdoutLines = first.stdout.pipe(split());
      while ((await once(firstStdoutLines, 'data')).toString() !== 'started') {
        // wait.
      }
      const additionalDataPromise = once(firstStdoutLines, 'data');

      const secondInstanceArgs = [process.execPath, appPath, ...testArgs.args, '--some-switch', 'some-arg'];
      const second = cp.spawn(secondInstanceArgs[0], secondInstanceArgs.slice(1));
      const secondExited = once(second, 'exit');

      const [code2] = await secondExited;
      expect(code2).to.equal(1);
      const [code1] = await firstExited;
      expect(code1).to.equal(0);
      const dataFromSecondInstance = await additionalDataPromise;
      const [args, additionalData] = dataFromSecondInstance[0].toString('ascii').split('||');
      const secondInstanceArgsReceived: string[] = JSON.parse(args.toString('ascii'));
      const secondInstanceDataReceived = JSON.parse(additionalData.toString('ascii'));

      // Ensure secondInstanceArgs is a subset of secondInstanceArgsReceived
      for (const arg of secondInstanceArgs) {
        expect(secondInstanceArgsReceived).to.include(arg,
          `argument ${arg} is missing from received second args`);
      }
      expect(secondInstanceDataReceived).to.be.deep.equal(testArgs.expectedAdditionalData,
        `received data ${JSON.stringify(secondInstanceDataReceived)} is not equal to expected data ${JSON.stringify(testArgs.expectedAdditionalData)}.`);
    }

    it('passes arguments to the second-instance event no additional data', async () => {
      await testArgumentPassing({
        args: [],
        expectedAdditionalData: null
      });
    });

    it('sends and receives JSON object data', async () => {
      const expectedAdditionalData = {
        level: 1,
        testkey: 'testvalue1',
        inner: {
          level: 2,
          testkey: 'testvalue2'
        }
      };
      await testArgumentPassing({
        args: ['--send-data'],
        expectedAdditionalData
      });
    });

    it('sends and receives numerical data', async () => {
      await testArgumentPassing({
        args: ['--send-data', '--data-content=2'],
        expectedAdditionalData: 2
      });
    });

    it('sends and receives string data', async () => {
      await testArgumentPassing({
        args: ['--send-data', '--data-content="data"'],
        expectedAdditionalData: 'data'
      });
    });

    it('sends and receives boolean data', async () => {
      await testArgumentPassing({
        args: ['--send-data', '--data-content=false'],
        expectedAdditionalData: false
      });
    });

    it('sends and receives array data', async () => {
      await testArgumentPassing({
        args: ['--send-data', '--data-content=[2, 3, 4]'],
        expectedAdditionalData: [2, 3, 4]
      });
    });

    it('sends and receives mixed array data', async () => {
      await testArgumentPassing({
        args: ['--send-data', '--data-content=["2", true, 4]'],
        expectedAdditionalData: ['2', true, 4]
      });
    });

    it('sends and receives null data', async () => {
      await testArgumentPassing({
        args: ['--send-data', '--data-content=null'],
        expectedAdditionalData: null
      });
    });

    it('cannot send or receive undefined data', async () => {
      try {
        await testArgumentPassing({
          args: ['--send-ack', '--ack-content="undefined"', '--prevent-default', '--send-data', '--data-content="undefined"'],
          expectedAdditionalData: undefined
        });
        assert(false);
      } catch {
        // This is expected.
      }
    });
  });

  describe('app.relaunch', () => {
    let server: net.Server | null = null;
    const socketPath = process.platform === 'win32' ? '\\\\.\\pipe\\electron-app-relaunch' : '/tmp/electron-app-relaunch';

    beforeEach(done => {
      fs.unlink(socketPath, () => {
        server = net.createServer();
        server.listen(socketPath);
        done();
      });
    });

    afterEach((done) => {
      server!.close(() => {
        if (process.platform === 'win32') {
          done();
        } else {
          fs.unlink(socketPath, () => done());
        }
      });
    });

    it('relaunches the app', function (done) {
      this.timeout(120000);

      let state = 'none';
      server!.once('error', error => done(error));
      server!.on('connection', client => {
        client.once('data', data => {
          if (String(data) === '--first' && state === 'none') {
            state = 'first-launch';
          } else if (String(data) === '--second' && state === 'first-launch') {
            state = 'second-launch';
          } else if (String(data) === '--third' && state === 'second-launch') {
            done();
          } else {
            done(`Unexpected state: "${state}", data: "${data}"`);
          }
        });
      });

      const appPath = path.join(fixturesPath, 'api', 'relaunch');
      const child = cp.spawn(process.execPath, [appPath, '--first']);
      child.stdout.on('data', (c) => console.log(c.toString()));
      child.stderr.on('data', (c) => console.log(c.toString()));
      child.on('exit', (code, signal) => {
        if (code !== 0) {
          console.log(`Process exited with code "${code}" signal "${signal}"`);
        }
      });
    });
  });

  ifdescribe(process.platform === 'darwin')('app.setUserActivity(type, userInfo)', () => {
    it('sets the current activity', () => {
      app.setUserActivity('com.electron.testActivity', { testData: '123' });
      expect(app.getCurrentActivityType()).to.equal('com.electron.testActivity');
    });
  });

  describe('certificate-error event', () => {
    afterEach(closeAllWindows);
    it('is emitted when visiting a server with a self-signed cert', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL(secureUrl);
      await once(app, 'certificate-error');
    });

    describe('when denied', () => {
      before(() => {
        app.on('certificate-error', (event, webContents, url, error, certificate, callback) => {
          callback(false);
        });
      });

      after(() => {
        app.removeAllListeners('certificate-error');
      });

      it('causes did-fail-load', async () => {
        const w = new BrowserWindow({ show: false });
        w.loadURL(secureUrl);
        await once(w.webContents, 'did-fail-load');
      });
    });
  });

  // xdescribe('app.importCertificate', () => {
  //   let w = null

  //   before(function () {
  //     if (process.platform !== 'linux') {
  //       this.skip()
  //     }
  //   })

  //   afterEach(() => closeWindow(w).then(() => { w = null }))

  //   it('can import certificate into platform cert store', done => {
  //     const options = {
  //       certificate: path.join(certPath, 'client.p12'),
  //       password: 'electron'
  //     }

  //     w = new BrowserWindow({
  //       show: false,
  //       webPreferences: {
  //         nodeIntegration: true
  //       }
  //     })

  //     w.webContents.on('did-finish-load', () => {
  //       expect(w.webContents.getTitle()).to.equal('authorized')
  //       done()
  //     })

  //     ipcRenderer.once('select-client-certificate', (event, webContentsId, list) => {
  //       expect(webContentsId).to.equal(w.webContents.id)
  //       expect(list).to.have.lengthOf(1)

  //       expect(list[0]).to.deep.equal({
  //         issuerName: 'Intermediate CA',
  //         subjectName: 'Client Cert',
  //         issuer: { commonName: 'Intermediate CA' },
  //         subject: { commonName: 'Client Cert' }
  //       })

  //       event.sender.send('client-certificate-response', list[0])
  //     })

  //     app.importCertificate(options, result => {
  //       expect(result).toNotExist()
  //       ipcRenderer.sendSync('set-client-certificate-option', false)
  //       w.loadURL(secureUrl)
  //     })
  //   })
  // })

  describe('BrowserWindow events', () => {
    let w: BrowserWindow = null as any;

    afterEach(() => {
      closeWindow(w).then(() => { w = null as any; });
    });

    it('should emit browser-window-focus event when window is focused', async () => {
      const emitted = once(app, 'browser-window-focus') as Promise<[any, BrowserWindow]>;
      w = new BrowserWindow({ show: false });
      w.emit('focus');
      const [, window] = await emitted;
      expect(window.id).to.equal(w.id);
    });

    it('should emit browser-window-blur event when window is blurred', async () => {
      const emitted = once(app, 'browser-window-blur') as Promise<[any, BrowserWindow]>;
      w = new BrowserWindow({ show: false });
      w.emit('blur');
      const [, window] = await emitted;
      expect(window.id).to.equal(w.id);
    });

    it('should emit browser-window-created event when window is created', async () => {
      const emitted = once(app, 'browser-window-created') as Promise<[any, BrowserWindow]>;
      w = new BrowserWindow({ show: false });
      const [, window] = await emitted;
      expect(window.id).to.equal(w.id);
    });

    it('should emit web-contents-created event when a webContents is created', async () => {
      const emitted = once(app, 'web-contents-created') as Promise<[any, WebContents]>;
      w = new BrowserWindow({ show: false });
      const [, webContents] = await emitted;
      expect(webContents.id).to.equal(w.webContents.id);
    });

    // FIXME: re-enable this test on win32.
    ifit(process.platform !== 'win32')('should emit render-process-gone event when renderer crashes', async () => {
      w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          contextIsolation: false
        }
      });
      await w.loadURL('about:blank');

      const emitted = once(app, 'render-process-gone') as Promise<[any, WebContents, Electron.RenderProcessGoneDetails]>;
      w.webContents.executeJavaScript('process.crash()');

      const [, webContents, details] = await emitted;
      expect(webContents).to.equal(w.webContents);
      expect(details.reason).to.be.oneOf(['crashed', 'abnormal-exit']);
    });
  });

  describe('app.badgeCount', () => {
    const platformIsNotSupported =
        (process.platform === 'win32') ||
        (process.platform === 'linux' && !app.isUnityRunning());

    const expectedBadgeCount = 42;

    after(() => { app.badgeCount = 0; });

    ifdescribe(!platformIsNotSupported)('on supported platform', () => {
      describe('with properties', () => {
        it('sets a badge count', function () {
          app.badgeCount = expectedBadgeCount;
          expect(app.badgeCount).to.equal(expectedBadgeCount);
        });
      });

      describe('with functions', () => {
        it('sets a numerical badge count', function () {
          app.setBadgeCount(expectedBadgeCount);
          expect(app.getBadgeCount()).to.equal(expectedBadgeCount);
        });
        it('sets an non numeric (dot) badge count', function () {
          app.setBadgeCount();
          // Badge count should be zero when non numeric (dot) is requested
          expect(app.getBadgeCount()).to.equal(0);
        });
      });
    });

    ifdescribe(process.platform !== 'win32' && platformIsNotSupported)('on unsupported platform', () => {
      describe('with properties', () => {
        it('does not set a badge count', function () {
          app.badgeCount = 9999;
          expect(app.badgeCount).to.equal(0);
        });
      });

      describe('with functions', () => {
        it('does not set a badge count)', function () {
          app.setBadgeCount(9999);
          expect(app.getBadgeCount()).to.equal(0);
        });
      });
    });
  });

  ifdescribe(process.platform !== 'linux' && !process.mas && (process.platform !== 'darwin' || process.arch === 'arm64'))('app.get/setLoginItemSettings API', function () {
    const isMac = process.platform === 'darwin';
    const isWin = process.platform === 'win32';

    const updateExe = path.resolve(path.dirname(process.execPath), '..', 'Update.exe');
    const processStartArgs = [
      '--processStart', `"${path.basename(process.execPath)}"`,
      '--process-start-args', '"--hidden"'
    ];
    const regAddArgs = [
      'ADD',
      'HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run',
      '/v',
      'additionalEntry',
      '/t',
      'REG_BINARY',
      '/f',
      '/d'
    ];
    const productVersion = isMac ? cp.execSync('sw_vers -productVersion').toString().trim() : '';
    const isVenturaOrHigher = semver.gt(semver.coerce(productVersion) || '0.0.0', '13.0.0');

    beforeEach(() => {
      app.setLoginItemSettings({ openAtLogin: false });
      app.setLoginItemSettings({ openAtLogin: false, path: updateExe, args: processStartArgs });
      app.setLoginItemSettings({ name: 'additionalEntry', openAtLogin: false });
    });

    afterEach(() => {
      app.setLoginItemSettings({ openAtLogin: false });
      app.setLoginItemSettings({ openAtLogin: false, path: updateExe, args: processStartArgs });
      app.setLoginItemSettings({ name: 'additionalEntry', openAtLogin: false });
    });

    ifit(!isWin)('sets and returns the app as a login item', () => {
      app.setLoginItemSettings({ openAtLogin: true });

      const settings = app.getLoginItemSettings();
      expect(settings.openAtLogin).to.equal(true);
      expect(settings.openAsHidden).to.equal(false);
      expect(settings.wasOpenedAtLogin).to.equal(false);
      expect(settings.wasOpenedAsHidden).to.equal(false);
      expect(settings.restoreState).to.equal(false);
      if (isVenturaOrHigher) expect(settings.status).to.equal('enabled');
    });

    ifit(isWin)('sets and returns the app as a login item (windows)', () => {
      app.setLoginItemSettings({ openAtLogin: true, enabled: true });
      expect(app.getLoginItemSettings()).to.deep.equal({
        openAtLogin: true,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: true,
        launchItems: [{
          name: 'electron.app.Electron',
          path: process.execPath,
          args: [],
          scope: 'user',
          enabled: true
        }]
      });

      app.setLoginItemSettings({ openAtLogin: false });
      app.setLoginItemSettings({ openAtLogin: true, enabled: false });
      expect(app.getLoginItemSettings()).to.deep.equal({
        openAtLogin: true,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: false,
        launchItems: [{
          name: 'electron.app.Electron',
          path: process.execPath,
          args: [],
          scope: 'user',
          enabled: false
        }]
      });
    });

    ifit(!isWin)('adds a login item that loads in hidden mode', () => {
      app.setLoginItemSettings({ openAtLogin: true, openAsHidden: true });

      const settings = app.getLoginItemSettings();
      expect(settings.openAtLogin).to.equal(true);

      const hasOpenAsHidden = process.platform === 'darwin' && !isVenturaOrHigher;
      expect(settings.openAsHidden).to.equal(hasOpenAsHidden);
      expect(settings.wasOpenedAtLogin).to.equal(false);
      expect(settings.wasOpenedAsHidden).to.equal(false);
      expect(settings.restoreState).to.equal(false);
      if (isVenturaOrHigher) expect(settings.status).to.equal('enabled');
    });

    ifit(isWin)('adds a login item that loads in hidden mode (windows)', () => {
      app.setLoginItemSettings({ openAtLogin: true, openAsHidden: true });
      expect(app.getLoginItemSettings()).to.deep.equal({
        openAtLogin: true,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: true,
        launchItems: [{
          name: 'electron.app.Electron',
          path: process.execPath,
          args: [],
          scope: 'user',
          enabled: true
        }]
      });
    });

    it('correctly sets and unsets the LoginItem', () => {
      expect(app.getLoginItemSettings().openAtLogin).to.equal(false);

      app.setLoginItemSettings({ openAtLogin: true });
      expect(app.getLoginItemSettings().openAtLogin).to.equal(true);

      app.setLoginItemSettings({ openAtLogin: false });
      expect(app.getLoginItemSettings().openAtLogin).to.equal(false);
    });

    ifit(isMac)('correctly sets and unsets the LoginItem as hidden', () => {
      expect(app.getLoginItemSettings().openAtLogin).to.equal(false);
      expect(app.getLoginItemSettings().openAsHidden).to.equal(false);

      app.setLoginItemSettings({ openAtLogin: true, openAsHidden: true });
      expect(app.getLoginItemSettings().openAtLogin).to.equal(true);
      expect(app.getLoginItemSettings().openAsHidden).to.equal(!isVenturaOrHigher);

      app.setLoginItemSettings({ openAtLogin: true, openAsHidden: false });
      expect(app.getLoginItemSettings().openAtLogin).to.equal(true);
      expect(app.getLoginItemSettings().openAsHidden).to.equal(false);
    });

    ifdescribe(isMac)('using SMAppService', () => {
      ifit(isVenturaOrHigher)('can set a login item', () => {
        app.setLoginItemSettings({
          openAtLogin: true,
          type: 'mainAppService'
        });

        expect(app.getLoginItemSettings()).to.deep.equal({
          status: 'enabled',
          openAtLogin: true,
          openAsHidden: false,
          restoreState: false,
          wasOpenedAtLogin: false,
          wasOpenedAsHidden: false
        });
      });

      ifit(isVenturaOrHigher)('throws when setting non-default type with no name', () => {
        expect(() => {
          app.setLoginItemSettings({
            openAtLogin: true,
            type: 'daemonService'
          });
        }).to.throw(/'name' is required when type is not mainAppService/);
      });

      ifit(isVenturaOrHigher)('throws when getting non-default type with no name', () => {
        expect(() => {
          app.getLoginItemSettings({
            type: 'daemonService'
          });
        }).to.throw(/'name' is required when type is not mainAppService/);
      });

      ifit(isVenturaOrHigher)('can unset a login item', () => {
        app.setLoginItemSettings({
          openAtLogin: true,
          type: 'mainAppService'
        });

        app.setLoginItemSettings({
          openAtLogin: false,
          type: 'mainAppService'
        });

        expect(app.getLoginItemSettings()).to.deep.equal({
          status: 'not-registered',
          openAtLogin: false,
          openAsHidden: false,
          restoreState: false,
          wasOpenedAtLogin: false,
          wasOpenedAsHidden: false
        });
      });
    });

    ifit(isWin)('allows you to pass a custom executable and arguments', () => {
      app.setLoginItemSettings({ openAtLogin: true, path: updateExe, args: processStartArgs, enabled: true });
      expect(app.getLoginItemSettings().openAtLogin).to.equal(false);
      const openAtLoginTrueEnabledTrue = app.getLoginItemSettings({
        path: updateExe,
        args: processStartArgs
      });

      expect(openAtLoginTrueEnabledTrue.openAtLogin).to.equal(true);
      expect(openAtLoginTrueEnabledTrue.executableWillLaunchAtLogin).to.equal(true);

      app.setLoginItemSettings({ openAtLogin: true, path: updateExe, args: processStartArgs, enabled: false });
      const openAtLoginTrueEnabledFalse = app.getLoginItemSettings({
        path: updateExe,
        args: processStartArgs
      });

      expect(openAtLoginTrueEnabledFalse.openAtLogin).to.equal(true);
      expect(openAtLoginTrueEnabledFalse.executableWillLaunchAtLogin).to.equal(false);

      app.setLoginItemSettings({ openAtLogin: false, path: updateExe, args: processStartArgs, enabled: false });
      const openAtLoginFalseEnabledFalse = app.getLoginItemSettings({
        path: updateExe,
        args: processStartArgs
      });

      expect(openAtLoginFalseEnabledFalse.openAtLogin).to.equal(false);
      expect(openAtLoginFalseEnabledFalse.executableWillLaunchAtLogin).to.equal(false);
    });

    ifit(isWin)('allows you to pass a custom name', () => {
      app.setLoginItemSettings({ openAtLogin: true });
      app.setLoginItemSettings({ openAtLogin: true, name: 'additionalEntry', enabled: false });
      expect(app.getLoginItemSettings()).to.deep.equal({
        openAtLogin: true,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: true,
        launchItems: [{
          name: 'additionalEntry',
          path: process.execPath,
          args: [],
          scope: 'user',
          enabled: false
        }, {
          name: 'electron.app.Electron',
          path: process.execPath,
          args: [],
          scope: 'user',
          enabled: true
        }]
      });

      app.setLoginItemSettings({ openAtLogin: false, name: 'additionalEntry' });
      expect(app.getLoginItemSettings()).to.deep.equal({
        openAtLogin: true,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: true,
        launchItems: [{
          name: 'electron.app.Electron',
          path: process.execPath,
          args: [],
          scope: 'user',
          enabled: true
        }]
      });
    });

    ifit(isWin)('finds launch items independent of args', () => {
      app.setLoginItemSettings({ openAtLogin: true, args: ['arg1'] });
      app.setLoginItemSettings({ openAtLogin: true, name: 'additionalEntry', enabled: false, args: ['arg2'] });
      expect(app.getLoginItemSettings()).to.deep.equal({
        openAtLogin: false,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: true,
        launchItems: [{
          name: 'additionalEntry',
          path: process.execPath,
          args: ['arg2'],
          scope: 'user',
          enabled: false
        }, {
          name: 'electron.app.Electron',
          path: process.execPath,
          args: ['arg1'],
          scope: 'user',
          enabled: true
        }]
      });
    });

    ifit(isWin)('finds launch items independent of path quotation or casing', () => {
      const expectation = {
        openAtLogin: false,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: true,
        launchItems: [{
          name: 'additionalEntry',
          path: 'C:\\electron\\myapp.exe',
          args: ['arg1'],
          scope: 'user',
          enabled: true
        }]
      };

      app.setLoginItemSettings({ openAtLogin: true, name: 'additionalEntry', enabled: true, path: 'C:\\electron\\myapp.exe', args: ['arg1'] });
      expect(app.getLoginItemSettings({ path: '"C:\\electron\\MYAPP.exe"' })).to.deep.equal(expectation);

      app.setLoginItemSettings({ openAtLogin: false, name: 'additionalEntry' });
      app.setLoginItemSettings({ openAtLogin: true, name: 'additionalEntry', enabled: true, path: '"C:\\electron\\MYAPP.exe"', args: ['arg1'] });
      expect(app.getLoginItemSettings({ path: 'C:\\electron\\myapp.exe' })).to.deep.equal({
        ...expectation,
        launchItems: [
          {
            name: 'additionalEntry',
            path: 'C:\\electron\\MYAPP.exe',
            args: ['arg1'],
            scope: 'user',
            enabled: true
          }
        ]
      });
    });

    ifit(isWin)('detects disabled by TaskManager', async () => {
      app.setLoginItemSettings({ openAtLogin: true, name: 'additionalEntry', enabled: true, args: ['arg1'] });
      const appProcess = cp.spawn('reg', [...regAddArgs, '030000000000000000000000']);
      await once(appProcess, 'exit');
      expect(app.getLoginItemSettings()).to.deep.equal({
        openAtLogin: false,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: false,
        launchItems: [{
          name: 'additionalEntry',
          path: process.execPath,
          args: ['arg1'],
          scope: 'user',
          enabled: false
        }]
      });
    });

    ifit(isWin)('detects enabled by TaskManager', async () => {
      const expectation = {
        openAtLogin: false,
        openAsHidden: false,
        wasOpenedAtLogin: false,
        wasOpenedAsHidden: false,
        restoreState: false,
        executableWillLaunchAtLogin: true,
        launchItems: [{
          name: 'additionalEntry',
          path: process.execPath,
          args: ['arg1'],
          scope: 'user',
          enabled: true
        }]
      };

      app.setLoginItemSettings({ openAtLogin: true, name: 'additionalEntry', enabled: false, args: ['arg1'] });
      let appProcess = cp.spawn('reg', [...regAddArgs, '020000000000000000000000']);
      await once(appProcess, 'exit');
      expect(app.getLoginItemSettings()).to.deep.equal(expectation);

      app.setLoginItemSettings({ openAtLogin: true, name: 'additionalEntry', enabled: false, args: ['arg1'] });
      appProcess = cp.spawn('reg', [...regAddArgs, '000000000000000000000000']);
      await once(appProcess, 'exit');
      expect(app.getLoginItemSettings()).to.deep.equal(expectation);
    });
  });

  ifdescribe(process.platform !== 'linux')('accessibilitySupportEnabled property', () => {
    it('with properties', () => {
      it('can set accessibility support enabled', () => {
        expect(app.accessibilitySupportEnabled).to.eql(false);

        app.accessibilitySupportEnabled = true;
        expect(app.accessibilitySupportEnabled).to.eql(true);
      });
    });

    it('with functions', () => {
      it('can set accessibility support enabled', () => {
        expect(app.isAccessibilitySupportEnabled()).to.eql(false);

        app.setAccessibilitySupportEnabled(true);
        expect(app.isAccessibilitySupportEnabled()).to.eql(true);
      });
    });
  });

  ifdescribe(process.platform === 'win32')('setJumpList(categories)', () => {
    it('throws an error when categories is not null or an array', () => {
      expect(() => {
        app.setJumpList('string' as any);
      }).to.throw('Argument must be null or an array of categories');
    });

    it('can get jump list settings', () => {
      const settings = app.getJumpListSettings();
      expect(settings).to.eql({ minItems: 10, removedItems: [] });
    });

    it('can set a jump list with an array of categories', () => {
      expect(() => {
        app.setJumpList([
          { type: 'frequent' },
          {
            items: [{
              type: 'task',
              title: 'New Project',
              program: process.execPath,
              args: '--new-project',
              description: 'Create a new project.'
            },
            { type: 'separator' },
            {
              type: 'task',
              title: 'Recover Project',
              program: process.execPath,
              args: '--recover-project',
              description: 'Recover Project'
            }]
          }
        ]);
      }).to.not.throw();
    });
  });

  describe('getAppPath', () => {
    it('works for directories with package.json', async () => {
      const { appPath } = await runTestApp('app-path');
      expect(appPath).to.equal(path.resolve(fixturesPath, 'api/app-path'));
    });

    it('works for directories with index.js', async () => {
      const { appPath } = await runTestApp('app-path/lib');
      expect(appPath).to.equal(path.resolve(fixturesPath, 'api/app-path/lib'));
    });

    it('works for files without extension', async () => {
      const { appPath } = await runTestApp('app-path/lib/index');
      expect(appPath).to.equal(path.resolve(fixturesPath, 'api/app-path/lib'));
    });

    it('works for files', async () => {
      const { appPath } = await runTestApp('app-path/lib/index.js');
      expect(appPath).to.equal(path.resolve(fixturesPath, 'api/app-path/lib'));
    });
  });

  describe('getPath(name)', () => {
    it('returns paths that exist', () => {
      const paths = [
        fs.existsSync(app.getPath('exe')),
        fs.existsSync(app.getPath('home')),
        fs.existsSync(app.getPath('temp'))
      ];
      expect(paths).to.deep.equal([true, true, true]);
    });

    it('throws an error when the name is invalid', () => {
      expect(() => {
        app.getPath('does-not-exist' as any);
      }).to.throw(/Failed to get 'does-not-exist' path/);
    });

    it('returns the overridden path', () => {
      app.setPath('music', __dirname);
      expect(app.getPath('music')).to.equal(__dirname);
    });

    if (process.platform === 'win32') {
      it('gets the folder for recent files', () => {
        const recent = app.getPath('recent');

        // We expect that one of our test machines have overridden this
        // to be something crazy, it'll always include the word "Recent"
        // unless people have been registry-hacking like crazy
        expect(recent).to.include('Recent');
      });

      it('can override the recent files path', () => {
        app.setPath('recent', 'C:\\fake-path');
        expect(app.getPath('recent')).to.equal('C:\\fake-path');
      });
    }

    it('uses the app name in getPath(userData)', () => {
      expect(app.getPath('userData')).to.include(app.name);
    });
  });

  describe('setPath(name, path)', () => {
    it('throws when a relative path is passed', () => {
      const badPath = 'hey/hi/hello';

      expect(() => {
        app.setPath('music', badPath);
      }).to.throw(/Path must be absolute/);
    });

    it('does not create a new directory by default', () => {
      const badPath = path.join(__dirname, 'music');

      expect(fs.existsSync(badPath)).to.be.false();
      app.setPath('music', badPath);
      expect(fs.existsSync(badPath)).to.be.false();

      expect(() => { app.getPath(badPath as any); }).to.throw();
    });

    describe('sessionData', () => {
      const appPath = path.join(__dirname, 'fixtures', 'apps', 'set-path');
      const appName = JSON.parse(fs.readFileSync(path.join(appPath, 'package.json'), 'utf8')).name;
      const userDataPath = path.join(app.getPath('appData'), appName);
      const tempBrowserDataPath = path.join(app.getPath('temp'), appName);

      const sessionFiles = [
        'Preferences',
        'Code Cache',
        'Local Storage',
        'IndexedDB',
        'Service Worker'
      ];
      const hasSessionFiles = (dir: string) => {
        for (const file of sessionFiles) {
          if (!fs.existsSync(path.join(dir, file))) {
            return false;
          }
        }
        return true;
      };

      beforeEach(() => {
        fs.rmSync(userDataPath, { force: true, recursive: true });
        fs.rmSync(tempBrowserDataPath, { force: true, recursive: true });
      });

      it('writes to userData by default', () => {
        expect(hasSessionFiles(userDataPath)).to.equal(false);
        cp.spawnSync(process.execPath, [appPath]);
        expect(hasSessionFiles(userDataPath)).to.equal(true);
      });

      it('can be changed', () => {
        expect(hasSessionFiles(userDataPath)).to.equal(false);
        cp.spawnSync(process.execPath, [appPath, 'sessionData', tempBrowserDataPath]);
        expect(hasSessionFiles(userDataPath)).to.equal(false);
        expect(hasSessionFiles(tempBrowserDataPath)).to.equal(true);
      });

      it('changing userData affects default sessionData', () => {
        expect(hasSessionFiles(userDataPath)).to.equal(false);
        cp.spawnSync(process.execPath, [appPath, 'userData', tempBrowserDataPath]);
        expect(hasSessionFiles(userDataPath)).to.equal(false);
        expect(hasSessionFiles(tempBrowserDataPath)).to.equal(true);
      });
    });
  });

  describe('setAppLogsPath(path)', () => {
    it('throws when a relative path is passed', () => {
      const badPath = 'hey/hi/hello';

      expect(() => {
        app.setAppLogsPath(badPath);
      }).to.throw(/Path must be absolute/);
    });
  });

  ifdescribe(process.platform !== 'linux')('select-client-certificate event', () => {
    let w: BrowserWindow;

    before(function () {
      session.fromPartition('empty-certificate').setCertificateVerifyProc((req, cb) => { cb(0); });
    });

    beforeEach(() => {
      w = new BrowserWindow({
        show: false,
        webPreferences: {
          nodeIntegration: true,
          partition: 'empty-certificate'
        }
      });
    });

    afterEach(() => closeWindow(w).then(() => { w = null as any; }));

    after(() => session.fromPartition('empty-certificate').setCertificateVerifyProc(null));

    it('can respond with empty certificate list', async () => {
      app.once('select-client-certificate', function (event, webContents, url, list, callback) {
        console.log('select-client-certificate emitted');
        event.preventDefault();
        callback();
      });
      await w.webContents.loadURL(secureUrl);
      expect(w.webContents.getTitle()).to.equal('denied');
    });
  });

  ifdescribe(process.platform === 'win32')('setAsDefaultProtocolClient(protocol, path, args)', () => {
    const protocol = 'electron-test';
    const updateExe = path.resolve(path.dirname(process.execPath), '..', 'Update.exe');
    const processStartArgs = [
      '--processStart', `"${path.basename(process.execPath)}"`,
      '--process-start-args', '"--hidden"'
    ];

    let Winreg: any;
    let classesKey: any;

    before(function () {
      Winreg = require('winreg');

      classesKey = new Winreg({
        hive: Winreg.HKCU,
        key: '\\Software\\Classes\\'
      });
    });

    after(function (done) {
      if (process.platform !== 'win32') {
        done();
      } else {
        const protocolKey = new Winreg({
          hive: Winreg.HKCU,
          key: `\\Software\\Classes\\${protocol}`
        });

        // The last test leaves the registry dirty,
        // delete the protocol key for those of us who test at home
        protocolKey.destroy(() => done());
      }
    });

    beforeEach(() => {
      app.removeAsDefaultProtocolClient(protocol);
      app.removeAsDefaultProtocolClient(protocol, updateExe, processStartArgs);
    });

    afterEach(() => {
      app.removeAsDefaultProtocolClient(protocol);
      expect(app.isDefaultProtocolClient(protocol)).to.equal(false);

      app.removeAsDefaultProtocolClient(protocol, updateExe, processStartArgs);
      expect(app.isDefaultProtocolClient(protocol, updateExe, processStartArgs)).to.equal(false);
    });

    it('sets the app as the default protocol client', () => {
      expect(app.isDefaultProtocolClient(protocol)).to.equal(false);
      app.setAsDefaultProtocolClient(protocol);
      expect(app.isDefaultProtocolClient(protocol)).to.equal(true);
    });

    it('allows a custom path and args to be specified', () => {
      expect(app.isDefaultProtocolClient(protocol, updateExe, processStartArgs)).to.equal(false);
      app.setAsDefaultProtocolClient(protocol, updateExe, processStartArgs);

      expect(app.isDefaultProtocolClient(protocol, updateExe, processStartArgs)).to.equal(true);
      expect(app.isDefaultProtocolClient(protocol)).to.equal(false);
    });

    it('creates a registry entry for the protocol class', async () => {
      app.setAsDefaultProtocolClient(protocol);

      const keys = await promisify(classesKey.keys).call(classesKey) as any[];
      const exists = keys.some(key => key.key.includes(protocol));
      expect(exists).to.equal(true);
    });

    it('completely removes a registry entry for the protocol class', async () => {
      app.setAsDefaultProtocolClient(protocol);
      app.removeAsDefaultProtocolClient(protocol);

      const keys = await promisify(classesKey.keys).call(classesKey) as any[];
      const exists = keys.some(key => key.key.includes(protocol));
      expect(exists).to.equal(false);
    });

    it('only unsets a class registry key if it contains other data', async () => {
      app.setAsDefaultProtocolClient(protocol);

      const protocolKey = new Winreg({
        hive: Winreg.HKCU,
        key: `\\Software\\Classes\\${protocol}`
      });

      await promisify(protocolKey.set).call(protocolKey, 'test-value', 'REG_BINARY', '123');
      app.removeAsDefaultProtocolClient(protocol);

      const keys = await promisify(classesKey.keys).call(classesKey) as any[];
      const exists = keys.some(key => key.key.includes(protocol));
      expect(exists).to.equal(true);
    });

    it('sets the default client such that getApplicationNameForProtocol returns Electron', () => {
      app.setAsDefaultProtocolClient(protocol);
      expect(app.getApplicationNameForProtocol(`${protocol}://`)).to.equal('Electron');
    });
  });

  describe('getApplicationNameForProtocol()', () => {
    // TODO: Linux CI doesn't have registered http & https handlers
    ifit(!(process.env.CI && process.platform === 'linux'))('returns application names for common protocols', function () {
      // We can't expect particular app names here, but these protocols should
      // at least have _something_ registered. Except on our Linux CI
      // environment apparently.
      const protocols = [
        'http://',
        'https://'
      ];
      for (const protocol of protocols) {
        expect(app.getApplicationNameForProtocol(protocol)).to.not.equal('');
      }
    });

    it('returns an empty string for a bogus protocol', () => {
      expect(app.getApplicationNameForProtocol('bogus-protocol://')).to.equal('');
    });
  });

  ifdescribe(process.platform !== 'linux')('getApplicationInfoForProtocol()', () => {
    it('returns promise rejection for a bogus protocol', async function () {
      await expect(
        app.getApplicationInfoForProtocol('bogus-protocol://')
      ).to.eventually.be.rejectedWith(
        'Unable to retrieve installation path to app'
      );
    });

    it('returns resolved promise with appPath, displayName and icon', async function () {
      const appInfo = await app.getApplicationInfoForProtocol('https://');
      expect(appInfo.path).not.to.be.undefined();
      expect(appInfo.name).not.to.be.undefined();
      expect(appInfo.icon).not.to.be.undefined();
    });
  });

  describe('isDefaultProtocolClient()', () => {
    it('returns false for a bogus protocol', () => {
      expect(app.isDefaultProtocolClient('bogus-protocol://')).to.equal(false);
    });
  });

  ifdescribe(process.platform === 'win32')('app launch through uri', () => {
    it('does not launch for argument following a URL', async () => {
      const appPath = path.join(fixturesPath, 'api', 'quit-app');
      // App should exit with non 123 code.
      const first = cp.spawn(process.execPath, [appPath, 'electron-test:?', 'abc']);
      const [code] = await once(first, 'exit');
      expect(code).to.not.equal(123);
    });

    it('launches successfully for argument following a file path', async () => {
      const appPath = path.join(fixturesPath, 'api', 'quit-app');
      // App should exit with code 123.
      const first = cp.spawn(process.execPath, [appPath, 'e:\\abc', 'abc']);
      const [code] = await once(first, 'exit');
      expect(code).to.equal(123);
    });

    it('launches successfully for multiple URIs following --', async () => {
      const appPath = path.join(fixturesPath, 'api', 'quit-app');
      // App should exit with code 123.
      const first = cp.spawn(process.execPath, [appPath, '--', 'http://electronjs.org', 'electron-test://testdata']);
      const [code] = await once(first, 'exit');
      expect(code).to.equal(123);
    });
  });

  // FIXME Get these specs running on Linux CI
  ifdescribe(process.platform !== 'linux')('getFileIcon() API', () => {
    const iconPath = path.join(__dirname, 'fixtures/assets/icon.ico');
    const sizes = {
      small: 16,
      normal: 32,
      large: process.platform === 'win32' ? 32 : 48
    };

    it('fetches a non-empty icon', async () => {
      const icon = await app.getFileIcon(iconPath);
      expect(icon.isEmpty()).to.equal(false);
    });

    it('fetches normal icon size by default', async () => {
      const icon = await app.getFileIcon(iconPath);
      const size = icon.getSize();

      expect(size.height).to.equal(sizes.normal);
      expect(size.width).to.equal(sizes.normal);
    });

    describe('size option', () => {
      it('fetches a small icon', async () => {
        const icon = await app.getFileIcon(iconPath, { size: 'small' });
        const size = icon.getSize();

        expect(size.height).to.equal(sizes.small);
        expect(size.width).to.equal(sizes.small);
      });

      it('fetches a normal icon', async () => {
        const icon = await app.getFileIcon(iconPath, { size: 'normal' });
        const size = icon.getSize();

        expect(size.height).to.equal(sizes.normal);
        expect(size.width).to.equal(sizes.normal);
      });

      it('fetches a large icon', async () => {
        // macOS does not support large icons
        if (process.platform === 'darwin') return;

        const icon = await app.getFileIcon(iconPath, { size: 'large' });
        const size = icon.getSize();

        expect(size.height).to.equal(sizes.large);
        expect(size.width).to.equal(sizes.large);
      });
    });
  });

  describe('getAppMetrics() API', () => {
    it('returns memory and cpu stats of all running electron processes', () => {
      const appMetrics = app.getAppMetrics();
      expect(appMetrics).to.be.an('array').and.have.lengthOf.at.least(1, 'App memory info object is not > 0');

      const types = [];
      for (const entry of appMetrics) {
        expect(entry.pid).to.be.above(0, 'pid is not > 0');
        expect(entry.type).to.be.a('string').that.does.not.equal('');
        expect(entry.creationTime).to.be.a('number').that.is.greaterThan(0);

        types.push(entry.type);
        expect(entry.cpu).to.have.ownProperty('percentCPUUsage').that.is.a('number');
        expect(entry.cpu).to.have.ownProperty('cumulativeCPUUsage').that.is.a('number');
        expect(entry.cpu).to.have.ownProperty('idleWakeupsPerSecond').that.is.a('number');

        expect(entry.memory).to.have.property('workingSetSize').that.is.greaterThan(0);
        expect(entry.memory).to.have.property('peakWorkingSetSize').that.is.greaterThan(0);

        if (entry.type === 'Utility' || entry.type === 'GPU') {
          expect(entry.serviceName).to.be.a('string').that.does.not.equal('');
        }

        if (entry.type === 'Utility') {
          expect(entry).to.have.property('name').that.is.a('string');
        }

        if (process.platform === 'win32') {
          expect(entry.memory).to.have.property('privateBytes').that.is.greaterThan(0);
        }

        if (process.platform !== 'linux') {
          expect(entry.sandboxed).to.be.a('boolean');
        }

        if (process.platform === 'win32') {
          expect(entry.integrityLevel).to.be.a('string');
        }
      }

      if (process.platform === 'darwin') {
        expect(types).to.include('GPU');
      }

      expect(types).to.include('Browser');
    });
  });

  describe('getGPUFeatureStatus() API', () => {
    it('returns the graphic features statuses', () => {
      const features = app.getGPUFeatureStatus();
      expect(features).to.have.ownProperty('webgl').that.is.a('string');
      expect(features).to.have.ownProperty('gpu_compositing').that.is.a('string');
    });
  });

  ifdescribe(!process.env.IS_ASAN)('getGPUInfo() API', () => {
    const appPath = path.join(fixturesPath, 'api', 'gpu-info.js');

    const getGPUInfo = async (type: string) => {
      const appProcess = cp.spawn(process.execPath, [appPath, type]);
      let gpuInfoData = '';
      let errorData = '';
      appProcess.stdout.on('data', (data) => {
        gpuInfoData += data;
      });
      appProcess.stderr.on('data', (data) => {
        errorData += data;
      });
      const [exitCode] = await once(appProcess, 'exit');
      if (exitCode === 0) {
        try {
          const [, json] = /HERE COMES THE JSON: (.+) AND THERE IT WAS/.exec(gpuInfoData)!;
          // return info data on successful exit
          return JSON.parse(json);
        } catch (e) {
          console.error('Failed to interpret the following as JSON:');
          console.error(gpuInfoData);
          throw e;
        }
      } else {
        // return error if not clean exit
        throw new Error(errorData);
      }
    };
    const verifyBasicGPUInfo = async (gpuInfo: any) => {
      // Devices information is always present in the available info.
      expect(gpuInfo).to.have.ownProperty('gpuDevice')
        .that.is.an('array')
        .and.does.not.equal([]);

      const device = gpuInfo.gpuDevice[0];
      expect(device).to.be.an('object')
        .and.to.have.property('deviceId')
        .that.is.a('number')
        .not.lessThan(0);
    };

    it('succeeds with basic GPUInfo', async () => {
      const gpuInfo = await getGPUInfo('basic');
      await verifyBasicGPUInfo(gpuInfo);
    });

    it('succeeds with complete GPUInfo', async () => {
      const completeInfo = await getGPUInfo('complete');
      if (process.platform === 'linux') {
        // For linux and macOS complete info is same as basic info
        await verifyBasicGPUInfo(completeInfo);
        const basicInfo = await getGPUInfo('basic');
        expect(completeInfo).to.deep.equal(basicInfo);
      } else {
        // Gl version is present in the complete info.
        expect(completeInfo).to.have.ownProperty('auxAttributes')
          .that.is.an('object');
        if (completeInfo.gpuDevice.active) {
          expect(completeInfo.auxAttributes).to.have.ownProperty('glVersion')
            .that.is.a('string')
            .and.does.not.equal([]);
        }
      }
    });

    it('fails for invalid info_type', () => {
      const invalidType = 'invalid';
      const expectedErrorMessage = "Invalid info type. Use 'basic' or 'complete'";
      return expect(app.getGPUInfo(invalidType as any)).to.eventually.be.rejectedWith(expectedErrorMessage);
    });
  });

  ifdescribe(!(process.platform === 'linux' && (process.arch === 'arm64' || process.arch === 'arm')))('sandbox options', () => {
    // Our ARM tests are run on VSTS rather than CircleCI, and the Docker
    // setup on VSTS disallows syscalls that Chrome requires for setting up
    // sandboxing.
    // See:
    // - https://docs.docker.com/engine/security/seccomp/#significant-syscalls-blocked-by-the-default-profile
    // - https://chromium.googlesource.com/chromium/src/+/70.0.3538.124/sandbox/linux/services/credentials.cc#292
    // - https://github.com/docker/docker-ce/blob/ba7dfc59ccfe97c79ee0d1379894b35417b40bca/components/engine/profiles/seccomp/seccomp_default.go#L497
    // - https://blog.jessfraz.com/post/how-to-use-new-docker-seccomp-profiles/
    //
    // Adding `--cap-add SYS_ADMIN` or `--security-opt seccomp=unconfined`
    // to the Docker invocation allows the syscalls that Chrome needs, but
    // are probably more permissive than we'd like.

    let appProcess: cp.ChildProcess = null as any;
    let server: net.Server = null as any;
    const socketPath = process.platform === 'win32' ? '\\\\.\\pipe\\electron-mixed-sandbox' : '/tmp/electron-mixed-sandbox';

    beforeEach(function (done) {
      fs.unlink(socketPath, () => {
        server = net.createServer();
        server.listen(socketPath);
        done();
      });
    });

    afterEach(done => {
      if (appProcess != null) appProcess.kill();

      if (server) {
        server.close(() => {
          if (process.platform === 'win32') {
            done();
          } else {
            fs.unlink(socketPath, () => done());
          }
        });
      } else {
        done();
      }
    });

    describe('when app.enableSandbox() is called', () => {
      it('adds --enable-sandbox to all renderer processes', done => {
        const appPath = path.join(fixturesPath, 'api', 'mixed-sandbox-app');
        appProcess = cp.spawn(process.execPath, [appPath, '--app-enable-sandbox'], { stdio: 'inherit' });

        server.once('error', error => { done(error); });

        server.on('connection', client => {
          client.once('data', (data) => {
            const argv = JSON.parse(data.toString());
            expect(argv.sandbox).to.include('--enable-sandbox');
            expect(argv.sandbox).to.not.include('--no-sandbox');

            expect(argv.noSandbox).to.include('--enable-sandbox');
            expect(argv.noSandbox).to.not.include('--no-sandbox');

            expect(argv.noSandboxDevtools).to.equal(true);
            expect(argv.sandboxDevtools).to.equal(true);

            done();
          });
        });
      });
    });

    describe('when the app is launched with --enable-sandbox', () => {
      it('adds --enable-sandbox to all renderer processes', done => {
        const appPath = path.join(fixturesPath, 'api', 'mixed-sandbox-app');
        appProcess = cp.spawn(process.execPath, [appPath, '--enable-sandbox'], { stdio: 'inherit' });

        server.once('error', error => { done(error); });

        server.on('connection', client => {
          client.once('data', data => {
            const argv = JSON.parse(data.toString());
            expect(argv.sandbox).to.include('--enable-sandbox');
            expect(argv.sandbox).to.not.include('--no-sandbox');

            expect(argv.noSandbox).to.include('--enable-sandbox');
            expect(argv.noSandbox).to.not.include('--no-sandbox');

            expect(argv.noSandboxDevtools).to.equal(true);
            expect(argv.sandboxDevtools).to.equal(true);

            done();
          });
        });
      });
    });
  });

  describe('disableDomainBlockingFor3DAPIs() API', () => {
    it('throws when called after app is ready', () => {
      expect(() => {
        app.disableDomainBlockingFor3DAPIs();
      }).to.throw(/before app is ready/);
    });
  });

  ifdescribe(process.platform === 'darwin')('app hide and show API', () => {
    describe('app.isHidden', () => {
      it('returns true when the app is hidden', async () => {
        app.hide();
        await expect(
          waitUntil(() => app.isHidden())
        ).to.eventually.be.fulfilled();
      });
      it('returns false when the app is shown', async () => {
        app.show();
        await expect(
          waitUntil(() => !app.isHidden())
        ).to.eventually.be.fulfilled();
      });
    });
  });

  ifdescribe(process.platform === 'darwin')('dock APIs', () => {
    after(async () => {
      await app.dock.show();
    });

    describe('dock.setMenu', () => {
      it('can be retrieved via dock.getMenu', () => {
        expect(app.dock.getMenu()).to.equal(null);
        const menu = new Menu();
        app.dock.setMenu(menu);
        expect(app.dock.getMenu()).to.equal(menu);
      });

      it('keeps references to the menu', () => {
        app.dock.setMenu(new Menu());
        const v8Util = process._linkedBinding('electron_common_v8_util');
        v8Util.requestGarbageCollectionForTesting();
      });
    });

    describe('dock.setIcon', () => {
      it('throws a descriptive error for a bad icon path', () => {
        const badPath = path.resolve('I', 'Do', 'Not', 'Exist');
        expect(() => {
          app.dock.setIcon(badPath);
        }).to.throw(/Failed to load image from path (.+)/);
      });
    });

    describe('dock.bounce', () => {
      it('should return -1 for unknown bounce type', () => {
        expect(app.dock.bounce('bad type' as any)).to.equal(-1);
      });

      it('should return a positive number for informational type', () => {
        const appHasFocus = !!BrowserWindow.getFocusedWindow();
        if (!appHasFocus) {
          expect(app.dock.bounce('informational')).to.be.at.least(0);
        }
      });

      it('should return a positive number for critical type', () => {
        const appHasFocus = !!BrowserWindow.getFocusedWindow();
        if (!appHasFocus) {
          expect(app.dock.bounce('critical')).to.be.at.least(0);
        }
      });
    });

    describe('dock.cancelBounce', () => {
      it('should not throw', () => {
        app.dock.cancelBounce(app.dock.bounce('critical'));
      });
    });

    describe('dock.setBadge', () => {
      after(() => {
        app.dock.setBadge('');
      });

      it('should not throw', () => {
        app.dock.setBadge('1');
      });

      it('should be retrievable via getBadge', () => {
        app.dock.setBadge('test');
        expect(app.dock.getBadge()).to.equal('test');
      });
    });

    describe('dock.hide', () => {
      it('should not throw', () => {
        app.dock.hide();
        expect(app.dock.isVisible()).to.equal(false);
      });
    });

    // Note that dock.show tests should run after dock.hide tests, to work
    // around a bug of macOS.
    // See https://github.com/electron/electron/pull/25269 for more.
    describe('dock.show', () => {
      it('should not throw', () => {
        return app.dock.show().then(() => {
          expect(app.dock.isVisible()).to.equal(true);
        });
      });

      it('returns a Promise', () => {
        expect(app.dock.show()).to.be.a('promise');
      });

      it('eventually fulfills', async () => {
        await expect(app.dock.show()).to.eventually.be.fulfilled.equal(undefined);
      });
    });
  });

  describe('whenReady', () => {
    it('returns a Promise', () => {
      expect(app.whenReady()).to.be.a('promise');
    });

    it('becomes fulfilled if the app is already ready', async () => {
      expect(app.isReady()).to.equal(true);
      await expect(app.whenReady()).to.be.eventually.fulfilled.equal(undefined);
    });
  });

  describe('app.applicationMenu', () => {
    it('has the applicationMenu property', () => {
      expect(app).to.have.property('applicationMenu');
    });
  });

  describe('commandLine.hasSwitch', () => {
    it('returns true when present', () => {
      app.commandLine.appendSwitch('foobar1');
      expect(app.commandLine.hasSwitch('foobar1')).to.equal(true);
    });

    it('returns false when not present', () => {
      expect(app.commandLine.hasSwitch('foobar2')).to.equal(false);
    });
  });

  describe('commandLine.hasSwitch (existing argv)', () => {
    it('returns true when present', async () => {
      const { hasSwitch } = await runTestApp('command-line', '--foobar');
      expect(hasSwitch).to.equal(true);
    });

    it('returns false when not present', async () => {
      const { hasSwitch } = await runTestApp('command-line');
      expect(hasSwitch).to.equal(false);
    });
  });

  describe('commandLine.getSwitchValue', () => {
    it('returns the value when present', () => {
      app.commandLine.appendSwitch('foobar', 'æøåü');
      expect(app.commandLine.getSwitchValue('foobar')).to.equal('æøåü');
    });

    it('returns an empty string when present without value', () => {
      app.commandLine.appendSwitch('foobar1');
      expect(app.commandLine.getSwitchValue('foobar1')).to.equal('');
    });

    it('returns an empty string when not present', () => {
      expect(app.commandLine.getSwitchValue('foobar2')).to.equal('');
    });
  });

  describe('commandLine.getSwitchValue (existing argv)', () => {
    it('returns the value when present', async () => {
      const { getSwitchValue } = await runTestApp('command-line', '--foobar=test');
      expect(getSwitchValue).to.equal('test');
    });

    it('returns an empty string when present without value', async () => {
      const { getSwitchValue } = await runTestApp('command-line', '--foobar');
      expect(getSwitchValue).to.equal('');
    });

    it('returns an empty string when not present', async () => {
      const { getSwitchValue } = await runTestApp('command-line');
      expect(getSwitchValue).to.equal('');
    });
  });

  describe('commandLine.removeSwitch', () => {
    it('no-ops a non-existent switch', async () => {
      expect(app.commandLine.hasSwitch('foobar3')).to.equal(false);
      app.commandLine.removeSwitch('foobar3');
      expect(app.commandLine.hasSwitch('foobar3')).to.equal(false);
    });

    it('removes an existing switch', async () => {
      app.commandLine.appendSwitch('foobar3', 'test');
      expect(app.commandLine.hasSwitch('foobar3')).to.equal(true);
      app.commandLine.removeSwitch('foobar3');
      expect(app.commandLine.hasSwitch('foobar3')).to.equal(false);
    });
  });

  ifdescribe(process.platform === 'darwin')('app.setSecureKeyboardEntryEnabled', () => {
    it('changes Secure Keyboard Entry is enabled', () => {
      app.setSecureKeyboardEntryEnabled(true);
      expect(app.isSecureKeyboardEntryEnabled()).to.equal(true);
      app.setSecureKeyboardEntryEnabled(false);
      expect(app.isSecureKeyboardEntryEnabled()).to.equal(false);
    });
  });

  describe('configureHostResolver', () => {
    after(() => {
      // Returns to the default configuration.
      app.configureHostResolver({});
    });

    it('fails on bad arguments', () => {
      expect(() => {
        (app.configureHostResolver as any)();
      }).to.throw();
      expect(() => {
        app.configureHostResolver({
          secureDnsMode: 'notAValidValue' as any
        });
      }).to.throw();
      expect(() => {
        app.configureHostResolver({
          secureDnsServers: [123 as any]
        });
      }).to.throw();
    });

    it('affects dns lookup behavior', async () => {
      // 1. resolve a domain name to check that things are working
      await expect(new Promise((resolve, reject) => {
        electronNet.request({
          method: 'HEAD',
          url: 'https://www.electronjs.org'
        }).on('response', resolve)
          .on('error', reject)
          .end();
      })).to.eventually.be.fulfilled();
      // 2. change the host resolver configuration to something that will
      // always fail
      app.configureHostResolver({
        secureDnsMode: 'secure',
        secureDnsServers: ['https://127.0.0.1:1234']
      });
      // 3. check that resolving domain names now fails
      await expect(new Promise((resolve, reject) => {
        electronNet.request({
          method: 'HEAD',
          // Needs to be a slightly different domain to above, otherwise the
          // response will come from the cache.
          url: 'https://electronjs.org'
        }).on('response', resolve)
          .on('error', reject)
          .end();
      })).to.eventually.be.rejectedWith(/ERR_NAME_NOT_RESOLVED/);
    });
  });

  describe('about panel', () => {
    it('app.setAboutPanelOptions() does not crash', () => {
      app.setAboutPanelOptions({
        applicationName: 'electron!!',
        version: '1.2.3'
      });
    });

    it('app.showAboutPanel() does not crash & runs asynchronously', () => {
      app.showAboutPanel();
    });
  });

  describe('app.setProxy(options)', () => {
    let server: http.Server;

    afterEach(async () => {
      if (server) {
        server.close();
      }
      await app.setProxy({ mode: 'direct' as const });
    });

    it('allows configuring proxy settings', async () => {
      const config = { proxyRules: 'http=myproxy:80' };
      await app.setProxy(config);
      const proxy = await app.resolveProxy('http://example.com/');
      expect(proxy).to.equal('PROXY myproxy:80');
    });

    it('allows removing the implicit bypass rules for localhost', async () => {
      const config = {
        proxyRules: 'http=myproxy:80',
        proxyBypassRules: '<-loopback>'
      };

      await app.setProxy(config);
      const proxy = await app.resolveProxy('http://localhost');
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
        await app.setProxy(config);
        const proxy = await app.resolveProxy('https://google.com');
        expect(proxy).to.equal('PROXY myproxy:8132');
      }
      {
        const config = { mode: 'pac_script' as any, pacScript: url };
        await app.setProxy(config);
        const proxy = await app.resolveProxy('https://google.com');
        expect(proxy).to.equal('PROXY myproxy:8132');
      }
    });

    it('allows bypassing proxy settings', async () => {
      const config = {
        proxyRules: 'http=myproxy:80',
        proxyBypassRules: '<local>'
      };
      await app.setProxy(config);
      const proxy = await app.resolveProxy('http://example/');
      expect(proxy).to.equal('DIRECT');
    });

    it('allows configuring proxy settings with mode `direct`', async () => {
      const config = { mode: 'direct' as const, proxyRules: 'http=myproxy:80' };
      await app.setProxy(config);
      const proxy = await app.resolveProxy('http://example.com/');
      expect(proxy).to.equal('DIRECT');
    });

    it('allows configuring proxy settings with mode `auto_detect`', async () => {
      const config = { mode: 'auto_detect' as const };
      await app.setProxy(config);
    });

    it('allows configuring proxy settings with mode `pac_script`', async () => {
      const config = { mode: 'pac_script' as const };
      await app.setProxy(config);
      const proxy = await app.resolveProxy('http://example.com/');
      expect(proxy).to.equal('DIRECT');
    });

    it('allows configuring proxy settings with mode `fixed_servers`', async () => {
      const config = { mode: 'fixed_servers' as const, proxyRules: 'http=myproxy:80' };
      await app.setProxy(config);
      const proxy = await app.resolveProxy('http://example.com/');
      expect(proxy).to.equal('PROXY myproxy:80');
    });

    it('allows configuring proxy settings with mode `system`', async () => {
      const config = { mode: 'system' as const };
      await app.setProxy(config);
    });

    it('disallows configuring proxy settings with mode `invalid`', async () => {
      const config = { mode: 'invalid' as any };
      await expect(app.setProxy(config)).to.eventually.be.rejectedWith(/Invalid mode/);
    });

    it('impacts proxy for requests made from utility process', async () => {
      const utilityFixturePath = path.resolve(__dirname, 'fixtures', 'api', 'utility-process', 'api-net-spec.js');
      const fn = async () => {
        const urlRequest = electronNet.request('http://example.com/');
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        const message = await collectStreamBody(response);
        expect(message).to.equal('ok from proxy\n');
      };
      server = http.createServer((req, res) => {
        res.writeHead(200);
        res.end('ok from proxy\n');
      });
      const { port, hostname } = await listen(server);
      const config = { mode: 'fixed_servers' as const, proxyRules: `http=${hostname}:${port}` };
      await app.setProxy(config);
      const proxy = await app.resolveProxy('http://example.com/');
      expect(proxy).to.equal(`PROXY ${hostname}:${port}`);
      const child = utilityProcess.fork(utilityFixturePath, [], {
        execArgv: ['--expose-gc']
      });
      child.postMessage({ fn: `(${fn})()` });
      const [data] = await once(child, 'message');
      expect(data.ok).to.be.true(data.message);
      // Cleanup.
      const [code] = await once(child, 'exit');
      expect(code).to.equal(0);
    });

    it('does not impact proxy for requests made from main process', async () => {
      server = http.createServer((req, res) => {
        res.writeHead(200);
        res.end('ok from server\n');
      });
      const { url } = await listen(server);
      const config = { mode: 'fixed_servers' as const, proxyRules: 'http=myproxy:80' };
      await app.setProxy(config);
      const proxy = await app.resolveProxy('http://example.com/');
      expect(proxy).to.equal('PROXY myproxy:80');
      const urlRequest = electronNet.request(url);
      const response = await getResponse(urlRequest);
      expect(response.statusCode).to.equal(200);
      const message = await collectStreamBody(response);
      expect(message).to.equal('ok from server\n');
    });
  });
});

describe('default behavior', () => {
  describe('application menu', () => {
    it('creates the default menu if the app does not set it', async () => {
      const result = await runTestApp('default-menu');
      expect(result).to.equal(false);
    });

    it('does not create the default menu if the app sets a custom menu', async () => {
      const result = await runTestApp('default-menu', '--custom-menu');
      expect(result).to.equal(true);
    });

    it('does not create the default menu if the app sets a null menu', async () => {
      const result = await runTestApp('default-menu', '--null-menu');
      expect(result).to.equal(true);
    });
  });

  describe('window-all-closed', () => {
    afterEach(closeAllWindows);

    it('quits when the app does not handle the event', async () => {
      const result = await runTestApp('window-all-closed');
      expect(result).to.equal(false);
    });

    it('does not quit when the app handles the event', async () => {
      const result = await runTestApp('window-all-closed', '--handle-event');
      expect(result).to.equal(true);
    });

    it('should omit closed windows from getAllWindows', async () => {
      const w = new BrowserWindow({ show: false });
      const len = new Promise(resolve => {
        app.on('window-all-closed', () => {
          resolve(BrowserWindow.getAllWindows().length);
        });
      });
      w.close();
      expect(await len).to.equal(0);
    });
  });

  describe('user agent fallback', () => {
    let initialValue: string;

    before(() => {
      initialValue = app.userAgentFallback!;
    });

    it('should have a reasonable default', () => {
      expect(initialValue).to.include(`Electron/${process.versions.electron}`);
      expect(initialValue).to.include(`Chrome/${process.versions.chrome}`);
    });

    it('should be overridable', () => {
      app.userAgentFallback = 'test-agent/123';
      expect(app.userAgentFallback).to.equal('test-agent/123');
    });

    it('should be restorable', () => {
      app.userAgentFallback = 'test-agent/123';
      app.userAgentFallback = '';
      expect(app.userAgentFallback).to.equal(initialValue);
    });
  });

  describe('login event', () => {
    afterEach(closeAllWindows);
    let server: http.Server;
    let serverUrl: string;

    before(async () => {
      server = http.createServer((request, response) => {
        if (request.headers.authorization) {
          return response.end('ok');
        }
        response
          .writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' })
          .end();
      });

      serverUrl = (await listen(server)).url;
    });

    it('should emit a login event on app when a WebContents hits a 401', async () => {
      const w = new BrowserWindow({ show: false });
      w.loadURL(serverUrl);
      const [, webContents] = await once(app, 'login') as [any, WebContents];
      expect(webContents).to.equal(w.webContents);
    });
  });

  describe('running under ARM64 translation', () => {
    it('does not throw an error', () => {
      if (process.platform === 'darwin' || process.platform === 'win32') {
        expect(app.runningUnderARM64Translation).not.to.be.undefined();
        expect(() => {
          return app.runningUnderARM64Translation;
        }).not.to.throw();
      } else {
        expect(app.runningUnderARM64Translation).to.be.undefined();
      }
    });
  });
});

async function runTestApp (name: string, ...args: any[]) {
  const appPath = path.join(fixturesPath, 'api', name);
  const electronPath = process.execPath;
  const appProcess = cp.spawn(electronPath, [appPath, ...args]);

  let output = '';
  appProcess.stdout.on('data', (data) => { output += data; });

  await once(appProcess.stdout, 'end');

  return JSON.parse(output);
}
