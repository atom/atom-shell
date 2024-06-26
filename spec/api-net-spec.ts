import { expect } from 'chai';
import { net, ClientRequest, ClientRequestConstructorOptions, utilityProcess } from 'electron/main';
import * as http from 'node:http';
import * as path from 'node:path';
import * as url from 'node:url';
import { once } from 'node:events';
import { setTimeout } from 'node:timers/promises';
import { collectStreamBody, collectStreamBodyBuffer, getResponse, kOneKiloByte, kOneMegaByte, randomBuffer, randomString, respondNTimes, respondOnce } from './lib/net-helpers';

const utilityFixturePath = path.resolve(__dirname, 'fixtures', 'api', 'utility-process', 'api-net-spec.js');

async function itUtility (name: string, fn?: Function, args?: {[key:string]: any}) {
  it(`${name} in utility process`, async () => {
    const child = utilityProcess.fork(utilityFixturePath, [], {
      execArgv: ['--expose-gc']
    });
    if (fn) {
      child.postMessage({ fn: `(${fn})()`, args });
    } else {
      child.postMessage({ fn: '(() => {})()', args });
    }
    const [data] = await once(child, 'message');
    expect(data.ok).to.be.true(data.message);
    // Cleanup.
    const [code] = await once(child, 'exit');
    expect(code).to.equal(0);
  });
}

// eslint-disable-next-line @typescript-eslint/no-unused-vars
async function itIgnoringArgs (name: string, fn?: Mocha.Func|Mocha.AsyncFunc, args?: {[key:string]: any}) {
  it(name, fn);
}

