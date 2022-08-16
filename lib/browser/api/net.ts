import * as url from 'url';
import { Readable, Writable } from 'stream';
import { app } from 'electron/main';
import type { ClientRequestConstructorOptions, UploadProgress } from 'electron/main';

const {
  isOnline,
  isValidHeaderName,
  isValidHeaderValue,
  createURLLoader
} = process._linkedBinding('electron_browser_net');

const kSupportedProtocols = new Set(['http:', 'https:']);

// set of headers that Node.js discards duplicates for
// see https://nodejs.org/api/http.html#http_message_headers
const discardableDuplicateHeaders = new Set([
  'content-type',
  'content-length',
  'user-agent',
  'referer',
  'host',
  'authorization',
  'proxy-authorization',
  'if-modified-since',
  'if-unmodified-since',
  'from',
  'location',
  'max-forwards',
  'retry-after',
  'etag',
  'last-modified',
  'server',
  'age',
  'expires'
]);

class IncomingMessage extends Readable {
  _shouldPush: boolean = false;
  _data: (Buffer | null)[] = [];
  _responseHead: NodeJS.ResponseHead;
  _resume: (() => void) | null = null;

  constructor (responseHead: NodeJS.ResponseHead) {
    super();
    this._responseHead = responseHead;
  }

  get statusCode () {
    return this._responseHead.statusCode;
  }

  get statusMessage () {
    return this._responseHead.statusMessage;
  }

  get headers () {
    const filteredHeaders: Record<string, string | string[]> = {};
    const { rawHeaders } = this._responseHead;
    rawHeaders.forEach(header => {
      const keyLowerCase = header.key.toLowerCase();
      if (Object.prototype.hasOwnProperty.call(filteredHeaders, keyLowerCase) &&
          discardableDuplicateHeaders.has(keyLowerCase)) {
        // do nothing with discardable duplicate headers
      } else {
        if (keyLowerCase === 'set-cookie') {
          // keep set-cookie as an array per Node.js rules
          // see https://nodejs.org/api/http.html#http_message_headers
          if (Object.prototype.hasOwnProperty.call(filteredHeaders, keyLowerCase)) {
            (filteredHeaders[keyLowerCase] as string[]).push(header.value);
          } else {
            filteredHeaders[keyLowerCase] = [header.value];
          }
        } else {
          // for non-cookie headers, the values are joined together with ', '
          if (Object.prototype.hasOwnProperty.call(filteredHeaders, keyLowerCase)) {
            filteredHeaders[keyLowerCase] += `, ${header.value}`;
          } else {
            filteredHeaders[keyLowerCase] = header.value;
          }
        }
      }
    });
    return filteredHeaders;
  }

  get rawHeaders () {
    const rawHeadersArr: string[] = [];
    const { rawHeaders } = this._responseHead;
    rawHeaders.forEach(header => {
      rawHeadersArr.push(header.key, header.value);
    });
    return rawHeadersArr;
  }

  get httpVersion () {
    return `${this.httpVersionMajor}.${this.httpVersionMinor}`;
  }

  get httpVersionMajor () {
    return this._responseHead.httpVersion.major;
  }

  get httpVersionMinor () {
    return this._responseHead.httpVersion.minor;
  }

  get rawTrailers () {
    throw new Error('HTTP trailers are not supported');
  }

  get trailers () {
    throw new Error('HTTP trailers are not supported');
  }

  _storeInternalData (chunk: Buffer | null, resume: (() => void) | null) {
    // save the network callback for use in _pushInternalData
    this._resume = resume;
    this._data.push(chunk);
    this._pushInternalData();
  }

  _pushInternalData () {
    while (this._shouldPush && this._data.length > 0) {
      const chunk = this._data.shift();
      this._shouldPush = this.push(chunk);
    }
    if (this._shouldPush && this._resume) {
      // Reset the callback, so that a new one is used for each
      // batch of throttled data. Do this before calling resume to avoid a
      // potential race-condition
      const resume = this._resume;
      this._resume = null;

      resume();
    }
  }

  _read () {
    this._shouldPush = true;
    this._pushInternalData();
  }
}

/** Writable stream that buffers up everything written to it. */
class SlurpStream extends Writable {
  _data: Buffer;
  constructor () {
    super();
    this._data = Buffer.alloc(0);
  }

  _write (chunk: Buffer, encoding: string, callback: () => void) {
    this._data = Buffer.concat([this._data, chunk]);
    callback();
  }

  data () { return this._data; }
}

