const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const YARN_VERSION = /'yarn_version': '(.+?)'/.exec(fs.readFileSync(path.resolve(__dirname, '../DEPS'), 'utf8'))[1];
const NPX_CMD = process.platform === 'win32' ? 'npx.cmd' : 'npx';

if (require.main === module) {
  const child = cp.spawn(NPX_CMD, [`yarn@${YARN_VERSION}`, ...process.argv.slice(2)], {
    stdio: 'inherit',
    env: {
      ...process.env,
      npm_config_yes: 'true'
    },
    shell: process.platform === 'win32'
  });

  child.on('exit', code => process.exit(code));
}

exports.YARN_VERSION = YARN_VERSION;
