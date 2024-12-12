import { app } from 'electron';

import { fork } from 'node:child_process';
import { on } from 'node:events';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { WPTRunner } from './runner/runner.mjs';

const serverPath = fileURLToPath(join(import.meta.url, '../server/server.mjs'));

app.whenReady().then(async () => {
  const child = fork(serverPath, [], {
    stdio: ['pipe', 'pipe', 'pipe', 'ipc']
  });

  child.stdout.pipe(process.stdout);
  child.stderr.pipe(process.stderr);
  child.on('exit', (code) => process.exit(code));

  for await (const [message] of on(child, 'message')) {
    if (message.server) {
      const runner = new WPTRunner('fetch', message.server);
      runner.run();
      runner.once('completion', () => {
        if (child.connected) {
          child.send('shutdown');
        }
      });
    }
  }
});
