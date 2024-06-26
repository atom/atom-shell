#!/usr/bin/env node

'use strict';

const fs = require('node:fs');
const path = require('node:path');

const { GitProcess } = require('dugite');

const { Octokit } = require('@octokit/rest');
const octokit = new Octokit({
  auth: process.env.ELECTRON_GITHUB_TOKEN
});

const { ELECTRON_DIR } = require('../../lib/utils');

const MAX_FAIL_COUNT = 3;
const CHECK_INTERVAL = 5000;

const TROP_LOGIN = 'trop[bot]';

const NO_NOTES = 'No notes';

const docTypes = new Set(['doc', 'docs']);
const featTypes = new Set(['feat', 'feature']);
const fixTypes = new Set(['fix']);
const otherTypes = new Set(['spec', 'build', 'test', 'chore', 'deps', 'refactor', 'tools', 'perf', 'style', 'ci']);
const knownTypes = new Set([...docTypes.keys(), ...featTypes.keys(), ...fixTypes.keys(), ...otherTypes.keys()]);

const getCacheDir = () => process.env.NOTES_CACHE_PATH || path.resolve(__dirname, '.cache');

/**
***
**/

// link to a GitHub item, e.g. an issue or pull request
class GHKey {
  constructor (owner, repo, number) {
    this.owner = owner;
    this.repo = repo;
    this.number = number;
  }

  static NewFromPull (pull) {
    const owner = pull.base.repo.owner.login;
    const repo = pull.base.repo.name;
    const number = pull.number;
    return new GHKey(owner, repo, number);
  }
}

class Commit {
  constructor (hash, owner, repo) {
    this.hash = hash; // string
    this.owner = owner; // string
    this.repo = repo; // string

    this.isBreakingChange = false;
    this.note = null; // string

    // A set of branches to which this change has been merged.
    // '8-x-y' => GHKey { owner: 'electron', repo: 'electron', number: 23714 }
    this.trops = new Map(); // Map<string,GHKey>

    this.prKeys = new Set(); // GHKey
    this.revertHash = null; // string
    this.semanticType = null; // string
    this.subject = null; // string
  }
}

class Pool {
  constructor () {
    this.commits = []; // Array<Commit>
    this.processedHashes = new Set();
    this.pulls = {}; // GHKey.number => octokit pull object
  }
}

/**
***
**/

const runGit = async (dir, args) => {
  const response = await GitProcess.exec(args, dir);
  if (response.exitCode !== 0) {
    throw new Error(response.stderr.trim());
  }
  return response.stdout.trim();
};

const getCommonAncestor = async (dir, point1, point2) => {
  return runGit(dir, ['merge-base', point1, point2]);
};

const getNoteFromClerk = async (ghKey) => {
  const comments = await getComments(ghKey);
  if (!comments || !comments.data) return;

  const CLERK_LOGIN = 'release-clerk[bot]';
  const CLERK_NO_NOTES = '**No Release Notes**';
  const PERSIST_LEAD = '**Release Notes Persisted**';
  const QUOTE_LEAD = '> ';

  for (const comment of comments.data.reverse()) {
    if (comment.user.login !== CLERK_LOGIN) {
      continue;
    }
    if (comment.body === CLERK_NO_NOTES) {
      return NO_NOTES;
    }
    if (comment.body.startsWith(PERSIST_LEAD)) {
      let lines = comment.body
        .slice(PERSIST_LEAD.length).trim() // remove PERSIST_LEAD
        .split(/\r?\n/) // split into lines
        .map(line => line.trim())
        .map(line => line.replace('&lt;', '<'))
        .map(line => line.replace('&gt;', '>'))
        .filter(line => line.startsWith(QUOTE_LEAD)) // notes are quoted
        .map(line => line.slice(QUOTE_LEAD.length)); // unquote the lines

      const firstLine = lines.shift();
      // indent anything after the first line to ensure that
      // multiline notes with their own sub-lists don't get
      // parsed in the markdown as part of the top-level list
      // (example: https://github.com/electron/electron/pull/25216)
      lines = lines.map(line => '  ' + line);
      return [firstLine, ...lines]
        .join('\n') // join the lines
        .trim();
    }
  }

  console.warn(`WARN: no notes found in ${buildPullURL(ghKey)}`);
};

/**
 * Looks for our project's conventions in the commit message:
 *
 * 'semantic: some description' -- sets semanticType, subject
 * 'some description (#99999)' -- sets subject, pr
 * 'Merge pull request #99999 from ${branchname}' -- sets pr
 * 'This reverts commit ${sha}' -- sets revertHash
 * line starting with 'BREAKING CHANGE' in body -- sets isBreakingChange
 * 'Backport of #99999' -- sets pr
 */
