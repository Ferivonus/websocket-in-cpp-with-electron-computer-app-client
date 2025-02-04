const { app, BrowserWindow } = require('electron');

function createWindow() {
  const win = new BrowserWindow({
    width: 800,
    height: 600,
    webPreferences: {
      nodeIntegration: true,
    },
  });

  win.loadFile('index.html'); // HTML dosyasını yükle
  win.webContents.openDevTools(); // Geliştirici araçlarını aç (isteğe bağlı)
}

app.whenReady().then(createWindow);

// Pencere kapatıldığında uygulamayı sonlandır
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});