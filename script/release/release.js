#!/usr/bin/env node

if (!process.env.CI) require('dotenv-safe').load();

const args = require('minimist')(process.argv.slice(2), {
  boolean: [
    'validateRelease',
    'verboseNugget'
  ],
  default: { verboseNugget: false }
});
const fs = require('node:fs');
const { execSync } = require('node:child_process');
const got = require('got');
const path = require('node:path');
const semver = require('semver');
const temp = require('temp').track();
const { BlobServiceClient } = require('@azure/storage-blob');
const { Octokit } = require('@octokit/rest');

require('colors');
const pass = '✓'.green;
const fail = '✗'.red;

const { ELECTRON_DIR } = require('../lib/utils');
const { getElectronVersion } = require('../lib/get-version');
const getUrlHash = require('./get-url-hash');

const pkgVersion = `v${getElectronVersion()}`;

const octokit = new Octokit({
  auth: process.env.ELECTRON_GITHUB_TOKEN
});

function getRepo () {
  return pkgVersion.indexOf('nightly') > 0 ? 'nightlies' : 'electron';
}

const targetRepo = getRepo();
let failureCount = 0;

async function getDraftRelease (version, skipValidation) {
  const releaseInfo = await octokit.repos.listReleases({
    owner: 'electron',
    repo: targetRepo
  });

  const versionToCheck = version || pkgVersion;
  const drafts = releaseInfo.data.filter(release => {
    return release.tag_name === versionToCheck && release.draft === true;
  });

  const draft = drafts[0];
  if (!skipValidation) {
    failureCount = 0;
    check(drafts.length === 1, 'one draft exists', true);
    if (versionToCheck.includes('beta')) {
      check(draft.prerelease, 'draft is a prerelease');
    }
    check(draft.body.length > 50 && !draft.body.includes('(placeholder)'), 'draft has release notes');
    check((failureCount === 0), 'Draft release looks good to go.', true);
  }
  return draft;
}

async function validateReleaseAssets (release, validatingRelease) {
  const requiredAssets = assetsForVersion(release.tag_name, validatingRelease).sort();
  const extantAssets = release.assets.map(asset => asset.name).sort();
  const downloadUrls = release.assets.map(asset => ({ url: asset.browser_download_url, file: asset.name })).sort((a, b) => a.file.localeCompare(b.file));

  failureCount = 0;
  for (const asset of requiredAssets) {
    check(extantAssets.includes(asset), asset);
  }
  check((failureCount === 0), 'All required GitHub assets exist for release', true);

  if (!validatingRelease || !release.draft) {
    if (release.draft) {
      await verifyDraftGitHubReleaseAssets(release);
    } else {
      await verifyShasumsForRemoteFiles(downloadUrls)
        .catch(err => {
          console.error(`${fail} error verifyingShasums`, err);
        });
    }
    const azRemoteFiles = azRemoteFilesForVersion(release.tag_name);
    await verifyShasumsForRemoteFiles(azRemoteFiles, true);
  }
}

function check (condition, statement, exitIfFail = false) {
  if (condition) {
    console.log(`${pass} ${statement}`);
  } else {
    failureCount++;
    console.error(`${fail} ${statement}`);
    if (exitIfFail) process.exit(1);
  }
}