const parseCommitMessage = (commitMessage, commit) => {
  const { owner, repo } = commit;

  // split commitMessage into subject & body
  let subject = commitMessage;
  let body = '';
  const pos = subject.indexOf('\n');
  if (pos !== -1) {
    body = subject.slice(pos).trim();
    subject = subject.slice(0, pos).trim();
  }

  // if the subject ends in ' (#dddd)', treat it as a pull request id
  let match;
  if ((match = subject.match(/^(.*)\s\(#(\d+)\)$/))) {
    commit.prKeys.add(new GHKey(owner, repo, parseInt(match[2])));
    subject = match[1];
  }

  // if the subject begins with 'word:', treat it as a semantic commit
  if ((match = subject.match(/^(\w+):\s(.*)$/))) {
    const semanticType = match[1].toLocaleLowerCase();
    if (knownTypes.has(semanticType)) {
      commit.semanticType = semanticType;
      subject = match[2];
    }
  }

  // Check for GitHub commit message that indicates a PR
  if ((match = subject.match(/^Merge pull request #(\d+) from (.*)$/))) {
    commit.prKeys.add(new GHKey(owner, repo, parseInt(match[1])));
  }

  // Check for a comment that indicates a PR
  const backportPattern = /(?:^|\n)(?:manual |manually )?backport.*(?:#(\d+)|\/pull\/(\d+))/im;
  if ((match = commitMessage.match(backportPattern))) {
    // This might be the first or second capture group depending on if it's a link or not.
    const backportNumber = match[1] ? parseInt(match[1], 10) : parseInt(match[2], 10);
    commit.prKeys.add(new GHKey(owner, repo, backportNumber));
  }

  // https://help.github.com/articles/closing-issues-using-keywords/
  if (body.match(/\b(?:close|closes|closed|fix|fixes|fixed|resolve|resolves|resolved|for)\s#(\d+)\b/i)) {
    commit.semanticType = commit.semanticType || 'fix';
  }

  // https://www.conventionalcommits.org/en
  if (commitMessage
    .split(/\r?\n/) // split into lines
    .map(line => line.trim())
    .some(line => line.startsWith('BREAKING CHANGE'))) {
    commit.isBreakingChange = true;
  }

  // Check for a reversion commit
  if ((match = body.match(/This reverts commit ([a-f0-9]{40})\./))) {
    commit.revertHash = match[1];
  }

  commit.subject = subject.trim();
  return commit;
};

const parsePullText = (pull, commit) => parseCommitMessage(`${pull.data.title}\n\n${pull.data.body}`, commit);

const getLocalCommitHashes = async (dir, ref) => {
  const args = ['log', '--format=%H', ref];
  return (await runGit(dir, args))
    .split(/\r?\n/) // split into lines
    .map(hash => hash.trim());
};

// return an array of Commits
const getLocalCommits = async (module, point1, point2) => {
  const { owner, repo, dir } = module;

  const fieldSep = ',';
  const format = ['%H', '%s'].join(fieldSep);
  const args = ['log', '--cherry-pick', '--right-only', '--first-parent', `--format=${format}`, `${point1}..${point2}`];
  const logs = (await runGit(dir, args))
    .split(/\r?\n/) // split into lines
    .map(field => field.trim());

  const commits = [];
  for (const log of logs) {
    if (!log) {
      continue;
    }
    const [hash, subject] = log.split(fieldSep, 2).map(field => field.trim());
    commits.push(parseCommitMessage(subject, new Commit(hash, owner, repo)));
  }
  return commits;
};

const checkCache = async (name, operation) => {
  const filename = path.resolve(getCacheDir(), name);
  if (fs.existsSync(filename)) {
    return JSON.parse(fs.readFileSync(filename, 'utf8'));
  }
  process.stdout.write('.');
  const response = await operation();
  if (response) {
    fs.writeFileSync(filename, JSON.stringify(response));
  }
  return response;
};

// helper function to add some resiliency to volatile GH api endpoints
async function runRetryable (fn, maxRetries) {
  let lastError;
  for (let i = 0; i < maxRetries; i++) {
    try {
      return await fn();
    } catch (error) {
      await new Promise(resolve => setTimeout(resolve, CHECK_INTERVAL));
      lastError = error;
    }
  }
  // Silently eat 404s.
  // Silently eat 422s, which come from "No commit found for SHA"
  if (lastError.status !== 404 && lastError.status !== 422) throw lastError;
}

const getPullCacheFilename = ghKey => `${ghKey.owner}-${ghKey.repo}-pull-${ghKey.number}`;

const getCommitPulls = async (owner, repo, hash) => {
  const name = `${owner}-${repo}-commit-${hash}`;
  const retryableFunc = () => octokit.repos.listPullRequestsAssociatedWithCommit({ owner, repo, commit_sha: hash });
  let ret = await checkCache(name, () => runRetryable(retryableFunc, MAX_FAIL_COUNT));

  // only merged pulls belong in release notes
  if (ret && ret.data) {
    ret.data = ret.data.filter(pull => pull.merged_at);
  }

  // cache the pulls
  if (ret && ret.data) {
    for (const pull of ret.data) {
      const cachefile = getPullCacheFilename(GHKey.NewFromPull(pull));
      const payload = { ...ret, data: pull };
      await checkCache(cachefile, () => payload);
    }
  }

  // ensure the return value has the expected structure, even on failure
  if (!ret || !ret.data) {
    ret = { data: [] };
  }

  return ret;
};

const getPullRequest = async (ghKey) => {
  const { number, owner, repo } = ghKey;
  const name = getPullCacheFilename(ghKey);
  const retryableFunc = () => octokit.pulls.get({ pull_number: number, owner, repo });
  return checkCache(name, () => runRetryable(retryableFunc, MAX_FAIL_COUNT));
};

const getComments = async (ghKey) => {
  const { number, owner, repo } = ghKey;
  const name = `${owner}-${repo}-issue-${number}-comments`;
  const retryableFunc = () => octokit.issues.listComments({ issue_number: number, owner, repo, per_page: 100 });
  return checkCache(name, () => runRetryable(retryableFunc, MAX_FAIL_COUNT));
};

const addRepoToPool = async (pool, repo, from, to) => {
  const commonAncestor = await getCommonAncestor(repo.dir, from, to);

  // mark the old branch's commits as old news
  for (const oldHash of await getLocalCommitHashes(repo.dir, from)) {
    pool.processedHashes.add(oldHash);
  }

  // get the new branch's commits and the pulls associated with them
  const commits = await getLocalCommits(repo, commonAncestor, to);
  for (const commit of commits) {
    const { owner, repo, hash } = commit;
    for (const pull of (await getCommitPulls(owner, repo, hash)).data) {
      commit.prKeys.add(GHKey.NewFromPull(pull));
    }
  }

  pool.commits.push(...commits);

  // add the pulls
  for (const commit of commits) {
    let prKey;
    for (prKey of commit.prKeys.values()) {
      const pull = await getPullRequest(prKey);
      if (!pull || !pull.data) continue; // couldn't get it
      pool.pulls[prKey.number] = pull;
      parsePullText(pull, commit);
    }
  }
};

// @return Map<string,GHKey>
//   where the key is a branch name (e.g. '7-1-x' or '8-x-y')
//   and the value is a GHKey to the PR
async function getMergedTrops (commit, pool) {
  const branches = new Map();

  for (const prKey of commit.prKeys.values()) {
    const pull = pool.pulls[prKey.number];
    const mergedBranches = new Set(
      ((pull && pull.data && pull.data.labels) ? pull.data.labels : [])
        .map(label => ((label && label.name) ? label.name : '').match(/merged\/([0-9]+-[x0-9]-[xy0-9])/))
        .filter(match => match)
        .map(match => match[1])
    );

    if (mergedBranches.size > 0) {
      const isTropComment = (comment) => comment && comment.user && comment.user.login === TROP_LOGIN;

      const ghKey = GHKey.NewFromPull(pull.data);
      const backportRegex = /backported this PR to "(.*)",\s+please check out #(\d+)/;
      const getBranchNameAndPullKey = (comment) => {
        const match = ((comment && comment.body) ? comment.body : '').match(backportRegex);
        return match ? [match[1], new GHKey(ghKey.owner, ghKey.repo, parseInt(match[2]))] : null;
      };

      const comments = await getComments(ghKey);
      ((comments && comments.data) ? comments.data : [])
        .filter(isTropComment)
        .map(getBranchNameAndPullKey)
        .filter(pair => pair)
        .filter(([branch]) => mergedBranches.has(branch))
        .forEach(([branch, key]) => branches.set(branch, key));
    }
  }

  return branches;
}

// @return the shorthand name of the branch that `ref` is on,
//   e.g. a ref of '10.0.0-beta.1' will return '10-x-y'
async function getBranchNameOfRef (ref, dir) {
  return (await runGit(dir, ['branch', '--all', '--contains', ref, '--sort', 'version:refname']))
    .split(/\r?\n/) // split into lines
    .shift() // we sorted by refname and want the first result
    .match(/(?:\s?\*\s){0,1}(.*)/)[1] // if present, remove leading '* ' in case we're currently in that branch
    .match(/(?:.*\/)?(.*)/)[1] // 'remote/origins/10-x-y' -> '10-x-y'
    .trim();
}

/***
****  Main
***/

const getNotes = async (fromRef, toRef, newVersion) => {
  const cacheDir = getCacheDir();
  if (!fs.existsSync(cacheDir)) {
    fs.mkdirSync(cacheDir);
  }

  const pool = new Pool();
  const toBranch = await getBranchNameOfRef(toRef, ELECTRON_DIR);

  console.log(`Generating release notes between '${fromRef}' and '${toRef}' for version '${newVersion}' in branch '${toBranch}'`);

  // get the electron/electron commits
  const electron = { owner: 'electron', repo: 'electron', dir: ELECTRON_DIR };
  await addRepoToPool(pool, electron, fromRef, toRef);

  // remove any old commits
  pool.commits = pool.commits.filter(commit => !pool.processedHashes.has(commit.hash));

  // if a commit _and_ revert occurred in the unprocessed set, skip them both
  for (const commit of pool.commits) {
    const revertHash = commit.revertHash;
    if (!revertHash) {
      continue;
    }

    const revert = pool.commits.find(commit => commit.hash === revertHash);
    if (!revert) {
      continue;
    }

    commit.note = NO_NOTES;
    revert.note = NO_NOTES;
    pool.processedHashes.add(commit.hash);
    pool.processedHashes.add(revertHash);
  }

  // ensure the commit has a note
  for (const commit of pool.commits) {
    for (const prKey of commit.prKeys.values()) {
      if (commit.note) {
        break;
      }
      commit.note = await getNoteFromClerk(prKey);
    }
  }

  // remove non-user-facing commits
  pool.commits = pool.commits
    .filter(commit => commit && commit.note)
    .filter(commit => commit.note !== NO_NOTES)
    .filter(commit => commit.note.match(/^[Bb]ump v\d+\.\d+\.\d+/) === null);

  for (const commit of pool.commits) {
    commit.trops = await getMergedTrops(commit, pool);
  }

  pool.commits = removeSupercededStackUpdates(pool.commits);

  const notes = {
    breaking: [],
    docs: [],
    feat: [],
    fix: [],
    other: [],
    unknown: [],
    name: newVersion,
    toBranch
  };

  for (const commit of pool.commits) {
    const str = commit.semanticType;
    if (commit.isBreakingChange) {
      notes.breaking.push(commit);
    } else if (!str) {
      notes.unknown.push(commit);
    } else if (docTypes.has(str)) {
      notes.docs.push(commit);
    } else if (featTypes.has(str)) {
      notes.feat.push(commit);
    } else if (fixTypes.has(str)) {
      notes.fix.push(commit);
    } else if (otherTypes.has(str)) {
      notes.other.push(commit);
    } else {
      notes.unknown.push(commit);
    }
  }

  return notes;
};

const compareVersions = (v1, v2) => {
  const [split1, split2] = [v1.split('.'), v2.split('.')];

  if (split1.length !== split2.length) {
    throw new Error(`Expected version strings to have same number of sections: ${split1} and ${split2}`);
  }
  for (let i = 0; i < split1.length; i++) {
    const p1 = parseInt(split1[i], 10);
    const p2 = parseInt(split2[i], 10);

    if (p1 > p2) return 1;
    else if (p1 < p2) return -1;
    // Continue checking the value if this portion is equal
  }

  return 0;
};

const removeSupercededStackUpdates = (commits) => {
  const updateRegex = /^Updated ([a-zA-Z.]+) to v?([\d.]+)/;
  const notupdates = [];

  const newest = {};
  for (const commit of commits) {
    const match = (commit.note || commit.subject).match(updateRegex);
    if (!match) {
      notupdates.push(commit);
      continue;
    }

    const [, dep, version] = match;
    if (!newest[dep] || compareVersions(version, newest[dep].version) > 0) {
      newest[dep] = { commit, version };
    }
  }

  return [...notupdates, ...Object.values(newest).map(o => o.commit)];
};

/***
****  Render
***/

// @return the pull request's GitHub URL
const buildPullURL = ghKey => `https://github.com/${ghKey.owner}/${ghKey.repo}/pull/${ghKey.number}`;

const renderPull = ghKey => `[#${ghKey.number}](${buildPullURL(ghKey)})`;

// @return the commit's GitHub URL
const buildCommitURL = commit => `https://github.com/${commit.owner}/${commit.repo}/commit/${commit.hash}`;

const renderCommit = commit => `[${commit.hash.slice(0, 8)}](${buildCommitURL(commit)})`;

// @return a markdown link to the PR if available; otherwise, the git commit
function renderLink (commit) {
  const maybePull = commit.prKeys.values().next();
  return maybePull.value ? renderPull(maybePull.value) : renderCommit(commit);
}

// @return a terser branch name,
//   e.g. '7-2-x' -> '7.2' and '8-x-y' -> '8'
const renderBranchName = name => name.replace(/-[a-zA-Z]/g, '').replace('-', '.');

const renderTrop = (branch, ghKey) => `[${renderBranchName(branch)}](${buildPullURL(ghKey)})`;

// @return markdown-formatted links to other branches' trops,
//   e.g. "(Also in 7.2, 8, 9)"
function renderTrops (commit, excludeBranch) {
  const body = [...commit.trops.entries()]
    .filter(([branch]) => branch !== excludeBranch)
    .sort(([branchA], [branchB]) => parseInt(branchA) - parseInt(branchB)) // sort by semver major
    .map(([branch, key]) => renderTrop(branch, key))
    .join(', ');
  return body ? `<span style="font-size:small;">(Also in ${body})</span>` : body;
}

// @return a slightly cleaned-up human-readable change description
function renderDescription (commit) {
  let note = commit.note || commit.subject || '';
  note = note.trim();

  // release notes bullet point every change, so if the note author
  // manually started the content with a bullet point, that will confuse
  // the markdown renderer -- remove the redundant bullet point
  // (example: https://github.com/electron/electron/pull/25216)
  if (note.startsWith('*')) {
    note = note.slice(1).trim();
  }

  if (note.length !== 0) {
    note = note[0].toUpperCase() + note.substr(1);

    if (!note.endsWith('.')) {
      note = note + '.';
    }

    const commonVerbs = {
      Added: ['Add'],
      Backported: ['Backport'],
      Cleaned: ['Clean'],
      Disabled: ['Disable'],
      Ensured: ['Ensure'],
      Exported: ['Export'],
      Fixed: ['Fix', 'Fixes'],
      Handled: ['Handle'],
      Improved: ['Improve'],
      Made: ['Make'],
      Removed: ['Remove'],
      Repaired: ['Repair'],
      Reverted: ['Revert'],
      Stopped: ['Stop'],
      Updated: ['Update'],
      Upgraded: ['Upgrade']
    };
    for (const [key, values] of Object.entries(commonVerbs)) {
      for (const value of values) {
        const start = `${value} `;
        if (note.startsWith(start)) {
          note = `${key} ${note.slice(start.length)}`;
        }
      }
    }
  }

  return note;
}

// @return markdown-formatted release note line item,
//   e.g. '* Fixed a foo. #12345 (Also in 7.2, 8, 9)'
const renderNote = (commit, excludeBranch) =>
  `* ${renderDescription(commit)} ${renderLink(commit)} ${renderTrops(commit, excludeBranch)}\n`;

const renderNotes = (notes, unique = false) => {
  const rendered = [`# Release Notes for ${notes.name}\n\n`];

  const renderSection = (title, commits, unique) => {
    if (unique) {
      // omit changes that also landed in other branches
      commits = commits.filter((commit) => renderTrops(commit, notes.toBranch).length === 0);
    }
    if (commits.length > 0) {
      rendered.push(
        `## ${title}\n\n`,
        ...(commits.map(commit => renderNote(commit, notes.toBranch)).sort())
      );
    }
  };

  renderSection('Breaking Changes', notes.breaking, unique);
  renderSection('Features', notes.feat, unique);
  renderSection('Fixes', notes.fix, unique);
  renderSection('Other Changes', notes.other, unique);

  if (notes.docs.length) {
    const docs = notes.docs.map(commit => renderLink(commit)).sort();
    rendered.push('## Documentation\n\n', ` * Documentation changes: ${docs.join(', ')}\n`, '\n');
  }

  renderSection('Unknown', notes.unknown, unique);

  return rendered.join('');
};

/***
****  Module
***/

module.exports = {
  get: getNotes,
  render: renderNotes
};
