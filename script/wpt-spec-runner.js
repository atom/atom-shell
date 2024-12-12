const cp = require('node:child_process');
const path = require('node:path');

const utils = require('./lib/utils');

const BASE = path.resolve(__dirname, '../..');
const WPT_DIR = path.resolve(BASE, 'third_party', 'blink', 'web_tests', 'external', 'wpt');

if (!require.main) {
  throw new Error('Must call the wpt spec runner directly');
}

async function main () {
  const testChild = cp.spawn(utils.getAbsoluteElectronExec(), [path.join(__dirname, 'wpt'), '--enable-logging=stderr'], {
    env: {
      ...process.env,
      WPT_DIR
    },
    stdio: 'inherit'
  });
  testChild.on('exit', (testCode) => {
    process.exit(testCode);
  });
}

main().catch((err) => {
  console.error('An unhandled error occurred in the wpt spec runner', err);
  process.exit(1);
});
