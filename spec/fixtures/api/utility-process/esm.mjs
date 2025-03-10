const write = (writable, chunk) => new Promise((resolve) => writable.write(chunk, resolve));

write(process.stdout, `${import.meta.url}\n`)
  .then(() => process.exit(0));
