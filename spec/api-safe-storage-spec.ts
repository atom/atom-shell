import * as cp from 'node:child_process';
import * as path from 'node:path';
import { safeStorage } from 'electron/main';
import { expect } from 'chai';
import { ifdescribe } from './lib/spec-helpers';
import * as fs from 'node:fs';
import { once } from 'node:events';

describe('safeStorage module', () => {
  it('safeStorage before and after app is ready', async () => {
    const appPath = path.join(__dirname, 'fixtures', 'crash-cases', 'safe-storage');
    const appProcess = cp.spawn(process.execPath, [appPath]);

    let output = '';
    appProcess.stdout.on('data', data => { output += data; });
    appProcess.stderr.on('data', data => { output += data; });

    const code = (await once(appProcess, 'exit'))[0] ?? 1;

    if (code !== 0 && output) {
      console.log(output);
    }
    expect(code).to.equal(0);
  });
});

describe('safeStorage module', () => {
  before(() => {
    if (process.platform === 'linux') {
      safeStorage.setUsePlainTextEncryption(true);
    }
  });

  after(async () => {
    const pathToEncryptedString = path.resolve(__dirname, 'fixtures', 'api', 'safe-storage', 'encrypted.txt');
    if (fs.existsSync(pathToEncryptedString)) {
      await fs.promises.rm(pathToEncryptedString, { force: true, recursive: true });
    }
  });

  describe('SafeStorage.isEncryptionAvailable()', () => {
    it('should return true when encryption key is available (macOS, Windows)', () => {
      expect(safeStorage.isEncryptionAvailable()).to.equal(true);
    });
  });

  ifdescribe(process.platform === 'linux')('SafeStorage.getSelectedStorageBackend()', () => {
    it('should return a valid backend', () => {
      expect(safeStorage.getSelectedStorageBackend()).to.equal('basic_text');
    });
  });

  describe('SafeStorage.encryptString()', () => {
    it('valid input should correctly encrypt string', () => {
      const plaintext = 'plaintext';
      const encrypted = safeStorage.encryptString(plaintext);
      expect(Buffer.isBuffer(encrypted)).to.equal(true);
    });

    it('UTF-16 characters can be encrypted', () => {
      const plaintext = '€ - utf symbol';
      const encrypted = safeStorage.encryptString(plaintext);
      expect(Buffer.isBuffer(encrypted)).to.equal(true);
    });
  });

  describe('SafeStorage.decryptString()', () => {
    it('valid input should correctly decrypt string', () => {
      const encrypted = safeStorage.encryptString('plaintext');
      expect(safeStorage.decryptString(encrypted)).to.equal('plaintext');
    });

    it('UTF-16 characters can be decrypted', () => {
      const plaintext = '€ - utf symbol';
      const encrypted = safeStorage.encryptString(plaintext);
      expect(safeStorage.decryptString(encrypted)).to.equal(plaintext);
    });

    it('unencrypted input should throw', () => {
      const plaintextBuffer = Buffer.from('I am unencoded!', 'utf-8');
      expect(() => {
        safeStorage.decryptString(plaintextBuffer);
      }).to.throw(Error);
    });

    it('non-buffer input should throw', () => {
      const notABuffer = {} as any;
      expect(() => {
        safeStorage.decryptString(notABuffer);
      }).to.throw(Error);
    });
  });

  describe('safeStorage persists encryption key across app relaunch', () => {
    it('can decrypt after closing and reopening app', async () => {
      const fixturesPath = path.resolve(__dirname, 'fixtures');

      const encryptAppPath = path.join(fixturesPath, 'api', 'safe-storage', 'encrypt-app');
      const encryptAppProcess = cp.spawn(process.execPath, [encryptAppPath]);
      let stdout: string = '';
      encryptAppProcess.stderr.on('data', data => { stdout += data; });
      encryptAppProcess.stderr.on('data', data => { stdout += data; });

      try {
        await once(encryptAppProcess, 'exit');

        const appPath = path.join(fixturesPath, 'api', 'safe-storage', 'decrypt-app');
        const relaunchedAppProcess = cp.spawn(process.execPath, [appPath]);

        let output = '';
        relaunchedAppProcess.stdout.on('data', data => { output += data; });
        relaunchedAppProcess.stderr.on('data', data => { output += data; });

        const [code] = await once(relaunchedAppProcess, 'exit');

        if (!output.includes('plaintext')) {
          console.log(code, output);
        }
        expect(output).to.include('plaintext');
      } catch (e) {
        console.log(stdout);
        throw e;
      }
    });
  });
});
