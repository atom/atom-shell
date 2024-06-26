#!/usr/bin/env node

const crypto = require('node:crypto');
const { GitProcess } = require('dugite');
const childProcess = require('node:child_process');
const { ESLint } = require('eslint');
const fs = require('node:fs');
const minimist = require('minimist');
const path = require('node:path');
const { getCodeBlocks } = require('@electron/lint-roller/dist/lib/markdown');

const { chunkFilenames, findMatchingFiles } = require('./lib/utils');

const ELECTRON_ROOT = path.normalize(path.dirname(__dirname));
const SOURCE_ROOT = path.resolve(ELECTRON_ROOT, '..');
const DEPOT_TOOLS = path.resolve(SOURCE_ROOT, 'third_party', 'depot_tools');

// Augment the PATH for this script so that we can find executables
// in the depot_tools folder even if folks do not have an instance of
// DEPOT_TOOLS in their path already
process.env.PATH = `${process.env.PATH}${path.delimiter}${DEPOT_TOOLS}`;

const IGNORELIST = new Set([
  ['shell', 'browser', 'resources', 'win', 'resource.h'],
  ['shell', 'common', 'node_includes.h'],
  ['spec', 'fixtures', 'pages', 'jquery-3.6.0.min.js']
].map(tokens => path.join(ELECTRON_ROOT, ...tokens)));

const IS_WINDOWS = process.platform === 'win32';

const CPPLINT_FILTERS = [
  // from presubmit_canned_checks.py OFF_BY_DEFAULT_LINT_FILTERS
  '-build/include',
  '-build/include_order',
  '-build/namespaces',
  '-readability/casting',
  '-runtime/int',
  '-whitespace/braces',
  // from presubmit_canned_checks.py OFF_UNLESS_MANUALLY_ENABLED_LINT_FILTERS
  '-build/c++11',
  '-build/header_guard',
  '-readability/todo',
  '-runtime/references',
  '-whitespace/braces',
  '-whitespace/comma',
  '-whitespace/end_of_line',
  '-whitespace/forcolon',
  '-whitespace/indent',
  '-whitespace/line_length',
  '-whitespace/newline',
  '-whitespace/operators',
  '-whitespace/parens',
  '-whitespace/semicolon',
  '-whitespace/tab'
];

function spawnAndCheckExitCode (cmd, args, opts) {
  opts = { stdio: 'inherit', ...opts };
  const { error, status, signal } = childProcess.spawnSync(cmd, args, opts);
  if (error) {
    // the subprocess failed or timed out
    console.error(error);
    process.exit(1);
  }
  if (status === null) {
    // the subprocess terminated due to a signal
    console.error(signal);
    process.exit(1);
  }
  if (status !== 0) {
    // `status` is an exit code
    process.exit(status);
  }
}

function cpplint (args) {
  args.unshift(`--root=${SOURCE_ROOT}`);
  const cmd = IS_WINDOWS ? 'cpplint.bat' : 'cpplint.py';
  const result = childProcess.spawnSync(cmd, args, { encoding: 'utf8', shell: true });
  // cpplint.py writes EVERYTHING to stderr, including status messages
  if (result.stderr) {
    for (const line of result.stderr.split(/[\r\n]+/)) {
      if (line.length && !line.startsWith('Done processing ') && line !== 'Total errors found: 0') {
        console.warn(line);
      }
    }
  }
  if (result.status !== 0) {
    if (result.error) console.error(result.error);
    process.exit(result.status || 1);
  }
}

function isObjCHeader (filename) {
  return /\/(mac|cocoa)\//.test(filename);
}

