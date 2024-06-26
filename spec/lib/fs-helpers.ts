import * as cp from 'node:child_process';
import * as fs from 'original-fs';
import * as os from 'node:os';
import * as path from 'node:path';

export async function copyApp (targetDir: string): Promise<string> {
  // On macOS we can just copy the app bundle, easier too because of symlinks
  if (process.platform === 'darwin') {
    const appBundlePath = path.resolve(process.execPath, '../../..');
    const newPath = path.resolve(targetDir, 'Electron.app');
    cp.spawnSync('cp', ['-R', appBundlePath, path.dirname(newPath)]);
    return newPath;
  }

  // On windows and linux we should read the zip manifest files and then copy each of those files
  // one by one
  const baseDir = path.dirname(process.execPath);
  const zipManifestPath = path.resolve(__dirname, '..', '..', 'script', 'zip_manifests', `dist_zip.${process.platform === 'win32' ? 'win' : 'linux'}.${process.arch}.manifest`);
  const filesToCopy = (fs.readFileSync(zipManifestPath, 'utf-8')).split('\n').filter(f => f !== 'LICENSE' && f !== 'LICENSES.chromium.html' && f !== 'version' && f.trim());
  await Promise.all(
    filesToCopy.map(async rel => {
      await fs.promises.mkdir(path.dirname(path.resolve(targetDir, rel)), { recursive: true });
      fs.copyFileSync(path.resolve(baseDir, rel), path.resolve(targetDir, rel));
    })
  );

  return path.resolve(targetDir, path.basename(process.execPath));
}

export async function withTempDirectory (fn: (dir: string) => Promise<void>, autoCleanUp = true) {
  const dir = await fs.promises.mkdtemp(path.resolve(os.tmpdir(), 'electron-update-spec-'));
  try {
    await fn(dir);
  } finally {
    if (autoCleanUp) {
      cp.spawnSync('rm', ['-r', dir]);
    }
  }
};
