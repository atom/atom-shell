const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const BASE = path.resolve(__dirname, '../..');
const NAN_DIR = path.resolve(BASE, 'third_party', 'nan');
const NPX_CMD = process.platform === 'win32' ? 'npx.cmd' : 'npx';

const utils = require('./lib/utils');
const { YARN_VERSION } = require('./yarn');

if (!require.main) {
  throw new Error('Must call the nan spec runner directly');
}

const args = require('minimist')(process.argv.slice(2), {
  string: ['only']
});

async function main () {
  const outDir = utils.getOutDir({ shouldLog: true });
  const nodeDir = path.resolve(BASE, 'out', outDir, 'gen', 'node_headers');
  const env = {
    ...process.env,
    npm_config_nodedir: nodeDir,
    npm_config_msvs_version: '2019',
    npm_config_arch: process.env.NPM_CONFIG_ARCH,
    npm_config_yes: 'true'
  };

  const clangDir = path.resolve(BASE, 'third_party', 'llvm-build', 'Release+Asserts', 'bin');
  const cc = path.resolve(clangDir, 'clang');
  const cxx = path.resolve(clangDir, 'clang++');
  const ld = path.resolve(clangDir, 'lld');

  const platformFlags = [];
  if (process.platform === 'darwin') {
    const sdkPath = path.resolve(BASE, 'out', outDir, 'sdk', 'xcode_links');
    const sdks = (await fs.promises.readdir(sdkPath)).filter(fileName => fileName.endsWith('.sdk'));
    const sdkToUse = sdks[0];
    if (!sdkToUse) {
      console.error('Could not find an SDK to use for the NAN tests');
      process.exit(1);
    }

    if (sdks.length) {
      console.warn(`Multiple SDKs found in the xcode_links directory - using ${sdkToUse}`);
    }

    platformFlags.push(
      `-isysroot ${path.resolve(sdkPath, sdkToUse)}`
    );
  }

  // TODO(ckerr) this is cribbed from read obj/electron/electron_app.ninja.
  // Maybe it would be better to have this script literally open up that
  // file and pull cflags_cc from it instead of using bespoke code here?
  // I think it's unlikely to work; but if it does, it would be more futureproof
  const cxxflags = [
    '-std=c++20',
    '-Wno-trigraphs',
    '-fno-exceptions',
    '-fno-rtti',
    '-nostdinc++',
    `-isystem "${path.resolve(BASE, 'buildtools', 'third_party', 'libc++')}"`,
    `-isystem "${path.resolve(BASE, 'third_party', 'libc++', 'src', 'include')}"`,
    `-isystem "${path.resolve(BASE, 'third_party', 'libc++abi', 'src', 'include')}"`,
    ' -fvisibility-inlines-hidden',
    '-fPIC',
    '-D_LIBCPP_ABI_NAMESPACE=Cr',
    '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE',
    ...platformFlags
  ].join(' ');

  const ldflags = [
    '-stdlib=libc++',
    '-fuse-ld=lld',
    `-L"${path.resolve(BASE, 'out', outDir, 'obj', 'buildtools', 'third_party', 'libc++abi')}"`,
    `-L"${path.resolve(BASE, 'out', outDir, 'obj', 'buildtools', 'third_party', 'libc++')}"`,
    '-lc++abi',
    ...platformFlags
  ].join(' ');

  if (process.platform !== 'win32') {
    env.CC = cc;
    env.CFLAGS = cxxflags;
    env.CXX = cxx;
    env.LD = ld;
    env.CXXFLAGS = cxxflags;
    env.LDFLAGS = ldflags;
  }

  const { status: buildStatus, signal } = cp.spawnSync(NPX_CMD, ['node-gyp', 'rebuild', '--verbose', '--directory', 'test', '-j', 'max'], {
    env,
    cwd: NAN_DIR,
    stdio: 'inherit',
    shell: process.platform === 'win32'
  });

  if (buildStatus !== 0 || signal != null) {
    console.error('Failed to build nan test modules');
    return process.exit(buildStatus !== 0 ? buildStatus : signal);
  }

  const { status: installStatus } = cp.spawnSync(NPX_CMD, [`yarn@${YARN_VERSION}`, 'install'], {
    env,
    cwd: NAN_DIR,
    stdio: 'inherit',
    shell: process.platform === 'win32'
  });

  if (installStatus !== 0 || signal != null) {
    console.error('Failed to install nan node_modules');
    return process.exit(installStatus !== 0 ? installStatus : signal);
  }

  const onlyTests = args.only && args.only.split(',');

  const DISABLED_TESTS = new Set([
    'nannew-test.js',
    'buffer-test.js'
  ]);
  const testsToRun = fs.readdirSync(path.resolve(NAN_DIR, 'test', 'js'))
    .filter(test => !DISABLED_TESTS.has(test))
    .filter(test => {
      return !onlyTests || onlyTests.includes(test) || onlyTests.includes(test.replace('.js', '')) || onlyTests.includes(test.replace('-test.js', ''));
    })
    .map(test => `test/js/${test}`);

  const testChild = cp.spawn(utils.getAbsoluteElectronExec(), ['node_modules/.bin/tap', ...testsToRun], {
    env: {
      ...process.env,
      ELECTRON_RUN_AS_NODE: 'true'
    },
    cwd: NAN_DIR,
    stdio: 'inherit'
  });
  testChild.on('exit', (testCode) => {
    process.exit(testCode);
  });
}

main().catch((err) => {
  console.error('An unhandled error occurred in the nan spec runner', err);
  process.exit(1);
});
