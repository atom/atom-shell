import { once } from 'node:events';
import { createReadStream, readFileSync, existsSync } from 'node:fs';
import { createServer } from 'node:http';
import { join } from 'node:path';
import process from 'node:process';
import { setTimeout as sleep } from 'node:timers/promises';

import { symbols } from './constants.mjs';
import { route as redirectRoute } from './routes/redirect.mjs';

const tests = process.env.WPT_DIR;

// https://web-platform-tests.org/tools/wptserve/docs/stash.html
class Stash extends Map {
  take (key) {
    if (this.has(key)) {
      const value = this.get(key);

      this.delete(key);
      return value.value;
    }
  }

  put (key, value, path) {
    this.set(key, { value, path });
  }
}

const stash = new Stash();

const server = createServer(async (req, res) => {
  const fullUrl = new URL(req.url, `http://localhost:${server.address().port}`);

  switch (fullUrl.pathname) {
    case '/fetch/content-encoding/resources/big.text.gz':
    case '/fetch/content-encoding/resources/foo.octetstream.gz':
    case '/fetch/content-encoding/resources/foo.text.gz':
    case '/fetch/api/resources/cors-top.txt':
    case '/fetch/api/resources/top.txt':
    case '/fetch/data-urls/resources/base64.json':
    case '/fetch/data-urls/resources/data-urls.json':
    case '/fetch/api/resources/empty.txt':
    case '/fetch/api/resources/data.json': {
      // If this specific resources requires custom headers
      const customHeadersPath = join(tests, fullUrl.pathname + '.headers');
      if (existsSync(customHeadersPath)) {
        const headers = readFileSync(customHeadersPath, 'utf-8')
          .trim()
          .split(/\r?\n/g)
          .map((h) => h.split(': '));

        for (const [key, value] of headers) {
          if (!key || !value) {
            console.warn(`Skipping ${key}:${value} header pair`);
            continue;
          }
          res.setHeader(key, value);
        }
      }

      // https://github.com/web-platform-tests/wpt/blob/6ae3f702a332e8399fab778c831db6b7dca3f1c6/fetch/api/resources/data.json
      createReadStream(join(tests, fullUrl.pathname))
        .on('end', () => res.end())
        .pipe(res);

      break;
    }
    case '/fetch/api/resources/trickle.py': {
      // Note: python's time.sleep(...) takes seconds, while setTimeout
      // takes ms.
      const delay = parseFloat(fullUrl.searchParams.get('ms') ?? 500);
      const count = parseInt(fullUrl.searchParams.get('count') ?? 50);

      // eslint-disable-next-line no-unused-vars
      for await (const chunk of req); // read request body

      await sleep(delay);

      if (!fullUrl.searchParams.has('notype')) {
        res.setHeader('Content-type', 'text/plain');
      }

      res.statusCode = 200;
      await sleep(delay);

      for (let i = 0; i < count; i++) {
        res.write('TEST_TRICKLE\n');
        await sleep(delay);
      }

      res.end();
      break;
    }
    case '/fetch/api/resources/infinite-slow-response.py': {
      // https://github.com/web-platform-tests/wpt/blob/master/fetch/api/resources/infinite-slow-response.py
      const stateKey = fullUrl.searchParams.get('stateKey') ?? '';
      const abortKey = fullUrl.searchParams.get('abortKey') ?? '';

      if (stateKey) {
        stash.put(stateKey, 'open', fullUrl.pathname);
      }

      res.setHeader('Content-Type', 'text/plain');
      res.statusCode = 200;

      res.write('.'.repeat(2048));

      while (true) {
        if (!res.write('.')) {
          break;
        } else if (abortKey && stash.take(abortKey)) {
          break;
        }

        await sleep(100);
      }

      if (stateKey) {
        stash.put(stateKey, 'closed', fullUrl.pathname);
      }

      res.end();
      break;
    }
    case '/fetch/api/resources/stash-take.py': {
      // https://github.com/web-platform-tests/wpt/blob/6ae3f702a332e8399fab778c831db6b7dca3f1c6/fetch/api/resources/stash-take.py

      const key = fullUrl.searchParams.get('key');
      res.setHeader('Access-Control-Allow-Origin', '*');

      const took = stash.take(key, fullUrl.pathname) ?? null;

      res.write(JSON.stringify(took));
      res.end();
      break;
    }
    case '/fetch/api/resources/echo-content.py': {
      res.setHeader('X-Request-Method', req.method);
      res.setHeader('X-Request-Content-Length', req.headers['content-length'] ?? 'NO');
      res.setHeader('X-Request-Content-Type', req.headers['content-type'] ?? 'NO');
      res.setHeader('Content-Type', 'text/plain');

      for await (const chunk of req) {
        res.write(chunk);
      }

      res.end();
      break;
    }
    case '/fetch/api/resources/cache.py': {
      if (req.headers['if-none-match'] === '"123abc"') {
        res.statusCode = 304;
        res.statusMessage = 'Not Modified';
        res.setHeader('X-HTTP-STATUS', '304');
        res.end();
      } else {
        // cache miss, so respond with the actual content
        res.statusCode = 200;
        res.statusMessage = 'OK';
        res.setHeader('Content-Type', 'text/plain');
        res.setHeader('ETag', '"123abc"');
        res.end('lorem ipsum dolor sit amet');
      }
      break;
    }
    case '/fetch/api/resources/status.py': {
      const code = parseInt(fullUrl.searchParams.get('code') ?? 200);
      const text = fullUrl.searchParams.get('text') ?? 'OMG';
      const content = fullUrl.searchParams.get('content') ?? '';
      const type = fullUrl.searchParams.get('type') ?? '';
      res.statusCode = code;
      res.statusMessage = text;
      res.setHeader('Content-Type', type);
      res.setHeader('X-Request-Method', req.method);
      res.end(content);
      break;
    }
    case '/fetch/api/resources/inspect-headers.py': {
      const query = fullUrl.searchParams;
      const checkedHeaders = query.get('headers')
        ?.split('|')
        .map(h => h.toLowerCase()) ?? [];

      if (query.has('headers')) {
        for (const header of checkedHeaders) {
          if (Object.hasOwn(req.headers, header)) {
            res.setHeader(`x-request-${header}`, req.headers[header] ?? '');
          }
        }
      }

      if (query.has('cors')) {
        if (Object.hasOwn(req.headers, 'origin')) {
          res.setHeader('Access-Control-Allow-Origin', req.headers.origin ?? '');
        } else {
          res.setHeader('Access-Control-Allow-Origin', '*');
        }

        res.setHeader('Access-Control-Allow-Credentials', 'true');
        res.setHeader('Access-Control-Allow-Methods', 'GET, POST, HEAD');
        const exposedHeaders = checkedHeaders.map(h => `x-request-${h}`).join(', ');
        res.setHeader('Access-Control-Expose-Headers', exposedHeaders);
        if (query.has('allow_headers')) {
          res.setHeader('Access-Control-Allow-Headers', query.get('allowed_headers'));
        } else {
          res.setHeader('Access-Control-Allow-Headers', Object.keys(req.headers).join(', '));
        }
      }

      res.setHeader('content-type', 'text/plain');
      res.end('');
      break;
    }
    case '/fetch/api/resources/bad-chunk-encoding.py': {
      const query = fullUrl.searchParams;

      const delay = parseFloat(query.get('ms') ?? 1000);
      const count = parseInt(query.get('count') ?? 50);
      await sleep(delay);
      res.socket.write(
        'HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n'
      );
      await sleep(delay);

      for (let i = 0; i < count; i++) {
        res.socket.write('a\r\nTEST_CHUNK\r\n');
        await sleep(delay);
      }

      res.end('garbage');
      break;
    }
    case '/fetch/api/resources/redirect.py': {
      redirectRoute(req, res, fullUrl);
      break;
    }
    case '/fetch/api/resources/method.py': {
      if (fullUrl.searchParams.has('cors')) {
        res.setHeader('Access-Control-Allow-Origin', '*');
        res.setHeader('Access-Control-Allow-Credentials', 'true');
        res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, FOO');
        res.setHeader('Access-Control-Allow-Headers', 'x-test, x-foo');
        res.setHeader('Access-Control-Expose-Headers', 'x-request-method');
      }

      res.setHeader('x-request-method', req.method);
      res.setHeader('x-request-content-type', req.headers['content-type'] ?? 'NO');
      res.setHeader('x-request-content-length', req.headers['content-length'] ?? 'NO');
      res.setHeader('x-request-content-encoding', req.headers['content-encoding'] ?? 'NO');
      res.setHeader('x-request-content-language', req.headers['content-language'] ?? 'NO');
      res.setHeader('x-request-content-location', req.headers['content-location'] ?? 'NO');

      for await (const chunk of req) {
        res.write(chunk);
      }

      res.end();
      break;
    }
    case '/fetch/api/resources/clean-stash.py': {
      const token = fullUrl.searchParams.get('token');
      const took = stash.take(token);

      if (took) {
        res.end('1');
      } else {
        res.end('0');
      }

      break;
    }
    case '/fetch/content-encoding/resources/bad-gzip-body.py': {
      res.setHeader('Content-Encoding', 'gzip');
      res.end('not actually gzip');
      break;
    }
    case '/fetch/api/resources/dump-authorization-header.py': {
      res.setHeader('Content-Type', 'text/html');
      res.setHeader('Cache-Control', 'no-cache');

      if (req.headers.origin) {
        res.setHeader('Access-Control-Allow-Origin', req.headers.origin);
        res.setHeader('Access-Control-Allow-Credentials', 'true');
      } else {
        res.setHeader('Access-Control-Allow-Origin', '*');
      }

      res.setHeader('Access-Control-Allow-Headers', 'Authorization');
      res.statusCode = 200;

      if (req.headers.authorization) {
        res.end(req.headers.authorization);
        break;
      }

      res.end('none');
      break;
    }
    case '/fetch/api/resources/authentication.py': {
      const auth = Buffer.from(req.headers.authorization.slice('Basic '.length), 'base64');
      const [user, password] = auth.toString().split(':');

      if (user === 'user' && password === 'password') {
        res.end('Authentication done');
        break;
      }

      const realm = fullUrl.searchParams.get('realm') ?? 'test';

      res.statusCode = 401;
      res.setHeader('WWW-Authenticate', `Basic realm="${realm}"`);
      res.end('Please login with credentials \'user\' and \'password\'');
      break;
    }
    case '/fetch/api/resources/redirect-empty-location.py': {
      res.setHeader('location', '');
      res.statusCode = 302;
      res.end('');
      break;
    }
    default: {
      res.statusCode = 200;
      res.end(fullUrl.toString());
    }
  }

  if (res[symbols.kContent]) {
    res.write(res[symbols.kContent]);
  }
}).listen(0);

await once(server, 'listening');

const send = (message) => {
  if (typeof process.send === 'function') {
    process.send(message);
  }
};

const url = `http://localhost:${server.address().port}`;
console.log('server opened ' + url);
send({ server: url });

process.on('message', (message) => {
  if (message === 'shutdown') {
    server.close((err) => process.exit(err ? 1 : 0));
  }
});

export { server };
