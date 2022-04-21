const { app, shell } = require('electron');
const path = require('path');

app.whenReady().then(async () => {
  await shell.openPath(path.join(__dirname, 'hello.groot'));
  app.quit();
});
