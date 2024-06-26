#!/usr/bin/env node

const { ElectronVersions, Installer } = require('@electron/fiddle-core');
const childProcess = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const { hashElement } = require('folder-hash');
const os = require('node:os');
const path = require('node:path');
const unknownFlags = [];

require('colors');
const pass = '✓'.green;
const fail = '✗'.red;

const args = require('minimist')(process.argv, {
  string: ['runners', 'target', 'electronVersion'],
  unknown: arg => unknownFlags.push(arg)
});

const unknownArgs = [];
for (const flag of unknownFlags) {
  unknownArgs.push(flag);
  const onlyFlag = flag.replace(/^-+/, '');
  if (args[onlyFlag]) {
    unknownArgs.push(args[onlyFlag]);
  }
}

const utils = require('./lib/utils');
const { YARN_VERSION } = require('./yarn');

const BASE = path.resolve(__dirname, '../..');
const NPX_CMD = process.platform === 'win32' ? 'npx.cmd' : 'npx';

const runners = new Map([
  ['main', { description: 'Main process specs', run: runMainProcessElectronTests }]
]);

const specHashPath = path.resolve(__dirname, '../spec/.hash');

if (args.electronVersion) {
  if (args.runners && args.runners !== 'main') {
    console.log(`${fail} only 'main' runner can be used with --electronVersion`);
    process.exit(1);
  }

  args.runners = 'main';
}

let runnersToRun = null;
if (args.runners !== undefined) {
  runnersToRun = args.runners.split(',').filter(value => value);
  if (!runnersToRun.every(r => [...runners.keys()].includes(r))) {
    console.log(`${fail} ${runnersToRun} must be a subset of [${[...runners.keys()].join(' | ')}]`);
    process.exit(1);
  }
  console.log('Only running:', runnersToRun);
} else {
  console.log(`Triggering runners: ${[...runners.keys()].join(', ')}`);
}

async function main () {
  if (args.electronVersion) {
    const versions = await ElectronVersions.create();
    if (args.electronVersion === 'latest') {
      args.electronVersion = versions.latest.version;
    } else if (args.electronVersion.startsWith('latest@')) {
      const majorVersion = parseInt(args.electronVersion.slice('latest@'.length));
      const ver = versions.inMajor(majorVersion).slice(-1)[0];
      if (ver) {
        args.electronVersion = ver.version;
      } else {
        console.log(`${fail} '${majorVersion}' is not a recognized Electron major version`);
        process.exit(1);
      }
    } else if (!versions.isVersion(args.electronVersion)) {
      console.log(`${fail} '${args.electronVersion}' is not a recognized Electron version`);
      process.exit(1);
    }

    const versionString = `v${args.electronVersion}`;
    console.log(`Running against Electron ${versionString.green}`);
  }

  const [lastSpecHash, lastSpecInstallHash] = loadLastSpecHash();
  const [currentSpecHash, currentSpecInstallHash] = await getSpecHash();
  const somethingChanged = (currentSpecHash !== lastSpecHash) ||
      (lastSpecInstallHash !== currentSpecInstallHash);

  if (somethingChanged) {
    await installSpecModules(path.resolve(__dirname, '..', 'spec'));
    await getSpecHash().then(saveSpecHash);
  }

  if (!fs.existsSync(path.resolve(__dirname, '../electron.d.ts'))) {
    console.log('Generating electron.d.ts as it is missing');
    generateTypeDefinitions();
  }

  await runElectronTests();
}

function generateTypeDefinitions () {
  const { status } = childProcess.spawnSync('npm', ['run', 'create-typescript-definitions'], {
    cwd: path.resolve(__dirname, '..'),
    stdio: 'inherit',
    shell: true
  });
  if (status !== 0) {
    throw new Error(`Electron typescript definition generation failed with exit code: ${status}.`);
  }
}

function loadLastSpecHash () {
  return fs.existsSync(specHashPath)
    ? fs.readFileSync(specHashPath, 'utf8').split('\n')
    : [null, null];
}

function saveSpecHash ([newSpecHash, newSpecInstallHash]) {
  fs.writeFileSync(specHashPath, `${newSpecHash}\n${newSpecInstallHash}`);
}