function assetsForVersion (version, validatingRelease) {
  const patterns = [
    `chromedriver-${version}-darwin-x64.zip`,
    `chromedriver-${version}-darwin-arm64.zip`,
    `chromedriver-${version}-linux-arm64.zip`,
    `chromedriver-${version}-linux-armv7l.zip`,
    `chromedriver-${version}-linux-x64.zip`,
    `chromedriver-${version}-mas-x64.zip`,
    `chromedriver-${version}-mas-arm64.zip`,
    `chromedriver-${version}-win32-ia32.zip`,
    `chromedriver-${version}-win32-x64.zip`,
    `chromedriver-${version}-win32-arm64.zip`,
    `electron-${version}-darwin-x64-dsym.zip`,
    `electron-${version}-darwin-x64-dsym-snapshot.zip`,
    `electron-${version}-darwin-x64-symbols.zip`,
    `electron-${version}-darwin-x64.zip`,
    `electron-${version}-darwin-arm64-dsym.zip`,
    `electron-${version}-darwin-arm64-dsym-snapshot.zip`,
    `electron-${version}-darwin-arm64-symbols.zip`,
    `electron-${version}-darwin-arm64.zip`,
    `electron-${version}-linux-arm64-symbols.zip`,
    `electron-${version}-linux-arm64.zip`,
    `electron-${version}-linux-armv7l-symbols.zip`,
    `electron-${version}-linux-armv7l.zip`,
    `electron-${version}-linux-x64-debug.zip`,
    `electron-${version}-linux-x64-symbols.zip`,
    `electron-${version}-linux-x64.zip`,
    `electron-${version}-mas-x64-dsym.zip`,
    `electron-${version}-mas-x64-dsym-snapshot.zip`,
    `electron-${version}-mas-x64-symbols.zip`,
    `electron-${version}-mas-x64.zip`,
    `electron-${version}-mas-arm64-dsym.zip`,
    `electron-${version}-mas-arm64-dsym-snapshot.zip`,
    `electron-${version}-mas-arm64-symbols.zip`,
    `electron-${version}-mas-arm64.zip`,
    `electron-${version}-win32-ia32-pdb.zip`,
    `electron-${version}-win32-ia32-symbols.zip`,
    `electron-${version}-win32-ia32.zip`,
    `electron-${version}-win32-x64-pdb.zip`,
    `electron-${version}-win32-x64-symbols.zip`,
    `electron-${version}-win32-x64.zip`,
    `electron-${version}-win32-arm64-pdb.zip`,
    `electron-${version}-win32-arm64-symbols.zip`,
    `electron-${version}-win32-arm64.zip`,
    'electron-api.json',
    'electron.d.ts',
    'hunspell_dictionaries.zip',
    'libcxx_headers.zip',
    'libcxxabi_headers.zip',
    `libcxx-objects-${version}-linux-arm64.zip`,
    `libcxx-objects-${version}-linux-armv7l.zip`,
    `libcxx-objects-${version}-linux-x64.zip`,
    `ffmpeg-${version}-darwin-x64.zip`,
    `ffmpeg-${version}-darwin-arm64.zip`,
    `ffmpeg-${version}-linux-arm64.zip`,
    `ffmpeg-${version}-linux-armv7l.zip`,
    `ffmpeg-${version}-linux-x64.zip`,
    `ffmpeg-${version}-mas-x64.zip`,
    `ffmpeg-${version}-mas-arm64.zip`,
    `ffmpeg-${version}-win32-ia32.zip`,
    `ffmpeg-${version}-win32-x64.zip`,
    `ffmpeg-${version}-win32-arm64.zip`,
    `mksnapshot-${version}-darwin-x64.zip`,
    `mksnapshot-${version}-darwin-arm64.zip`,
    `mksnapshot-${version}-linux-arm64-x64.zip`,
    `mksnapshot-${version}-linux-armv7l-x64.zip`,
    `mksnapshot-${version}-linux-x64.zip`,
    `mksnapshot-${version}-mas-x64.zip`,
    `mksnapshot-${version}-mas-arm64.zip`,
    `mksnapshot-${version}-win32-ia32.zip`,
    `mksnapshot-${version}-win32-x64.zip`,
    `mksnapshot-${version}-win32-arm64-x64.zip`,
    `electron-${version}-win32-ia32-toolchain-profile.zip`,
    `electron-${version}-win32-x64-toolchain-profile.zip`,
    `electron-${version}-win32-arm64-toolchain-profile.zip`
  ];
  if (!validatingRelease) {
    patterns.push('SHASUMS256.txt');
  }
  return patterns;
}

const cloudStoreFilePaths = (version) => [
  `iojs-${version}-headers.tar.gz`,
  `iojs-${version}.tar.gz`,
  `node-${version}.tar.gz`,
  'node.lib',
  'x64/node.lib',
  'win-x64/iojs.lib',
  'win-x86/iojs.lib',
  'win-arm64/iojs.lib',
  'win-x64/node.lib',
  'win-x86/node.lib',
  'win-arm64/node.lib',
  'arm64/node.lib',
  'SHASUMS.txt',
  'SHASUMS256.txt'
];

function azRemoteFilesForVersion (version) {
  const azCDN = 'https://artifacts.electronjs.org/headers/';
  const versionPrefix = `${azCDN}dist/${version}/`;
  return cloudStoreFilePaths(version).map((filePath) => ({
    file: filePath,
    url: `${versionPrefix}${filePath}`
  }));
}

function runScript (scriptName, scriptArgs, cwd) {
  const scriptCommand = `${scriptName} ${scriptArgs.join(' ')}`;
  const scriptOptions = {
    encoding: 'UTF-8'
  };
  if (cwd) scriptOptions.cwd = cwd;
  try {
    return execSync(scriptCommand, scriptOptions);
  } catch (err) {
    console.error(`${fail} Error running ${scriptName}`, err);
    process.exit(1);
  }
}

