import assert from 'node:assert';
import { sep } from 'node:path';
import { exit } from 'node:process';
import tty from 'node:tty';
import { inspect } from 'node:util';

/**
 * Parse the `Meta:` tags sometimes included in tests.
 * These can include resources to inject, how long it should
 * take to timeout, and which globals to expose.
 * @example
 * // META: timeout=long
 * // META: global=window,worker
 * // META: script=/common/utils.js
 * // META: script=/common/get-host-info.sub.js
 * // META: script=../request/request-error.js
 * @see https://nodejs.org/api/readline.html#readline_example_read_file_stream_line_by_line
 * @param {string} fileContents
 */
export function parseMeta (fileContents) {
  const lines = fileContents.split(/\r?\n/g);

  const meta = {
    /** @type {string|null} */
    timeout: null,
    /** @type {string[]} */
    global: [],
    /** @type {string[]} */
    scripts: [],
    /** @type {string[]} */
    variant: []
  };

  for (const line of lines) {
    if (!line.startsWith('// META: ')) {
      break;
    }

    const groups = /^\/\/ META: (?<type>.*?)=(?<match>.*)$/.exec(line)?.groups;

    if (!groups) {
      console.log(`Failed to parse META tag: ${line}`);
      exit(1);
    }

    switch (groups.type) {
      case 'variant':
        meta[groups.type].push(groups.match);
        break;
      case 'title':
      case 'timeout': {
        meta[groups.type] = groups.match;
        break;
      }
      case 'global': {
        // window,worker -> ['window', 'worker']
        meta.global.push(...groups.match.split(','));
        break;
      }
      case 'script': {
        // A relative or absolute file path to the resources
        // needed for the current test.
        meta.scripts.push(groups.match);
        break;
      }
      default: {
        console.log(`Unknown META tag: ${groups.type}`);
        exit(1);
      }
    }
  }

  return meta;
}

/**
 * @param {string} sub
 */
function parseSubBlock (sub) {
  const subName = sub.includes('[') ? sub.slice(0, sub.indexOf('[')) : sub;
  const options = sub.matchAll(/\[(.*?)\]/gm);

  return {
    sub: subName,
    options: [...options].map(match => match[1])
  };
}

/**
 * @see https://web-platform-tests.org/writing-tests/server-pipes.html?highlight=sub#built-in-pipes
 * @param {string} code
 * @param {string} url
 */
export function handlePipes (code, url) {
  const server = new URL(url);

  // "Substitutions are marked in a file using a block delimited by
  //  {{ and }}. Inside the block the following variables are available:"
  return code.replace(/{{(.*?)}}/gm, (_, match) => {
    const { sub } = parseSubBlock(match);

    switch (sub) {
      // "The host name of the server excluding any subdomain part."
      // eslint-disable-next-line no-fallthrough
      case 'host':
      // "The domain name of a particular subdomain e.g.
      //  {{domains[www]}} for the www subdomain."
      // eslint-disable-next-line no-fallthrough
      case 'domains':
      // "The domain name of a particular subdomain for a particular host.
      //  The first key may be empty (designating the “default” host) or
      //  the value alt; i.e., {{hosts[alt][]}} (designating the alternate
      //  host)."
      // eslint-disable-next-line no-fallthrough
      case 'hosts': {
        return 'localhost';
      }
      // "The port number of servers, by protocol e.g. {{ports[http][0]}}
      //  for the first (and, depending on setup, possibly only) http server"
      case 'ports': {
        return server.port;
      }
      default: {
        throw new TypeError(`Unknown substitute "${sub}".`);
      }
    }
  });
}

/**
 * Some test names may contain characters that JSON cannot handle.
 * @param {string} name
 */
export function normalizeName (name) {
  return name.replace(/(\v)/g, (_, match) => {
    switch (inspect(match)) {
      case '\'\\x0B\'': return '\\x0B';
      default: return match;
    }
  });
}

export function colors (str, color) {
  assert(Object.hasOwn(inspect.colors, color), `Missing color ${color}`);

  if (!tty.WriteStream.prototype.hasColors()) {
    return str;
  }

  const [start, end] = inspect.colors[color];

  return `\u001b[${start}m${str}\u001b[${end}m`;
}

/** @param {string} path */
export function resolveStatusPath (path, status) {
  const paths = path
    .slice(process.cwd().length + sep.length)
    .split(sep)
    .slice(5); // [test, wpt, tests, fetch, b, c.js] -> [fetch, b, c.js]

  // skip the first folder name
  for (let i = 1; i < paths.length - 1; i++) {
    status = status[paths[i]];

    if (!status) {
      break;
    }
  }

  return { fullPath: path, topLevel: status ?? {}, file: status?.[paths.at(-1)] ?? {} };
}
