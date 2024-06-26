const fs = require('node:fs');
const glob = require('glob');

const currentShard = parseInt(process.argv[2], 10);
const shardCount = parseInt(process.argv[3], 10);

const specFiles = glob.sync('spec/*-spec.ts');

const buckets = [];

for (let i = 0; i < shardCount; i++) {
  buckets.push([]);
}

const testsInSpecFile = Object.create(null);
for (const specFile of specFiles) {
  const testContent = fs.readFileSync(specFile, 'utf8');
  testsInSpecFile[specFile] = testContent.split('it(').length;
}

specFiles.sort((a, b) => {
  return testsInSpecFile[b] - testsInSpecFile[a];
});

let shard = 0;
for (const specFile of specFiles) {
  buckets[shard].push(specFile);
  shard++;
  if (shard === shardCount) shard = 0;
}

console.log(buckets[currentShard - 1].join(' '));