function uploadNodeShasums () {
  console.log('Uploading Node SHASUMS file to artifacts.electronjs.org.');
  const scriptPath = path.join(ELECTRON_DIR, 'script', 'release', 'uploaders', 'upload-node-checksums.py');
  runScript(scriptPath, ['-v', pkgVersion]);
  console.log(`${pass} Done uploading Node SHASUMS file to artifacts.electronjs.org.`);
}

function uploadIndexJson () {
  console.log('Uploading index.json to artifacts.electronjs.org.');
  const scriptPath = path.join(ELECTRON_DIR, 'script', 'release', 'uploaders', 'upload-index-json.py');
  runScript(scriptPath, [pkgVersion]);
  console.log(`${pass} Done uploading index.json to artifacts.electronjs.org.`);
}

async function mergeShasums (pkgVersion) {
  // Download individual checksum files for Electron zip files from artifact storage,
  // concatenate them, and upload to GitHub.

  const connectionString = process.env.ELECTRON_ARTIFACTS_BLOB_STORAGE;
  if (!connectionString) {
    throw new Error('Please set the $ELECTRON_ARTIFACTS_BLOB_STORAGE environment variable');
  }

  const blobServiceClient = BlobServiceClient.fromConnectionString(connectionString);
  const containerClient = blobServiceClient.getContainerClient('checksums-scratchpad');
  const blobsIter = containerClient.listBlobsFlat({
    prefix: `${pkgVersion}/`
  });
  const shasums = [];
  for await (const blob of blobsIter) {
    if (blob.name.endsWith('.sha256sum')) {
      const blobClient = containerClient.getBlockBlobClient(blob.name);
      const response = await blobClient.downloadToBuffer();
      shasums.push(response.toString('ascii').trim());
    }
  }
  return shasums.join('\n');
}

async function createReleaseShasums (release) {
  const fileName = 'SHASUMS256.txt';
  const existingAssets = release.assets.filter(asset => asset.name === fileName);
  if (existingAssets.length > 0) {
    console.log(`${fileName} already exists on GitHub; deleting before creating new file.`);
    await octokit.repos.deleteReleaseAsset({
      owner: 'electron',
      repo: targetRepo,
      asset_id: existingAssets[0].id
    }).catch(err => {
      console.error(`${fail} Error deleting ${fileName} on GitHub:`, err);
      process.exit(1);
    });
  }
  console.log(`Creating and uploading the release ${fileName}.`);
  const checksums = await mergeShasums(pkgVersion);

  console.log(`${pass} Generated release SHASUMS.`);
  const filePath = await saveShaSumFile(checksums, fileName);

  console.log(`${pass} Created ${fileName} file.`);
  await uploadShasumFile(filePath, fileName, release.id);

  console.log(`${pass} Successfully uploaded ${fileName} to GitHub.`);
}

async function uploadShasumFile (filePath, fileName, releaseId) {
  const uploadUrl = `https://uploads.github.com/repos/electron/${targetRepo}/releases/${releaseId}/assets{?name,label}`;
  return octokit.repos.uploadReleaseAsset({
    url: uploadUrl,
    headers: {
      'content-type': 'text/plain',
      'content-length': fs.statSync(filePath).size
    },
    data: fs.createReadStream(filePath),
    name: fileName
  }).catch(err => {
    console.error(`${fail} Error uploading ${filePath} to GitHub:`, err);
    process.exit(1);
  });
}

function saveShaSumFile (checksums, fileName) {
  return new Promise(resolve => {
    temp.open(fileName, (err, info) => {
      if (err) {
        console.error(`${fail} Could not create ${fileName} file`);
        process.exit(1);
      } else {
        fs.writeFileSync(info.fd, checksums);
        fs.close(info.fd, (err) => {
          if (err) {
            console.error(`${fail} Could close ${fileName} file`);
            process.exit(1);
          }
          resolve(info.path);
        });
      }
    });
  });
}

async function publishRelease (release) {
  let makeLatest = false;
  if (!release.prerelease) {
    const currentLatest = await octokit.repos.getLatestRelease({
      owner: 'electron',
      repo: targetRepo
    });

    makeLatest = semver.gte(release.tag_name, currentLatest.data.tag_name);
  }

  return octokit.repos.updateRelease({
    owner: 'electron',
    repo: targetRepo,
    release_id: release.id,
    tag_name: release.tag_name,
    draft: false,
    make_latest: makeLatest ? 'true' : 'false'
  }).catch(err => {
    console.error(`${fail} Error publishing release:`, err);
    process.exit(1);
  });
}

