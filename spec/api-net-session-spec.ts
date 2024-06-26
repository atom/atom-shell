import { expect } from 'chai';
import * as dns from 'node:dns';
import { net, session, BrowserWindow, ClientRequestConstructorOptions } from 'electron/main';
import { collectStreamBody, getResponse, respondNTimes, respondOnce } from './lib/net-helpers';

// See https://github.com/nodejs/node/issues/40702.
dns.setDefaultResultOrder('ipv4first');

describe('net module (session)', () => {
  beforeEach(() => {
    respondNTimes.routeFailure = false;
  });
  afterEach(async function () {
    await session.defaultSession.clearCache();
    if (respondNTimes.routeFailure && this.test) {
      if (!this.test.isFailed()) {
        throw new Error('Failing this test due an unhandled error in the respondOnce route handler, check the logs above for the actual error');
      }
    }
  });

  describe('HTTP basics', () => {
    for (const extraOptions of [{}, { credentials: 'include' }, { useSessionCookies: false, credentials: 'include' }] as ClientRequestConstructorOptions[]) {
      describe(`authentication when ${JSON.stringify(extraOptions)}`, () => {
        it('should share credentials with WebContents', async () => {
          const [user, pass] = ['user', 'pass'];
          const serverUrl = await respondNTimes.toSingleURL((request, response) => {
            if (!request.headers.authorization) {
              return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
            }
            return response.writeHead(200).end('ok');
          }, 2);
          const bw = new BrowserWindow({ show: false });
          bw.webContents.on('login', (event, details, authInfo, cb) => {
            event.preventDefault();
            cb(user, pass);
          });
          await bw.loadURL(serverUrl);
          bw.close();
          const request = net.request({ method: 'GET', url: serverUrl, ...extraOptions });
          let logInCount = 0;
          request.on('login', () => {
            logInCount++;
          });
          const response = await getResponse(request);
          await collectStreamBody(response);
          expect(logInCount).to.equal(0, 'should not receive a login event, credentials should be cached');
        });

        it('should share proxy credentials with WebContents', async () => {
          const [user, pass] = ['user', 'pass'];
          const proxyUrl = await respondNTimes((request, response) => {
            if (!request.headers['proxy-authorization']) {
              return response.writeHead(407, { 'Proxy-Authenticate': 'Basic realm="Foo"' }).end();
            }
            return response.writeHead(200).end('ok');
          }, 2);
          const customSession = session.fromPartition(`net-proxy-test-${Math.random()}`);
          await customSession.setProxy({ proxyRules: proxyUrl.replace('http://', ''), proxyBypassRules: '<-loopback>' });
          const bw = new BrowserWindow({ show: false, webPreferences: { session: customSession } });
          bw.webContents.on('login', (event, details, authInfo, cb) => {
            event.preventDefault();
            cb(user, pass);
          });
          await bw.loadURL('http://127.0.0.1:9999');
          bw.close();
          const request = net.request({ method: 'GET', url: 'http://127.0.0.1:9999', session: customSession, ...extraOptions });
          let logInCount = 0;
          request.on('login', () => {
            logInCount++;
          });
          const response = await getResponse(request);
          const body = await collectStreamBody(response);
          expect(response.statusCode).to.equal(200);
          expect(body).to.equal('ok');
          expect(logInCount).to.equal(0, 'should not receive a login event, credentials should be cached');
        });
      });
    }

    describe('authentication when {"credentials":"omit"}', () => {
      it('should not share credentials with WebContents', async () => {
        const [user, pass] = ['user', 'pass'];
        const serverUrl = await respondNTimes.toSingleURL((request, response) => {
          if (!request.headers.authorization) {
            return response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Foo"' }).end();
          }
          return response.writeHead(200).end('ok');
        }, 2);
        const bw = new BrowserWindow({ show: false });
        bw.webContents.on('login', (event, details, authInfo, cb) => {
          event.preventDefault();
          cb(user, pass);
        });
        await bw.loadURL(serverUrl);
        bw.close();
        const request = net.request({ method: 'GET', url: serverUrl, credentials: 'omit' });
        request.on('login', () => {
          expect.fail();
        });
        const response = await getResponse(request);
        expect(response.statusCode).to.equal(401);
        expect(response.headers['www-authenticate']).to.equal('Basic realm="Foo"');
      });

      it('should share proxy credentials with WebContents', async () => {
        const [user, pass] = ['user', 'pass'];
        const proxyUrl = await respondNTimes((request, response) => {
          if (!request.headers['proxy-authorization']) {
            return response.writeHead(407, { 'Proxy-Authenticate': 'Basic realm="Foo"' }).end();
          }
          return response.writeHead(200).end('ok');
        }, 2);
        const customSession = session.fromPartition(`net-proxy-test-${Math.random()}`);
        await customSession.setProxy({ proxyRules: proxyUrl.replace('http://', ''), proxyBypassRules: '<-loopback>' });
        const bw = new BrowserWindow({ show: false, webPreferences: { session: customSession } });
        bw.webContents.on('login', (event, details, authInfo, cb) => {
          event.preventDefault();
          cb(user, pass);
        });
        await bw.loadURL('http://127.0.0.1:9999');
        bw.close();
        const request = net.request({ method: 'GET', url: 'http://127.0.0.1:9999', session: customSession, credentials: 'omit' });
        request.on('login', () => {
          expect.fail();
        });
        const response = await getResponse(request);
        const body = await collectStreamBody(response);
        expect(response.statusCode).to.equal(200);
        expect(body).to.equal('ok');
      });
    });
  });

  describe('ClientRequest API', () => {
    it('should be able to set cookie header line', async () => {
      const cookieHeaderName = 'Cookie';
      const cookieHeaderValue = 'test=12345';
      const customSession = session.fromPartition(`test-cookie-header-${Math.random()}`);
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        expect(request.headers[cookieHeaderName.toLowerCase()]).to.equal(cookieHeaderValue);
        response.statusCode = 200;
        response.statusMessage = 'OK';
        response.end();
      });
      await customSession.cookies.set({
        url: `${serverUrl}`,
        name: 'test',
        value: '11111',
        expirationDate: 0
      });
      const urlRequest = net.request({
        method: 'GET',
        url: serverUrl,
        session: customSession
      });
      urlRequest.setHeader(cookieHeaderName, cookieHeaderValue);
      expect(urlRequest.getHeader(cookieHeaderName)).to.equal(cookieHeaderValue);
      const response = await getResponse(urlRequest);
      expect(response.statusCode).to.equal(200);
      await collectStreamBody(response);
    });

    it('should not use the sessions cookie store by default', async () => {
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        response.statusCode = 200;
        response.statusMessage = 'OK';
        response.setHeader('x-cookie', `${request.headers.cookie!}`);
        response.end();
      });
      const sess = session.fromPartition(`cookie-tests-${Math.random()}`);
      const cookieVal = `${Date.now()}`;
      await sess.cookies.set({
        url: serverUrl,
        name: 'wild_cookie',
        value: cookieVal
      });
      const urlRequest = net.request({
        url: serverUrl,
        session: sess
      });
      const response = await getResponse(urlRequest);
      expect(response.headers['x-cookie']).to.equal('undefined');
    });

    for (const extraOptions of [{ useSessionCookies: true }, { credentials: 'include' }] as ClientRequestConstructorOptions[]) {
      describe(`when ${JSON.stringify(extraOptions)}`, () => {
        it('should be able to use the sessions cookie store', async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            response.statusCode = 200;
            response.statusMessage = 'OK';
            response.setHeader('x-cookie', request.headers.cookie!);
            response.end();
          });
          const sess = session.fromPartition(`cookie-tests-${Math.random()}`);
          const cookieVal = `${Date.now()}`;
          await sess.cookies.set({
            url: serverUrl,
            name: 'wild_cookie',
            value: cookieVal
          });
          const urlRequest = net.request({
            url: serverUrl,
            session: sess,
            ...extraOptions
          });
          const response = await getResponse(urlRequest);
          expect(response.headers['x-cookie']).to.equal(`wild_cookie=${cookieVal}`);
        });

        it('should be able to use the sessions cookie store with set-cookie', async () => {
          const serverUrl = await respondOnce.toSingleURL((request, response) => {
            response.statusCode = 200;
            response.statusMessage = 'OK';
            response.setHeader('set-cookie', 'foo=bar');
            response.end();
          });
          const sess = session.fromPartition(`cookie-tests-${Math.random()}`);
          let cookies = await sess.cookies.get({});
          expect(cookies).to.have.lengthOf(0);
          const urlRequest = net.request({
            url: serverUrl,
            session: sess,
            ...extraOptions
          });
          await collectStreamBody(await getResponse(urlRequest));
          cookies = await sess.cookies.get({});
          expect(cookies).to.have.lengthOf(1);
          expect(cookies[0]).to.deep.equal({
            name: 'foo',
            value: 'bar',
            domain: '127.0.0.1',
            hostOnly: true,
            path: '/',
            secure: false,
            httpOnly: false,
            session: true,
            sameSite: 'unspecified'
          });
        });

        for (const mode of ['Lax', 'Strict']) {
          it(`should be able to use the sessions cookie store with same-site ${mode} cookies`, async () => {
            const serverUrl = await respondNTimes.toSingleURL((request, response) => {
              response.statusCode = 200;
              response.statusMessage = 'OK';
              response.setHeader('set-cookie', `same=site; SameSite=${mode}`);
              response.setHeader('x-cookie', `${request.headers.cookie}`);
              response.end();
            }, 2);
            const sess = session.fromPartition(`cookie-tests-${Math.random()}`);
            let cookies = await sess.cookies.get({});
            expect(cookies).to.have.lengthOf(0);
            const urlRequest = net.request({
              url: serverUrl,
              session: sess,
              ...extraOptions
            });
            const response = await getResponse(urlRequest);
            expect(response.headers['x-cookie']).to.equal('undefined');
            await collectStreamBody(response);
            cookies = await sess.cookies.get({});
            expect(cookies).to.have.lengthOf(1);
            expect(cookies[0]).to.deep.equal({
              name: 'same',
              value: 'site',
              domain: '127.0.0.1',
              hostOnly: true,
              path: '/',
              secure: false,
              httpOnly: false,
              session: true,
              sameSite: mode.toLowerCase()
            });
            const urlRequest2 = net.request({
              url: serverUrl,
              session: sess,
              ...extraOptions
            });
            const response2 = await getResponse(urlRequest2);
            expect(response2.headers['x-cookie']).to.equal('same=site');
          });
        }

        it('should be able to use the sessions cookie store safely across redirects', async () => {
          const serverUrl = await respondOnce.toSingleURL(async (request, response) => {
            response.statusCode = 302;
            response.statusMessage = 'Moved';
            const newUrl = await respondOnce.toSingleURL((req, res) => {
              res.statusCode = 200;
              res.statusMessage = 'OK';
              res.setHeader('x-cookie', req.headers.cookie!);
              res.end();
            });
            response.setHeader('x-cookie', request.headers.cookie!);
            response.setHeader('location', newUrl.replace('127.0.0.1', 'localhost'));
            response.end();
          });
          const sess = session.fromPartition(`cookie-tests-${Math.random()}`);
          const cookie127Val = `${Date.now()}-127`;
          const cookieLocalVal = `${Date.now()}-local`;
          const localhostUrl = serverUrl.replace('127.0.0.1', 'localhost');
          expect(localhostUrl).to.not.equal(serverUrl);
          // cookies with lax or strict same-site settings will not
          // persist after redirects. no_restriction must be used
          await Promise.all([
            sess.cookies.set({
              url: serverUrl,
              name: 'wild_cookie',
              sameSite: 'no_restriction',
              value: cookie127Val
            }), sess.cookies.set({
              url: localhostUrl,
              name: 'wild_cookie',
              sameSite: 'no_restriction',
              value: cookieLocalVal
            })
          ]);
          const urlRequest = net.request({
            url: serverUrl,
            session: sess,
            ...extraOptions
          });
          urlRequest.on('redirect', (status, method, url, headers) => {
            // The initial redirect response should have received the 127 value here
            expect(headers['x-cookie'][0]).to.equal(`wild_cookie=${cookie127Val}`);
            urlRequest.followRedirect();
          });
          const response = await getResponse(urlRequest);
          // We expect the server to have received the localhost value here
          // The original request was to a 127.0.0.1 URL
          // That request would have the cookie127Val cookie attached
          // The request is then redirect to a localhost URL (different site)
          // Because we are using the session cookie store it should do the safe / secure thing
          // and attach the cookies for the new target domain
          expect(response.headers['x-cookie']).to.equal(`wild_cookie=${cookieLocalVal}`);
        });
      });
    }

    it('should be able correctly filter out cookies that are secure', async () => {
      const sess = session.fromPartition(`cookie-tests-${Math.random()}`);

      await Promise.all([
        sess.cookies.set({
          url: 'https://electronjs.org',
          domain: 'electronjs.org',
          name: 'cookie1',
          value: '1',
          secure: true
        }),
        sess.cookies.set({
          url: 'https://electronjs.org',
          domain: 'electronjs.org',
          name: 'cookie2',
          value: '2',
          secure: false
        })
      ]);

      const secureCookies = await sess.cookies.get({
        secure: true
      });
      expect(secureCookies).to.have.lengthOf(1);
      expect(secureCookies[0].name).to.equal('cookie1');

      const cookies = await sess.cookies.get({
        secure: false
      });
      expect(cookies).to.have.lengthOf(1);
      expect(cookies[0].name).to.equal('cookie2');
    });

    it('throws when an invalid domain is passed', async () => {
      const sess = session.fromPartition(`cookie-tests-${Math.random()}`);

      await expect(sess.cookies.set({
        url: 'https://electronjs.org',
        domain: 'wssss.iamabaddomain.fun',
        name: 'cookie1'
      })).to.eventually.be.rejectedWith(/Failed to set cookie - The cookie was set with an invalid Domain attribute./);
    });

    it('should be able correctly filter out cookies that are session', async () => {
      const sess = session.fromPartition(`cookie-tests-${Math.random()}`);

      await Promise.all([
        sess.cookies.set({
          url: 'https://electronjs.org',
          domain: 'electronjs.org',
          name: 'cookie1',
          value: '1'
        }),
        sess.cookies.set({
          url: 'https://electronjs.org',
          domain: 'electronjs.org',
          name: 'cookie2',
          value: '2',
          expirationDate: Math.round(Date.now() / 1000) + 10000
        })
      ]);

      const sessionCookies = await sess.cookies.get({
        session: true
      });
      expect(sessionCookies).to.have.lengthOf(1);
      expect(sessionCookies[0].name).to.equal('cookie1');

      const cookies = await sess.cookies.get({
        session: false
      });
      expect(cookies).to.have.lengthOf(1);
      expect(cookies[0].name).to.equal('cookie2');
    });

    it('should be able correctly filter out cookies that are httpOnly', async () => {
      const sess = session.fromPartition(`cookie-tests-${Math.random()}`);

      await Promise.all([
        sess.cookies.set({
          url: 'https://electronjs.org',
          domain: 'electronjs.org',
          name: 'cookie1',
          value: '1',
          httpOnly: true
        }),
        sess.cookies.set({
          url: 'https://electronjs.org',
          domain: 'electronjs.org',
          name: 'cookie2',
          value: '2',
          httpOnly: false
        })
      ]);

      const httpOnlyCookies = await sess.cookies.get({
        httpOnly: true
      });
      expect(httpOnlyCookies).to.have.lengthOf(1);
      expect(httpOnlyCookies[0].name).to.equal('cookie1');

      const cookies = await sess.cookies.get({
        httpOnly: false
      });
      expect(cookies).to.have.lengthOf(1);
      expect(cookies[0].name).to.equal('cookie2');
    });

    describe('webRequest', () => {
      afterEach(() => {
        session.defaultSession.webRequest.onBeforeRequest(null);
      });

      it('Should throw when invalid filters are passed', () => {
        expect(() => {
          session.defaultSession.webRequest.onBeforeRequest(
            { urls: ['*://www.googleapis.com'] },
            (details, callback) => { callback({ cancel: false }); }
          );
        }).to.throw('Invalid url pattern *://www.googleapis.com: Empty path.');

        expect(() => {
          session.defaultSession.webRequest.onBeforeRequest(
            { urls: ['*://www.googleapis.com/', '*://blahblah.dev'] },
            (details, callback) => { callback({ cancel: false }); }
          );
        }).to.throw('Invalid url pattern *://blahblah.dev: Empty path.');
      });

      it('Should not throw when valid filters are passed', () => {
        expect(() => {
          session.defaultSession.webRequest.onBeforeRequest(
            { urls: ['*://www.googleapis.com/'] },
            (details, callback) => { callback({ cancel: false }); }
          );
        }).to.not.throw();
      });

      it('Requests should be intercepted by webRequest module', async () => {
        const requestUrl = '/requestUrl';
        const redirectUrl = '/redirectUrl';
        let requestIsRedirected = false;
        const serverUrl = await respondOnce.toURL(redirectUrl, (request, response) => {
          requestIsRedirected = true;
          response.end();
        });
        let requestIsIntercepted = false;
        session.defaultSession.webRequest.onBeforeRequest(
          (details, callback) => {
            if (details.url === `${serverUrl}${requestUrl}`) {
              requestIsIntercepted = true;
              callback({
                redirectURL: `${serverUrl}${redirectUrl}`
              });
            } else {
              callback({
                cancel: false
              });
            }
          });

        const urlRequest = net.request(`${serverUrl}${requestUrl}`);
        const response = await getResponse(urlRequest);

        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
        expect(requestIsRedirected).to.be.true('The server should receive a request to the forward URL');
        expect(requestIsIntercepted).to.be.true('The request should be intercepted by the webRequest module');
      });

      it('should to able to create and intercept a request using a custom session object', async () => {
        const requestUrl = '/requestUrl';
        const redirectUrl = '/redirectUrl';
        const customPartitionName = `custom-partition-${Math.random()}`;
        let requestIsRedirected = false;
        const serverUrl = await respondOnce.toURL(redirectUrl, (request, response) => {
          requestIsRedirected = true;
          response.end();
        });
        session.defaultSession.webRequest.onBeforeRequest(() => {
          expect.fail('Request should not be intercepted by the default session');
        });

        const customSession = session.fromPartition(customPartitionName, { cache: false });
        let requestIsIntercepted = false;
        customSession.webRequest.onBeforeRequest((details, callback) => {
          if (details.url === `${serverUrl}${requestUrl}`) {
            requestIsIntercepted = true;
            callback({
              redirectURL: `${serverUrl}${redirectUrl}`
            });
          } else {
            callback({
              cancel: false
            });
          }
        });

        const urlRequest = net.request({
          url: `${serverUrl}${requestUrl}`,
          session: customSession
        });
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
        expect(requestIsRedirected).to.be.true('The server should receive a request to the forward URL');
        expect(requestIsIntercepted).to.be.true('The request should be intercepted by the webRequest module');
      });

      it('should to able to create and intercept a request using a custom partition name', async () => {
        const requestUrl = '/requestUrl';
        const redirectUrl = '/redirectUrl';
        const customPartitionName = `custom-partition-${Math.random()}`;
        let requestIsRedirected = false;
        const serverUrl = await respondOnce.toURL(redirectUrl, (request, response) => {
          requestIsRedirected = true;
          response.end();
        });
        session.defaultSession.webRequest.onBeforeRequest(() => {
          expect.fail('Request should not be intercepted by the default session');
        });

        const customSession = session.fromPartition(customPartitionName, { cache: false });
        let requestIsIntercepted = false;
        customSession.webRequest.onBeforeRequest((details, callback) => {
          if (details.url === `${serverUrl}${requestUrl}`) {
            requestIsIntercepted = true;
            callback({
              redirectURL: `${serverUrl}${redirectUrl}`
            });
          } else {
            callback({
              cancel: false
            });
          }
        });

        const urlRequest = net.request({
          url: `${serverUrl}${requestUrl}`,
          partition: customPartitionName
        });
        const response = await getResponse(urlRequest);
        expect(response.statusCode).to.equal(200);
        await collectStreamBody(response);
        expect(requestIsRedirected).to.be.true('The server should receive a request to the forward URL');
        expect(requestIsIntercepted).to.be.true('The request should be intercepted by the webRequest module');
      });

      it('triggers webRequest handlers when bypassCustomProtocolHandlers', async () => {
        let webRequestDetails: Electron.OnBeforeRequestListenerDetails | null = null;
        const serverUrl = await respondOnce.toSingleURL((req, res) => res.end('hi'));
        session.defaultSession.webRequest.onBeforeRequest((details, cb) => {
          webRequestDetails = details;
          cb({});
        });
        const body = await net.fetch(serverUrl, { bypassCustomProtocolHandlers: true }).then(r => r.text());
        expect(body).to.equal('hi');
        expect(webRequestDetails).to.have.property('url', serverUrl);
      });
    });

    it('should throw if given an invalid session option', () => {
      expect(() => {
        net.request({
          url: 'https://foo',
          session: 1 as any
        });
      }).to.throw('`session` should be an instance of the Session class');
    });

    it('should throw if given an invalid partition option', () => {
      expect(() => {
        net.request({
          url: 'https://foo',
          partition: 1 as any
        });
      }).to.throw('`partition` should be a string');
    });

    it('should throw if given a header value that is empty(null/undefined)', () => {
      const emptyHeaderValues = [null, undefined];
      const errorMsg = '`value` required in setHeader("foo", value)';

      for (const emptyValue of emptyHeaderValues) {
        expect(() => {
          net.request({
            url: 'https://foo',
            headers: { foo: emptyValue as any }
          } as any);
        }).to.throw(errorMsg);

        const request = net.request({ url: 'https://foo' });
        expect(() => {
          request.setHeader('foo', emptyValue as any);
        }).to.throw(errorMsg);
      }
    });
  });

  describe('net.fetch', () => {
    it('should be able to use a session cookie store', async () => {
      const serverUrl = await respondOnce.toSingleURL((request, response) => {
        response.statusCode = 200;
        response.statusMessage = 'OK';
        response.setHeader('x-cookie', request.headers.cookie!);
        response.end();
      });
      const sess = session.fromPartition(`cookie-tests-${Math.random()}`);
      const cookieVal = `${Date.now()}`;
      await sess.cookies.set({
        url: serverUrl,
        name: 'wild_cookie',
        value: cookieVal
      });
      const response = await sess.fetch(serverUrl, {
        credentials: 'include'
      });
      expect(response.headers.get('x-cookie')).to.equal(`wild_cookie=${cookieVal}`);
    });
  });
});
