import { expect } from 'chai';
import * as cp from 'node:child_process';
import * as http from 'node:http';
import * as express from 'express';
import * as fs from 'node:fs';
import * as path from 'node:path';
import * as psList from 'ps-list';
import { AddressInfo } from 'node:net';
import { ifdescribe, ifit } from './lib/spec-helpers';
import { copyMacOSFixtureApp, getCodesignIdentity, shouldRunCodesignTests, signApp, spawn } from './lib/codesign-helpers';
import * as uuid from 'uuid';
import { autoUpdater, systemPreferences } from 'electron';
import { withTempDirectory } from './lib/fs-helpers';

// We can only test the auto updater on darwin non-component builds
ifdescribe(shouldRunCodesignTests)('autoUpdater behavior', function () {
  this.timeout(120000);

  let identity = '';

  beforeEach(function () {
    const result = getCodesignIdentity();
    if (result === null) {
      this.skip();
    } else {
      identity = result;
    }
  });

  it('should have a valid code signing identity', () => {
    expect(identity).to.be.a('string').with.lengthOf.at.least(1);
  });

  const launchApp = (appPath: string, args: string[] = []) => {
    return spawn(path.resolve(appPath, 'Contents/MacOS/Electron'), args);
  };

  const spawnAppWithHandle = (appPath: string, args: string[] = []) => {
    return cp.spawn(path.resolve(appPath, 'Contents/MacOS/Electron'), args);
  };

  const getRunningShipIts = async (appPath: string) => {
    const processes = await psList();
    const activeShipIts = processes.filter(p => p.cmd?.includes('Squirrel.framework/Resources/ShipIt com.github.Electron.ShipIt') && p.cmd!.startsWith(appPath));
    return activeShipIts;
  };

  const logOnError = (what: any, fn: () => void) => {
    try {
      fn();
    } catch (err) {
      console.error(what);
      throw err;
    }
  };

  const cachedZips: Record<string, string> = {};

  type Mutation = {
    mutate: (appPath: string) => Promise<void>,
    mutationKey: string,
  };

  const getOrCreateUpdateZipPath = async (version: string, fixture: string, mutateAppPreSign?: Mutation, mutateAppPostSign?: Mutation) => {
    const key = `${version}-${fixture}-${mutateAppPreSign?.mutationKey || 'no-pre-mutation'}-${mutateAppPostSign?.mutationKey || 'no-post-mutation'}`;
    if (!cachedZips[key]) {
      let updateZipPath: string;
      await withTempDirectory(async (dir) => {
        const secondAppPath = await copyMacOSFixtureApp(dir, fixture);
        const appPJPath = path.resolve(secondAppPath, 'Contents', 'Resources', 'app', 'package.json');
        await fs.promises.writeFile(
          appPJPath,
          (await fs.promises.readFile(appPJPath, 'utf8')).replace('1.0.0', version)
        );
        const infoPath = path.resolve(secondAppPath, 'Contents', 'Info.plist');
        await fs.promises.writeFile(
          infoPath,
          (await fs.promises.readFile(infoPath, 'utf8')).replace(/(<key>CFBundleShortVersionString<\/key>\s+<string>)[^<]+/g, `$1${version}`)
        );
        await mutateAppPreSign?.mutate(secondAppPath);
        await signApp(secondAppPath, identity);
        await mutateAppPostSign?.mutate(secondAppPath);
        updateZipPath = path.resolve(dir, 'update.zip');
        await spawn('zip', ['-0', '-r', '--symlinks', updateZipPath, './'], {
          cwd: dir
        });
      }, false);
      cachedZips[key] = updateZipPath!;
    }
    return cachedZips[key];
  };

  after(() => {
    for (const version of Object.keys(cachedZips)) {
      cp.spawnSync('rm', ['-r', path.dirname(cachedZips[version])]);
    }
  });

  // On arm64 builds the built app is self-signed by default so the setFeedURL call always works
  ifit(process.arch !== 'arm64')('should fail to set the feed URL when the app is not signed', async () => {
    await withTempDirectory(async (dir) => {
      const appPath = await copyMacOSFixtureApp(dir);
      const launchResult = await launchApp(appPath, ['http://myupdate']);
      console.log(launchResult);
      expect(launchResult.code).to.equal(1);
      expect(launchResult.out).to.include('Could not get code signature for running application');
    });
  });

  it('should cleanly set the feed URL when the app is signed', async () => {
    await withTempDirectory(async (dir) => {
      const appPath = await copyMacOSFixtureApp(dir);
      await signApp(appPath, identity);
      const launchResult = await launchApp(appPath, ['http://myupdate']);
      expect(launchResult.code).to.equal(0);
      expect(launchResult.out).to.include('Feed URL Set: http://myupdate');
    });
  });

  describe('with update server', () => {
    let port = 0;
    let server: express.Application = null as any;
    let httpServer: http.Server = null as any;
    let requests: express.Request[] = [];

    beforeEach((done) => {
      requests = [];
      server = express();
      server.use((req, res, next) => {
        requests.push(req);
        next();
      });
      httpServer = server.listen(0, '127.0.0.1', () => {
        port = (httpServer.address() as AddressInfo).port;
        done();
      });
    });

    afterEach(async () => {
      if (httpServer) {
        await new Promise<void>(resolve => {
          httpServer.close(() => {
            httpServer = null as any;
            server = null as any;
            resolve();
          });
        });
      }
    });

    it('should hit the update endpoint when checkForUpdates is called', async () => {
      await withTempDirectory(async (dir) => {
        const appPath = await copyMacOSFixtureApp(dir, 'check');
        await signApp(appPath, identity);
        server.get('/update-check', (req, res) => {
          res.status(204).send();
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult.code).to.equal(0);
          expect(requests).to.have.lengthOf(1);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[0].header('user-agent')).to.include('Electron/');
        });
      });
    });

    it('should hit the update endpoint with customer headers when checkForUpdates is called', async () => {
      await withTempDirectory(async (dir) => {
        const appPath = await copyMacOSFixtureApp(dir, 'check-with-headers');
        await signApp(appPath, identity);
        server.get('/update-check', (req, res) => {
          res.status(204).send();
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult.code).to.equal(0);
          expect(requests).to.have.lengthOf(1);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[0].header('x-test')).to.equal('this-is-a-test');
        });
      });
    });

    it('should hit the download endpoint when an update is available and error if the file is bad', async () => {
      await withTempDirectory(async (dir) => {
        const appPath = await copyMacOSFixtureApp(dir, 'update');
        await signApp(appPath, identity);
        server.get('/update-file', (req, res) => {
          res.status(500).send('This is not a file');
        });
        server.get('/update-check', (req, res) => {
          res.json({
            url: `http://localhost:${port}/update-file`,
            name: 'My Release Name',
            notes: 'Theses are some release notes innit',
            pub_date: (new Date()).toString()
          });
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 1);
          expect(launchResult.out).to.include('Update download failed. The server sent an invalid response.');
          expect(requests).to.have.lengthOf(2);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[1]).to.have.property('url', '/update-file');
          expect(requests[0].header('user-agent')).to.include('Electron/');
          expect(requests[1].header('user-agent')).to.include('Electron/');
        });
      });
    });

    const withUpdatableApp = async (opts: {
      nextVersion: string;
      startFixture: string;
      endFixture: string;
      mutateAppPreSign?: Mutation;
      mutateAppPostSign?: Mutation;
    }, fn: (appPath: string, zipPath: string) => Promise<void>) => {
      await withTempDirectory(async (dir) => {
        const appPath = await copyMacOSFixtureApp(dir, opts.startFixture);
        await opts.mutateAppPreSign?.mutate(appPath);
        const infoPath = path.resolve(appPath, 'Contents', 'Info.plist');
        await fs.promises.writeFile(
          infoPath,
          (await fs.promises.readFile(infoPath, 'utf8')).replace(/(<key>CFBundleShortVersionString<\/key>\s+<string>)[^<]+/g, '$11.0.0')
        );
        await signApp(appPath, identity);

        const updateZipPath = await getOrCreateUpdateZipPath(opts.nextVersion, opts.endFixture, opts.mutateAppPreSign, opts.mutateAppPostSign);

        await fn(appPath, updateZipPath);
      });
    };

    it('should hit the download endpoint when an update is available and update successfully when the zip is provided', async () => {
      await withUpdatableApp({
        nextVersion: '2.0.0',
        startFixture: 'update',
        endFixture: 'update'
      }, async (appPath, updateZipPath) => {
        server.get('/update-file', (req, res) => {
          res.download(updateZipPath);
        });
        server.get('/update-check', (req, res) => {
          res.json({
            url: `http://localhost:${port}/update-file`,
            name: 'My Release Name',
            notes: 'Theses are some release notes innit',
            pub_date: (new Date()).toString()
          });
        });
        const relaunchPromise = new Promise<void>((resolve) => {
          server.get('/update-check/updated/:version', (req, res) => {
            res.status(204).send();
            resolve();
          });
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 0);
          expect(launchResult.out).to.include('Update Downloaded');
          expect(requests).to.have.lengthOf(2);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[1]).to.have.property('url', '/update-file');
          expect(requests[0].header('user-agent')).to.include('Electron/');
          expect(requests[1].header('user-agent')).to.include('Electron/');
        });

        await relaunchPromise;
        expect(requests).to.have.lengthOf(3);
        expect(requests[2].url).to.equal('/update-check/updated/2.0.0');
        expect(requests[2].header('user-agent')).to.include('Electron/');
      });
    });

    it('should hit the download endpoint when an update is available and update successfully when the zip is provided even after a different update was staged', async () => {
      await withUpdatableApp({
        nextVersion: '2.0.0',
        startFixture: 'update-stack',
        endFixture: 'update-stack'
      }, async (appPath, updateZipPath2) => {
        await withUpdatableApp({
          nextVersion: '3.0.0',
          startFixture: 'update-stack',
          endFixture: 'update-stack'
        }, async (_, updateZipPath3) => {
          let updateCount = 0;
          server.get('/update-file', (req, res) => {
            res.download(updateCount > 1 ? updateZipPath3 : updateZipPath2);
          });
          server.get('/update-check', (req, res) => {
            updateCount++;
            res.json({
              url: `http://localhost:${port}/update-file`,
              name: 'My Release Name',
              notes: 'Theses are some release notes innit',
              pub_date: (new Date()).toString()
            });
          });
          const relaunchPromise = new Promise<void>((resolve) => {
            server.get('/update-check/updated/:version', (req, res) => {
              res.status(204).send();
              resolve();
            });
          });
          const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
          logOnError(launchResult, () => {
            expect(launchResult).to.have.property('code', 0);
            expect(launchResult.out).to.include('Update Downloaded');
            expect(requests).to.have.lengthOf(4);
            expect(requests[0]).to.have.property('url', '/update-check');
            expect(requests[1]).to.have.property('url', '/update-file');
            expect(requests[0].header('user-agent')).to.include('Electron/');
            expect(requests[1].header('user-agent')).to.include('Electron/');
            expect(requests[2]).to.have.property('url', '/update-check');
            expect(requests[3]).to.have.property('url', '/update-file');
            expect(requests[2].header('user-agent')).to.include('Electron/');
            expect(requests[3].header('user-agent')).to.include('Electron/');
          });

          await relaunchPromise;
          expect(requests).to.have.lengthOf(5);
          expect(requests[4].url).to.equal('/update-check/updated/3.0.0');
          expect(requests[4].header('user-agent')).to.include('Electron/');
        });
      });
    });

    it('should update to lower version numbers', async () => {
      await withUpdatableApp({
        nextVersion: '0.0.1',
        startFixture: 'update',
        endFixture: 'update'
      }, async (appPath, updateZipPath) => {
        server.get('/update-file', (req, res) => {
          res.download(updateZipPath);
        });
        server.get('/update-check', (req, res) => {
          res.json({
            url: `http://localhost:${port}/update-file`,
            name: 'My Release Name',
            notes: 'Theses are some release notes innit',
            pub_date: (new Date()).toString()
          });
        });
        const relaunchPromise = new Promise<void>((resolve) => {
          server.get('/update-check/updated/:version', (req, res) => {
            res.status(204).send();
            resolve();
          });
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 0);
          expect(launchResult.out).to.include('Update Downloaded');
          expect(requests).to.have.lengthOf(2);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[1]).to.have.property('url', '/update-file');
          expect(requests[0].header('user-agent')).to.include('Electron/');
          expect(requests[1].header('user-agent')).to.include('Electron/');
        });

        await relaunchPromise;
        expect(requests).to.have.lengthOf(3);
        expect(requests[2].url).to.equal('/update-check/updated/0.0.1');
        expect(requests[2].header('user-agent')).to.include('Electron/');
      });
    });

    describe('with ElectronSquirrelPreventDowngrades enabled', () => {
      it('should not update to lower version numbers', async () => {
        await withUpdatableApp({
          nextVersion: '0.0.1',
          startFixture: 'update',
          endFixture: 'update',
          mutateAppPreSign: {
            mutationKey: 'prevent-downgrades',
            mutate: async (appPath) => {
              const infoPath = path.resolve(appPath, 'Contents', 'Info.plist');
              await fs.promises.writeFile(
                infoPath,
                (await fs.promises.readFile(infoPath, 'utf8')).replace('<key>NSSupportsAutomaticGraphicsSwitching</key>', '<key>ElectronSquirrelPreventDowngrades</key><true/><key>NSSupportsAutomaticGraphicsSwitching</key>')
              );
            }
          }
        }, async (appPath, updateZipPath) => {
          server.get('/update-file', (req, res) => {
            res.download(updateZipPath);
          });
          server.get('/update-check', (req, res) => {
            res.json({
              url: `http://localhost:${port}/update-file`,
              name: 'My Release Name',
              notes: 'Theses are some release notes innit',
              pub_date: (new Date()).toString()
            });
          });
          const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
          logOnError(launchResult, () => {
            expect(launchResult).to.have.property('code', 1);
            expect(launchResult.out).to.include('Cannot update to a bundle with a lower version number');
            expect(requests).to.have.lengthOf(2);
            expect(requests[0]).to.have.property('url', '/update-check');
            expect(requests[1]).to.have.property('url', '/update-file');
            expect(requests[0].header('user-agent')).to.include('Electron/');
            expect(requests[1].header('user-agent')).to.include('Electron/');
          });
        });
      });

      it('should not update to version strings that are not simple Major.Minor.Patch', async () => {
        await withUpdatableApp({
          nextVersion: '2.0.0-bad',
          startFixture: 'update',
          endFixture: 'update',
          mutateAppPreSign: {
            mutationKey: 'prevent-downgrades',
            mutate: async (appPath) => {
              const infoPath = path.resolve(appPath, 'Contents', 'Info.plist');
              await fs.promises.writeFile(
                infoPath,
                (await fs.promises.readFile(infoPath, 'utf8')).replace('<key>NSSupportsAutomaticGraphicsSwitching</key>', '<key>ElectronSquirrelPreventDowngrades</key><true/><key>NSSupportsAutomaticGraphicsSwitching</key>')
              );
            }
          }
        }, async (appPath, updateZipPath) => {
          server.get('/update-file', (req, res) => {
            res.download(updateZipPath);
          });
          server.get('/update-check', (req, res) => {
            res.json({
              url: `http://localhost:${port}/update-file`,
              name: 'My Release Name',
              notes: 'Theses are some release notes innit',
              pub_date: (new Date()).toString()
            });
          });
          const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
          logOnError(launchResult, () => {
            expect(launchResult).to.have.property('code', 1);
            expect(launchResult.out).to.include('Cannot update to a bundle with a lower version number');
            expect(requests).to.have.lengthOf(2);
            expect(requests[0]).to.have.property('url', '/update-check');
            expect(requests[1]).to.have.property('url', '/update-file');
            expect(requests[0].header('user-agent')).to.include('Electron/');
            expect(requests[1].header('user-agent')).to.include('Electron/');
          });
        });
      });

      it('should still update to higher version numbers', async () => {
        await withUpdatableApp({
          nextVersion: '1.0.1',
          startFixture: 'update',
          endFixture: 'update'
        }, async (appPath, updateZipPath) => {
          server.get('/update-file', (req, res) => {
            res.download(updateZipPath);
          });
          server.get('/update-check', (req, res) => {
            res.json({
              url: `http://localhost:${port}/update-file`,
              name: 'My Release Name',
              notes: 'Theses are some release notes innit',
              pub_date: (new Date()).toString()
            });
          });
          const relaunchPromise = new Promise<void>((resolve) => {
            server.get('/update-check/updated/:version', (req, res) => {
              res.status(204).send();
              resolve();
            });
          });
          const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
          logOnError(launchResult, () => {
            expect(launchResult).to.have.property('code', 0);
            expect(launchResult.out).to.include('Update Downloaded');
            expect(requests).to.have.lengthOf(2);
            expect(requests[0]).to.have.property('url', '/update-check');
            expect(requests[1]).to.have.property('url', '/update-file');
            expect(requests[0].header('user-agent')).to.include('Electron/');
            expect(requests[1].header('user-agent')).to.include('Electron/');
          });

          await relaunchPromise;
          expect(requests).to.have.lengthOf(3);
          expect(requests[2].url).to.equal('/update-check/updated/1.0.1');
          expect(requests[2].header('user-agent')).to.include('Electron/');
        });
      });

      it('should compare version numbers correctly', () => {
        expect(autoUpdater.isVersionAllowedForUpdate!('1.0.0', '2.0.0')).to.equal(true);
        expect(autoUpdater.isVersionAllowedForUpdate!('1.0.1', '1.0.10')).to.equal(true);
        expect(autoUpdater.isVersionAllowedForUpdate!('1.0.10', '1.0.1')).to.equal(false);
        expect(autoUpdater.isVersionAllowedForUpdate!('1.31.1', '1.32.0')).to.equal(true);
        expect(autoUpdater.isVersionAllowedForUpdate!('1.31.1', '0.32.0')).to.equal(false);
      });
    });

    it('should abort the update if the application is still running when ShipIt kicks off', async () => {
      await withUpdatableApp({
        nextVersion: '2.0.0',
        startFixture: 'update',
        endFixture: 'update'
      }, async (appPath, updateZipPath) => {
        server.get('/update-file', (req, res) => {
          res.download(updateZipPath);
        });
        server.get('/update-check', (req, res) => {
          res.json({
            url: `http://localhost:${port}/update-file`,
            name: 'My Release Name',
            notes: 'Theses are some release notes innit',
            pub_date: (new Date()).toString()
          });
        });

        enum FlipFlop {
          INITIAL,
          FLIPPED,
          FLOPPED,
        }

        const shipItFlipFlopPromise = new Promise<void>((resolve) => {
          let state = FlipFlop.INITIAL;
          const checker = setInterval(async () => {
            const running = await getRunningShipIts(appPath);
            switch (state) {
              case FlipFlop.INITIAL: {
                if (running.length) state = FlipFlop.FLIPPED;
                break;
              }
              case FlipFlop.FLIPPED: {
                if (!running.length) state = FlipFlop.FLOPPED;
                break;
              }
            }
            if (state === FlipFlop.FLOPPED) {
              clearInterval(checker);
              resolve();
            }
          }, 500);
        });

        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        const retainerHandle = spawnAppWithHandle(appPath, ['remain-open']);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 0);
          expect(launchResult.out).to.include('Update Downloaded');
          expect(requests).to.have.lengthOf(2);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[1]).to.have.property('url', '/update-file');
          expect(requests[0].header('user-agent')).to.include('Electron/');
          expect(requests[1].header('user-agent')).to.include('Electron/');
        });

        await shipItFlipFlopPromise;
        expect(requests).to.have.lengthOf(2, 'should not have relaunched the updated app');
        expect(JSON.parse(await fs.promises.readFile(path.resolve(appPath, 'Contents/Resources/app/package.json'), 'utf8')).version).to.equal('1.0.0', 'should still be the old version on disk');

        retainerHandle.kill('SIGINT');
      });
    });

    describe('with SquirrelMacEnableDirectContentsWrite enabled', () => {
      let previousValue: any;

      beforeEach(() => {
        previousValue = systemPreferences.getUserDefault('SquirrelMacEnableDirectContentsWrite', 'boolean');
        systemPreferences.setUserDefault('SquirrelMacEnableDirectContentsWrite', 'boolean', true as any);
      });

      afterEach(() => {
        systemPreferences.setUserDefault('SquirrelMacEnableDirectContentsWrite', 'boolean', previousValue as any);
      });

      it('should hit the download endpoint when an update is available and update successfully when the zip is provided leaving the parent directory untouched', async () => {
        await withUpdatableApp({
          nextVersion: '2.0.0',
          startFixture: 'update',
          endFixture: 'update'
        }, async (appPath, updateZipPath) => {
          const randomID = uuid.v4();
          cp.spawnSync('xattr', ['-w', 'spec-id', randomID, appPath]);
          server.get('/update-file', (req, res) => {
            res.download(updateZipPath);
          });
          server.get('/update-check', (req, res) => {
            res.json({
              url: `http://localhost:${port}/update-file`,
              name: 'My Release Name',
              notes: 'Theses are some release notes innit',
              pub_date: (new Date()).toString()
            });
          });
          const relaunchPromise = new Promise<void>((resolve) => {
            server.get('/update-check/updated/:version', (req, res) => {
              res.status(204).send();
              resolve();
            });
          });
          const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
          logOnError(launchResult, () => {
            expect(launchResult).to.have.property('code', 0);
            expect(launchResult.out).to.include('Update Downloaded');
            expect(requests).to.have.lengthOf(2);
            expect(requests[0]).to.have.property('url', '/update-check');
            expect(requests[1]).to.have.property('url', '/update-file');
            expect(requests[0].header('user-agent')).to.include('Electron/');
            expect(requests[1].header('user-agent')).to.include('Electron/');
          });

          await relaunchPromise;
          expect(requests).to.have.lengthOf(3);
          expect(requests[2].url).to.equal('/update-check/updated/2.0.0');
          expect(requests[2].header('user-agent')).to.include('Electron/');
          const result = cp.spawnSync('xattr', ['-l', appPath]);
          expect(result.stdout.toString()).to.include(`spec-id: ${randomID}`);
        });
      });
    });

    it('should hit the download endpoint when an update is available and fail when the zip signature is invalid', async () => {
      await withUpdatableApp({
        nextVersion: '2.0.0',
        startFixture: 'update',
        endFixture: 'update',
        mutateAppPostSign: {
          mutationKey: 'add-resource',
          mutate: async (appPath) => {
            const resourcesPath = path.resolve(appPath, 'Contents', 'Resources', 'app', 'injected.txt');
            await fs.promises.writeFile(resourcesPath, 'demo');
          }
        }
      }, async (appPath, updateZipPath) => {
        server.get('/update-file', (req, res) => {
          res.download(updateZipPath);
        });
        server.get('/update-check', (req, res) => {
          res.json({
            url: `http://localhost:${port}/update-file`,
            name: 'My Release Name',
            notes: 'Theses are some release notes innit',
            pub_date: (new Date()).toString()
          });
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 1);
          expect(launchResult.out).to.include('Code signature at URL');
          expect(launchResult.out).to.include('a sealed resource is missing or invalid');
          expect(requests).to.have.lengthOf(2);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[1]).to.have.property('url', '/update-file');
          expect(requests[0].header('user-agent')).to.include('Electron/');
          expect(requests[1].header('user-agent')).to.include('Electron/');
        });
      });
    });

    it('should hit the download endpoint when an update is available and fail when the ShipIt binary is a symlink', async () => {
      await withUpdatableApp({
        nextVersion: '2.0.0',
        startFixture: 'update',
        endFixture: 'update',
        mutateAppPostSign: {
          mutationKey: 'modify-shipit',
          mutate: async (appPath) => {
            const shipItPath = path.resolve(appPath, 'Contents', 'Frameworks', 'Squirrel.framework', 'Resources', 'ShipIt');
            await fs.promises.rm(shipItPath, { force: true, recursive: true });
            await fs.promises.symlink('/tmp/ShipIt', shipItPath, 'file');
          }
        }
      }, async (appPath, updateZipPath) => {
        server.get('/update-file', (req, res) => {
          res.download(updateZipPath);
        });
        server.get('/update-check', (req, res) => {
          res.json({
            url: `http://localhost:${port}/update-file`,
            name: 'My Release Name',
            notes: 'Theses are some release notes innit',
            pub_date: (new Date()).toString()
          });
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 1);
          expect(launchResult.out).to.include('Code signature at URL');
          expect(launchResult.out).to.include('a sealed resource is missing or invalid');
          expect(requests).to.have.lengthOf(2);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[1]).to.have.property('url', '/update-file');
          expect(requests[0].header('user-agent')).to.include('Electron/');
          expect(requests[1].header('user-agent')).to.include('Electron/');
        });
      });
    });

    it('should hit the download endpoint when an update is available and fail when the Electron Framework is modified', async () => {
      await withUpdatableApp({
        nextVersion: '2.0.0',
        startFixture: 'update',
        endFixture: 'update',
        mutateAppPostSign: {
          mutationKey: 'modify-eframework',
          mutate: async (appPath) => {
            const shipItPath = path.resolve(appPath, 'Contents', 'Frameworks', 'Electron Framework.framework', 'Electron Framework');
            await fs.promises.appendFile(shipItPath, Buffer.from('123'));
          }
        }
      }, async (appPath, updateZipPath) => {
        server.get('/update-file', (req, res) => {
          res.download(updateZipPath);
        });
        server.get('/update-check', (req, res) => {
          res.json({
            url: `http://localhost:${port}/update-file`,
            name: 'My Release Name',
            notes: 'Theses are some release notes innit',
            pub_date: (new Date()).toString()
          });
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 1);
          expect(launchResult.out).to.include('Code signature at URL');
          expect(launchResult.out).to.include(' main executable failed strict validation');
          expect(requests).to.have.lengthOf(2);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[1]).to.have.property('url', '/update-file');
          expect(requests[0].header('user-agent')).to.include('Electron/');
          expect(requests[1].header('user-agent')).to.include('Electron/');
        });
      });
    });

    it('should hit the download endpoint when an update is available and update successfully when the zip is provided with JSON update mode', async () => {
      await withUpdatableApp({
        nextVersion: '2.0.0',
        startFixture: 'update-json',
        endFixture: 'update-json'
      }, async (appPath, updateZipPath) => {
        server.get('/update-file', (req, res) => {
          res.download(updateZipPath);
        });
        server.get('/update-check', (req, res) => {
          res.json({
            currentRelease: '2.0.0',
            releases: [
              {
                version: '2.0.0',
                updateTo: {
                  version: '2.0.0',
                  url: `http://localhost:${port}/update-file`,
                  name: 'My Release Name',
                  notes: 'Theses are some release notes innit',
                  pub_date: (new Date()).toString()
                }
              }
            ]
          });
        });
        const relaunchPromise = new Promise<void>((resolve) => {
          server.get('/update-check/updated/:version', (req, res) => {
            res.status(204).send();
            resolve();
          });
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 0);
          expect(launchResult.out).to.include('Update Downloaded');
          expect(requests).to.have.lengthOf(2);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[1]).to.have.property('url', '/update-file');
          expect(requests[0].header('user-agent')).to.include('Electron/');
          expect(requests[1].header('user-agent')).to.include('Electron/');
        });

        await relaunchPromise;
        expect(requests).to.have.lengthOf(3);
        expect(requests[2]).to.have.property('url', '/update-check/updated/2.0.0');
        expect(requests[2].header('user-agent')).to.include('Electron/');
      });
    });

    it('should hit the download endpoint when an update is available and not update in JSON update mode when the currentRelease is older than the current version', async () => {
      await withUpdatableApp({
        nextVersion: '0.1.0',
        startFixture: 'update-json',
        endFixture: 'update-json'
      }, async (appPath, updateZipPath) => {
        server.get('/update-file', (req, res) => {
          res.download(updateZipPath);
        });
        server.get('/update-check', (req, res) => {
          res.json({
            currentRelease: '0.1.0',
            releases: [
              {
                version: '0.1.0',
                updateTo: {
                  version: '0.1.0',
                  url: `http://localhost:${port}/update-file`,
                  name: 'My Release Name',
                  notes: 'Theses are some release notes innit',
                  pub_date: (new Date()).toString()
                }
              }
            ]
          });
        });
        const launchResult = await launchApp(appPath, [`http://localhost:${port}/update-check`]);
        logOnError(launchResult, () => {
          expect(launchResult).to.have.property('code', 1);
          expect(launchResult.out).to.include('No update available');
          expect(requests).to.have.lengthOf(1);
          expect(requests[0]).to.have.property('url', '/update-check');
          expect(requests[0].header('user-agent')).to.include('Electron/');
        });
      });
    });
  });
});
