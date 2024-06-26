import { expect } from 'chai';
import * as cp from 'node:child_process';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { ifit } from './lib/spec-helpers';

const fixturePath = path.resolve(__dirname, 'fixtures', 'crash-cases');

let children: cp.ChildProcessWithoutNullStreams[] = [];

const runFixtureAndEnsureCleanExit = async (args: string[]) => {
  let out = '';
  const child = cp.spawn(process.execPath, args);
  children.push(child);
  child.stdout.on('data', (chunk: Buffer) => {
    out += chunk.toString();
  });
  child.stderr.on('data', (chunk: Buffer) => {
    out += chunk.toString();
  });

  type CodeAndSignal = {code: number | null, signal: NodeJS.Signals | null};
  const { code, signal } = await new Promise<CodeAndSignal>((resolve) => {
    child.on('exit', (code, signal) => {
      resolve({ code, signal });
    });
  });
  if (code !== 0 || signal !== null) {
    console.error(out);
  }
  children = children.filter(c => c !== child);

  expect(signal).to.equal(null, 'exit signal should be null');
  expect(code).to.equal(0, 'should have exited with code 0');
};

const shouldRunCase = (crashCase: string) => {
  switch (crashCase) {
    // TODO(jkleinsc) fix this flaky test on Windows 32-bit
    case 'quit-on-crashed-event': {
      return (process.platform !== 'win32' || process.arch !== 'ia32');
    }
    // TODO(jkleinsc) fix this test on Linux on arm/arm64 and 32bit windows
    case 'js-execute-iframe': {
      if (process.platform === 'win32') {
        return process.arch !== 'ia32';
      } else {
        return (process.platform !== 'linux' || (process.arch !== 'arm64' && process.arch !== 'arm'));
      }
    }
    default: {
      return true;
    }
  }
};

describe('crash cases', () => {
  afterEach(() => {
    for (const child of children) {
      child.kill();
    }
    expect(children).to.have.lengthOf(0, 'all child processes should have exited cleanly');
    children.length = 0;
  });
  const cases = fs.readdirSync(fixturePath);

  for (const crashCase of cases) {
    ifit(shouldRunCase(crashCase))(`the "${crashCase}" case should not crash`, () => {
      const fixture = path.resolve(fixturePath, crashCase);
      const argsFile = path.resolve(fixture, 'electron.args');
      const args = [fixture];
      if (fs.existsSync(argsFile)) {
        args.push(...fs.readFileSync(argsFile, 'utf8').trim().split('\n'));
      }
      return runFixtureAndEnsureCleanExit(args);
    });
  }
});
