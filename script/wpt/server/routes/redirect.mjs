import { setTimeout } from 'node:timers/promises';

const stash = new Map();

/**
 * @see https://github.com/web-platform-tests/wpt/blob/master/fetch/connection-pool/resources/network-partition-key.py
 * @param {Parameters<import('http').RequestListener>[0]} req
 * @param {Parameters<import('http').RequestListener>[1]} res
 * @param {URL} fullUrl
 */
export async function route (req, res, fullUrl) {
  const { searchParams } = fullUrl;

  let stashedData = { count: 0, preflight: 0 };
  let status = 302;
  res.setHeader('Content-Type', 'text/plain');
  res.setHeader('Cache-Control', 'no-cache');
  res.setHeader('Pragma', 'no-cache');

  if (Object.hasOwn(req.headers, 'origin')) {
    res.setHeader('Access-Control-Allow-Origin', req.headers.origin ?? '');
    res.setHeader('Access-Control-Allow-Credentials', 'true');
  } else {
    res.setHeader('Access-Control-Allow-Origin', '*');
  }

  let token = null;
  if (searchParams.has('token')) {
    token = searchParams.get('token');
    const data = stash.get(token);
    stash.delete(token);
    if (data) {
      stashedData = data;
    }
  }

  if (req.method === 'OPTIONS') {
    if (searchParams.has('allow_headers')) {
      res.setHeader('Access-Control-Allow-Headers', searchParams.get('allow_headers'));
    }

    stashedData.preflight = '1';

    if (!searchParams.has('redirect_preflight')) {
      if (token) {
        stash.set(searchParams.get('token'), stashedData);
      }

      res.statusCode = 200;
      res.end('');
      return;
    }
  }

  if (searchParams.has('redirect_status')) {
    status = parseInt(searchParams.get('redirect_status'));
  }

  stashedData.count += 1;

  if (searchParams.has('location')) {
    let url = decodeURIComponent(searchParams.get('location'));

    if (!searchParams.has('simple')) {
      const scheme = new URL(url, fullUrl).protocol;

      if (scheme === 'http:' || scheme === 'https:') {
        url += url.includes('?') ? '&' : '?';

        for (const [key, value] of searchParams) {
          url += '&' + encodeURIComponent(key) + '=' + encodeURIComponent(value);
        }

        url += '&count=' + stashedData.count;
      }
    }

    res.setHeader('location', url);
  }

  if (searchParams.has('redirect_referrerpolicy')) {
    res.setHeader('Referrer-Policy', searchParams.get('redirect_referrerpolicy'));
  }

  if (searchParams.has('delay')) {
    await setTimeout(parseFloat(searchParams.get('delay') ?? 0));
  }

  if (token) {
    stash.set(searchParams.get('token'), stashedData);

    if (searchParams.has('max_count')) {
      const maxCount = parseInt(searchParams.get('max_count'));

      if (stashedData.count > maxCount) {
        res.end((stashedData.count - 1).toString());
        return;
      }
    }
  }

  res.statusCode = status;
  res.end('');
}