class ChunkedBodyStream extends Writable {
  _pendingChunk: Buffer | undefined;
  _downstream?: NodeJS.DataPipe;
  _pendingCallback?: (error?: Error) => void;
  _clientRequest: ClientRequest;

  constructor (clientRequest: ClientRequest) {
    super();
    this._clientRequest = clientRequest;
  }

  _write (chunk: Buffer, encoding: string, callback: () => void) {
    if (this._downstream) {
      this._downstream.write(chunk).then(callback, callback);
    } else {
      // the contract of _write is that we won't be called again until we call
      // the callback, so we're good to just save a single chunk.
      this._pendingChunk = chunk;
      this._pendingCallback = callback;

      // The first write to a chunked body stream begins the request.
      this._clientRequest._startRequest();
    }
  }

  _final (callback: () => void) {
    this._downstream!.done();
    callback();
  }

  startReading (pipe: NodeJS.DataPipe) {
    if (this._downstream) {
      throw new Error('two startReading calls???');
    }
    this._downstream = pipe;
    if (this._pendingChunk) {
      const doneWriting = (maybeError: Error | void) => {
        // If the underlying request has been aborted, we honestly don't care about the error
        // all work should cease as soon as we abort anyway, this error is probably a
        // "mojo pipe disconnected" error (code=9)
        if (this._clientRequest._aborted) return;

        const cb = this._pendingCallback!;
        delete this._pendingCallback;
        delete this._pendingChunk;
        cb(maybeError || undefined);
      };
      this._downstream.write(this._pendingChunk).then(doneWriting, doneWriting);
    }
  }
}

type RedirectPolicy = 'manual' | 'follow' | 'error';

function parseOptions (optionsIn: ClientRequestConstructorOptions | string): NodeJS.CreateURLLoaderOptions & { redirectPolicy: RedirectPolicy, headers: Record<string, { name: string, value: string | string[] }> } {
  const options: any = typeof optionsIn === 'string' ? url.parse(optionsIn) : { ...optionsIn };

  let urlStr: string = options.url;

  if (!urlStr) {
    const urlObj: url.UrlObject = {};
    const protocol = options.protocol || 'http:';
    if (!kSupportedProtocols.has(protocol)) {
      throw new Error('Protocol "' + protocol + '" not supported');
    }
    urlObj.protocol = protocol;

    if (options.host) {
      urlObj.host = options.host;
    } else {
      if (options.hostname) {
        urlObj.hostname = options.hostname;
      } else {
        urlObj.hostname = 'localhost';
      }

      if (options.port) {
        urlObj.port = options.port;
      }
    }

    if (options.path && / /.test(options.path)) {
      // The actual regex is more like /[^A-Za-z0-9\-._~!$&'()*+,;=/:@]/
      // with an additional rule for ignoring percentage-escaped characters
      // but that's a) hard to capture in a regular expression that performs
      // well, and b) possibly too restrictive for real-world usage. That's
      // why it only scans for spaces because those are guaranteed to create
      // an invalid request.
      throw new TypeError('Request path contains unescaped characters');
    }
    const pathObj = url.parse(options.path || '/');
    urlObj.pathname = pathObj.pathname;
    urlObj.search = pathObj.search;
    urlObj.hash = pathObj.hash;
    urlStr = url.format(urlObj);
  }

  const redirectPolicy = options.redirect || 'follow';
  if (!['follow', 'error', 'manual'].includes(redirectPolicy)) {
    throw new Error('redirect mode should be one of follow, error or manual');
  }

  if (options.headers != null && typeof options.headers !== 'object') {
    throw new TypeError('headers must be an object');
  }

  const urlLoaderOptions: NodeJS.CreateURLLoaderOptions & { redirectPolicy: RedirectPolicy, headers: Record<string, { name: string, value: string | string[] }> } = {
    method: (options.method || 'GET').toUpperCase(),
    url: urlStr,
    redirectPolicy,
    headers: {},
    body: null as any,
    useSessionCookies: options.useSessionCookies,
    credentials: options.credentials,
    origin: options.origin
  };
  const headers: Record<string, string | string[]> = options.headers || {};
  for (const [name, value] of Object.entries(headers)) {
    if (!isValidHeaderName(name)) {
      throw new Error(`Invalid header name: '${name}'`);
    }
    if (!isValidHeaderValue(value.toString())) {
      throw new Error(`Invalid value for header '${name}': '${value}'`);
    }
    const key = name.toLowerCase();
    urlLoaderOptions.headers[key] = { name, value };
  }
  if (options.session) {
    // Weak check, but it should be enough to catch 99% of accidental misuses.
    if (options.session.constructor && options.session.constructor.name === 'Session') {
      urlLoaderOptions.session = options.session;
    } else {
      throw new TypeError('`session` should be an instance of the Session class');
    }
  } else if (options.partition) {
    if (typeof options.partition === 'string') {
      urlLoaderOptions.partition = options.partition;
    } else {
      throw new TypeError('`partition` should be a string');
    }
  }
  return urlLoaderOptions;
}

