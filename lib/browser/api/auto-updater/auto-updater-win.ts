import * as squirrelUpdate from '@electron/internal/browser/api/auto-updater/squirrel-update-win';

import { app } from 'electron/main';

import { EventEmitter } from 'events';

class AutoUpdater extends EventEmitter implements Electron.AutoUpdater {
  updateAvailable: boolean = false;
  updateURL: string | null = null;

  quitAndInstall () {
    if (!this.updateAvailable) {
      return this.emitError(new Error('No update available, can\'t quit and install'));
    }
    squirrelUpdate.processStart();
    app.quit();
  }

  getFeedURL () {
    return this.updateURL ?? '';
  }

  setFeedURL (options: { url: string } | string) {
    let updateURL: string;
    if (typeof options === 'object') {
      if (typeof options.url === 'string') {
        updateURL = options.url;
      } else {
        throw new TypeError('Expected options object to contain a \'url\' string property in setFeedUrl call');
      }
    } else if (typeof options === 'string') {
      updateURL = options;
    } else {
      throw new TypeError('Expected an options object with a \'url\' property to be provided');
    }
    this.updateURL = updateURL;
  }

  async checkForUpdates () {
    const url = this.updateURL;
    if (!url) {
      return this.emitError(new Error('Update URL is not set'));
    }
    if (!squirrelUpdate.supported()) {
      return this.emitError(new Error('Can not find Squirrel'));
    }
    this.emit('checking-for-update');
    try {
      const update = await squirrelUpdate.checkForUpdate(url);
      if (update == null) {
        return this.emit('update-not-available');
      }
      this.updateAvailable = true;
      this.emit('update-available');

      await squirrelUpdate.update(url);
      const { releaseNotes, version } = update;
      // Date is not available on Windows, so fake it.
      const date = new Date();
      this.emit('update-downloaded', {}, releaseNotes, version, date, this.updateURL, () => {
        this.quitAndInstall();
      });
    } catch (error) {
      this.emitError(error as Error);
    }
  }

  prepareUpdateFromFile () {
    throw new Error('Not supported on Windows');
  }

  // Private: Emit both error object and message, this is to keep compatibility
  // with Old APIs.
  emitError (error: Error) {
    this.emit('error', error, error.message);
  }
}

export default new AutoUpdater();
