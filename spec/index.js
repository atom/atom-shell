const fs = require('node:fs');
const path = require('node:path');
const v8 = require('node:v8');

// We want to terminate on errors, not throw up a dialog
process.on('uncaughtException', (err) => {
  console.error('Unhandled exception in main spec runner:', err);
  process.exit(1);
});

// Tell ts-node which tsconfig to use
process.env.TS_NODE_PROJECT = path.resolve(__dirname, '../tsconfig.spec.json');
process.env.ELECTRON_DISABLE_SECURITY_WARNINGS = 'true';

const { app, protocol } = require('electron');

// Some Linux machines have broken hardware acceleration support.
if (process.env.ELECTRON_TEST_DISABLE_HARDWARE_ACCELERATION) {
  app.disableHardwareAcceleration();
}

v8.setFlagsFromString('--expose_gc');
app.commandLine.appendSwitch('js-flags', '--expose_gc');
// Prevent the spec runner quitting when the first window closes
app.on('window-all-closed', () => null);

// Use fake device for Media Stream to replace actual camera and microphone.
app.commandLine.appendSwitch('use-fake-device-for-media-stream');
app.commandLine.appendSwitch('host-rules', 'MAP localhost2 127.0.0.1');
app.commandLine.appendSwitch('host-resolver-rules', [
  'MAP ipv4.localhost2 10.0.0.1',
  'MAP ipv6.localhost2 [::1]',
  'MAP notfound.localhost2 ~NOTFOUND'
].join(', '));

global.standardScheme = 'app';
global.zoomScheme = 'zoom';
global.serviceWorkerScheme = 'sw';
protocol.registerSchemesAsPrivileged([
  { scheme: global.standardScheme, privileges: { standard: true, secure: true, stream: false } },
  { scheme: global.zoomScheme, privileges: { standard: true, secure: true } },
  { scheme: global.serviceWorkerScheme, privileges: { allowServiceWorkers: true, standard: true, secure: true } },
  { scheme: 'http-like', privileges: { standard: true, secure: true, corsEnabled: true, supportFetchAPI: true } },
  { scheme: 'cors-blob', privileges: { corsEnabled: true, supportFetchAPI: true } },
  { scheme: 'cors', privileges: { corsEnabled: true, supportFetchAPI: true } },
  { scheme: 'no-cors', privileges: { supportFetchAPI: true } },
  { scheme: 'no-fetch', privileges: { corsEnabled: true } },
  { scheme: 'stream', privileges: { standard: true, stream: true } },
  { scheme: 'foo', privileges: { standard: true } },
  { scheme: 'bar', privileges: { standard: true } }
]);

app.whenReady().then(async () => {
  require('ts-node/register');

  const argv = require('yargs')
    .boolean('ci')
    .array('files')
    .string('g').alias('g', 'grep')
    .boolean('i').alias('i', 'invert')
    .argv;

  const Mocha = require('mocha');
  const mochaOptions = {
    forbidOnly: process.env.CI
  };
  if (process.env.CI) {
    mochaOptions.retries = 3;
  }
  if (process.env.MOCHA_REPORTER) {
    mochaOptions.reporter = process.env.MOCHA_REPORTER;
  }
  if (process.env.MOCHA_MULTI_REPORTERS) {
    mochaOptions.reporterOptions = {
      reporterEnabled: process.env.MOCHA_MULTI_REPORTERS
    };
  }
  // The MOCHA_GREP and MOCHA_INVERT are used in some vendor builds for sharding
  // tests.
  if (process.env.MOCHA_GREP) {
    mochaOptions.grep = process.env.MOCHA_GREP;
  }
  if (process.env.MOCHA_INVERT) {
    mochaOptions.invert = process.env.MOCHA_INVERT === 'true';
  }
  const mocha = new Mocha(mochaOptions);

  // Add a root hook on mocha to skip any tests that are disabled
  const disabledTests = new Set(
    JSON.parse(
      fs.readFileSync(path.join(__dirname, 'disabled-tests.json'), 'utf8')
    )
  );
  mocha.suite.beforeEach(function () {
    // TODO(clavin): add support for disabling *suites* by title, not just tests
    if (disabledTests.has(this.currentTest?.fullTitle())) {
      this.skip();
    }
  });

  // The cleanup method is registered this way rather than through an
  // `afterEach` at the top level so that it can run before other `afterEach`
  // methods.
  //
  // The order of events is:
  // 1. test completes,
  // 2. `defer()`-ed methods run, in reverse order,
  // 3. regular `afterEach` hooks run.
  const { runCleanupFunctions } = require('./lib/spec-helpers');
  mocha.suite.on('suite', function attach (suite) {
    suite.afterEach('cleanup', runCleanupFunctions);
    suite.on('suite', attach);
  });

  if (!process.env.MOCHA_REPORTER) {
    mocha.ui('bdd').reporter('tap');
  }
  const mochaTimeout = process.env.MOCHA_TIMEOUT || 30000;
  mocha.timeout(mochaTimeout);

  if (argv.grep) mocha.grep(argv.grep);
  if (argv.invert) mocha.invert();

  const baseElectronDir = path.resolve(__dirname, '..');
  const validTestPaths = argv.files && argv.files.map(file =>
    path.isAbsolute(file)
      ? path.relative(baseElectronDir, file)
      : file);
  const filter = (file) => {
    if (!/-spec\.[tj]s$/.test(file)) {
      return false;
    }

    // This allows you to run specific modules only:
    // npm run test -match=menu
    const moduleMatch = process.env.npm_config_match
      ? new RegExp(process.env.npm_config_match, 'g')
      : null;
    if (moduleMatch && !moduleMatch.test(file)) {
      return false;
    }

    if (validTestPaths && !validTestPaths.includes(path.relative(baseElectronDir, file))) {
      return false;
    }

    return true;
  };

  const { getFiles } = require('./get-files');
  const testFiles = await getFiles(__dirname, filter);
  for (const file of testFiles.sort()) {
    mocha.addFile(file);
  }

  if (validTestPaths && validTestPaths.length > 0 && testFiles.length === 0) {
    console.error('Test files were provided, but they did not match any searched files');
    console.error('provided file paths (relative to electron/):', validTestPaths);
    process.exit(1);
  }

  const cb = () => {
    // Ensure the callback is called after runner is defined
    process.nextTick(() => {
      process.exit(runner.failures);
    });
  };

  // Set up chai in the correct order
  const chai = require('chai');
  chai.use(require('chai-as-promised'));
  chai.use(require('dirty-chai'));

  // Show full object diff
  // https://github.com/chaijs/chai/issues/469
  chai.config.truncateThreshold = 0;

  const runner = mocha.run(cb);
}).catch((err) => {
  console.error('An error occurred while running the spec runner');
  console.error(err);
  process.exit(1);
});