export class ClientRequest extends Writable implements Electron.ClientRequest {
  _started: boolean = false;
  _firstWrite: boolean = false;
  _aborted: boolean = false;
  _chunkedEncoding: boolean | undefined;
  _body: Writable | undefined;
  _urlLoaderOptions: NodeJS.CreateURLLoaderOptions & { headers: Record<string, { name: string, value: string | string[] }> };
  _redirectPolicy: RedirectPolicy;
  _followRedirectCb?: () => void;
  _uploadProgress?: { active: boolean, started: boolean, current: number, total: number };
  _urlLoader?: NodeJS.URLLoader;
  _response?: IncomingMessage;

  constructor (options: ClientRequestConstructorOptions | string, callback?: (message: IncomingMessage) => void) {
    super({ autoDestroy: true });

    if (!app.isReady()) {
      throw new Error('net module can only be used after app is ready');
    }

    if (callback) {
      this.once('response', callback);
    }

    const { redirectPolicy, ...urlLoaderOptions } = parseOptions(options);
    this._urlLoaderOptions = urlLoaderOptions;
    this._redirectPolicy = redirectPolicy;
  }

  get chunkedEncoding () {
    return this._chunkedEncoding || false;
  }

  set chunkedEncoding (value: boolean) {
    if (this._started) {
      throw new Error('chunkedEncoding can only be set before the request is started');
    }
    if (typeof this._chunkedEncoding !== 'undefined') {
      throw new Error('chunkedEncoding can only be set once');
    }
    this._chunkedEncoding = !!value;
    if (this._chunkedEncoding) {
      this._body = new ChunkedBodyStream(this);
      this._urlLoaderOptions.body = (pipe: NodeJS.DataPipe) => {
        (this._body! as ChunkedBodyStream).startReading(pipe);
      };
    }
  }

  setHeader (name: string, value: string) {
    if (typeof name !== 'string') {
      throw new TypeError('`name` should be a string in setHeader(name, value)');
    }
    if (value == null) {
      throw new Error('`value` required in setHeader("' + name + '", value)');
    }
    if (this._started || this._firstWrite) {
      throw new Error('Can\'t set headers after they are sent');
    }
    if (!isValidHeaderName(name)) {
      throw new Error(`Invalid header name: '${name}'`);
    }
    if (!isValidHeaderValue(value.toString())) {
      throw new Error(`Invalid value for header '${name}': '${value}'`);
    }

    const key = name.toLowerCase();
    this._urlLoaderOptions.headers[key] = { name, value };
  }

  getHeader (name: string) {
    if (name == null) {
      throw new Error('`name` is required for getHeader(name)');
    }

    const key = name.toLowerCase();
    const header = this._urlLoaderOptions.headers[key];
    return header && header.value as any;
  }

  removeHeader (name: string) {
    if (name == null) {
      throw new Error('`name` is required for removeHeader(name)');
    }

    if (this._started || this._firstWrite) {
      throw new Error('Can\'t remove headers after they are sent');
    }

    const key = name.toLowerCase();
    delete this._urlLoaderOptions.headers[key];
  }

  _write (chunk: Buffer, encoding: BufferEncoding, callback: () => void) {
    this._firstWrite = true;
    if (!this._body) {
      this._body = new SlurpStream();
      this._body.on('finish', () => {
        this._urlLoaderOptions.body = (this._body as SlurpStream).data();
        this._startRequest();
      });
    }
    // TODO: is this the right way to forward to another stream?
    this._body.write(chunk, encoding, callback);
  }

  _final (callback: () => void) {
    if (this._body) {
      // TODO: is this the right way to forward to another stream?
      this._body.end(callback);
    } else {
      // end() called without a body, go ahead and start the request
      this._startRequest();
      callback();
    }
  }

