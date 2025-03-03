const { app, BrowserWindow } = require('electron');
const path = require('node:path');

const appPathBase = path.basename(app.getAppPath());
const newPath = path.join(appPathBase, 'lib_alt');

app.setAppPath(newPath);

function report (payload, exitStatus = 0) {
  process.stdout.write(JSON.stringify(payload));
  process.stdout.end();

  process.exit(exitStatus);
}

app.on('ready', () => {
  const win = new BrowserWindow({
    show: false
  });

  win.webContents.on('did-finish-load', () => {
    const payload = {
      appPath: app.getAppPath()
    };

    report(payload);
  });

  win.webContents.on('did-fail-load', (error) => {
    const payload = {
      error: error.message
    };

    report(payload, 1);
  });

  win.loadFile('index.html');
});