async function runElectronTests () {
  const errors = [];

  const testResultsDir = process.env.ELECTRON_TEST_RESULTS_DIR;
  for (const [runnerId, { description, run }] of runners) {
    if (runnersToRun && !runnersToRun.includes(runnerId)) {
      console.info('\nSkipping:', description);
      continue;
    }
    try {
      console.info('\nRunning:', description);
      if (testResultsDir) {
        process.env.MOCHA_FILE = path.join(testResultsDir, `test-results-${runnerId}.xml`);
      }
      await run();
    } catch (err) {
      errors.push([runnerId, err]);
    }
  }

  if (errors.length !== 0) {
    for (const err of errors) {
      console.error('\n\nRunner Failed:', err[0]);
      console.error(err[1]);
    }
    console.log(`${fail} Electron test runners have failed`);
    process.exit(1);
  }
}

async function runTestUsingElectron (specDir, testName) {
  let exe;
  if (args.electronVersion) {
    const installer = new Installer();
    exe = await installer.install(args.electronVersion);
  } else {
    exe = path.resolve(BASE, utils.getElectronExec());
  }
  const runnerArgs = [`electron/${specDir}`, ...unknownArgs.slice(2)];
  if (process.platform === 'linux') {
    runnerArgs.unshift(path.resolve(__dirname, 'dbus_mock.py'), exe);
    exe = 'python3';
  }
  const { status, signal } = childProcess.spawnSync(exe, runnerArgs, {
    cwd: path.resolve(__dirname, '../..'),
    stdio: 'inherit'
  });
  if (status !== 0) {
    if (status) {
      const textStatus = process.platform === 'win32' ? `0x${status.toString(16)}` : status.toString();
      console.log(`${fail} Electron tests failed with code ${textStatus}.`);
    } else {
      console.log(`${fail} Electron tests failed with kill signal ${signal}.`);
    }
    process.exit(1);
  }
  console.log(`${pass} Electron ${testName} process tests passed.`);
}

async function runMainProcessElectronTests () {
  await runTestUsingElectron('spec', 'main');
}

async function installSpecModules (dir) {
  // v8 headers use c++17 so override the gyp default of -std=c++14,
  // but don't clobber any other CXXFLAGS that were passed into spec-runner.js
  const CXXFLAGS = ['-std=c++17', process.env.CXXFLAGS].filter(x => !!x).join(' ');

  const env = {
    ...process.env,
    CXXFLAGS,
    npm_config_msvs_version: '2019',
    npm_config_yes: 'true'
  };
  if (args.electronVersion) {
    env.npm_config_target = args.electronVersion;
    env.npm_config_disturl = 'https://electronjs.org/headers';
    env.npm_config_runtime = 'electron';
    env.npm_config_devdir = path.join(os.homedir(), '.electron-gyp');
    env.npm_config_build_from_source = 'true';
    const { status } = childProcess.spawnSync('npm', ['run', 'node-gyp-install', '--ensure'], {
      env,
      cwd: dir,
      stdio: 'inherit',
      shell: true
    });
    if (status !== 0) {
      console.log(`${fail} Failed to "npm run node-gyp-install" install in '${dir}'`);
      process.exit(1);
    }
  } else {
    env.npm_config_nodedir = path.resolve(BASE, `out/${utils.getOutDir({ shouldLog: true })}/gen/node_headers`);
  }
  if (fs.existsSync(path.resolve(dir, 'node_modules'))) {
    await fs.promises.rm(path.resolve(dir, 'node_modules'), { force: true, recursive: true });
  }
  const { status } = childProcess.spawnSync(NPX_CMD, [`yarn@${YARN_VERSION}`, 'install', '--frozen-lockfile'], {
    env,
    cwd: dir,
    stdio: 'inherit',
    shell: process.platform === 'win32'
  });
  if (status !== 0 && !process.env.IGNORE_YARN_INSTALL_ERROR) {
    console.log(`${fail} Failed to yarn install in '${dir}'`);
    process.exit(1);
  }
}

function getSpecHash () {
  return Promise.all([
    (async () => {
      const hasher = crypto.createHash('SHA256');
      hasher.update(fs.readFileSync(path.resolve(__dirname, '../spec/package.json')));
      hasher.update(fs.readFileSync(path.resolve(__dirname, '../spec/yarn.lock')));
      hasher.update(fs.readFileSync(path.resolve(__dirname, '../script/spec-runner.js')));
      return hasher.digest('hex');
    })(),
    (async () => {
      const specNodeModulesPath = path.resolve(__dirname, '../spec/node_modules');
      if (!fs.existsSync(specNodeModulesPath)) {
        return null;
      }
      const { hash } = await hashElement(specNodeModulesPath, {
        folders: {
          exclude: ['.bin']
        }
      });
      return hash;
    })()
  ]);
}

main().catch((error) => {
  console.error('An error occurred inside the spec runner:', error);
  process.exit(1);
});