async function makeRelease (releaseToValidate) {
  if (releaseToValidate) {
    if (releaseToValidate === true) {
      releaseToValidate = pkgVersion;
    } else {
      console.log('Release to validate !=== true');
    }
    console.log(`Validating release ${releaseToValidate}`);
    const release = await getDraftRelease(releaseToValidate);
    await validateReleaseAssets(release, true);
  } else {
    let draftRelease = await getDraftRelease();
    uploadNodeShasums();
    await createReleaseShasums(draftRelease);

    // Fetch latest version of release before verifying
    draftRelease = await getDraftRelease(pkgVersion, true);
    await validateReleaseAssets(draftRelease);
    // index.json goes live once uploaded so do these uploads as
    // late as possible to reduce the chances it contains a release
    // which fails to publish. It has to be done before the final
    // publish to ensure there aren't published releases not contained
    // in index.json, which causes other problems in downstream projects
    uploadIndexJson();
    await publishRelease(draftRelease);
    console.log(`${pass} SUCCESS!!! Release has been published. Please run ` +
      '"npm run publish-to-npm" to publish release to npm.');
  }
}

const SHASUM_256_FILENAME = 'SHASUMS256.txt';
const SHASUM_1_FILENAME = 'SHASUMS.txt';

async function verifyDraftGitHubReleaseAssets (release) {
  console.log('Fetching authenticated GitHub artifact URLs to verify shasums');

  const remoteFilesToHash = await Promise.all(release.assets.map(async asset => {
    const requestOptions = octokit.repos.getReleaseAsset.endpoint({
      owner: 'electron',
      repo: targetRepo,
      asset_id: asset.id,
      headers: {
        Accept: 'application/octet-stream'
      }
    });

    const { url, headers } = requestOptions;
    headers.authorization = `token ${process.env.ELECTRON_GITHUB_TOKEN}`;

    const response = await got(url, {
      followRedirect: false,
      method: 'HEAD',
      headers
    });

    return { url: response.headers.location, file: asset.name };
  })).catch(err => {
    console.error(`${fail} Error downloading files from GitHub`, err);
    process.exit(1);
  });

  await verifyShasumsForRemoteFiles(remoteFilesToHash);
}

async function getShaSumMappingFromUrl (shaSumFileUrl, fileNamePrefix) {
  const response = await got(shaSumFileUrl);
  const raw = response.body;
  return raw.split('\n').map(line => line.trim()).filter(Boolean).reduce((map, line) => {
    const [sha, file] = line.replace('  ', ' ').split(' ');
    map[file.slice(fileNamePrefix.length)] = sha;
    return map;
  }, {});
}

async function validateFileHashesAgainstShaSumMapping (remoteFilesWithHashes, mapping) {
  for (const remoteFileWithHash of remoteFilesWithHashes) {
    check(remoteFileWithHash.hash === mapping[remoteFileWithHash.file], `Release asset ${remoteFileWithHash.file} should have hash of ${mapping[remoteFileWithHash.file]} but found ${remoteFileWithHash.hash}`, true);
  }
}

async function verifyShasumsForRemoteFiles (remoteFilesToHash, filesAreNodeJSArtifacts = false) {
  console.log(`Generating SHAs for ${remoteFilesToHash.length} files to verify shasums`);

  // Only used for node.js artifact uploads
  const shaSum1File = remoteFilesToHash.find(({ file }) => file === SHASUM_1_FILENAME);
  // Used for both node.js artifact uploads and normal electron artifacts
  const shaSum256File = remoteFilesToHash.find(({ file }) => file === SHASUM_256_FILENAME);
  remoteFilesToHash = remoteFilesToHash.filter(({ file }) => file !== SHASUM_1_FILENAME && file !== SHASUM_256_FILENAME);

  const remoteFilesWithHashes = await Promise.all(remoteFilesToHash.map(async (file) => {
    return {
      hash: await getUrlHash(file.url, 'sha256'),
      ...file
    };
  }));

  await validateFileHashesAgainstShaSumMapping(remoteFilesWithHashes, await getShaSumMappingFromUrl(shaSum256File.url, filesAreNodeJSArtifacts ? '' : '*'));

  if (filesAreNodeJSArtifacts) {
    const remoteFilesWithSha1Hashes = await Promise.all(remoteFilesToHash.map(async (file) => {
      return {
        hash: await getUrlHash(file.url, 'sha1'),
        ...file
      };
    }));

    await validateFileHashesAgainstShaSumMapping(remoteFilesWithSha1Hashes, await getShaSumMappingFromUrl(shaSum1File.url, filesAreNodeJSArtifacts ? '' : '*'));
  }
}

makeRelease(args.validateRelease)
  .catch((err) => {
    console.error('Error occurred while making release:', err);
    process.exit(1);
  });
