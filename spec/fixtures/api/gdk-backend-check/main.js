const { app } = require('electron');

app.whenReady().then(() => {
  process.stdout.write(String(process.env.GDK_BACKEND));
  process.stdout.end();

  app.quit();
});
