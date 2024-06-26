import { expect } from 'chai';
import * as childProcess from 'node:child_process';
import * as fs from 'node:fs';
import * as path from 'node:path';
import * as util from 'node:util';
import { getRemoteContext, ifdescribe, ifit, itremote, useRemoteContext } from './lib/spec-helpers';
import { copyMacOSFixtureApp, getCodesignIdentity, shouldRunCodesignTests, signApp, spawn } from './lib/codesign-helpers';
import { webContents } from 'electron/main';
import { EventEmitter } from 'node:stream';
import { once } from 'node:events';
import { withTempDirectory } from './lib/fs-helpers';

const mainFixturesPath = path.resolve(__dirname, 'fixtures');

describe('node feature', () => {
  const fixtures = path.join(__dirname, 'fixtures');

  describe('child_process', () => {
    describe('child_process.fork', () => {
      it('Works in browser process', async () => {
        const child = childProcess.fork(path.join(fixtures, 'module', 'ping.js'));
        const message = once(child, 'message');
        child.send('message');
        const [msg] = await message;
        expect(msg).to.equal('message');
      });

      it('Has its module searth paths restricted', async () => {
        const child = childProcess.fork(path.join(fixtures, 'module', 'module-paths.js'));
        const [msg] = await once(child, 'message');
        expect(msg.length).to.equal(2);
      });
    });
  });

  describe('child_process in renderer', () => {
    useRemoteContext();

    describe('child_process.fork', () => {
      itremote('works in current process', async (fixtures: string) => {
        const child = require('node:child_process').fork(require('node:path').join(fixtures, 'module', 'ping.js'));
        const message = new Promise<any>(resolve => child.once('message', resolve));
        child.send('message');
        const msg = await message;
        expect(msg).to.equal('message');
      }, [fixtures]);

      itremote('preserves args', async (fixtures: string) => {
        const args = ['--expose_gc', '-test', '1'];
        const child = require('node:child_process').fork(require('node:path').join(fixtures, 'module', 'process_args.js'), args);
        const message = new Promise<any>(resolve => child.once('message', resolve));
        child.send('message');
        const msg = await message;
        expect(args).to.deep.equal(msg.slice(2));
      }, [fixtures]);

      itremote('works in forked process', async (fixtures: string) => {
        const child = require('node:child_process').fork(require('node:path').join(fixtures, 'module', 'fork_ping.js'));
        const message = new Promise<any>(resolve => child.once('message', resolve));
        child.send('message');
        const msg = await message;
        expect(msg).to.equal('message');
      }, [fixtures]);

      itremote('works in forked process when options.env is specified', async (fixtures: string) => {
        const child = require('node:child_process').fork(require('node:path').join(fixtures, 'module', 'fork_ping.js'), [], {
          path: process.env.PATH
        });
        const message = new Promise<any>(resolve => child.once('message', resolve));
        child.send('message');
        const msg = await message;
        expect(msg).to.equal('message');
      }, [fixtures]);

      itremote('has String::localeCompare working in script', async (fixtures: string) => {
        const child = require('node:child_process').fork(require('node:path').join(fixtures, 'module', 'locale-compare.js'));
        const message = new Promise<any>(resolve => child.once('message', resolve));
        child.send('message');
        const msg = await message;
        expect(msg).to.deep.equal([0, -1, 1]);
      }, [fixtures]);

      itremote('has setImmediate working in script', async (fixtures: string) => {
        const child = require('node:child_process').fork(require('node:path').join(fixtures, 'module', 'set-immediate.js'));
        const message = new Promise<any>(resolve => child.once('message', resolve));
        child.send('message');
        const msg = await message;
        expect(msg).to.equal('ok');
      }, [fixtures]);

      itremote('pipes stdio', async (fixtures: string) => {
        const child = require('node:child_process').fork(require('node:path').join(fixtures, 'module', 'process-stdout.js'), { silent: true });
        let data = '';
        child.stdout.on('data', (chunk: any) => {
          data += String(chunk);
        });
        const code = await new Promise<any>(resolve => child.once('close', resolve));
        expect(code).to.equal(0);
        expect(data).to.equal('pipes stdio');
      }, [fixtures]);

      itremote('works when sending a message to a process forked with the --eval argument', async () => {
        const source = "process.on('message', (message) => { process.send(message) })";
        const forked = require('node:child_process').fork('--eval', [source]);
        const message = new Promise(resolve => forked.once('message', resolve));
        forked.send('hello');
        const msg = await message;
        expect(msg).to.equal('hello');
      });

      it('has the electron version in process.versions', async () => {
        const source = 'process.send(process.versions)';
        const forked = require('node:child_process').fork('--eval', [source]);
        const [message] = await once(forked, 'message');
        expect(message)
          .to.have.own.property('electron')
          .that.is.a('string')
          .and.matches(/^\d+\.\d+\.\d+(\S*)?$/);
      });
    });

    describe('child_process.spawn', () => {
      itremote('supports spawning Electron as a node process via the ELECTRON_RUN_AS_NODE env var', async (fixtures: string) => {
        const child = require('node:child_process').spawn(process.execPath, [require('node:path').join(fixtures, 'module', 'run-as-node.js')], {
          env: {
            ELECTRON_RUN_AS_NODE: true
          }
        });

        let output = '';
        child.stdout.on('data', (data: any) => {
          output += data;
        });
        try {
          await new Promise(resolve => child.stdout.once('close', resolve));
          expect(JSON.parse(output)).to.deep.equal({
            stdoutType: 'pipe',
            processType: 'undefined',
            window: 'undefined'
          });
        } finally {
          child.kill();
        }
      }, [fixtures]);
    });

    describe('child_process.exec', () => {
      ifit(process.platform === 'linux')('allows executing a setuid binary from non-sandboxed renderer', async () => {
        // Chrome uses prctl(2) to set the NO_NEW_PRIVILEGES flag on Linux (see
        // https://github.com/torvalds/linux/blob/40fde647cc/Documentation/userspace-api/no_new_privs.rst).
        // We disable this for unsandboxed processes, which the renderer tests
        // are running in. If this test fails with an error like 'effective uid
        // is not 0', then it's likely that our patch to prevent the flag from
        // being set has become ineffective.
        const w = await getRemoteContext();
        const stdout = await w.webContents.executeJavaScript('require(\'child_process\').execSync(\'sudo --help\')');
        expect(stdout).to.not.be.empty();
      });
    });
  });

  describe('fetch', () => {
    itremote('works correctly when nodeIntegration is enabled in the renderer', async (fixtures: string) => {
      const file = require('node:path').join(fixtures, 'hello.txt');
      expect(() => {
        fetch('file://' + file);
      }).to.not.throw();

      expect(() => {
        const formData = new FormData();
        formData.append('username', 'Groucho');
      }).not.to.throw();

      expect(() => {
        const request = new Request('https://example.com', {
          method: 'POST',
          body: JSON.stringify({ foo: 'bar' })
        });
        expect(request.method).to.equal('POST');
      }).not.to.throw();

      expect(() => {
        const response = new Response('Hello, world!');
        expect(response.status).to.equal(200);
      }).not.to.throw();

      expect(() => {
        const headers = new Headers();
        headers.append('Content-Type', 'text/xml');
      }).not.to.throw();
    }, [fixtures]);
  });

  it('does not hang when using the fs module in the renderer process', async () => {
    const appPath = path.join(mainFixturesPath, 'apps', 'libuv-hang', 'main.js');
    const appProcess = childProcess.spawn(process.execPath, [appPath], {
      cwd: path.join(mainFixturesPath, 'apps', 'libuv-hang'),
      stdio: 'inherit'
    });
    const [code] = await once(appProcess, 'close');
    expect(code).to.equal(0);
  });

  describe('contexts', () => {
    describe('setTimeout called under Chromium event loop in browser process', () => {
      it('Can be scheduled in time', (done) => {
        setTimeout(done, 0);
      });

      it('Can be promisified', (done) => {
        util.promisify(setTimeout)(0).then(done);
      });
    });

    describe('setInterval called under Chromium event loop in browser process', () => {
      it('can be scheduled in time', (done) => {
        let interval: any = null;
        let clearing = false;
        const clear = () => {
          if (interval === null || clearing) return;

          // interval might trigger while clearing (remote is slow sometimes)
          clearing = true;
          clearInterval(interval);
          clearing = false;
          interval = null;
          done();
        };
        interval = setInterval(clear, 10);
      });
    });

    const suspendListeners = (emitter: EventEmitter, eventName: string, callback: (...args: any[]) => void) => {
      const listeners = emitter.listeners(eventName) as ((...args: any[]) => void)[];
      emitter.removeAllListeners(eventName);
      emitter.once(eventName, (...args) => {
        emitter.removeAllListeners(eventName);
        for (const listener of listeners) {
          emitter.on(eventName, listener);
        }

        callback(...args);
      });
    };
    describe('error thrown in main process node context', () => {
      it('gets emitted as a process uncaughtException event', async () => {
        fs.readFile(__filename, () => {
          throw new Error('hello');
        });
        const result = await new Promise(resolve => suspendListeners(process, 'uncaughtException', (error) => {
          resolve(error.message);
        }));
        expect(result).to.equal('hello');
      });
    });

    describe('promise rejection in main process node context', () => {
      it('gets emitted as a process unhandledRejection event', async () => {
        fs.readFile(__filename, () => {
          Promise.reject(new Error('hello'));
        });
        const result = await new Promise(resolve => suspendListeners(process, 'unhandledRejection', (error) => {
          resolve(error.message);
        }));
        expect(result).to.equal('hello');
      });

      it('does not log the warning more than once when the rejection is unhandled', async () => {
        const appPath = path.join(mainFixturesPath, 'api', 'unhandled-rejection.js');
        const appProcess = childProcess.spawn(process.execPath, [appPath]);

        let output = '';
        const out = (data: string) => {
          output += data;
          if (/UnhandledPromiseRejectionWarning/.test(data)) {
            appProcess.kill();
          }
        };
        appProcess.stdout!.on('data', out);
        appProcess.stderr!.on('data', out);

        await once(appProcess, 'exit');
        expect(/UnhandledPromiseRejectionWarning/.test(output)).to.equal(true);
        const matches = output.match(/Error: oops/gm);
        expect(matches).to.have.lengthOf(1);
      });

      it('does not log the warning more than once when the rejection is handled', async () => {
        const appPath = path.join(mainFixturesPath, 'api', 'unhandled-rejection-handled.js');
        const appProcess = childProcess.spawn(process.execPath, [appPath]);

        let output = '';
        const out = (data: string) => { output += data; };
        appProcess.stdout!.on('data', out);
        appProcess.stderr!.on('data', out);

        const [code] = await once(appProcess, 'exit');
        expect(code).to.equal(0);
        expect(/UnhandledPromiseRejectionWarning/.test(output)).to.equal(false);
        const matches = output.match(/Error: oops/gm);
        expect(matches).to.have.lengthOf(1);
      });
    });
  });

  describe('contexts in renderer', () => {
    useRemoteContext();

    describe('setTimeout in fs callback', () => {
      itremote('does not crash', async (filename: string) => {
        await new Promise(resolve => require('node:fs').readFile(filename, () => {
          setTimeout(resolve, 0);
        }));
      }, [__filename]);
    });

    describe('error thrown in renderer process node context', () => {
      itremote('gets emitted as a process uncaughtException event', async (filename: string) => {
        const error = new Error('boo!');
        require('node:fs').readFile(filename, () => {
          throw error;
        });
        await new Promise<void>((resolve, reject) => {
          process.once('uncaughtException', (thrown) => {
            try {
              expect(thrown).to.equal(error);
              resolve();
            } catch (e) {
              reject(e);
            }
          });
        });
      }, [__filename]);
    });

    describe('URL handling in the renderer process', () => {
      itremote('can successfully handle WHATWG URLs constructed by Blink', (fixtures: string) => {
        const url = new URL('file://' + require('node:path').resolve(fixtures, 'pages', 'base-page.html'));
        expect(() => {
          require('node:fs').createReadStream(url);
        }).to.not.throw();
      }, [fixtures]);
    });

    describe('setTimeout called under blink env in renderer process', () => {
      itremote('can be scheduled in time', async () => {
        await new Promise(resolve => setTimeout(resolve, 10));
      });

      itremote('works from the timers module', async () => {
        await new Promise(resolve => require('node:timers').setTimeout(resolve, 10));
      });
    });

    describe('setInterval called under blink env in renderer process', () => {
      itremote('can be scheduled in time', async () => {
        await new Promise<void>(resolve => {
          const id = setInterval(() => {
            clearInterval(id);
            resolve();
          }, 10);
        });
      });

      itremote('can be scheduled in time from timers module', async () => {
        const { setInterval, clearInterval } = require('node:timers');
        await new Promise<void>(resolve => {
          const id = setInterval(() => {
            clearInterval(id);
            resolve();
          }, 10);
        });
      });
    });
  });

  describe('message loop in renderer', () => {
    useRemoteContext();

    describe('process.nextTick', () => {
      itremote('emits the callback', () => new Promise(resolve => process.nextTick(resolve)));

      itremote('works in nested calls', () =>
        new Promise(resolve => {
          process.nextTick(() => {
            process.nextTick(() => process.nextTick(resolve));
          });
        }));
    });

    describe('setImmediate', () => {
      itremote('emits the callback', () => new Promise(resolve => setImmediate(resolve)));

      itremote('works in nested calls', () => new Promise(resolve => {
        setImmediate(() => {
          setImmediate(() => setImmediate(resolve));
        });
      }));
    });
  });

  ifdescribe(process.platform === 'darwin')('net.connect', () => {
    itremote('emit error when connect to a socket path without listeners', async (fixtures: string) => {
      const socketPath = require('node:path').join(require('node:os').tmpdir(), 'electron-test.sock');
      const script = require('node:path').join(fixtures, 'module', 'create_socket.js');
      const child = require('node:child_process').fork(script, [socketPath]);
      const code = await new Promise(resolve => child.once('exit', resolve));
      expect(code).to.equal(0);
      const client = require('node:net').connect(socketPath);
      const error = await new Promise<any>(resolve => client.once('error', resolve));
      expect(error.code).to.equal('ECONNREFUSED');
    }, [fixtures]);
  });

  describe('Buffer', () => {
    useRemoteContext();

    itremote('can be created from Blink external string', () => {
      const p = document.createElement('p');
      p.innerText = '闲云潭影日悠悠，物换星移几度秋';
      const b = Buffer.from(p.innerText);
      expect(b.toString()).to.equal('闲云潭影日悠悠，物换星移几度秋');
      expect(Buffer.byteLength(p.innerText)).to.equal(45);
    });

    itremote('correctly parses external one-byte UTF8 string', () => {
      const p = document.createElement('p');
      p.innerText = 'Jøhänñéß';
      const b = Buffer.from(p.innerText);
      expect(b.toString()).to.equal('Jøhänñéß');
      expect(Buffer.byteLength(p.innerText)).to.equal(13);
    });

    itremote('does not crash when creating large Buffers', () => {
      let buffer = Buffer.from(new Array(4096).join(' '));
      expect(buffer.length).to.equal(4095);
      buffer = Buffer.from(new Array(4097).join(' '));
      expect(buffer.length).to.equal(4096);
    });

    itremote('does not crash for crypto operations', () => {
      const crypto = require('node:crypto');
      const data = 'lG9E+/g4JmRmedDAnihtBD4Dfaha/GFOjd+xUOQI05UtfVX3DjUXvrS98p7kZQwY3LNhdiFo7MY5rGft8yBuDhKuNNag9vRx/44IuClDhdQ=';
      const key = 'q90K9yBqhWZnAMCMTOJfPQ==';
      const cipherText = '{"error_code":114,"error_message":"Tham số không hợp lệ","data":null}';
      for (let i = 0; i < 10000; ++i) {
        const iv = Buffer.from('0'.repeat(32), 'hex');
        const input = Buffer.from(data, 'base64');
        const decipher = crypto.createDecipheriv('aes-128-cbc', Buffer.from(key, 'base64'), iv);
        const result = Buffer.concat([decipher.update(input), decipher.final()]).toString('utf8');
        expect(cipherText).to.equal(result);
      }
    });

    itremote('does not crash when using crypto.diffieHellman() constructors', () => {
      const crypto = require('node:crypto');

      crypto.createDiffieHellman('abc');
      crypto.createDiffieHellman('abc', 2);

      // Needed to test specific DiffieHellman ctors.

      crypto.createDiffieHellman('abc', Buffer.from([2]));
      crypto.createDiffieHellman('abc', '123');
    });

    itremote('does not crash when calling crypto.createPrivateKey() with an unsupported algorithm', () => {
      const crypto = require('node:crypto');

      const ed448 = {
        crv: 'Ed448',
        x: 'KYWcaDwgH77xdAwcbzOgvCVcGMy9I6prRQBhQTTdKXUcr-VquTz7Fd5adJO0wT2VHysF3bk3kBoA',
        d: 'UhC3-vN5vp_g9PnTknXZgfXUez7Xvw-OfuJ0pYkuwzpYkcTvacqoFkV_O05WMHpyXkzH9q2wzx5n',
        kty: 'OKP'
      };

      expect(() => {
        crypto.createPrivateKey({ key: ed448, format: 'jwk' });
      }).to.throw(/Invalid JWK data/);
    });
  });

  describe('process.stdout', () => {
    useRemoteContext();

    itremote('does not throw an exception when accessed', () => {
      expect(() => process.stdout).to.not.throw();
    });

    itremote('does not throw an exception when calling write()', () => {
      expect(() => {
        process.stdout.write('test');
      }).to.not.throw();
    });

    // TODO: figure out why process.stdout.isTTY is true on Darwin but not Linux/Win.
    ifdescribe(process.platform !== 'darwin')('isTTY', () => {
      itremote('should be undefined in the renderer process', function () {
        expect(process.stdout.isTTY).to.be.undefined();
      });
    });
  });

  describe('process.stdin', () => {
    useRemoteContext();

    itremote('does not throw an exception when accessed', () => {
      expect(() => process.stdin).to.not.throw();
    });

    itremote('returns null when read from', () => {
      expect(process.stdin.read()).to.be.null();
    });
  });

  describe('process.version', () => {
    itremote('should not have -pre', () => {
      expect(process.version.endsWith('-pre')).to.be.false();
    });
  });

  describe('vm.runInNewContext', () => {
    itremote('should not crash', () => {
      require('node:vm').runInNewContext('');
    });
  });

  describe('crypto', () => {
    useRemoteContext();
    itremote('should list the ripemd160 hash in getHashes', () => {
      expect(require('node:crypto').getHashes()).to.include('ripemd160');
    });

    itremote('should be able to create a ripemd160 hash and use it', () => {
      const hash = require('node:crypto').createHash('ripemd160');
      hash.update('electron-ripemd160');
      expect(hash.digest('hex')).to.equal('fa7fec13c624009ab126ebb99eda6525583395fe');
    });

    itremote('should list aes-{128,256}-cfb in getCiphers', () => {
      expect(require('node:crypto').getCiphers()).to.include.members(['aes-128-cfb', 'aes-256-cfb']);
    });

    itremote('should be able to create an aes-128-cfb cipher', () => {
      require('node:crypto').createCipheriv('aes-128-cfb', '0123456789abcdef', '0123456789abcdef');
    });

    itremote('should be able to create an aes-256-cfb cipher', () => {
      require('node:crypto').createCipheriv('aes-256-cfb', '0123456789abcdef0123456789abcdef', '0123456789abcdef');
    });

    itremote('should be able to create a bf-{cbc,cfb,ecb} ciphers', () => {
      require('node:crypto').createCipheriv('bf-cbc', Buffer.from('0123456789abcdef'), Buffer.from('01234567'));
      require('node:crypto').createCipheriv('bf-cfb', Buffer.from('0123456789abcdef'), Buffer.from('01234567'));
      require('node:crypto').createCipheriv('bf-ecb', Buffer.from('0123456789abcdef'), Buffer.from('01234567'));
    });

    itremote('should list des-ede-cbc in getCiphers', () => {
      expect(require('node:crypto').getCiphers()).to.include('des-ede-cbc');
    });

    itremote('should be able to create an des-ede-cbc cipher', () => {
      const key = Buffer.from('0123456789abcdeff1e0d3c2b5a49786', 'hex');
      const iv = Buffer.from('fedcba9876543210', 'hex');
      require('node:crypto').createCipheriv('des-ede-cbc', key, iv);
    });

    itremote('should not crash when getting an ECDH key', () => {
      const ecdh = require('node:crypto').createECDH('prime256v1');
      expect(ecdh.generateKeys()).to.be.an.instanceof(Buffer);
      expect(ecdh.getPrivateKey()).to.be.an.instanceof(Buffer);
    });

    itremote('should not crash when generating DH keys or fetching DH fields', () => {
      const dh = require('node:crypto').createDiffieHellman('modp15');
      expect(dh.generateKeys()).to.be.an.instanceof(Buffer);
      expect(dh.getPublicKey()).to.be.an.instanceof(Buffer);
      expect(dh.getPrivateKey()).to.be.an.instanceof(Buffer);
      expect(dh.getPrime()).to.be.an.instanceof(Buffer);
      expect(dh.getGenerator()).to.be.an.instanceof(Buffer);
    });

    itremote('should not crash when creating an ECDH cipher', () => {
      const crypto = require('node:crypto');
      const dh = crypto.createECDH('prime256v1');
      dh.generateKeys();
      dh.setPrivateKey(dh.getPrivateKey());
    });
  });

  itremote('includes the electron version in process.versions', () => {
    expect(process.versions)
      .to.have.own.property('electron')
      .that.is.a('string')
      .and.matches(/^\d+\.\d+\.\d+(\S*)?$/);
  });

  itremote('includes the chrome version in process.versions', () => {
    expect(process.versions)
      .to.have.own.property('chrome')
      .that.is.a('string')
      .and.matches(/^\d+\.\d+\.\d+\.\d+$/);
  });

  describe('NODE_OPTIONS', () => {
    let child: childProcess.ChildProcessWithoutNullStreams;
    let exitPromise: Promise<any[]>;

    it('Fails for options disallowed by Node.js itself', (done) => {
      after(async () => {
        const [code, signal] = await exitPromise;
        expect(signal).to.equal(null);

        // Exit code 9 indicates cli flag parsing failure
        expect(code).to.equal(9);
        child.kill();
      });

      const env = { ...process.env, NODE_OPTIONS: '--v8-options' };
      child = childProcess.spawn(process.execPath, { env });
      exitPromise = once(child, 'exit');

      let output = '';
      let success = false;
      const cleanup = () => {
        child.stderr.removeListener('data', listener);
        child.stdout.removeListener('data', listener);
      };

      const listener = (data: Buffer) => {
        output += data;
        if (/electron: --v8-options is not allowed in NODE_OPTIONS/m.test(output)) {
          success = true;
          cleanup();
          done();
        }
      };

      child.stderr.on('data', listener);
      child.stdout.on('data', listener);
      child.on('exit', () => {
        if (!success) {
          cleanup();
          done(new Error(`Unexpected output: ${output.toString()}`));
        }
      });
    });

    it('Disallows crypto-related options', (done) => {
      after(() => {
        child.kill();
      });

      const appPath = path.join(fixtures, 'module', 'noop.js');
      const env = { ...process.env, NODE_OPTIONS: '--use-openssl-ca' };
      child = childProcess.spawn(process.execPath, ['--enable-logging', appPath], { env });

      let output = '';
      const cleanup = () => {
        child.stderr.removeListener('data', listener);
        child.stdout.removeListener('data', listener);
      };

      const listener = (data: Buffer) => {
        output += data;
        if (/The NODE_OPTION --use-openssl-ca is not supported in Electron/m.test(output)) {
          cleanup();
          done();
        }
      };

      child.stderr.on('data', listener);
      child.stdout.on('data', listener);
    });

    it('does allow --require in non-packaged apps', async () => {
      const appPath = path.join(fixtures, 'module', 'noop.js');
      const env = {
        ...process.env,
        NODE_OPTIONS: `--require=${path.join(fixtures, 'module', 'fail.js')}`
      };
      // App should exit with code 1.
      const child = childProcess.spawn(process.execPath, [appPath], { env });
      const [code] = await once(child, 'exit');
      expect(code).to.equal(1);
    });

    it('does not allow --require in packaged apps', async () => {
      const appPath = path.join(fixtures, 'module', 'noop.js');
      const env = {
        ...process.env,
        ELECTRON_FORCE_IS_PACKAGED: 'true',
        NODE_OPTIONS: `--require=${path.join(fixtures, 'module', 'fail.js')}`
      };
      // App should exit with code 0.
      const child = childProcess.spawn(process.execPath, [appPath], { env });
      const [code] = await once(child, 'exit');
      expect(code).to.equal(0);
    });
  });

  ifdescribe(shouldRunCodesignTests)('NODE_OPTIONS in signed app', function () {
    let identity = '';

    beforeEach(function () {
      const result = getCodesignIdentity();
      if (result === null) {
        this.skip();
      } else {
        identity = result;
      }
    });

    const script = path.join(fixtures, 'api', 'fork-with-node-options.js');
    const nodeOptionsWarning = 'Node.js environment variables are disabled because this process is invoked by other apps';

    it('is disabled when invoked by other apps in ELECTRON_RUN_AS_NODE mode', async () => {
      await withTempDirectory(async (dir) => {
        const appPath = await copyMacOSFixtureApp(dir);
        await signApp(appPath, identity);
        // Invoke Electron by using the system node binary as middle layer, so
        // the check of NODE_OPTIONS will think the process is started by other
        // apps.
        const { code, out } = await spawn('node', [script, path.join(appPath, 'Contents/MacOS/Electron')]);
        expect(code).to.equal(0);
        expect(out).to.include(nodeOptionsWarning);
      });
    });

    it('is disabled when invoked by alien binary in app bundle in ELECTRON_RUN_AS_NODE mode', async function () {
      await withTempDirectory(async (dir) => {
        const appPath = await copyMacOSFixtureApp(dir);
        await signApp(appPath, identity);
        // Find system node and copy it to app bundle.
        const nodePath = process.env.PATH?.split(path.delimiter).find(dir => fs.existsSync(path.join(dir, 'node')));
        if (!nodePath) {
          this.skip();
          return;
        }
        const alienBinary = path.join(appPath, 'Contents/MacOS/node');
        await fs.promises.cp(path.join(nodePath, 'node'), alienBinary, { recursive: true });
        // Try to execute electron app from the alien node in app bundle.
        const { code, out } = await spawn(alienBinary, [script, path.join(appPath, 'Contents/MacOS/Electron')]);
        expect(code).to.equal(0);
        expect(out).to.include(nodeOptionsWarning);
      });
    });

    it('is respected when invoked from self', async () => {
      await withTempDirectory(async (dir) => {
        const appPath = await copyMacOSFixtureApp(dir, null);
        await signApp(appPath, identity);
        const appExePath = path.join(appPath, 'Contents/MacOS/Electron');
        const { code, out } = await spawn(appExePath, [script, appExePath]);
        expect(code).to.equal(1);
        expect(out).to.not.include(nodeOptionsWarning);
        expect(out).to.include('NODE_OPTIONS passed to child');
      });
    });
  });

  describe('Node.js cli flags', () => {
    let child: childProcess.ChildProcessWithoutNullStreams;
    let exitPromise: Promise<any[]>;

    it('Prohibits crypto-related flags in ELECTRON_RUN_AS_NODE mode', (done) => {
      after(async () => {
        const [code, signal] = await exitPromise;
        expect(signal).to.equal(null);
        expect(code).to.equal(9);
        child.kill();
      });

      child = childProcess.spawn(process.execPath, ['--force-fips'], {
        env: { ELECTRON_RUN_AS_NODE: 'true' }
      });
      exitPromise = once(child, 'exit');

      let output = '';
      const cleanup = () => {
        child.stderr.removeListener('data', listener);
        child.stdout.removeListener('data', listener);
      };

      const listener = (data: Buffer) => {
        output += data;
        if (/.*The Node.js cli flag --force-fips is not supported in Electron/m.test(output)) {
          cleanup();
          done();
        }
      };

      child.stderr.on('data', listener);
      child.stdout.on('data', listener);
    });
  });

  describe('process.stdout', () => {
    it('is a real Node stream', () => {
      expect((process.stdout as any)._type).to.not.be.undefined();
    });
  });

  describe('fs.readFile', () => {
    it('can accept a FileHandle as the Path argument', async () => {
      const filePathForHandle = path.resolve(mainFixturesPath, 'dogs-running.txt');
      const fileHandle = await fs.promises.open(filePathForHandle, 'r');

      const file = await fs.promises.readFile(fileHandle, { encoding: 'utf8' });
      expect(file).to.not.be.empty();
      await fileHandle.close();
    });
  });

  describe('inspector', () => {
    let child: childProcess.ChildProcessWithoutNullStreams;
    let exitPromise: Promise<any[]> | null;

    afterEach(async () => {
      if (child && exitPromise) {
        const [code, signal] = await exitPromise;
        expect(signal).to.equal(null);
        expect(code).to.equal(0);
      } else if (child) {
        child.kill();
      }
      child = null as any;
      exitPromise = null as any;
    });

    it('Supports starting the v8 inspector with --inspect/--inspect-brk', (done) => {
      child = childProcess.spawn(process.execPath, ['--inspect-brk', path.join(fixtures, 'module', 'run-as-node.js')], {
        env: { ELECTRON_RUN_AS_NODE: 'true' }
      });

      let output = '';
      const cleanup = () => {
        child.stderr.removeListener('data', listener);
        child.stdout.removeListener('data', listener);
      };

      const listener = (data: Buffer) => {
        output += data;
        if (/Debugger listening on ws:/m.test(output)) {
          cleanup();
          done();
        }
      };

      child.stderr.on('data', listener);
      child.stdout.on('data', listener);
    });

    it('Supports starting the v8 inspector with --inspect and a provided port', async () => {
      child = childProcess.spawn(process.execPath, ['--inspect=17364', path.join(fixtures, 'module', 'run-as-node.js')], {
        env: { ELECTRON_RUN_AS_NODE: 'true' }
      });
      exitPromise = once(child, 'exit');

      let output = '';
      const listener = (data: Buffer) => { output += data; };
      const cleanup = () => {
        child.stderr.removeListener('data', listener);
        child.stdout.removeListener('data', listener);
      };

      child.stderr.on('data', listener);
      child.stdout.on('data', listener);
      await once(child, 'exit');
      cleanup();
      if (/^Debugger listening on ws:/m.test(output)) {
        expect(output.trim()).to.contain(':17364', 'should be listening on port 17364');
      } else {
        throw new Error(`Unexpected output: ${output.toString()}`);
      }
    });

    it('Does not start the v8 inspector when --inspect is after a -- argument', async () => {
      child = childProcess.spawn(process.execPath, [path.join(fixtures, 'module', 'noop.js'), '--', '--inspect']);
      exitPromise = once(child, 'exit');

      let output = '';
      const listener = (data: Buffer) => { output += data; };
      child.stderr.on('data', listener);
      child.stdout.on('data', listener);
      await once(child, 'exit');
      if (output.trim().startsWith('Debugger listening on ws://')) {
        throw new Error('Inspector was started when it should not have been');
      }
    });

    // IPC Electron child process not supported on Windows.
    ifit(process.platform !== 'win32')('does not crash when quitting with the inspector connected', function (done) {
      child = childProcess.spawn(process.execPath, [path.join(fixtures, 'module', 'delay-exit'), '--inspect=0'], {
        stdio: ['ipc']
      }) as childProcess.ChildProcessWithoutNullStreams;
      exitPromise = once(child, 'exit');

      const cleanup = () => {
        child.stderr.removeListener('data', listener);
        child.stdout.removeListener('data', listener);
      };

      let output = '';
      const success = false;
      function listener (data: Buffer) {
        output += data;
        console.log(data.toString()); // NOTE: temporary debug logging to try to catch flake.
        const match = /^Debugger listening on (ws:\/\/.+:\d+\/.+)\n/m.exec(output.trim());
        if (match) {
          cleanup();
          // NOTE: temporary debug logging to try to catch flake.
          child.stderr.on('data', (m) => console.log(m.toString()));
          child.stdout.on('data', (m) => console.log(m.toString()));
          const w = (webContents as typeof ElectronInternal.WebContents).create();
          w.loadURL('about:blank')
            .then(() => w.executeJavaScript(`new Promise(resolve => {
              const connection = new WebSocket(${JSON.stringify(match[1])})
              connection.onopen = () => {
                connection.onclose = () => resolve()
                connection.close()
              }
            })`))
            .then(() => {
              w.destroy();
              child.send('plz-quit');
              done();
            });
        }
      }

      child.stderr.on('data', listener);
      child.stdout.on('data', listener);
      child.on('exit', () => {
        if (!success) cleanup();
      });
    });

    it('Supports js binding', async () => {
      child = childProcess.spawn(process.execPath, ['--inspect', path.join(fixtures, 'module', 'inspector-binding.js')], {
        env: { ELECTRON_RUN_AS_NODE: 'true' },
        stdio: ['ipc']
      }) as childProcess.ChildProcessWithoutNullStreams;
      exitPromise = once(child, 'exit');

      const [{ cmd, debuggerEnabled, success }] = await once(child, 'message');
      expect(cmd).to.equal('assert');
      expect(debuggerEnabled).to.be.true();
      expect(success).to.be.true();
    });
  });

  it('Can find a module using a package.json main field', () => {
    const result = childProcess.spawnSync(process.execPath, [path.resolve(fixtures, 'api', 'electron-main-module', 'app.asar')], { stdio: 'inherit' });
    expect(result.status).to.equal(0);
  });

  it('handles Promise timeouts correctly', async () => {
    const scriptPath = path.join(fixtures, 'module', 'node-promise-timer.js');
    const child = childProcess.spawn(process.execPath, [scriptPath], {
      env: { ELECTRON_RUN_AS_NODE: 'true' }
    });
    const [code, signal] = await once(child, 'exit');
    expect(code).to.equal(0);
    expect(signal).to.equal(null);
    child.kill();
  });

  it('performs microtask checkpoint correctly', (done) => {
    let timer : NodeJS.Timeout;
    const listener = () => {
      done(new Error('catch block is delayed to next tick'));
    };

    const f3 = async () => {
      return new Promise((resolve, reject) => {
        timer = setTimeout(listener);
        reject(new Error('oops'));
      });
    };

    setTimeout(() => {
      f3().catch(() => {
        clearTimeout(timer);
        done();
      });
    });
  });
});
