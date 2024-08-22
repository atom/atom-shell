import { expect } from 'chai';
import * as childProcess from 'node:child_process';
import * as path from 'node:path';
import { BrowserWindow, MessageChannelMain, utilityProcess, app } from 'electron/main';
import { ifit, startRemoteControlApp } from './lib/spec-helpers';
import { closeWindow } from './lib/window-helpers';
import { respondOnce, randomString, kOneKiloByte } from './lib/net-helpers';
import { once } from 'node:events';
import { pathToFileURL } from 'node:url';
import { setImmediate } from 'node:timers/promises';
import { systemPreferences } from 'electron';

const fixturesPath = path.resolve(__dirname, 'fixtures', 'api', 'utility-process');
const isWindowsOnArm = process.platform === 'win32' && process.arch === 'arm64';
const isWindows32Bit = process.platform === 'win32' && process.arch === 'ia32';

describe('utilityProcess module', () => {
  describe('UtilityProcess constructor', () => {
    it('throws when empty script path is provided', async () => {
      expect(() => {
        utilityProcess.fork('');
      }).to.throw();
    });

    it('throws when options.stdio is not valid', async () => {
      expect(() => {
        utilityProcess.fork(path.join(fixturesPath, 'empty.js'), [], {
          execArgv: ['--test', '--test2'],
          serviceName: 'test',
          stdio: 'ipc'
        });
      }).to.throw(/stdio must be of the following values: inherit, pipe, ignore/);

      expect(() => {
        utilityProcess.fork(path.join(fixturesPath, 'empty.js'), [], {
          execArgv: ['--test', '--test2'],
          serviceName: 'test',
          stdio: ['ignore', 'ignore']
        });
      }).to.throw(/configuration missing for stdin, stdout or stderr/);

      expect(() => {
        utilityProcess.fork(path.join(fixturesPath, 'empty.js'), [], {
          execArgv: ['--test', '--test2'],
          serviceName: 'test',
          stdio: ['pipe', 'inherit', 'inherit']
        });
      }).to.throw(/stdin value other than ignore is not supported/);
    });
  });

  describe('lifecycle events', () => {
    it('emits \'spawn\' when child process successfully launches', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'empty.js'));
      await once(child, 'spawn');
    });

    it('emits \'exit\' when child process exits gracefully', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'empty.js'));
      const [code] = await once(child, 'exit');
      expect(code).to.equal(0);
    });

    ifit(!isWindows32Bit)('emits the correct error code when child process exits nonzero', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'empty.js'));
      await once(child, 'spawn');
      const exit = once(child, 'exit');
      process.kill(child.pid!);
      const [code] = await exit;
      expect(code).to.not.equal(0);
    });

    ifit(!isWindows32Bit)('emits the correct error code when child process is killed', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'empty.js'));
      await once(child, 'spawn');
      const exit = once(child, 'exit');
      process.kill(child.pid!);
      const [code] = await exit;
      expect(code).to.not.equal(0);
    });

    ifit(!isWindows32Bit)('emits \'exit\' when child process crashes', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'crash.js'));
      // SIGSEGV code can differ across pipelines but should never be 0.
      const [code] = await once(child, 'exit');
      expect(code).to.not.equal(0);
    });

    ifit(!isWindows32Bit)('emits \'exit\' corresponding to the child process', async () => {
      const child1 = utilityProcess.fork(path.join(fixturesPath, 'endless.js'));
      await once(child1, 'spawn');
      const child2 = utilityProcess.fork(path.join(fixturesPath, 'crash.js'));
      await once(child2, 'exit');
      expect(child1.kill()).to.be.true();
      await once(child1, 'exit');
    });

    it('emits \'exit\' when there is uncaught exception', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'exception.js'));
      const [code] = await once(child, 'exit');
      expect(code).to.equal(1);
    });

    it('emits \'exit\' when there is uncaught exception in ESM', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'exception.mjs'));
      const [code] = await once(child, 'exit');
      expect(code).to.equal(1);
    });

    it('emits \'exit\' when process.exit is called', async () => {
      const exitCode = 2;
      const child = utilityProcess.fork(path.join(fixturesPath, 'custom-exit.js'), [`--exitCode=${exitCode}`]);
      const [code] = await once(child, 'exit');
      expect(code).to.equal(exitCode);
    });
  });

  describe('app \'child-process-gone\' event', () => {
    ifit(!isWindows32Bit)('with default serviceName', async () => {
      utilityProcess.fork(path.join(fixturesPath, 'crash.js'));
      const [, details] = await once(app, 'child-process-gone') as [any, Electron.Details];
      expect(details.type).to.equal('Utility');
      expect(details.serviceName).to.equal('node.mojom.NodeService');
      expect(details.name).to.equal('Node Utility Process');
      expect(details.reason).to.be.oneOf(['crashed', 'abnormal-exit']);
    });

    ifit(!isWindows32Bit)('with custom serviceName', async () => {
      utilityProcess.fork(path.join(fixturesPath, 'crash.js'), [], { serviceName: 'Hello World!' });
      const [, details] = await once(app, 'child-process-gone') as [any, Electron.Details];
      expect(details.type).to.equal('Utility');
      expect(details.serviceName).to.equal('node.mojom.NodeService');
      expect(details.name).to.equal('Hello World!');
      expect(details.reason).to.be.oneOf(['crashed', 'abnormal-exit']);
    });
  });

  describe('app.getAppMetrics()', () => {
    it('with default serviceName', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'endless.js'));
      await once(child, 'spawn');
      expect(child.pid).to.not.be.null();

      await setImmediate();

      const details = app.getAppMetrics().find(item => item.pid === child.pid)!;
      expect(details).to.be.an('object');
      expect(details.type).to.equal('Utility');
      expect(details.serviceName).to.to.equal('node.mojom.NodeService');
      expect(details.name).to.equal('Node Utility Process');
    });

    it('with custom serviceName', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'endless.js'), [], { serviceName: 'Hello World!' });
      await once(child, 'spawn');
      expect(child.pid).to.not.be.null();

      await setImmediate();

      const details = app.getAppMetrics().find(item => item.pid === child.pid)!;
      expect(details).to.be.an('object');
      expect(details.type).to.equal('Utility');
      expect(details.serviceName).to.to.equal('node.mojom.NodeService');
      expect(details.name).to.equal('Hello World!');
    });
  });

  describe('kill() API', () => {
    it('terminates the child process gracefully', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'endless.js'), [], {
        serviceName: 'endless'
      });
      await once(child, 'spawn');
      expect(child.kill()).to.be.true();
      await once(child, 'exit');
    });
  });

  describe('esm', () => {
    it('is launches an mjs file', async () => {
      const fixtureFile = path.join(fixturesPath, 'esm.mjs');
      const child = utilityProcess.fork(fixtureFile, [], {
        stdio: 'pipe'
      });
      await once(child, 'spawn');
      expect(child.stdout).to.not.be.null();
      let log = '';
      child.stdout!.on('data', (chunk) => {
        log += chunk.toString('utf8');
      });
      await once(child, 'exit');
      expect(log).to.equal(pathToFileURL(fixtureFile) + '\n');
    });
  });

  describe('pid property', () => {
    it('is valid when child process launches successfully', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'empty.js'));
      await once(child, 'spawn');
      expect(child.pid).to.not.be.null();
    });

    it('is undefined when child process fails to launch', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'does-not-exist.js'));
      expect(child.pid).to.be.undefined();
    });
  });

  describe('stdout property', () => {
    it('is null when child process launches with default stdio', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'log.js'));
      await once(child, 'spawn');
      expect(child.stdout).to.be.null();
      expect(child.stderr).to.be.null();
      await once(child, 'exit');
    });

    it('is null when child process launches with ignore stdio configuration', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'log.js'), [], {
        stdio: 'ignore'
      });
      await once(child, 'spawn');
      expect(child.stdout).to.be.null();
      expect(child.stderr).to.be.null();
      await once(child, 'exit');
    });

    it('is valid when child process launches with pipe stdio configuration', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'log.js'), [], {
        stdio: 'pipe'
      });
      await once(child, 'spawn');
      expect(child.stdout).to.not.be.null();
      let log = '';
      child.stdout!.on('data', (chunk) => {
        log += chunk.toString('utf8');
      });
      await once(child, 'exit');
      expect(log).to.equal('hello\n');
    });
  });

  describe('stderr property', () => {
    it('is null when child process launches with default stdio', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'log.js'));
      await once(child, 'spawn');
      expect(child.stdout).to.be.null();
      expect(child.stderr).to.be.null();
      await once(child, 'exit');
    });

    it('is null when child process launches with ignore stdio configuration', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'log.js'), [], {
        stdio: 'ignore'
      });
      await once(child, 'spawn');
      expect(child.stderr).to.be.null();
      await once(child, 'exit');
    });

    ifit(!isWindowsOnArm)('is valid when child process launches with pipe stdio configuration', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'log.js'), [], {
        stdio: ['ignore', 'pipe', 'pipe']
      });
      await once(child, 'spawn');
      expect(child.stderr).to.not.be.null();
      let log = '';
      child.stderr!.on('data', (chunk) => {
        log += chunk.toString('utf8');
      });
      await once(child, 'exit');
      expect(log).to.equal('world');
    });
  });

  describe('postMessage() API', () => {
    it('establishes a default ipc channel with the child process', async () => {
      const result = 'I will be echoed.';
      const child = utilityProcess.fork(path.join(fixturesPath, 'post-message.js'));
      await once(child, 'spawn');
      child.postMessage(result);
      const [data] = await once(child, 'message');
      expect(data).to.equal(result);
      const exit = once(child, 'exit');
      expect(child.kill()).to.be.true();
      await exit;
    });

    it('supports queuing messages on the receiving end', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'post-message-queue.js'));
      const p = once(child, 'spawn');
      child.postMessage('This message');
      child.postMessage(' is');
      child.postMessage(' queued');
      await p;
      const [data] = await once(child, 'message');
      expect(data).to.equal('This message is queued');
      const exit = once(child, 'exit');
      expect(child.kill()).to.be.true();
      await exit;
    });

    it('handles the parent port trying to send an non-clonable object', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'non-cloneable.js'));
      await once(child, 'spawn');
      child.postMessage('non-cloneable');
      const [data] = await once(child, 'message');
      expect(data).to.equal('caught-non-cloneable');
      const exit = once(child, 'exit');
      expect(child.kill()).to.be.true();
      await exit;
    });
  });

  describe('behavior', () => {
    it('supports starting the v8 inspector with --inspect-brk', (done) => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'log.js'), [], {
        stdio: 'pipe',
        execArgv: ['--inspect-brk']
      });

      let output = '';
      const cleanup = () => {
        child.stderr!.removeListener('data', listener);
        child.stdout!.removeListener('data', listener);
        child.once('exit', () => { done(); });
        child.kill();
      };

      const listener = (data: Buffer) => {
        output += data;
        if (/Debugger listening on ws:/m.test(output)) {
          cleanup();
        }
      };

      child.stderr!.on('data', listener);
      child.stdout!.on('data', listener);
    });

    it('supports starting the v8 inspector with --inspect and a provided port', (done) => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'log.js'), [], {
        stdio: 'pipe',
        execArgv: ['--inspect=17364']
      });

      let output = '';
      const cleanup = () => {
        child.stderr!.removeListener('data', listener);
        child.stdout!.removeListener('data', listener);
        child.once('exit', () => { done(); });
        child.kill();
      };

      const listener = (data: Buffer) => {
        output += data;
        if (/Debugger listening on ws:/m.test(output)) {
          expect(output.trim()).to.contain(':17364', 'should be listening on port 17364');
          cleanup();
        }
      };

      child.stderr!.on('data', listener);
      child.stdout!.on('data', listener);
    });

    it('supports changing dns verbatim with --dns-result-order', (done) => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'dns-result-order.js'), [], {
        stdio: 'pipe',
        execArgv: ['--dns-result-order=ipv4first']
      });

      let output = '';
      const cleanup = () => {
        child.stderr!.removeListener('data', listener);
        child.stdout!.removeListener('data', listener);
        child.once('exit', () => { done(); });
        child.kill();
      };

      const listener = (data: Buffer) => {
        output += data;
        expect(output.trim()).to.contain('ipv4first', 'default verbatim should be ipv4first');
        cleanup();
      };

      child.stderr!.on('data', listener);
      child.stdout!.on('data', listener);
    });

    ifit(process.platform !== 'win32')('supports redirecting stdout to parent process', async () => {
      const result = 'Output from utility process';
      const appProcess = childProcess.spawn(process.execPath, [path.join(fixturesPath, 'inherit-stdout'), `--payload=${result}`]);
      let output = '';
      appProcess.stdout.on('data', (data: Buffer) => { output += data; });
      await once(appProcess, 'exit');
      expect(output).to.equal(result);
    });

    ifit(process.platform !== 'win32')('supports redirecting stderr to parent process', async () => {
      const result = 'Error from utility process';
      const appProcess = childProcess.spawn(process.execPath, [path.join(fixturesPath, 'inherit-stderr'), `--payload=${result}`]);
      let output = '';
      appProcess.stderr.on('data', (data: Buffer) => { output += data; });
      await once(appProcess, 'exit');
      expect(output).to.include(result);
    });

    ifit(process.platform !== 'linux')('can access exposed main process modules from the utility process', async () => {
      const message = 'Message from utility process';
      const child = utilityProcess.fork(path.join(fixturesPath, 'expose-main-process-module.js'));
      await once(child, 'spawn');
      child.postMessage(message);
      const [data] = await once(child, 'message');
      expect(data).to.equal(systemPreferences.getMediaAccessStatus('screen'));
      const exit = once(child, 'exit');
      expect(child.kill()).to.be.true();
      await exit;
    });

    it('can establish communication channel with sandboxed renderer', async () => {
      const result = 'Message from sandboxed renderer';
      const w = new BrowserWindow({
        show: false,
        webPreferences: {
          preload: path.join(fixturesPath, 'preload.js')
        }
      });
      await w.loadFile(path.join(__dirname, 'fixtures', 'blank.html'));
      // Create Message port pair for Renderer <-> Utility Process.
      const { port1: rendererPort, port2: childPort1 } = new MessageChannelMain();
      w.webContents.postMessage('port', result, [rendererPort]);
      // Send renderer and main channel port to utility process.
      const child = utilityProcess.fork(path.join(fixturesPath, 'receive-message.js'));
      await once(child, 'spawn');
      child.postMessage('', [childPort1]);
      const [data] = await once(child, 'message');
      expect(data).to.equal(result);
      // Cleanup.
      const exit = once(child, 'exit');
      expect(child.kill()).to.be.true();
      await exit;
      await closeWindow(w);
    });

    ifit(process.platform === 'linux')('allows executing a setuid binary with child_process', async () => {
      const child = utilityProcess.fork(path.join(fixturesPath, 'suid.js'));
      await once(child, 'spawn');
      const [data] = await once(child, 'message');
      expect(data).to.not.be.empty();
      const exit = once(child, 'exit');
      expect(child.kill()).to.be.true();
      await exit;
    });

    it('inherits parent env as default', async () => {
      const appProcess = childProcess.spawn(process.execPath, [path.join(fixturesPath, 'env-app')], {
        env: {
          FROM: 'parent',
          ...process.env
        }
      });
      let output = '';
      appProcess.stdout.on('data', (data: Buffer) => { output += data; });
      await once(appProcess.stdout, 'end');
      const result = process.platform === 'win32' ? '\r\nparent' : 'parent';
      expect(output).to.equal(result);
    });

    it('does not inherit parent env when custom env is provided', async () => {
      const appProcess = childProcess.spawn(process.execPath, [path.join(fixturesPath, 'env-app'), '--create-custom-env'], {
        env: {
          FROM: 'parent',
          ...process.env
        }
      });
      let output = '';
      appProcess.stdout.on('data', (data: Buffer) => { output += data; });
      await once(appProcess.stdout, 'end');
      const result = process.platform === 'win32' ? '\r\nchild' : 'child';
      expect(output).to.equal(result);
    });

    it('changes working directory with cwd', async () => {
      const child = utilityProcess.fork('./log.js', [], {
        cwd: fixturesPath,
        stdio: ['ignore', 'pipe', 'ignore']
      });
      await once(child, 'spawn');
      expect(child.stdout).to.not.be.null();
      let log = '';
      child.stdout!.on('data', (chunk) => {
        log += chunk.toString('utf8');
      });
      await once(child, 'exit');
      expect(log).to.equal('hello\n');
    });

    it('does not crash when running eval', async () => {
      const child = utilityProcess.fork('./eval.js', [], {
        cwd: fixturesPath,
        stdio: 'ignore'
      });
      await once(child, 'spawn');
      const [data] = await once(child, 'message');
      expect(data).to.equal(42);
      // Cleanup.
      const exit = once(child, 'exit');
      expect(child.kill()).to.be.true();
      await exit;
    });

    it('should emit the app#login event when 401', async () => {
      const { remotely } = await startRemoteControlApp();
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        if (!request.headers.authorization) {
          return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
        }
        response.writeHead(200).end('ok');
      });
      const [loginAuthInfo, statusCode] = await remotely(async (serverUrl: string, fixture: string) => {
        const { app, utilityProcess } = require('electron');
        const { once } = require('node:events');
        const child = utilityProcess.fork(fixture, [`--server-url=${serverUrl}`], {
          stdio: 'ignore',
          respondToAuthRequestsFromMainProcess: true
        });
        await once(child, 'spawn');
        const [ev,,, authInfo, cb] = await once(app, 'login');
        ev.preventDefault();
        cb('dummy', 'pass');
        const [result] = await once(child, 'message');
        return [authInfo, ...result];
      }, serverUrl, path.join(fixturesPath, 'net.js'));
      expect(statusCode).to.equal(200);
      expect(loginAuthInfo!.realm).to.equal('Foo');
      expect(loginAuthInfo!.scheme).to.equal('basic');
    });

    it('should receive 401 response when cancelling authentication via app#login event', async () => {
      const { remotely } = await startRemoteControlApp();
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        if (!request.headers.authorization) {
          response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' });
          response.end('unauthenticated');
        } else {
          response.writeHead(200).end('ok');
        }
      });
      const [authDetails, responseBody, statusCode] = await remotely(async (serverUrl: string, fixture: string) => {
        const { app, utilityProcess } = require('electron');
        const { once } = require('node:events');
        const child = utilityProcess.fork(fixture, [`--server-url=${serverUrl}`], {
          stdio: 'ignore',
          respondToAuthRequestsFromMainProcess: true
        });
        await once(child, 'spawn');
        const [,, details,, cb] = await once(app, 'login');
        cb();
        const [response] = await once(child, 'message');
        const [responseBody] = await once(child, 'message');
        return [details, responseBody, ...response];
      }, serverUrl, path.join(fixturesPath, 'net.js'));
      expect(authDetails.url).to.equal(serverUrl);
      expect(statusCode).to.equal(401);
      expect(responseBody).to.equal('unauthenticated');
    });

    it('should upload body when 401', async () => {
      const { remotely } = await startRemoteControlApp();
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        if (!request.headers.authorization) {
          return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
        }
        response.writeHead(200);
        request.on('data', (chunk) => response.write(chunk));
        request.on('end', () => response.end());
      });
      const requestData = randomString(kOneKiloByte);
      const [authDetails, responseBody, statusCode] = await remotely(async (serverUrl: string, requestData: string, fixture: string) => {
        const { app, utilityProcess } = require('electron');
        const { once } = require('node:events');
        const child = utilityProcess.fork(fixture, [`--server-url=${serverUrl}`, '--request-data'], {
          stdio: 'ignore',
          respondToAuthRequestsFromMainProcess: true
        });
        await once(child, 'spawn');
        await once(child, 'message');
        child.postMessage(requestData);
        const [,, details,, cb] = await once(app, 'login');
        cb('user', 'pass');
        const [response] = await once(child, 'message');
        const [responseBody] = await once(child, 'message');
        return [details, responseBody, ...response];
      }, serverUrl, requestData, path.join(fixturesPath, 'net.js'));
      expect(authDetails.url).to.equal(serverUrl);
      expect(statusCode).to.equal(200);
      expect(responseBody).to.equal(requestData);
    });

    it('should not emit the app#login event when 401 with {"credentials":"omit"}', async () => {
      const rc = await startRemoteControlApp();
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        if (!request.headers.authorization) {
          return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
        }
        response.writeHead(200).end('ok');
      });
      const [statusCode, responseHeaders] = await rc.remotely(async (serverUrl: string, fixture: string) => {
        const { app, utilityProcess } = require('electron');
        const { once } = require('node:events');
        let gracefulExit = true;
        const child = utilityProcess.fork(fixture, [`--server-url=${serverUrl}`, '--omit-credentials'], {
          stdio: 'ignore',
          respondToAuthRequestsFromMainProcess: true
        });
        await once(child, 'spawn');
        app.on('login', () => {
          gracefulExit = false;
        });
        const [result] = await once(child, 'message');
        setTimeout(() => {
          if (gracefulExit) {
            app.quit();
          } else {
            process.exit(1);
          }
        });
        return result;
      }, serverUrl, path.join(fixturesPath, 'net.js'));
      const [code] = await once(rc.process, 'exit');
      expect(code).to.equal(0);
      expect(statusCode).to.equal(401);
      expect(responseHeaders['www-authenticate']).to.equal('Basic realm="Foo"');
    });

    it('should not emit the app#login event with default respondToAuthRequestsFromMainProcess', async () => {
      const rc = await startRemoteControlApp();
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        if (!request.headers.authorization) {
          return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
        }
        response.writeHead(200).end('ok');
      });
      const [loginAuthInfo, statusCode] = await rc.remotely(async (serverUrl: string, fixture: string) => {
        const { app, utilityProcess } = require('electron');
        const { once } = require('node:events');
        let gracefulExit = true;
        const child = utilityProcess.fork(fixture, [`--server-url=${serverUrl}`, '--use-net-login-event'], {
          stdio: 'ignore'
        });
        await once(child, 'spawn');
        app.on('login', () => {
          gracefulExit = false;
        });
        const [authInfo] = await once(child, 'message');
        const [result] = await once(child, 'message');
        setTimeout(() => {
          if (gracefulExit) {
            app.quit();
          } else {
            process.exit(1);
          }
        });
        return [authInfo, ...result];
      }, serverUrl, path.join(fixturesPath, 'net.js'));
      const [code] = await once(rc.process, 'exit');
      expect(code).to.equal(0);
      expect(statusCode).to.equal(200);
      expect(loginAuthInfo!.realm).to.equal('Foo');
      expect(loginAuthInfo!.scheme).to.equal('basic');
    });

    it('should emit the app#login event when creating requests with fetch API', async () => {
      const { remotely } = await startRemoteControlApp();
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        if (!request.headers.authorization) {
          return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
        }
        response.writeHead(200).end('ok');
      });
      const [loginAuthInfo, statusCode] = await remotely(async (serverUrl: string, fixture: string) => {
        const { app, utilityProcess } = require('electron');
        const { once } = require('node:events');
        const child = utilityProcess.fork(fixture, [`--server-url=${serverUrl}`, '--use-fetch-api'], {
          stdio: 'ignore',
          respondToAuthRequestsFromMainProcess: true
        });
        await once(child, 'spawn');
        const [ev,,, authInfo, cb] = await once(app, 'login');
        ev.preventDefault();
        cb('dummy', 'pass');
        const [response] = await once(child, 'message');
        return [authInfo, ...response];
      }, serverUrl, path.join(fixturesPath, 'net.js'));
      expect(statusCode).to.equal(200);
      expect(loginAuthInfo!.realm).to.equal('Foo');
      expect(loginAuthInfo!.scheme).to.equal('basic');
    });
  });
});