describe('net module', () => {
  beforeEach(() => {
    respondNTimes.routeFailure = false;
  });
  afterEach(async function () {
    if (respondNTimes.routeFailure && this.test) {
      if (!this.test.isFailed()) {
        throw new Error('Failing this test due an unhandled error in the respondOnce route handler, check the logs above for the actual error');
      }
    }
  });

  for (const test of [itIgnoringArgs, itUtility]) {
    describe('HTTP basics', () => {
      test('should be able to issue a basic GET request', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.method).to.equal('GET');
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should be able to issue a basic POST request', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.method).to.equal('POST');
          response.end();
        });
        const urlRequest = net.request({
          method: 'POST',
          url: serverUrl
        });
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should fetch correct data in a GET request', async () => {
        const expectedBodyData = 'Hello World!';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.method).to.equal('GET');
          response.end(expectedBodyData);
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        const body = await collectStreamBody(response);
        expect(body).to.equal(expectedBodyData);
      });

      test('should post the correct data in a POST request', async () => {
        const bodyData = 'Hello World!';
        let postedBodyData: string = '';
        const serverUrl = await respondOnce.toSingleURL(async (request, response) => {
          postedBodyData = await collectStreamBody(request);
          response.end();
        });
        const urlRequest = net.request({
          method: 'POST',
          url: serverUrl
        });
        urlRequest.write(bodyData);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(postedBodyData).to.equal(bodyData);
      });

      test('a 307 redirected POST request preserves the body', async () => {
        const bodyData = 'Hello World!';
        let postedBodyData: string = '';
        let methodAfterRedirect: string | undefined;
        const serverUrl = await respondNTimes.toRoutes({
          '/redirect': (req, res) => {
            res.statusCode = 307;
            res.setHeader('location', serverUrl);
            return res.end();
          },
          '/': async (req, res) => {
            methodAfterRedirect = req.method;
            postedBodyData = await collectStreamBody(req);
            res.end();
          }
        }, 2);
        const urlRequest = net.request({
          method: 'POST',
          url: serverUrl + '/redirect'
        });
        urlRequest.write(bodyData);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
        expect(methodAfterRedirect).to.equal('POST');
        expect(postedBodyData).to.equal(bodyData);
      });

      test('a 302 redirected POST request DOES NOT preserve the body', async () => {
        const bodyData = 'Hello World!';
        let postedBodyData: string = '';
        let methodAfterRedirect: string | undefined;
        const serverUrl = await respondNTimes.toRoutes({
          '/redirect': (req, res) => {
            res.statusCode = 302;
            res.setHeader('location', serverUrl);
            return res.end();
          },
          '/': async (req, res) => {
            methodAfterRedirect = req.method;
            postedBodyData = await collectStreamBody(req);
            res.end();
          }
        }, 2);
        const urlRequest = net.request({
          method: 'POST',
          url: serverUrl + '/redirect'
        });
        urlRequest.write(bodyData);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
        expect(methodAfterRedirect).to.equal('GET');
        expect(postedBodyData).to.equal('');
      });

      test('should support chunked encoding', async () => {
        let receivedRequest: http.IncomingMessage = null as any;
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.chunkedEncoding = true;
          receivedRequest = request;
          request.on('data', (chunk: Buffer) => {
            response.write(chunk);
          });
          request.on('end', (chunk: Buffer) => {
            response.end(chunk);
          });
        });
        const urlRequest = net.request({
          method: 'POST',
          url: serverUrl
        });

        let chunkIndex = 0;
        const chunkCount = 100;
        let sent = Buffer.alloc(0);

        urlRequest.chunkedEncoding = true;
        while (chunkIndex < chunkCount) {
          chunkIndex += 1;
          const chunk = randomBuffer(kOneKiloByte);
          sent = Buffer.concat([sent, chunk]);
          urlRequest.write(chunk);
        }

        const response = await getResponse(urlRequest);
        expect(receivedRequest.method).to.equal('POST');
        expect(receivedRequest.headers['transfer-encoding']).to.equal('chunked');
        expect(receivedRequest.headers['content-length']).to.equal(undefined);
        expect(response.statusCode).to.equal(200);
        const received = await collectStreamBodyBuffer(response);
        expect(sent.equals(received)).to.be.true();
        expect(chunkIndex).to.be.equal(chunkCount);
      });

      for (const extraOptions of [{}, { credentials: 'include' }, { useSessionCookies: false, credentials: 'include' }] as ClientRequestConstructorOptions[]) {
        describe(`authentication when ${JSON.stringify(extraOptions)}`, () => {
          test('should emit the login event when 401', async () => {
            const [user, pass] = ['user', 'pass'];
            const serverUrl = await respondOnce.toSingleURL((request, response) => {
              if (!request.headers.authorization) {
                return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
              }
              response.writeHead(200).end('ok');
            });
            let loginAuthInfo: Electron.AuthInfo;
            const request = net.request({ method: 'GET', url: serverUrl, ...extraOptions });
            request.on('login', (authInfo, cb) => {
              loginAuthInfo = authInfo;
              cb(user, pass);
            });
            const response = await getResponse(request);
            expect(response.statusCode).to.equal(200);
            expect(loginAuthInfo!.realm).to.equal('Foo');
            expect(loginAuthInfo!.scheme).to.equal('basic');
          }, { extraOptions });

          test('should receive 401 response when cancelling authentication', async () => {
            const serverUrl = await respondOnce.toSingleURL((request, response) => {
              if (!request.headers.authorization) {
                response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' });
                response.end('unauthenticated');
              } else {
                response.writeHead(200).end('ok');
              }
            });
            const request = net.request({ method: 'GET', url: serverUrl, ...extraOptions });
            request.on('login', (authInfo, cb) => {
              cb();
            });
            const response = await getResponse(request);
            const body = await collectStreamBody(response);
            expect(response.statusCode).to.equal(401);
            expect(body).to.equal('unauthenticated');
          }, { extraOptions });

          test('should upload body when 401', async () => {
            const [user, pass] = ['user', 'pass'];
            const serverUrl = await respondOnce.toSingleURL((request, response) => {
              if (!request.headers.authorization) {
                return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
              }
              response.writeHead(200);
              request.on('data', (chunk) => response.write(chunk));
              request.on('end', () => response.end());
            });
            const requestData = randomString(kOneKiloByte);
            const request = net.request({ method: 'GET', url: serverUrl, ...extraOptions });
            request.on('login', (authInfo, cb) => {
              cb(user, pass);
            });
            request.write(requestData);
            const response = await getResponse(request);
            const responseData = await collectStreamBody(response);
            expect(responseData).to.equal(requestData);
          }, { extraOptions });
        });
      }

      describe('authentication when {"credentials":"omit"}', () => {
        test('should not emit the login event when 401', async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            if (!request.headers.authorization) {
              return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
            }
            response.writeHead(200).end('ok');
          });
          const request = net.request({ method: 'GET', url: serverUrl, credentials: 'omit' });
          request.on('login', () => {
            expect.fail('unexpected login event');
          });
          const response = await getResponse(request);
          expect(response.statusCode).to.equal(401);
          expect(response.headers['www-authenticate']).to.equal('Basic realm="Foo"');
        });
      });
    });

    describe('ClientRequest API', () => {
      test('request/response objects should emit expected events', async () => {
        const bodyData = randomString(kOneKiloByte);
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.end(bodyData);
        });

        const urlRequest = net.request(serverUrl);
        // request close event
        const closePromise = once(urlRequest, 'close');
        // request finish event
        const finishPromise = once(urlRequest, 'close');
        // request "response" event
        const response = await getResponse(urlRequest);
        response.on('error', (error: Error) => {
          expect(error).to.be.an('Error');
        });
        const statusCode = response.statusCode;
        expect(statusCode).to.equal(200);
        // response data event
        // respond end event
        const body = await collectStreamBody(response);
        expect(body).to.equal(bodyData);
        urlRequest.on('error', (error) => {
          expect(error).to.be.an('Error');
        });
        await Promise.all([closePromise, finishPromise]);
      });

      test('should be able to set a custom HTTP request header before first write', async () => {
        const customHeaderName = 'Some-Custom-Header-Name';
        const customHeaderValue = 'Some-Customer-Header-Value';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers[customHeaderName.toLowerCase()]).to.equal(customHeaderValue);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.setHeader(customHeaderName, customHeaderValue);
        expect(urlRequest.getHeader(customHeaderName)).to.equal(customHeaderValue);
        expect(urlRequest.getHeader(customHeaderName.toLowerCase())).to.equal(customHeaderValue);
        urlRequest.write('');
        expect(urlRequest.getHeader(customHeaderName)).to.equal(customHeaderValue);
        expect(urlRequest.getHeader(customHeaderName.toLowerCase())).to.equal(customHeaderValue);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should be able to set a non-string object as a header value', async () => {
        const customHeaderName = 'Some-Integer-Value';
        const customHeaderValue = 900;
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers[customHeaderName.toLowerCase()]).to.equal(customHeaderValue.toString());
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });

        const urlRequest = net.request(serverUrl);
        urlRequest.setHeader(customHeaderName, customHeaderValue as any);
        expect(urlRequest.getHeader(customHeaderName)).to.equal(customHeaderValue);
        expect(urlRequest.getHeader(customHeaderName.toLowerCase())).to.equal(customHeaderValue);
        urlRequest.write('');
        expect(urlRequest.getHeader(customHeaderName)).to.equal(customHeaderValue);
        expect(urlRequest.getHeader(customHeaderName.toLowerCase())).to.equal(customHeaderValue);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should not change the case of header name', async () => {
        const customHeaderName = 'X-Header-Name';
        const customHeaderValue = 'value';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers[customHeaderName.toLowerCase()]).to.equal(customHeaderValue.toString());
          expect(request.rawHeaders.includes(customHeaderName)).to.equal(true);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });

        const urlRequest = net.request(serverUrl);
        urlRequest.setHeader(customHeaderName, customHeaderValue);
        expect(urlRequest.getHeader(customHeaderName)).to.equal(customHeaderValue);
        urlRequest.write('');
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should not be able to set a custom HTTP request header after first write', async () => {
        const customHeaderName = 'Some-Custom-Header-Name';
        const customHeaderValue = 'Some-Customer-Header-Value';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers[customHeaderName.toLowerCase()]).to.equal(undefined);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.write('');
        expect(() => {
          urlRequest.setHeader(customHeaderName, customHeaderValue);
        }).to.throw();
        expect(urlRequest.getHeader(customHeaderName)).to.equal(undefined);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should be able to remove a custom HTTP request header before first write', async () => {
        const customHeaderName = 'Some-Custom-Header-Name';
        const customHeaderValue = 'Some-Customer-Header-Value';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers[customHeaderName.toLowerCase()]).to.equal(undefined);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.setHeader(customHeaderName, customHeaderValue);
        expect(urlRequest.getHeader(customHeaderName)).to.equal(customHeaderValue);
        urlRequest.removeHeader(customHeaderName);
        expect(urlRequest.getHeader(customHeaderName)).to.equal(undefined);
        urlRequest.write('');
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should not be able to remove a custom HTTP request header after first write', async () => {
        const customHeaderName = 'Some-Custom-Header-Name';
        const customHeaderValue = 'Some-Customer-Header-Value';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers[customHeaderName.toLowerCase()]).to.equal(customHeaderValue);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.setHeader(customHeaderName, customHeaderValue);
        expect(urlRequest.getHeader(customHeaderName)).to.equal(customHeaderValue);
        urlRequest.write('');
        expect(() => {
          urlRequest.removeHeader(customHeaderName);
        }).to.throw();
        expect(urlRequest.getHeader(customHeaderName)).to.equal(customHeaderValue);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should keep the order of headers', async () => {
        const customHeaderNameA = 'X-Header-100';
        const customHeaderNameB = 'X-Header-200';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          const headerNames = Array.from(Object.keys(request.headers));
          const headerAIndex = headerNames.indexOf(customHeaderNameA.toLowerCase());
          const headerBIndex = headerNames.indexOf(customHeaderNameB.toLowerCase());
          expect(headerBIndex).to.be.below(headerAIndex);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });

        const urlRequest = net.request(serverUrl);
        urlRequest.setHeader(customHeaderNameB, 'b');
        urlRequest.setHeader(customHeaderNameA, 'a');
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should be able to receive cookies', async () => {
        const cookie = ['cookie1', 'cookie2'];
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader('set-cookie', cookie);
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.headers['set-cookie']).to.have.same.members(cookie);
      });

      test('should be able to receive content-type', async () => {
        const contentType = 'mime/test; charset=test';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader('content-type', contentType);
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.headers['content-type']).to.equal(contentType);
      });

      describe('when {"credentials":"omit"}', () => {
        test('should not send cookies');
        test('should not store cookies');
      });

      test('should set sec-fetch-site to same-origin for request from same origin', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers['sec-fetch-site']).to.equal('same-origin');
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl,
          origin: serverUrl
        });
        await collectStreamBody(await getResponse(urlRequest));
      });

      test('should set sec-fetch-site to same-origin for request with the same origin header', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers['sec-fetch-site']).to.equal('same-origin');
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl
        });
        urlRequest.setHeader('Origin', serverUrl);
        await collectStreamBody(await getResponse(urlRequest));
      });

      test('should set sec-fetch-site to cross-site for request from other origin', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers['sec-fetch-site']).to.equal('cross-site');
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl,
          origin: 'https://not-exists.com'
        });
        await collectStreamBody(await getResponse(urlRequest));
      });

      test('should not send sec-fetch-user header by default', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers).not.to.have.property('sec-fetch-user');
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl
        });
        await collectStreamBody(await getResponse(urlRequest));
      });

      test('should set sec-fetch-user to ?1 if requested', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers['sec-fetch-user']).to.equal('?1');
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl
        });
        urlRequest.setHeader('sec-fetch-user', '?1');
        await collectStreamBody(await getResponse(urlRequest));
      });

      test('should set sec-fetch-mode to no-cors by default', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers['sec-fetch-mode']).to.equal('no-cors');
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl
        });
        await collectStreamBody(await getResponse(urlRequest));
      });

      for (const mode of ['navigate', 'cors', 'no-cors', 'same-origin']) {
        test(`should set sec-fetch-mode to ${mode} if requested`, async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            expect(request.headers['sec-fetch-mode']).to.equal(mode);
            response.statusCode = 200;
            response.statusMessage = 'OK';
            response.end();
          });
          const urlRequest = net.request({
            url: serverUrl,
            origin: serverUrl
          });
          urlRequest.setHeader('sec-fetch-mode', mode);
          await collectStreamBody(await getResponse(urlRequest));
        }, { mode });
      }

      test('should set sec-fetch-dest to empty by default', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers['sec-fetch-dest']).to.equal('empty');
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl
        });
        await collectStreamBody(await getResponse(urlRequest));
      });

      for (const dest of [
        'empty', 'audio', 'audioworklet', 'document', 'embed', 'font',
        'frame', 'iframe', 'image', 'manifest', 'object', 'paintworklet',
        'report', 'script', 'serviceworker', 'style', 'track', 'video',
        'worker', 'xslt'
      ]) {
        test(`should set sec-fetch-dest to ${dest} if requested`, async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            expect(request.headers['sec-fetch-dest']).to.equal(dest);
            response.statusCode = 200;
            response.statusMessage = 'OK';
            response.end();
          });
          const urlRequest = net.request({
            url: serverUrl,
            origin: serverUrl
          });
          urlRequest.setHeader('sec-fetch-dest', dest);
          await collectStreamBody(await getResponse(urlRequest));
        }, { dest });
      }

      test('should be able to abort an HTTP request before first write', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.end();
          expect.fail('Unexpected request event');
        });

        const urlRequest = net.request(serverUrl);
        urlRequest.on('response', () => {
          expect.fail('unexpected response event');
        });
        const aborted = once(urlRequest, 'abort');
        urlRequest.abort();
        urlRequest.write('');
        urlRequest.end();
        await aborted;
      });

      test('it should be able to abort an HTTP request before request end', async () => {
        let requestReceivedByServer = false;
        let urlRequest: ClientRequest | null = null;
        const serverUrl = await respondOnce.toSingleURL(() => {
          requestReceivedByServer = true;
          urlRequest!.abort();
        });
        let requestAbortEventEmitted = false;

        urlRequest = net.request(serverUrl);
        urlRequest.on('response', () => {
          expect.fail('Unexpected response event');
        });
        urlRequest.on('finish', () => {
          expect.fail('Unexpected finish event');
        });
        urlRequest.on('error', () => {
          expect.fail('Unexpected error event');
        });
        urlRequest.on('abort', () => {
          requestAbortEventEmitted = true;
        });

        const p = once(urlRequest, 'close');
        urlRequest.chunkedEncoding = true;
        urlRequest.write(randomString(kOneKiloByte));
        await p;
        expect(requestReceivedByServer).to.equal(true);
        expect(requestAbortEventEmitted).to.equal(true);
      });

      test('it should be able to abort an HTTP request after request end and before response', async () => {
        let requestReceivedByServer = false;
        let urlRequest: ClientRequest | null = null;
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          requestReceivedByServer = true;
          urlRequest!.abort();
          process.nextTick(() => {
            response.statusCode = 200;
            response.statusMessage = 'OK';
            response.end();
          });
        });
        let requestFinishEventEmitted = false;

        urlRequest = net.request(serverUrl);
        urlRequest.on('response', () => {
          expect.fail('Unexpected response event');
        });
        urlRequest.on('finish', () => {
          requestFinishEventEmitted = true;
        });
        urlRequest.on('error', () => {
          expect.fail('Unexpected error event');
        });
        urlRequest.end(randomString(kOneKiloByte));
        await once(urlRequest, 'abort');
        expect(requestFinishEventEmitted).to.equal(true);
        expect(requestReceivedByServer).to.equal(true);
      });

      test('it should be able to abort an HTTP request after response start', async () => {
        let requestReceivedByServer = false;
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          requestReceivedByServer = true;
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.write(randomString(kOneKiloByte));
        });
        let requestFinishEventEmitted = false;
        let requestResponseEventEmitted = false;
        let responseCloseEventEmitted = false;

        const urlRequest = net.request(serverUrl);
        urlRequest.on('response', (response) => {
          requestResponseEventEmitted = true;
          const statusCode = response.statusCode;
          expect(statusCode).to.equal(200);
          response.on('data', () => {});
          response.on('end', () => {
            expect.fail('Unexpected end event');
          });
          response.on('error', () => {
            expect.fail('Unexpected error event');
          });
          response.on('close' as any, () => {
            responseCloseEventEmitted = true;
          });
          urlRequest.abort();
        });
        urlRequest.on('finish', () => {
          requestFinishEventEmitted = true;
        });
        urlRequest.on('error', () => {
          expect.fail('Unexpected error event');
        });
        urlRequest.end(randomString(kOneKiloByte));
        await once(urlRequest, 'abort');
        expect(requestFinishEventEmitted).to.be.true('request should emit "finish" event');
        expect(requestReceivedByServer).to.be.true('request should be received by the server');
        expect(requestResponseEventEmitted).to.be.true('"response" event should be emitted');
        expect(responseCloseEventEmitted).to.be.true('response should emit "close" event');
      });

      test('abort event should be emitted at most once', async () => {
        let requestReceivedByServer = false;
        let urlRequest: ClientRequest | null = null;
        const serverUrl = await respondOnce.toSingleURL(() => {
          requestReceivedByServer = true;
          urlRequest!.abort();
          urlRequest!.abort();
        });
        let requestFinishEventEmitted = false;
        let abortsEmitted = 0;

        urlRequest = net.request(serverUrl);
        urlRequest.on('response', () => {
          expect.fail('Unexpected response event');
        });
        urlRequest.on('finish', () => {
          requestFinishEventEmitted = true;
        });
        urlRequest.on('error', () => {
          expect.fail('Unexpected error event');
        });
        urlRequest.on('abort', () => {
          abortsEmitted++;
        });
        urlRequest.end(randomString(kOneKiloByte));
        await once(urlRequest, 'abort');
        expect(requestFinishEventEmitted).to.be.true('request should emit "finish" event');
        expect(requestReceivedByServer).to.be.true('request should be received by server');
        expect(abortsEmitted).to.equal(1, 'request should emit exactly 1 "abort" event');
      });

      test('should allow to read response body from non-2xx response', async () => {
        const bodyData = randomString(kOneKiloByte);
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 404;
          response.end(bodyData);
        });

        const urlRequest = net.request(serverUrl);
        const bodyCheckPromise = getResponse(urlRequest).then(r => {
          expect(r.statusCode).to.equal(404);
          return r;
        }).then(collectStreamBody).then(receivedBodyData => {
          expect(receivedBodyData.toString()).to.equal(bodyData);
        });
        const eventHandlers = Promise.all([
          bodyCheckPromise,
          once(urlRequest, 'close')
        ]);

        urlRequest.end();

        await eventHandlers;
      });

      test('should throw when calling getHeader without a name', () => {
        expect(() => {
          (net.request({ url: 'https://test' }).getHeader as any)();
        }).to.throw(/`name` is required for getHeader\(name\)/);

        expect(() => {
          net.request({ url: 'https://test' }).getHeader(null as any);
        }).to.throw(/`name` is required for getHeader\(name\)/);
      });

      test('should throw when calling removeHeader without a name', () => {
        expect(() => {
          (net.request({ url: 'https://test' }).removeHeader as any)();
        }).to.throw(/`name` is required for removeHeader\(name\)/);

        expect(() => {
          net.request({ url: 'https://test' }).removeHeader(null as any);
        }).to.throw(/`name` is required for removeHeader\(name\)/);
      });

      test('should follow redirect when no redirect handler is provided', async () => {
        const requestUrl = '/302';
        const serverUrl = await respondOnce.toRoutes({
          '/302': (request, response) => {
            response.statusCode = 302;
            response.setHeader('Location', '/200');
            response.end();
          },
          '/200': (request, response) => {
            response.statusCode = 200;
            response.end();
          }
        });
        const urlRequest = net.request({
          url: `${serverUrl}${requestUrl}`
        });
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
      });

      test('should follow redirect chain when no redirect handler is provided', async () => {
        const serverUrl = await respondOnce.toRoutes({
          '/redirectChain': (request, response) => {
            response.statusCode = 302;
            response.setHeader('Location', '/302');
            response.end();
          },
          '/302': (request, response) => {
            response.statusCode = 302;
            response.setHeader('Location', '/200');
            response.end();
          },
          '/200': (request, response) => {
            response.statusCode = 200;
            response.end();
          }
        });
        const urlRequest = net.request({
          url: `${serverUrl}/redirectChain`
        });
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
      });

      test('should not follow redirect when request is canceled in redirect handler', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 302;
          response.setHeader('Location', '/200');
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl
        });
        urlRequest.end();
        urlRequest.on('redirect', () => { urlRequest.abort(); });
        urlRequest.on('error', () => {});
        urlRequest.on('response', () => {
          expect.fail('Unexpected response');
        });
        await once(urlRequest, 'abort');
      });

      test('should not follow redirect when mode is error', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 302;
          response.setHeader('Location', '/200');
          response.end();
        });
        const urlRequest = net.request({
          url: serverUrl,
          redirect: 'error'
        });
        urlRequest.end();
        await once(urlRequest, 'error');
      });

      test('should follow redirect when handler calls callback', async () => {
        const serverUrl = await respondOnce.toRoutes({
          '/redirectChain': (request, response) => {
            response.statusCode = 302;
            response.setHeader('Location', '/302');
            response.end();
          },
          '/302': (request, response) => {
            response.statusCode = 302;
            response.setHeader('Location', '/200');
            response.end();
          },
          '/200': (request, response) => {
            response.statusCode = 200;
            response.end();
          }
        });
        const urlRequest = net.request({ url: `${serverUrl}/redirectChain`, redirect: 'manual' });
        const redirects: string[] = [];
        urlRequest.on('redirect', (status, method, url) => {
          redirects.push(url);
          urlRequest.followRedirect();
        });
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(redirects).to.deep.equal([
          `${serverUrl}/302`,
          `${serverUrl}/200`
        ]);
      });

      test('should be able to create a request with options', async () => {
        const customHeaderName = 'Some-Custom-Header-Name';
        const customHeaderValue = 'Some-Customer-Header-Value';
        const serverUrlUnparsed = await respondOnce.toURL('/', (request, response) => {
          expect(request.method).to.equal('GET');
          expect(request.headers[customHeaderName.toLowerCase()]).to.equal(customHeaderValue);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const serverUrl = url.parse(serverUrlUnparsed);
        const urlRequest = net.request({
          port: serverUrl.port ? parseInt(serverUrl.port, 10) : undefined,
          hostname: '127.0.0.1',
          headers: { [customHeaderName]: customHeaderValue }
        });
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.be.equal(200);
        await collectStreamBody(response);
      });

      test('should be able to pipe a readable stream into a net request', async () => {
        const bodyData = randomString(kOneMegaByte);
        let netRequestReceived = false;
        let netRequestEnded = false;

        const [nodeServerUrl, netServerUrl] = await Promise.all([
          respondOnce.toSingleURL((request, response) => response.end(bodyData)),
          respondOnce.toSingleURL((request, response) => {
            netRequestReceived = true;
            let receivedBodyData = '';
            request.on('data', (chunk) => {
              receivedBodyData += chunk.toString();
            });
            request.on('end', (chunk: Buffer | undefined) => {
              netRequestEnded = true;
              if (chunk) {
                receivedBodyData += chunk.toString();
              }
              expect(receivedBodyData).to.be.equal(bodyData);
              response.end();
            });
          })
        ]);
        const nodeRequest = http.request(nodeServerUrl);
        const nodeResponse = await getResponse(nodeRequest as any) as any as http.ServerResponse;
        const netRequest = net.request(netServerUrl);
        const responsePromise = once(netRequest, 'response');
        // TODO(@MarshallOfSound) - FIXME with #22730
        nodeResponse.pipe(netRequest as any);
        const [netResponse] = await responsePromise;
        expect(netResponse.statusCode).to.equal(200);
        await collectStreamBody(netResponse);
        expect(netRequestReceived).to.be.true('net request received');
        expect(netRequestEnded).to.be.true('net request ended');
      });

      test('should report upload progress', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.end();
        });
        const netRequest = net.request({ url: serverUrl, method: 'POST' });
        expect(netRequest.getUploadProgress()).to.have.property('active', false);
        netRequest.end(Buffer.from('hello'));
        const [position, total] = await once(netRequest, 'upload-progress');
        expect(netRequest.getUploadProgress()).to.deep.equal({ active: true, started: true, current: position, total });
      });

      test('should emit error event on server socket destroy', async () => {
        const serverUrl = await respondOnce.toSingleURL((request) => {
          request.socket.destroy();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.end();
        const [error] = await once(urlRequest, 'error');
        expect(error.message).to.equal('net::ERR_EMPTY_RESPONSE');
      });

      test('should emit error event on server request destroy', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          request.destroy();
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.end(randomBuffer(kOneMegaByte));
        const [error] = await once(urlRequest, 'error');
        expect(error.message).to.be.oneOf(['net::ERR_FAILED', 'net::ERR_CONNECTION_RESET', 'net::ERR_CONNECTION_ABORTED']);
      });

      test('should not emit any event after close', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.end();
        });

        const urlRequest = net.request(serverUrl);
        urlRequest.end();

        await once(urlRequest, 'close');
        await new Promise((resolve, reject) => {
          for (const evName of ['finish', 'abort', 'close', 'error']) {
            urlRequest.on(evName as any, () => {
              reject(new Error(`Unexpected ${evName} event`));
            });
          }
          setTimeout(50).then(resolve);
        });
      });

      test('should remove the referer header when no referrer url specified', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers.referer).to.equal(undefined);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.end();

        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });

      test('should set the referer header when a referrer url specified', async () => {
        const referrerURL = 'https://www.electronjs.org/';
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          expect(request.headers.referer).to.equal(referrerURL);
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.end();
        });
        // The referrerPolicy must be unsafe-url because the referrer's origin
        // doesn't match the loaded page. With the default referrer policy
        // (strict-origin-when-cross-origin), the request will be canceled by the
        // network service when the referrer header is invalid.
        // See:
        // - https://source.chromium.org/chromium/chromium/src/+/main:net/url_request/url_request.cc;l=682-683;drc=ae587fa7cd2e5cc308ce69353ee9ce86437e5d41
        // - https://source.chromium.org/chromium/chromium/src/+/main:services/network/public/mojom/network_context.mojom;l=316-318;drc=ae5c7fcf09509843c1145f544cce3a61874b9698
        // - https://w3c.github.io/webappsec-referrer-policy/#determine-requests-referrer
        const urlRequest = net.request({ url: serverUrl, referrerPolicy: 'unsafe-url' });
        urlRequest.setHeader('referer', referrerURL);
        urlRequest.end();

        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
      });
    });

    describe('IncomingMessage API', () => {
      test('response object should implement the IncomingMessage API', async () => {
        const customHeaderName = 'Some-Custom-Header-Name';
        const customHeaderValue = 'Some-Customer-Header-Value';

        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader(customHeaderName, customHeaderValue);
          response.end();
        });

        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);

        expect(response.statusCode).to.equal(200);
        expect(response.statusMessage).to.equal('OK');

        const headers = response.headers;
        expect(headers).to.be.an('object');
        const headerValue = headers[customHeaderName.toLowerCase()];
        expect(headerValue).to.equal(customHeaderValue);

        const rawHeaders = response.rawHeaders;
        expect(rawHeaders).to.be.an('array');
        expect(rawHeaders[0]).to.equal(customHeaderName);
        expect(rawHeaders[1]).to.equal(customHeaderValue);

        const httpVersion = response.httpVersion;
        expect(httpVersion).to.be.a('string').and.to.have.lengthOf.at.least(1);

        const httpVersionMajor = response.httpVersionMajor;
        expect(httpVersionMajor).to.be.a('number').and.to.be.at.least(1);

        const httpVersionMinor = response.httpVersionMinor;
        expect(httpVersionMinor).to.be.a('number').and.to.be.at.least(0);

        await collectStreamBody(response);
      });

      test('should discard duplicate headers', async () => {
        const includedHeader = 'max-forwards';
        const discardableHeader = 'Max-Forwards';

        const includedHeaderValue = 'max-fwds-val';
        const discardableHeaderValue = 'max-fwds-val-two';

        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader(discardableHeader, discardableHeaderValue);
          response.setHeader(includedHeader, includedHeaderValue);
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(response.statusMessage).to.equal('OK');

        const headers = response.headers;
        expect(headers).to.be.an('object');

        expect(headers).to.have.property(includedHeader);
        expect(headers).to.not.have.property(discardableHeader);
        expect(headers[includedHeader]).to.equal(includedHeaderValue);

        await collectStreamBody(response);
      });

      test('should join repeated non-discardable header values with ,', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader('referrer-policy', ['first-text', 'second-text']);
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(response.statusMessage).to.equal('OK');

        const headers = response.headers;
        expect(headers).to.be.an('object');
        expect(headers).to.have.property('referrer-policy');
        expect(headers['referrer-policy']).to.equal('first-text, second-text');

        await collectStreamBody(response);
      });

      test('should not join repeated discardable header values with ,', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader('last-modified', ['yesterday', 'today']);
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(response.statusMessage).to.equal('OK');

        const headers = response.headers;
        expect(headers).to.be.an('object');
        expect(headers).to.have.property('last-modified');
        expect(headers['last-modified']).to.equal('yesterday');

        await collectStreamBody(response);
      });

      test('should make set-cookie header an array even if single value', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader('set-cookie', 'chocolate-chip');
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(response.statusMessage).to.equal('OK');

        const headers = response.headers;
        expect(headers).to.be.an('object');
        expect(headers).to.have.property('set-cookie');
        expect(headers['set-cookie']).to.be.an('array');
        expect(headers['set-cookie'][0]).to.equal('chocolate-chip');

        await collectStreamBody(response);
      });

      test('should keep set-cookie header an array when an array', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader('set-cookie', ['chocolate-chip', 'oatmeal']);
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(response.statusMessage).to.equal('OK');

        const headers = response.headers;
        expect(headers).to.be.an('object');
        expect(headers).to.have.property('set-cookie');
        expect(headers['set-cookie']).to.be.an('array');
        expect(headers['set-cookie'][0]).to.equal('chocolate-chip');
        expect(headers['set-cookie'][1]).to.equal('oatmeal');

        await collectStreamBody(response);
      });

      test('should lowercase header keys', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          response.setHeader('HEADER-KEY', ['header-value']);
          response.setHeader('SeT-CookiE', ['chocolate-chip', 'oatmeal']);
          response.setHeader('rEFERREr-pOLICy', ['first-text', 'second-text']);
          response.setHeader('LAST-modified', 'yesterday');

          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(response.statusMessage).to.equal('OK');

        const headers = response.headers;
        expect(headers).to.be.an('object');

        expect(headers).to.have.property('header-key');
        expect(headers).to.have.property('set-cookie');
        expect(headers).to.have.property('referrer-policy');
        expect(headers).to.have.property('last-modified');

        await collectStreamBody(response);
      });

      test('should return correct raw headers', async () => {
        const customHeaders: [string, string|string[]][] = [
          ['HEADER-KEY-ONE', 'header-value-one'],
          ['set-cookie', 'chocolate-chip'],
          ['header-key-two', 'header-value-two'],
          ['referrer-policy', ['first-text', 'second-text']],
          ['HEADER-KEY-THREE', 'header-value-three'],
          ['last-modified', ['first-text', 'second-text']],
          ['header-key-four', 'header-value-four']
        ];

        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.statusCode = 200;
          response.statusMessage = 'OK';
          for (const headerTuple of customHeaders) {
            response.setHeader(headerTuple[0], headerTuple[1]);
          }
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        expect(response.statusMessage).to.equal('OK');

        const rawHeaders = response.rawHeaders;
        expect(rawHeaders).to.be.an('array');

        let rawHeadersIdx = 0;
        for (const headerTuple of customHeaders) {
          const headerKey = headerTuple[0];
          const headerValues = Array.isArray(headerTuple[1]) ? headerTuple[1] : [headerTuple[1]];
          for (const headerValue of headerValues) {
            expect(rawHeaders[rawHeadersIdx]).to.equal(headerKey);
            expect(rawHeaders[rawHeadersIdx + 1]).to.equal(headerValue);
            rawHeadersIdx += 2;
          }
        }

        await collectStreamBody(response);
      });

      test('should be able to pipe a net response into a writable stream', async () => {
        const bodyData = randomString(kOneKiloByte);
        let nodeRequestProcessed = false;
        const [netServerUrl, nodeServerUrl] = await Promise.all([
          respondOnce.toSingleURL((request, response) => response.end(bodyData)),
          respondOnce.toSingleURL(async (request, response) => {
            const receivedBodyData = await collectStreamBody(request);
            expect(receivedBodyData).to.be.equal(bodyData);
            nodeRequestProcessed = true;
            response.end();
          })
        ]);
        const netRequest = net.request(netServerUrl);
        const netResponse = await getResponse(netRequest);
        const serverUrl = url.parse(nodeServerUrl);
        const nodeOptions = {
          method: 'POST',
          path: serverUrl.path,
          port: serverUrl.port
        };
        const nodeRequest = http.request(nodeOptions);
        const nodeResponsePromise = once(nodeRequest, 'response');
        // TODO(@MarshallOfSound) - FIXME with #22730
        (netResponse as any).pipe(nodeRequest);
        const [nodeResponse] = await nodeResponsePromise;
        netRequest.end();
        await collectStreamBody(nodeResponse);
        expect(nodeRequestProcessed).to.equal(true);
      });

      test('should correctly throttle an incoming stream', async () => {
        let numChunksSent = 0;
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          const data = randomString(kOneMegaByte);
          const write = () => {
            let ok = true;
            do {
              numChunksSent++;
              if (numChunksSent > 30) return;
              ok = response.write(data);
            } while (ok);
            response.once('drain', write);
          };
          write();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.on('response', () => {});
        urlRequest.end();
        await setTimeout(2000);
        // TODO(nornagon): I think this ought to max out at 20, but in practice
        // it seems to exceed that sometimes. This is at 25 to avoid test flakes,
        // but we should investigate if there's actually something broken here and
        // if so fix it and reset this to max at 20, and if not then delete this
        // comment.
        expect(numChunksSent).to.be.at.most(25);
      });
    });

    describe('net.isOnline', () => {
      test('getter returns boolean', () => {
        expect(net.isOnline()).to.be.a('boolean');
      });

      test('property returns boolean', () => {
        expect(net.online).to.be.a('boolean');
      });
    });

    describe('Stability and performance', () => {
      test('should free unreferenced, never-started request objects without crash', async () => {
        net.request('https://test');
        await new Promise<void>((resolve) => {
          process.nextTick(() => {
            const v8Util = process._linkedBinding('electron_common_v8_util');
            v8Util.requestGarbageCollectionForTesting();
            resolve();
          });
        });
      });

      test('should collect on-going requests without crash', async () => {
        let finishResponse: (() => void) | null = null;
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.write(randomString(kOneKiloByte));
          finishResponse = () => {
            response.write(randomString(kOneKiloByte));
            response.end();
          };
        });
        const urlRequest = net.request(serverUrl);
        const response = await getResponse(urlRequest);
        process.nextTick(() => {
          // Trigger a garbage collection.
          const v8Util = process._linkedBinding('electron_common_v8_util');
          v8Util.requestGarbageCollectionForTesting();
          finishResponse!();
        });
        await collectStreamBody(response);
      });

      test('should collect unreferenced, ended requests without crash', async () => {
        const serverUrl = await respondOnce.toSingleURL((request, response) => {
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        process.nextTick(() => {
          const v8Util = process._linkedBinding('electron_common_v8_util');
          v8Util.requestGarbageCollectionForTesting();
        });
        const response = await getResponse(urlRequest);
        await collectStreamBody(response);
      });

      test('should finish sending data when urlRequest is unreferenced', async () => {
        const serverUrl = await respondOnce.toSingleURL(async (request, response) => {
          const received = await collectStreamBodyBuffer(request);
          expect(received.length).to.equal(kOneMegaByte);
          response.end();
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.on('close', () => {
          process.nextTick(() => {
            const v8Util = process._linkedBinding('electron_common_v8_util');
            v8Util.requestGarbageCollectionForTesting();
          });
        });
        urlRequest.write(randomBuffer(kOneMegaByte));
        const response = await getResponse(urlRequest);
        await collectStreamBody(response);
      });

      test('should finish sending data when urlRequest is unreferenced for chunked encoding', async () => {
        const serverUrl = await respondOnce.toSingleURL(async (request, response) => {
          const received = await collectStreamBodyBuffer(request);
          response.end();
          expect(received.length).to.equal(kOneMegaByte);
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.chunkedEncoding = true;
        urlRequest.write(randomBuffer(kOneMegaByte));
        const response = await getResponse(urlRequest);
        await collectStreamBody(response);
        process.nextTick(() => {
          const v8Util = process._linkedBinding('electron_common_v8_util');
          v8Util.requestGarbageCollectionForTesting();
        });
      });

      test('should finish sending data when urlRequest is unreferenced before close event for chunked encoding', async () => {
        const serverUrl = await respondOnce.toSingleURL(async (request, response) => {
          const received = await collectStreamBodyBuffer(request);
          response.end();
          expect(received.length).to.equal(kOneMegaByte);
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.chunkedEncoding = true;
        urlRequest.write(randomBuffer(kOneMegaByte));
        const v8Util = process._linkedBinding('electron_common_v8_util');
        v8Util.requestGarbageCollectionForTesting();
        await collectStreamBody(await getResponse(urlRequest));
      });

      test('should finish sending data when urlRequest is unreferenced', async () => {
        const serverUrl = await respondOnce.toSingleURL(async (request, response) => {
          const received = await collectStreamBodyBuffer(request);
          response.end();
          expect(received.length).to.equal(kOneMegaByte);
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.on('close', () => {
          process.nextTick(() => {
            const v8Util = process._linkedBinding('electron_common_v8_util');
            v8Util.requestGarbageCollectionForTesting();
          });
        });
        urlRequest.write(randomBuffer(kOneMegaByte));
        await collectStreamBody(await getResponse(urlRequest));
      });

      test('should finish sending data when urlRequest is unreferenced for chunked encoding', async () => {
        const serverUrl = await respondOnce.toSingleURL(async (request, response) => {
          const received = await collectStreamBodyBuffer(request);
          response.end();
          expect(received.length).to.equal(kOneMegaByte);
        });
        const urlRequest = net.request(serverUrl);
        urlRequest.on('close', () => {
          process.nextTick(() => {
            const v8Util = process._linkedBinding('electron_common_v8_util');
            v8Util.requestGarbageCollectionForTesting();
          });
        });
        urlRequest.chunkedEncoding = true;
        urlRequest.write(randomBuffer(kOneMegaByte));
        await collectStreamBody(await getResponse(urlRequest));
      });
    });

    describe('non-http schemes', () => {
      test('should be rejected by net.request', async () => {
        expect(() => {
          net.request('file://bar');
        }).to.throw('ClientRequest only supports http: and https: protocols');
      });

      test('should be rejected by net.request when passed in url:', async () => {
        expect(() => {
          net.request({ url: 'file://bar' });
        }).to.throw('ClientRequest only supports http: and https: protocols');
      });
    });

    describe('net.fetch', () => {
      // NB. there exist much more comprehensive tests for fetch() in the form of
      // the WPT: https://github.com/web-platform-tests/wpt/tree/master/fetch
      // It's possible to run these tests against net.fetch(), but the test
      // harness to do so is quite complex and hasn't been munged to smoothly run
      // inside the Electron test runner yet.
      //
      // In the meantime, here are some tests for basic functionality and
      // Electron-specific behavior.

      describe('basic', () => {
        test('can fetch http urls', async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            response.end('test');
          });
          const resp = await net.fetch(serverUrl);
          expect(resp.ok).to.be.true();
          expect(await resp.text()).to.equal('test');
        });

        test('can upload a string body', async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            request.on('data', chunk => response.write(chunk));
            request.on('end', () => response.end());
          });
          const resp = await net.fetch(serverUrl, {
            method: 'POST',
            body: 'anchovies'
          });
          expect(await resp.text()).to.equal('anchovies');
        });

        test('can read response as an array buffer', async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            request.on('data', chunk => response.write(chunk));
            request.on('end', () => response.end());
          });
          const resp = await net.fetch(serverUrl, {
            method: 'POST',
            body: 'anchovies'
          });
          expect(new TextDecoder().decode(new Uint8Array(await resp.arrayBuffer()))).to.equal('anchovies');
        });

        test('can read response as form data', async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            response.setHeader('content-type', 'application/x-www-form-urlencoded');
            response.end('foo=bar');
          });
          const resp = await net.fetch(serverUrl);
          const result = await resp.formData();
          expect(result.get('foo')).to.equal('bar');
        });

        test('should reject promise on DNS failure', async () => {
          const r = net.fetch('https://i.do.not.exist');
          await expect(r).to.be.rejectedWith(/ERR_NAME_NOT_RESOLVED/);
        });

        test('should reject body promise when stream fails', async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            response.write('first chunk');
            setTimeout().then(() => response.destroy());
          });
          const r = await net.fetch(serverUrl);
          expect(r.status).to.equal(200);
          await expect(r.text()).to.be.rejectedWith(/ERR_INCOMPLETE_CHUNKED_ENCODING/);
        });
      });
    });

    describe('net.resolveHost', () => {
      test('resolves ipv4.localhost2', async () => {
        const { endpoints } = await net.resolveHost('ipv4.localhost2');
        expect(endpoints).to.be.a('array');
        expect(endpoints).to.have.lengthOf(1);
        expect(endpoints[0].family).to.equal('ipv4');
        expect(endpoints[0].address).to.equal('10.0.0.1');
      });

      test('fails to resolve AAAA record for ipv4.localhost2', async () => {
        await expect(net.resolveHost('ipv4.localhost2', {
          queryType: 'AAAA'
        }))
          .to.eventually.be.rejectedWith(/net::ERR_NAME_NOT_RESOLVED/);
      });

      test('resolves ipv6.localhost2', async () => {
        const { endpoints } = await net.resolveHost('ipv6.localhost2');
        expect(endpoints).to.be.a('array');
        expect(endpoints).to.have.lengthOf(1);
        expect(endpoints[0].family).to.equal('ipv6');
        expect(endpoints[0].address).to.equal('::1');
      });

      test('fails to resolve A record for ipv6.localhost2', async () => {
        await expect(net.resolveHost('notfound.localhost2', {
          queryType: 'A'
        }))
          .to.eventually.be.rejectedWith(/net::ERR_NAME_NOT_RESOLVED/);
      });

      test('fails to resolve notfound.localhost2', async () => {
        await expect(net.resolveHost('notfound.localhost2'))
          .to.eventually.be.rejectedWith(/net::ERR_NAME_NOT_RESOLVED/);
      });
    });
  }
});
