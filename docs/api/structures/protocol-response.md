# ProtocolResponse Object

* `error` Integer (optional) - When assigned, the `request` will fail with the
  `error` number . For the available error numbers you can use, please see the
  [net error list][net-error].
* `statusCode` number (optional) - The HTTP response code, default is 200.
* `charset` string (optional) - The charset of response body, default is
  `"utf-8"`.
* `mimeType` string (optional) - The MIME type of response body, default is
  `"text/html"`. Setting `mimeType` would implicitly set the `content-type`
  header in response, but if `content-type` is already set in `headers`, the
  `mimeType` would be ignored.
* `headers` Record\<string, string | string[]\> (optional) - An object containing the response headers. The
  keys must be string, and values must be either string or Array of string.
* `data` (Buffer | string | ReadableStream) (optional) - The response body. When
  returning stream as response, this is a Node.js readable stream representing
  the response body. When returning `Buffer` as response, this is a `Buffer`.
  When returning `string` as response, this is a `string`. This is ignored for
  other types of responses.
* `path` string (optional) - Path to the file which would be sent as response
  body. This is only used for file responses.
* `url` string (optional) - Download the `url` and pipe the result as response
  body. This is only used for URL responses.
* `referrer` string (optional) - The `referrer` URL. This is only used for file
  and URL responses.
* `method` string (optional) - The HTTP `method`. This is only used for file
  and URL responses.
* `session` Session (optional) - The session used for requesting URL, by default
  the HTTP request will reuse the current session. Setting `session` to `null`
  would use a random independent session. This is only used for URL responses.
* `uploadData` [ProtocolResponseUploadData](protocol-response-upload-data.md) (optional) - The data used as upload data. This is only
  used for URL responses when `method` is `"POST"`.

[net-error]: https://source.chromium.org/chromium/chromium/src/+/main:net/base/net_error_list.h