  _startRequest () {
    this._started = true;
    const stringifyValues = (obj: Record<string, { name: string, value: string | string[] }>) => {
      const ret: Record<string, string> = {};
      for (const k of Object.keys(obj)) {
        const kv = obj[k];
        ret[kv.name] = kv.value.toString();
      }
      return ret;
    };
    this._urlLoaderOptions.referrer = this.getHeader('referer') || '';
    this._urlLoaderOptions.origin = this._urlLoaderOptions.origin || this.getHeader('origin') || '';
    this._urlLoaderOptions.hasUserActivation = this.getHeader('sec-fetch-user') === '?1';
    this._urlLoaderOptions.mode = this.getHeader('sec-fetch-mode') || '';
    this._urlLoaderOptions.destination = this.getHeader('sec-fetch-dest') || '';
    const opts = { ...this._urlLoaderOptions, extraHeaders: stringifyValues(this._urlLoaderOptions.headers) };
    this._urlLoader = createURLLoader(opts);
    this._urlLoader.on('response-started', (event, finalUrl, responseHead) => {
      const response = this._response = new IncomingMessage(responseHead);
      this.emit('response', response);
    });
    this._urlLoader.on('data', (event, data, resume) => {
      this._response!._storeInternalData(Buffer.from(data), resume);
    });
    this._urlLoader.on('complete', () => {
      if (this._response) { this._response._storeInternalData(null, null); }
    });
    this._urlLoader.on('error', (event, netErrorString) => {
      this._die(new Error(netErrorString));
    });

    this._urlLoader.on('login', (event, authInfo, callback) => {
      const handled = this.emit('login', authInfo, callback);
      if (!handled) {
        // If there were no listeners, cancel the authentication request.
        callback();
      }
    });

    this._urlLoader.on('redirect', (event, redirectInfo, headers) => {
      const { statusCode, newMethod, newUrl } = redirectInfo;
      if (this._redirectPolicy === 'error') {
        this._die(new Error('Attempted to redirect, but redirect policy was \'error\''));
      } else if (this._redirectPolicy === 'manual') {
        let _followRedirect = false;
        this._followRedirectCb = () => { _followRedirect = true; };
        try {
          this.emit('redirect', statusCode, newMethod, newUrl, headers);
        } finally {
          this._followRedirectCb = undefined;
          if (!_followRedirect && !this._aborted) {
            this._die(new Error('Redirect was cancelled'));
          }
        }
      } else if (this._redirectPolicy === 'follow') {
        // Calling followRedirect() when the redirect policy is 'follow' is
        // allowed but does nothing. (Perhaps it should throw an error
        // though...? Since the redirect will happen regardless.)
        try {
          this._followRedirectCb = () => {};
          this.emit('redirect', statusCode, newMethod, newUrl, headers);
        } finally {
          this._followRedirectCb = undefined;
        }
      } else {
        this._die(new Error(`Unexpected redirect policy '${this._redirectPolicy}'`));
      }
    });

    this._urlLoader.on('upload-progress', (event, position, total) => {
      this._uploadProgress = { active: true, started: true, current: position, total };
      this.emit('upload-progress', position, total); // Undocumented, for now
    });

    this._urlLoader.on('download-progress', (event, current) => {
      if (this._response) {
        this._response.emit('download-progress', current); // Undocumented, for now
      }
    });
  }

  followRedirect () {
    if (this._followRedirectCb) {
      this._followRedirectCb();
    } else {
      throw new Error('followRedirect() called, but was not waiting for a redirect');
    }
  }

  abort () {
    if (!this._aborted) {
      process.nextTick(() => { this.emit('abort'); });
    }
    this._aborted = true;
    this._die();
  }

  _die (err?: Error) {
    // Node.js assumes that any stream which is ended is no longer capable of emitted events
    // which is a faulty assumption for the case of an object that is acting like a stream
    // (our urlRequest). If we don't emit here, this causes errors since we *do* expect
    // that error events can be emitted after urlRequest.end().
    if ((this as any)._writableState.destroyed && err) {
      this.emit('error', err);
    }

    this.destroy(err);
    if (this._urlLoader) {
      this._urlLoader.cancel();
      if (this._response) this._response.destroy();
    }
  }

  getUploadProgress (): UploadProgress {
    return this._uploadProgress ? { ...this._uploadProgress } : { active: false, started: false, current: 0, total: 0 };
  }
}

export function request (options: ClientRequestConstructorOptions | string, callback?: (message: IncomingMessage) => void) {
  return new ClientRequest(options, callback);
}

exports.isOnline = isOnline;

Object.defineProperty(exports, 'online', {
  get: () => isOnline()
});