const LINTERS = [{
  key: 'cpp',
  roots: ['shell'],
  test: filename => filename.endsWith('.cc') || (filename.endsWith('.h') && !isObjCHeader(filename)),
  run: (opts, filenames) => {
    const clangFormatFlags = opts.fix ? ['--fix'] : [];
    for (const chunk of chunkFilenames(filenames)) {
      spawnAndCheckExitCode('python3', ['script/run-clang-format.py', ...clangFormatFlags, ...chunk]);
      cpplint([`--filter=${CPPLINT_FILTERS.join(',')}`, ...chunk]);
    }
  }
}, {
  key: 'objc',
  roots: ['shell'],
  test: filename => filename.endsWith('.mm') || (filename.endsWith('.h') && isObjCHeader(filename)),
  run: (opts, filenames) => {
    const clangFormatFlags = opts.fix ? ['--fix'] : [];
    spawnAndCheckExitCode('python3', ['script/run-clang-format.py', '-r', ...clangFormatFlags, ...filenames]);
    const filter = [...CPPLINT_FILTERS, '-readability/braces'];
    cpplint(['--extensions=mm,h', `--filter=${filter.join(',')}`, ...filenames]);
  }
}, {
  key: 'python',
  roots: ['script'],
  test: filename => filename.endsWith('.py'),
  run: (opts, filenames) => {
    const rcfile = path.join(DEPOT_TOOLS, 'pylintrc-2.17');
    const args = ['--rcfile=' + rcfile, ...filenames];
    const env = { PYTHONPATH: path.join(ELECTRON_ROOT, 'script'), ...process.env };
    spawnAndCheckExitCode('pylint-2.17', args, { env });
  }
}, {
  key: 'javascript',
  roots: ['build', 'default_app', 'lib', 'npm', 'script', 'spec'],
  ignoreRoots: ['spec/node_modules'],
  test: filename => filename.endsWith('.js') || filename.endsWith('.ts'),
  run: async (opts, filenames) => {
    const eslint = new ESLint({
      // Do not use the lint cache on CI builds
      cache: !process.env.CI,
      cacheLocation: `node_modules/.eslintcache.${crypto.createHash('md5').update(fs.readFileSync(__filename)).digest('hex')}`,
      extensions: ['.js', '.ts'],
      fix: opts.fix,
      overrideConfigFile: path.join(ELECTRON_ROOT, '.eslintrc.json'),
      resolvePluginsRelativeTo: ELECTRON_ROOT
    });
    const formatter = await eslint.loadFormatter();
    let successCount = 0;
    const results = await eslint.lintFiles(filenames);
    for (const result of results) {
      successCount += result.errorCount === 0 ? 1 : 0;
      if (opts.verbose && result.errorCount === 0 && result.warningCount === 0) {
        console.log(`${result.filePath}: no errors or warnings`);
      }
    }
    console.log(formatter.format(results));
    if (opts.fix) {
      await ESLint.outputFixes(results);
    }
    if (successCount !== filenames.length) {
      console.error('Linting had errors');
      process.exit(1);
    }
  }
}, {
  key: 'gn',
  roots: ['.'],
  test: filename => filename.endsWith('.gn') || filename.endsWith('.gni'),
  run: (opts, filenames) => {
    const allOk = filenames.map(filename => {
      const env = {
        CHROMIUM_BUILDTOOLS_PATH: path.resolve(ELECTRON_ROOT, '..', 'buildtools'),
        DEPOT_TOOLS_WIN_TOOLCHAIN: '0',
        ...process.env
      };
      // Users may not have depot_tools in PATH.
      env.PATH = `${env.PATH}${path.delimiter}${DEPOT_TOOLS}`;
      const args = ['format', filename];
      if (!opts.fix) args.push('--dry-run');
      const result = childProcess.spawnSync('gn', args, { env, stdio: 'inherit', shell: true });
      if (result.status === 0) {
        return true;
      } else if (result.status === 2) {
        console.log(`GN format errors in "${filename}". Run 'gn format "${filename}"' or rerun with --fix to fix them.`);
        return false;
      } else {
        console.log(`Error running 'gn format --dry-run "${filename}"': exit code ${result.status}`);
        return false;
      }
    }).every(x => x);
    if (!allOk) {
      process.exit(1);
    }
  }
}, {
  key: 'patches',
  roots: ['patches'],
  test: filename => filename.endsWith('.patch'),
  run: (opts, filenames) => {
    const patchesDir = path.resolve(__dirname, '../patches');
    const patchesConfig = path.resolve(patchesDir, 'config.json');
    // If the config does not exist, that's a problem
    if (!fs.existsSync(patchesConfig)) {
      console.error(`Patches config file: "${patchesConfig}" does not exist`);
      process.exit(1);
    }

    for (const target of JSON.parse(fs.readFileSync(patchesConfig, 'utf8'))) {
      // The directory the config points to should exist
      const targetPatchesDir = path.resolve(__dirname, '../../..', target.patch_dir);
      if (!fs.existsSync(targetPatchesDir)) {
        console.error(`target patch directory: "${targetPatchesDir}" does not exist`);
        process.exit(1);
      }
      // We need a .patches file
      const dotPatchesPath = path.resolve(targetPatchesDir, '.patches');
      if (!fs.existsSync(dotPatchesPath)) {
        console.error(`.patches file: "${dotPatchesPath}" does not exist`);
        process.exit(1);
      }

      // Read the patch list
      const patchFileList = fs.readFileSync(dotPatchesPath, 'utf8').trim().split('\n');
      const patchFileSet = new Set(patchFileList);
      patchFileList.reduce((seen, file) => {
        if (seen.has(file)) {
          console.error(`'${file}' is listed in ${dotPatchesPath} more than once`);
          process.exit(1);
        }
        return seen.add(file);
      }, new Set());

      if (patchFileList.length !== patchFileSet.size) {
        console.error('Each patch file should only be in the .patches file once');
        process.exit(1);
      }

      for (const file of fs.readdirSync(targetPatchesDir)) {
        // Ignore the .patches file and READMEs
        if (file === '.patches' || file === 'README.md') continue;

        if (!patchFileSet.has(file)) {
          console.error(`Expected the .patches file at "${dotPatchesPath}" to contain a patch file ("${file}") present in the directory but it did not`);
          process.exit(1);
        }
        patchFileSet.delete(file);
      }

      // If anything is left in this set, it means it did not exist on disk
      if (patchFileSet.size > 0) {
        console.error(`Expected all the patch files listed in the .patches file at "${dotPatchesPath}" to exist but some did not:\n${JSON.stringify([...patchFileSet.values()], null, 2)}`);
        process.exit(1);
      }
    }

    const allOk = filenames.length > 0 && filenames.map(f => {
      const patchText = fs.readFileSync(f, 'utf8');

      const regex = /Subject: (.*?)\n\n([\s\S]*?)\s*(?=diff)/ms;
      const subjectAndDescription = regex.exec(patchText);
      if (!subjectAndDescription?.[2]) {
        console.warn(`Patch file '${f}' has no description. Every patch must contain a justification for why the patch exists and the plan for its removal.`);
        return false;
      }
      const trailingWhitespaceLines = patchText.split(/\r?\n/).map((line, index) => [line, index]).filter(([line]) => line.startsWith('+') && /\s+$/.test(line)).map(([, lineNumber]) => lineNumber + 1);
      if (trailingWhitespaceLines.length > 0) {
        console.warn(`Patch file '${f}' has trailing whitespace on some lines (${trailingWhitespaceLines.join(',')}).`);
        return false;
      }
      return true;
    }).every(x => x);
    if (!allOk) {
      process.exit(1);
    }
  }
}, {
  key: 'md',
  roots: ['.'],
  ignoreRoots: ['node_modules', 'spec/node_modules'],
  test: filename => filename.endsWith('.md'),
  run: async (opts, filenames) => {
    let errors = false;

    // Run markdownlint on all Markdown files
    for (const chunk of chunkFilenames(filenames)) {
      spawnAndCheckExitCode('markdownlint-cli2', chunk);
    }

    // Run the remaining checks only in docs
    const docs = filenames.filter(filename => path.dirname(filename).split(path.sep)[0] === 'docs');

    for (const filename of docs) {
      const contents = fs.readFileSync(filename, 'utf8');
      const codeBlocks = await getCodeBlocks(contents);

      for (const codeBlock of codeBlocks) {
        const line = codeBlock.position.start.line;

        if (codeBlock.lang) {
          // Enforce all lowercase language identifiers
          if (codeBlock.lang.toLowerCase() !== codeBlock.lang) {
            console.log(`${filename}:${line} Code block language identifiers should be all lowercase`);
            errors = true;
          }

          // Prefer js/ts to javascript/typescript as the language identifier
          if (codeBlock.lang === 'javascript') {
            console.log(`${filename}:${line} Use 'js' as code block language identifier instead of 'javascript'`);
            errors = true;
          }

          if (codeBlock.lang === 'typescript') {
            console.log(`${filename}:${line} Use 'typescript' as code block language identifier instead of 'ts'`);
            errors = true;
          }

          // Enforce latest fiddle code block syntax
          if (codeBlock.lang === 'javascript' && codeBlock.meta && codeBlock.meta.includes('fiddle=')) {
            console.log(`${filename}:${line} Use 'fiddle' as code block language identifier instead of 'javascript fiddle='`);
            errors = true;
          }

          // Ensure non-empty content in fiddle code blocks matches the file content
          if (codeBlock.lang === 'fiddle' && codeBlock.value.trim() !== '') {
            // This is copied and adapted from the website repo:
            // https://github.com/electron/website/blob/62a55ca0dd14f97339e1a361b5418d2f11c34a75/src/transformers/fiddle-embedder.ts#L89C6-L101
            const parseFiddleEmbedOptions = (
              optStrings
            ) => {
              // If there are optional parameters, parse them out to pass to the getFiddleAST method.
              return optStrings.reduce((opts, option) => {
                // Use indexOf to support bizarre combinations like `|key=Myvalue=2` (which will properly
                // parse to {'key': 'Myvalue=2'})
                const firstEqual = option.indexOf('=');
                const key = option.slice(0, firstEqual);
                const value = option.slice(firstEqual + 1);
                return { ...opts, [key]: value };
              }, {});
            };

            const [dir, ...others] = codeBlock.meta.split('|');
            const options = parseFiddleEmbedOptions(others);

            const fiddleFilename = path.join(dir, options.focus || 'main.js');

            try {
              const fiddleContent = fs.readFileSync(fiddleFilename, 'utf8').trim();

              if (fiddleContent !== codeBlock.value.trim()) {
                console.log(`${filename}:${line} Content for fiddle code block differs from content in ${fiddleFilename}`);
                errors = true;
              }
            } catch (err) {
              console.error(`${filename}:${line} Error linting fiddle code block content`);
              if (err.stack) {
                console.error(err.stack);
              }
              errors = true;
            }
          }
        }
      }
    }

    if (errors) {
      process.exit(1);
    }
  }
}];

