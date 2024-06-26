import { Octokit } from '@octokit/rest';
import * as fs from 'node:fs';

const octokit = new Octokit({
  auth: process.env.ELECTRON_GITHUB_TOKEN,
  log: console
});

if (!process.env.CI) require('dotenv-safe').load();

if (process.argv.length < 6) {
  console.log('Usage: upload-to-github filePath fileName releaseId');
  process.exit(1);
}

const filePath = process.argv[2];
const fileName = process.argv[3];
const releaseId = parseInt(process.argv[4], 10);
const releaseVersion = process.argv[5];

if (isNaN(releaseId)) {
  throw new TypeError('Provided release ID was not a valid integer');
}

const getHeaders = (filePath: string, fileName: string) => {
  const extension = fileName.split('.').pop();
  if (!extension) {
    throw new Error(`Failed to get headers for extensionless file: ${fileName}`);
  }
  console.log(`About to get size of ${filePath}`);
  const size = fs.statSync(filePath).size;
  console.log(`Got size of ${filePath}: ${size}`);
  const options: Record<string, string> = {
    json: 'text/json',
    zip: 'application/zip',
    txt: 'text/plain',
    ts: 'application/typescript'
  };

  return {
    'content-type': options[extension],
    'content-length': size
  };
};

function getRepo () {
  return releaseVersion.indexOf('nightly') > 0 ? 'nightlies' : 'electron';
}

const targetRepo = getRepo();
const uploadUrl = `https://uploads.github.com/repos/electron/${targetRepo}/releases/${releaseId}/assets{?name,label}`;
let retry = 0;

function uploadToGitHub () {
  console.log(`in uploadToGitHub for ${filePath}, ${fileName}`);
  const fileData = fs.createReadStream(filePath);
  console.log(`in uploadToGitHub, created readstream for ${filePath}`);
  octokit.repos.uploadReleaseAsset({
    url: uploadUrl,
    headers: getHeaders(filePath, fileName),
    data: fileData as any,
    name: fileName,
    owner: 'electron',
    repo: targetRepo,
    release_id: releaseId
  }).then(() => {
    console.log(`Successfully uploaded ${fileName} to GitHub.`);
    process.exit();
  }).catch((err) => {
    if (retry < 4) {
      console.log(`Error uploading ${fileName} to GitHub, will retry.  Error was:`, err);
      retry++;

      octokit.repos.listReleaseAssets({
        owner: 'electron',
        repo: targetRepo,
        release_id: releaseId,
        per_page: 100
      }).then(assets => {
        console.log('Got list of assets for existing release:');
        console.log(JSON.stringify(assets.data, null, '  '));
        const existingAssets = assets.data.filter(asset => asset.name === fileName);

        if (existingAssets.length > 0) {
          console.log(`${fileName} already exists; will delete before retrying upload.`);
          octokit.repos.deleteReleaseAsset({
            owner: 'electron',
            repo: targetRepo,
            asset_id: existingAssets[0].id
          }).catch((deleteErr) => {
            console.log(`Failed to delete existing asset ${fileName}.  Error was:`, deleteErr);
          }).then(uploadToGitHub);
        } else {
          console.log(`Current asset ${fileName} not found in existing assets; retrying upload.`);
          uploadToGitHub();
        }
      }).catch((getReleaseErr) => {
        console.log('Fatal: Unable to get current release assets via getRelease!  Error was:', getReleaseErr);
      });
    } else {
      console.log(`Error retrying uploading ${fileName} to GitHub:`, err);
      process.exitCode = 1;
    }
  });
}

uploadToGitHub();
