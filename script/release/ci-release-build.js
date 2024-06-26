if (!process.env.CI) require('dotenv-safe').load();

const assert = require('node:assert');
const got = require('got');

const { Octokit } = require('@octokit/rest');
const octokit = new Octokit({
  auth: process.env.ELECTRON_GITHUB_TOKEN
});

const BUILD_APPVEYOR_URL = 'https://ci.appveyor.com/api/builds';
const GH_ACTIONS_PIPELINE_URL = 'https://github.com/electron/electron/actions';
const GH_ACTIONS_API_URL = '/repos/electron/electron/actions';

const GH_ACTIONS_WAIT_TIME = process.env.GH_ACTIONS_WAIT_TIME || 30000;

const appVeyorJobs = {
  'electron-x64': 'electron-x64-release',
  'electron-ia32': 'electron-ia32-release',
  'electron-woa': 'electron-woa-release'
};

const ghActionsPublishWorkflows = [
  'linux-publish',
  'macos-publish'
];

let jobRequestedCount = 0;

async function makeRequest ({ auth, username, password, url, headers, body, method }) {
  const clonedHeaders = {
    ...(headers || {})
  };
  if (auth?.bearer) {
    clonedHeaders.Authorization = `Bearer ${auth.bearer}`;
  }

  const options = {
    headers: clonedHeaders,
    body,
    method
  };

  if (username || password) {
    options.username = username;
    options.password = password;
  }

  const response = await got(url, options);

  if (response.statusCode < 200 || response.statusCode >= 300) {
    console.error('Error: ', `(status ${response.statusCode})`, response.body);
    throw new Error(`Unexpected status code ${response.statusCode} from ${url}`);
  }
  return JSON.parse(response.body);
}

async function githubActionsCall (targetBranch, workflowName, options) {
  console.log(`Triggering GitHub Actions to run build job: ${workflowName} on branch: ${targetBranch} with release flag.`);
  const buildRequest = {
    branch: targetBranch,
    parameters: {}
  };
  if (options.ghRelease) {
    buildRequest.parameters['upload-to-storage'] = '0';
  } else {
    buildRequest.parameters['upload-to-storage'] = '1';
  }
  buildRequest.parameters[`run-${workflowName}`] = true;

  jobRequestedCount++;
  try {
    const commits = await octokit.repos.listCommits({
      owner: 'electron',
      repo: 'electron',
      sha: targetBranch,
      per_page: 5
    });
    if (!commits.data.length) {
      console.error('Could not fetch most recent commits for GitHub Actions, returning early');
    }

    await octokit.request(`POST ${GH_ACTIONS_API_URL}/workflows/${workflowName}.yml/dispatches`, {
      ref: `refs/tags/${options.newVersion}`,
      inputs: {
        ...buildRequest.parameters
      },
      headers: {
        'X-GitHub-Api-Version': '2022-11-28'
      }
    });

    const runNumber = await getGitHubActionsRun(workflowName, commits.data[0].sha);
    if (runNumber === -1) {
      return;
    }

    console.log(`GitHub Actions release build pipeline ${runNumber} for ${workflowName} triggered.`);
    const runUrl = `${GH_ACTIONS_PIPELINE_URL}/runs/${runNumber}`;

    if (options.runningPublishWorkflows) {
      console.log(`GitHub Actions release workflow request for ${workflowName} successful.  Check ${runUrl} for status.`);
    } else {
      console.log(`GitHub Actions release build workflow running at ${GH_ACTIONS_PIPELINE_URL}/runs/${runNumber} for ${workflowName}.`);
      console.log(`GitHub Actions release build request for ${workflowName} successful.  Check ${runUrl} for status.`);
    }
  } catch (err) {
    console.log('Error calling GitHub Actions: ', err);
  }
}

