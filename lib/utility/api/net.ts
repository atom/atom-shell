import { app } from 'electron/main';
import { IncomingMessage } from 'electron/utility';
import type { ClientRequestConstructorOptions } from 'electron/utility';
import { ClientRequest } from '@electron/internal/common/api/net-client-request';
import { fetchWithSession } from '@electron/internal/browser/api/net-fetch';

const { isOnline, resolveHost } = process._linkedBinding('electron_common_net');
const { _ensureNetworkServiceInitialized } = process._linkedBinding('electron_browser_utility_process');

if (app.isReady()) {
  _ensureNetworkServiceInitialized();
}

export function request (options: ClientRequestConstructorOptions | string, callback?: (message: IncomingMessage) => void) {
  if (!app.isReady()) {
    throw new Error('net module can only be used after app is ready');
  }
  return new ClientRequest(options, callback);
}

export function fetch (input: RequestInfo, init?: RequestInit): Promise<Response> {
  return fetchWithSession(input, init, undefined, request);
}

exports.resolveHost = resolveHost;

exports.isOnline = isOnline;

Object.defineProperty(exports, 'online', {
  get: () => isOnline()
});
