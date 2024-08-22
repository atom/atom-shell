const { net } = require('electron');
const serverUrl = process.argv[2].split('=')[1];
let configurableArg = null;
if (process.argv[3]) {
  configurableArg = process.argv[3].split('=')[0];
}
const data = [];

let request = null;
if (configurableArg === '--omit-credentials') {
  request = net.request({ method: 'GET', url: serverUrl, credentials: 'omit' });
} else if (configurableArg === '--use-fetch-api') {
  net.fetch(serverUrl).then((response) => {
    process.parentPort.postMessage([response.status, response.headers]);
  });
} else {
  request = net.request({ method: 'GET', url: serverUrl });
}

if (request) {
  if (configurableArg === '--use-net-login-event') {
    request.on('login', (authInfo, cb) => {
      process.parentPort.postMessage(authInfo);
      cb('user', 'pass');
    });
  }
  request.on('response', (response) => {
    process.parentPort.postMessage([response.statusCode, response.headers]);
    response.on('data', (chunk) => data.push(chunk));
    response.on('end', (chunk) => {
      if (chunk) data.push(chunk);
      process.parentPort.postMessage(Buffer.concat(data).toString());
    });
  });
  if (configurableArg === '--request-data') {
    process.parentPort.on('message', (e) => {
      request.write(e.data);
      request.end();
    });
    process.parentPort.postMessage('get-request-data');
  } else {
    request.end();
  }
}
