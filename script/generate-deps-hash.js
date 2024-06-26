const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

// Fallback to blow away old cache keys
const FALLBACK_HASH_VERSION = 3;

// Per platform hash versions to bust the cache on different platforms
const HASH_VERSIONS = {
  darwin: 3,
  win32: 4,
  linux: 3
};

// Base files to hash
const filesToHash = [
  path.resolve(__dirname, '../DEPS'),
  path.resolve(__dirname, '../yarn.lock'),
  path.resolve(__dirname, '../script/sysroots.json')
];

const addAllFiles = (dir) => {
  for (const child of fs.readdirSync(dir).sort()) {
    const childPath = path.resolve(dir, child);
    if (fs.statSync(childPath).isDirectory()) {
      addAllFiles(childPath);
    } else {
      filesToHash.push(childPath);
    }
  }
};

// Add all patch files to the hash
addAllFiles(path.resolve(__dirname, '../patches'));

// Create Hash
const hasher = crypto.createHash('SHA256');
const addToHashAndLog = (s) => {
  return hasher.update(s);
};
addToHashAndLog(`HASH_VERSION:${HASH_VERSIONS[process.platform] || FALLBACK_HASH_VERSION}`);
for (const file of filesToHash) {
  hasher.update(fs.readFileSync(file));
}

// Add the GCLIENT_EXTRA_ARGS variable to the hash
const extraArgs = process.env.GCLIENT_EXTRA_ARGS || 'no_extra_args';
addToHashAndLog(extraArgs);

const effectivePlatform = extraArgs.includes('host_os=mac') ? 'darwin' : process.platform;

// Write the hash to disk
fs.writeFileSync(path.resolve(__dirname, '../.depshash'), hasher.digest('hex'));

let targetContent = `${effectivePlatform}\n${process.env.TARGET_ARCH}\n${process.env.GN_CONFIG}\n${undefined}\n${process.env.GN_EXTRA_ARGS}\n${process.env.GN_BUILDFLAG_ARGS}`;
const argsDir = path.resolve(__dirname, '../build/args');
for (const argFile of fs.readdirSync(argsDir).sort()) {
  targetContent += `\n${argFile}--${crypto.createHash('SHA1').update(fs.readFileSync(path.resolve(argsDir, argFile))).digest('hex')}`;
}

fs.writeFileSync(path.resolve(__dirname, '../.depshash-target'), targetContent);