function parseCommandLine () {
  let help;
  const langs = ['cpp', 'objc', 'javascript', 'python', 'gn', 'patches', 'markdown'];
  const langRoots = langs.map(lang => lang + '-roots');
  const langIgnoreRoots = langs.map(lang => lang + '-ignore-roots');
  const opts = minimist(process.argv.slice(2), {
    boolean: [...langs, 'help', 'changed', 'fix', 'verbose', 'only'],
    alias: { cpp: ['c++', 'cc', 'cxx'], javascript: ['js', 'es'], python: 'py', markdown: 'md', changed: 'c', help: 'h', verbose: 'v' },
    string: [...langRoots, ...langIgnoreRoots],
    unknown: () => { help = true; }
  });
  if (help || opts.help) {
    const langFlags = langs.map(lang => `[--${lang}]`).join(' ');
    console.log(`Usage: script/lint.js ${langFlags} [-c|--changed] [-h|--help] [-v|--verbose] [--fix] [--only -- file1 file2]`);
    process.exit(0);
  }
  return opts;
}

function populateLinterWithArgs (linter, opts) {
  const extraRoots = opts[`${linter.key}-roots`];
  if (extraRoots) {
    linter.roots.push(...extraRoots.split(','));
  }
  const extraIgnoreRoots = opts[`${linter.key}-ignore-roots`];
  if (extraIgnoreRoots) {
    const list = extraIgnoreRoots.split(',');
    if (linter.ignoreRoots) {
      linter.ignoreRoots.push(...list);
    } else {
      linter.ignoreRoots = list;
    }
  }
}