async function getGitHubActionsRun (workflowId, headCommit) {
  let runNumber = 0;
  let actionRun;
  while (runNumber === 0) {
    const actionsRuns = await octokit.request(`GET ${GH_ACTIONS_API_URL}/workflows/${workflowId}.yml/runs`, {
      headers: {
        'X-GitHub-Api-Version': '2022-11-28'
      }
    });
    if (!actionsRuns.data.workflow_runs.length) {
      console.log(`No current workflow_runs found for ${workflowId}, response was: ${actionsRuns.data.workflow_runs}`);
      runNumber = -1;
      break;
    }

    for (const run of actionsRuns.data.workflow_runs) {
      if (run.head_sha === headCommit) {
        console.log(`GitHub Actions run ${run.html_url} found for ${headCommit}, waiting on status.`);
        actionRun = run;
        break;
      }
    }

    if (actionRun) {
      switch (actionRun.status) {
        case 'in_progress':
        case 'pending':
        case 'queued':
        case 'requested':
        case 'waiting': {
          if (actionRun.id && !isNaN(actionRun.id)) {
            console.log(`GitHub Actions run ${actionRun.status} for ${actionRun.html_url}.`);
            runNumber = actionRun.id;
          }
          break;
        }
        case 'action_required':
        case 'cancelled':
        case 'failure':
        case 'skipped':
        case 'timed_out':
        case 'failed': {
          console.log(`Error workflow run returned a status of ${actionRun.status} for ${actionRun.html_url}`);
          runNumber = -1;
          break;
        }
      }
      await new Promise(resolve => setTimeout(resolve, GH_ACTIONS_WAIT_TIME));
    }
  }
  return runNumber;
}

async function callAppVeyor (targetBranch, job, options) {
  console.log(`Triggering AppVeyor to run build job: ${job} on branch: ${targetBranch} with release flag.`);
  const environmentVariables = {
    ELECTRON_RELEASE: 1,
    APPVEYOR_BUILD_WORKER_CLOUD: 'electronhq-16-core'
  };

  if (!options.ghRelease) {
    environmentVariables.UPLOAD_TO_STORAGE = 1;
  }

  const requestOpts = {
    url: BUILD_APPVEYOR_URL,
    auth: {
      bearer: process.env.APPVEYOR_CLOUD_TOKEN
    },
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      accountName: 'electron-bot',
      projectSlug: appVeyorJobs[job],
      branch: targetBranch,
      commitId: options.commit || undefined,
      environmentVariables
    }),
    method: 'POST'
  };
  jobRequestedCount++;

  try {
    const { version } = await makeRequest(requestOpts, true);
    const buildUrl = `https://ci.appveyor.com/project/electron-bot/${appVeyorJobs[job]}/build/${version}`;
    console.log(`AppVeyor release build request for ${job} successful.  Check build status at ${buildUrl}`);
  } catch (err) {
    if (err.response?.body) {
      console.error('Could not call AppVeyor: ', {
        statusCode: err.response.statusCode,
        body: JSON.parse(err.response.body)
      });
    } else {
      console.error('Error calling AppVeyor:', err);
    }
  }
}

function buildAppVeyor (targetBranch, options) {
  const validJobs = Object.keys(appVeyorJobs);
  if (options.job) {
    assert(validJobs.includes(options.job), `Unknown AppVeyor CI job name: ${options.job}.  Valid values are: ${validJobs}.`);
    callAppVeyor(targetBranch, options.job, options);
  } else {
    for (const job of validJobs) {
      callAppVeyor(targetBranch, job, options);
    }
  }
}

function buildGHActions (targetBranch, options) {
  if (options.job) {
    assert(ghActionsPublishWorkflows.includes(options.job), `Unknown GitHub Actions workflow name: ${options.job}. Valid values are: ${ghActionsPublishWorkflows}.`);
    githubActionsCall(targetBranch, options.job, options);
  } else {
    assert(!options.arch, 'Cannot provide a single architecture while building all workflows, please specify a single workflow via --workflow');
    options.runningPublishWorkflows = true;
    for (const job of ghActionsPublishWorkflows) {
      githubActionsCall(targetBranch, job, options);
    }
  }
}

function runRelease (targetBranch, options) {
  if (options.ci) {
    switch (options.ci) {
      case 'GitHubActions': {
        buildGHActions(targetBranch, options);
        break;
      }
      case 'AppVeyor': {
        buildAppVeyor(targetBranch, options);
        break;
      }
      default: {
        console.log(`Error! Unknown CI: ${options.ci}.`);
        process.exit(1);
      }
    }
  } else {
    buildAppVeyor(targetBranch, options);
    buildGHActions(targetBranch, options);
  }
  console.log(`${jobRequestedCount} jobs were requested.`);
}

module.exports = runRelease;

if (require.main === module) {
  const args = require('minimist')(process.argv.slice(2), {
    boolean: ['ghRelease']
  });
  const targetBranch = args._[0];
  if (args._.length < 1) {
    console.log(`Trigger CI to build release builds of electron.
    Usage: ci-release-build.js [--job=CI_JOB_NAME] [--arch=INDIVIDUAL_ARCH] [--ci=AppVeyor|GitHubActions]
    [--ghRelease] [--appveyorJobId=xxx] [--commit=sha] TARGET_BRANCH
    `);
    process.exit(0);
  }
  runRelease(targetBranch, args);
}
