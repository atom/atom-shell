const dns = require('node:dns');

const write = (writable, chunk) => new Promise((resolve) => writable.write(chunk, resolve));

write(process.stdout, `${dns.getDefaultResultOrder()}\n`)
  .then(() => process.exit(0));
