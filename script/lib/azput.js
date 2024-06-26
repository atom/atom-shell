/* eslint-disable camelcase */
const { BlobServiceClient } = require('@azure/storage-blob');
const path = require('node:path');

const { ELECTRON_ARTIFACTS_BLOB_STORAGE } = process.env;
if (!ELECTRON_ARTIFACTS_BLOB_STORAGE) {
  console.error('Missing required ELECTRON_ARTIFACTS_BLOB_STORAGE environment variable.');
  process.exit(1);
}

const blobServiceClient = BlobServiceClient.fromConnectionString(ELECTRON_ARTIFACTS_BLOB_STORAGE);

const args = require('minimist')(process.argv.slice(2));

let { prefix = '/', key_prefix = '', _: files } = args;
if (prefix && !prefix.endsWith(path.sep)) prefix = path.resolve(prefix) + path.sep;

function filenameToKey (file) {
  file = path.resolve(file);
  if (file.startsWith(prefix)) file = file.substr(prefix.length - 1);
  return key_prefix + (path.sep === '\\' ? file.replace(/\\/g, '/') : file);
}

let anErrorOccurred = false;
function next (done) {
  const file = files.shift();
  if (!file) return done();
  const key = filenameToKey(file);

  const [containerName, ...keyPath] = key.split('/');
  const blobKey = keyPath.join('/');
  console.log(`Uploading '${file}' to container '${containerName}' with key '${blobKey}'...`);

  const containerClient = blobServiceClient.getContainerClient(containerName);
  const blockBlobClient = containerClient.getBlockBlobClient(blobKey);
  blockBlobClient.uploadFile(file)
    .then((uploadBlobResponse) => {
      console.log(`Upload block blob ${blobKey} successfully: https://artifacts.electronjs.org/${key}`, uploadBlobResponse.requestId);
    })
    .catch((err) => {
      console.error(err);
      anErrorOccurred = true;
    })
    .then(() => next(done));
}
next(() => {
  process.exit(anErrorOccurred ? 1 : 0);
});
