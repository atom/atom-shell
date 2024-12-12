import { utilityProcess } from 'electron';

import { EventEmitter, once } from 'node:events';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { isAbsolute, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { colors, handlePipes, normalizeName, parseMeta, resolveStatusPath } from './util.mjs';

const basePath = fileURLToPath(join(import.meta.url, '../..'));
const testPath = process.env.WPT_DIR;
const statusPath = join(basePath, 'status');

// https://github.com/web-platform-tests/wpt/blob/b24eedd/resources/testharness.js#L3705
function sanitizeUnpairedSurrogates (str) {
  return str.replace(
    /([\ud800-\udbff]+)(?![\udc00-\udfff])|(^|[^\ud800-\udbff])([\udc00-\udfff]+)/g,
    function (_, low, prefix, high) {
      let output = prefix || ''; // Prefix may be undefined
      const string = low || high; // Only one of these alternates can match
      for (let i = 0; i < string.length; i++) {
        output += codeUnitStr(string[i]);
      }
      return output;
    });
}

function codeUnitStr (char) {
  return 'U+' + char.charCodeAt(0).toString(16);
}

export class WPTRunner extends EventEmitter {
  /** @type {string} */
  #folderName;

  /** @type {string} */
  #folderPath;

  /** @type {string[]} */
  #files = [];

  /** @type {string[]} */
  #initScripts = [];

  /** @type {string} */
  #url;

  /** @type {import('../../status/fetch.status.json')} */
  #status;

  /** Tests that have expectedly failed mapped by file name */
  #statusOutput = {};

  #uncaughtExceptions = [];

  #stats = {
    completedTests: 0,
    failedTests: 0,
    passedTests: 0,
    expectedFailures: 0,
    failedFiles: 0,
    passedFiles: 0,
    skippedFiles: 0
  };

  constructor (folder, url) {
    super();

    this.#folderName = folder;
    this.#folderPath = join(testPath, folder);
    this.#files.push(
      ...WPTRunner.walk(
        this.#folderPath,
        (file) => file.endsWith('.any.js')
      )
    );

    this.#status = JSON.parse(readFileSync(join(statusPath, `${folder}.status.json`)));
    this.#url = url;

    if (this.#files.length === 0) {
      queueMicrotask(() => {
        this.emit('completion');
      });
    }

    this.once('completion', () => {
      for (const { error, test } of this.#uncaughtExceptions) {
        console.log(colors(`Uncaught exception in "${test}":`, 'red'));
        console.log(colors(`${error.stack}`, 'red'));
        console.log('='.repeat(96));
      }
    });
  }

  static walk (dir, fn) {
    const ini = new Set(readdirSync(dir));
    const files = new Set();

    while (ini.size !== 0) {
      for (const d of ini) {
        const path = resolve(dir, d);
        ini.delete(d); // remove from set
        const stats = statSync(path);

        if (stats.isDirectory()) {
          for (const f of readdirSync(path)) {
            ini.add(resolve(path, f));
          }
        } else if (stats.isFile() && fn(d)) {
          files.add(path);
        }
      }
    }

    return [...files].sort();
  }

  async run () {
    const workerPath = fileURLToPath(join(import.meta.url, '../worker.mjs'));
    /** @type {Set<Worker>} */
    const activeWorkers = new Set();
    let finishedFiles = 1;
    let total = this.#files.length;

    const files = this.#files.map((test) => {
      const code = test.includes('.sub.')
        ? handlePipes(readFileSync(test, 'utf-8'), this.#url)
        : readFileSync(test, 'utf-8');
      const meta = this.resolveMeta(code, test);

      if (meta.variant.length) {
        total += meta.variant.length - 1;
      }

      return [test, code, meta];
    });

    console.log('='.repeat(96));

    for (const [test, code, meta] of files) {
      console.log(`Started ${test}`);

      const status = resolveStatusPath(test, this.#status);

      if (status.file.skip || status.topLevel.skip) {
        this.#stats.skippedFiles += 1;

        console.log(colors(`[${finishedFiles}/${total}] SKIPPED - ${test}`, 'yellow'));
        console.log('='.repeat(96));

        finishedFiles++;
        continue;
      }

      const start = performance.now();

      for (const variant of meta.variant.length ? meta.variant : ['']) {
        const url = new URL(this.#url);
        if (variant) {
          url.search = variant;
        }
        const worker = new utilityProcess.fork(workerPath, [], {
          stdio: 'pipe'
        });
        await once(worker, 'spawn');
        worker.postMessage({
          workerData: {
            // The test file.
            test: code,
            // Parsed META tag information
            meta,
            url: url.href,
            path: test
          }
        });

        worker.stdout.pipe(process.stdout);
        worker.stderr.pipe(process.stderr);

        const fileUrl = new URL(`/${this.#folderName}${test.slice(this.#folderPath.length)}`, 'http://wpt');
        fileUrl.pathname = fileUrl.pathname.replace(/\.js$/, '.html');
        fileUrl.search = variant;
        const result = {
          test: fileUrl.href.slice(fileUrl.origin.length),
          subtests: [],
          status: ''
        };

        activeWorkers.add(worker);
        // These values come directly from the web-platform-tests
        const timeout = meta.timeout === 'long' ? 60_000 : 10_000;

        worker.on('message', (message) => {
          if (message.type === 'result') {
            this.handleIndividualTestCompletion(message, status, test, meta, result);
          } else if (message.type === 'completion') {
            this.handleTestCompletion(worker, status, test);
          } else if (message.type === 'error') {
            this.#uncaughtExceptions.push({ error: message.error, test });
            this.#stats.failedTests += 1;
            this.#stats.passedTests -= 1;
          }
        });

        try {
          await once(worker, 'exit', {
            signal: AbortSignal.timeout(timeout)
          });

          if (result.subtests.some((subtest) => subtest?.isExpectedFailure === false)) {
            this.#stats.failedFiles += 1;
            console.log(colors(`[${finishedFiles}/${total}] FAILED - ${test}`, 'red'));
          } else {
            this.#stats.passedFiles += 1;
            console.log(colors(`[${finishedFiles}/${total}] PASSED - ${test}`, 'green'));
          }

          if (variant) console.log('Variant:', variant);
          console.log(`File took ${(performance.now() - start).toFixed(2)}ms`);
          console.log('='.repeat(96));
        } catch (_) {
          // If the worker is terminated by the timeout signal, the test is marked as failed
          this.#stats.failedFiles += 1;
          console.log(colors(`[${finishedFiles}/${total}] FAILED - ${test}`, 'red'));

          if (variant) console.log('Variant:', variant);
          console.log(`File timed out after ${timeout}ms`);
          console.log('='.repeat(96));
        } finally {
          finishedFiles++;
          activeWorkers.delete(worker);
        }
      }
    }

    this.handleRunnerCompletion();
  }

  /**
   * Called after a test has succeeded or failed.
   */
  handleIndividualTestCompletion (message, status, path, meta, wptResult) {
    this.#stats.completedTests += 1;

    const { file, topLevel } = status;
    const isFailure = message.result.status === 1;

    const testResult = {
      status: isFailure ? 'FAIL' : 'PASS',
      name: sanitizeUnpairedSurrogates(message.result.name)
    };

    if (isFailure) {
      let isExpectedFailure = false;
      this.#stats.failedTests += 1;

      const name = normalizeName(message.result.name);
      const sanitizedMessage = sanitizeUnpairedSurrogates(message.result.message);

      if (file.flaky?.includes(name)) {
        isExpectedFailure = true;
        this.#stats.expectedFailures += 1;
        wptResult?.subtests.push({ ...testResult, message: sanitizedMessage, isExpectedFailure });
      } else if (file.allowUnexpectedFailures || topLevel.allowUnexpectedFailures || file.fail?.includes(name)) {
        if (!file.allowUnexpectedFailures && !topLevel.allowUnexpectedFailures) {
          if (Array.isArray(file.fail)) {
            this.#statusOutput[path] ??= [];
            this.#statusOutput[path].push(name);
          }
        }

        isExpectedFailure = true;
        this.#stats.expectedFailures += 1;
        wptResult?.subtests.push({ ...testResult, message: sanitizedMessage, isExpectedFailure });
      } else {
        wptResult?.subtests.push({ ...testResult, message: sanitizedMessage, isExpectedFailure });
        process.exitCode = 1;
        console.error(message.result);
      }
      if (!isExpectedFailure) {
        process._rawDebug(`Failed test: ${path}`);
      }
    } else {
      this.#stats.passedTests += 1;
      wptResult?.subtests.push(testResult);
    }
  }

  /**
   * Called after all the tests in a worker are completed.
   * @param {Worker} worker
   */
  handleTestCompletion (worker, status, path) {
    worker.kill();

    const { file } = status;
    const hasExpectedFailures = !!file.fail;
    const testHasFailures = !!this.#statusOutput?.[path];
    const failed = this.#statusOutput?.[path] ?? [];

    if (hasExpectedFailures !== testHasFailures) {
      console.log({ expected: file.fail, failed });

      if (failed.length === 0) {
        console.log(colors('Tests are marked as failure but did not fail, yay!', 'red'));
      } else if (!hasExpectedFailures) {
        console.log(colors('Test failed but there were no expected errors.', 'red'));
      }

      process.exitCode = 1;
    } else if (hasExpectedFailures && testHasFailures) {
      const diff = [
        ...file.fail.filter(x => !failed.includes(x)),
        ...failed.filter(x => !file.fail.includes(x))
      ];

      if (diff.length) {
        console.log({ diff });
        console.log(colors('Expected failures did not match actual failures', 'red'));
        process.exitCode = 1;
      }
    }
  }

  /**
   * Called after every test has completed.
   */
  handleRunnerCompletion () {
    // tests that failed
    if (Object.keys(this.#statusOutput).length !== 0) {
      console.log(this.#statusOutput);
    }

    this.emit('completion');

    const { passedFiles, failedFiles, skippedFiles } = this.#stats;
    console.log(
      `File results for folder [${this.#folderName}]: ` +
      `completed: ${this.#files.length}, passed: ${passedFiles}, failed: ${failedFiles}, ` +
      `skipped: ${skippedFiles}`
    );

    const { completedTests, failedTests, passedTests, expectedFailures } = this.#stats;
    console.log(
      `Test results for folder [${this.#folderName}]: ` +
      `completed: ${completedTests}, failed: ${failedTests}, passed: ${passedTests}, ` +
      `expected failures: ${expectedFailures}, ` +
      `unexpected failures: ${failedTests - expectedFailures}`
    );

    process.exit(failedTests - expectedFailures ? 1 : process.exitCode);
  }

  /**
   * Parses META tags and resolves any script file paths.
   * @param {string} code
   * @param {string} path The absolute path of the test
   */
  resolveMeta (code, path) {
    const meta = parseMeta(code);
    const scripts = meta.scripts.map((filePath) => {
      let content = '';

      if (filePath === '/resources/WebIDLParser.js') {
        // See https://github.com/web-platform-tests/wpt/pull/731
        return readFileSync(join(testPath, '/resources/webidl2/lib/webidl2.js'), 'utf-8');
      } else if (isAbsolute(filePath)) {
        content = readFileSync(join(testPath, filePath), 'utf-8');
      } else {
        content = readFileSync(resolve(path, '..', filePath), 'utf-8');
      }

      // If the file has any built-in pipes.
      if (filePath.includes('.sub.')) {
        content = handlePipes(content, this.#url);
      }

      return content;
    });

    return {
      ...meta,
      resourcePaths: meta.scripts,
      scripts
    };
  }
}