async function findChangedFiles (top) {
  const result = await GitProcess.exec(['diff', '--name-only', '--cached'], top);
  if (result.exitCode !== 0) {
    console.log('Failed to find changed files', GitProcess.parseError(result.stderr));
    process.exit(1);
  }
  const relativePaths = result.stdout.split(/\r\n|\r|\n/g);
  const absolutePaths = relativePaths.map(x => path.join(top, x));
  return new Set(absolutePaths);
}

async function findFiles (args, linter) {
  let filenames = [];
  let includelist = null;

  // build the includelist
  if (args.changed) {
    includelist = await findChangedFiles(ELECTRON_ROOT);
    if (!includelist.size) {
      return [];
    }
  } else if (args.only) {
    includelist = new Set(args._.map(p => path.resolve(p)));
  }

  // accumulate the raw list of files
  for (const root of linter.roots) {
    const files = await findMatchingFiles(path.join(ELECTRON_ROOT, root), linter.test);
    filenames.push(...files);
  }

  for (const ignoreRoot of (linter.ignoreRoots) || []) {
    const ignorePath = path.join(ELECTRON_ROOT, ignoreRoot);
    if (!fs.existsSync(ignorePath)) continue;

    const ignoreFiles = new Set(await findMatchingFiles(ignorePath, linter.test));
    filenames = filenames.filter(fileName => !ignoreFiles.has(fileName));
  }

  // remove ignored files
  filenames = filenames.filter(x => !IGNORELIST.has(x));

  // if a includelist exists, remove anything not in it
  if (includelist) {
    filenames = filenames.filter(x => includelist.has(x));
  }

  // it's important that filenames be relative otherwise clang-format will
  // produce patches with absolute paths in them, which `git apply` will refuse
  // to apply.
  return filenames.map(x => path.relative(ELECTRON_ROOT, x));
}

async function main () {
  const opts = parseCommandLine();

  // no mode specified? run 'em all
  if (!opts.cpp && !opts.javascript && !opts.objc && !opts.python && !opts.gn && !opts.patches && !opts.markdown) {
    opts.cpp = opts.javascript = opts.objc = opts.python = opts.gn = opts.patches = opts.markdown = true;
  }

  const linters = LINTERS.filter(x => opts[x.key]);

  for (const linter of linters) {
    populateLinterWithArgs(linter, opts);
    const filenames = await findFiles(opts, linter);
    if (filenames.length) {
      if (opts.verbose) { console.log(`linting ${filenames.length} ${linter.key} ${filenames.length === 1 ? 'file' : 'files'}`); }
      await linter.run(opts, filenames);
    }
  }
}

if (require.main === module) {
  main().catch((error) => {
    console.error(error);
    process.exit(1);
  });
}
