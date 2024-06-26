import { app, BaseWindow } from 'electron/main';
import type { OpenDialogOptions, OpenDialogReturnValue, MessageBoxOptions, SaveDialogOptions, SaveDialogReturnValue, MessageBoxReturnValue, CertificateTrustDialogOptions } from 'electron/main';
const dialogBinding = process._linkedBinding('electron_browser_dialog');

enum SaveFileDialogProperties {
  createDirectory = 1 << 0,
  showHiddenFiles = 1 << 1,
  treatPackageAsDirectory = 1 << 2,
  showOverwriteConfirmation = 1 << 3,
  dontAddToRecent = 1 << 4
}

enum OpenFileDialogProperties {
  openFile = 1 << 0,
  openDirectory = 1 << 1,
  multiSelections = 1 << 2,
  createDirectory = 1 << 3, // macOS
  showHiddenFiles = 1 << 4,
  promptToCreate = 1 << 5, // Windows
  noResolveAliases = 1 << 6, // macOS
  treatPackageAsDirectory = 1 << 7, // macOS
  dontAddToRecent = 1 << 8 // Windows
}

let nextId = 0;
const getNextId = function () {
  return ++nextId;
};

const normalizeAccessKey = (text: string) => {
  if (typeof text !== 'string') return text;

  // macOS does not have access keys so remove single ampersands
  // and replace double ampersands with a single ampersand
  if (process.platform === 'darwin') {
    return text.replaceAll(/&(&?)/g, '$1');
  }

  // Linux uses a single underscore as an access key prefix so escape
  // existing single underscores with a second underscore, replace double
  // ampersands with a single ampersand, and replace a single ampersand with
  // a single underscore
  if (process.platform === 'linux') {
    return text.replaceAll('_', '__').replaceAll(/&(.?)/g, (match, after) => {
      if (after === '&') return after;
      return `_${after}`;
    });
  }

  return text;
};

const checkAppInitialized = function () {
  if (!app.isReady()) {
    throw new Error('dialog module can only be used after app is ready');
  }
};

const setupOpenDialogProperties = (properties: (keyof typeof OpenFileDialogProperties)[]): number => {
  let dialogProperties = 0;
  for (const property of properties) {
    if (Object.hasOwn(OpenFileDialogProperties, property)) { dialogProperties |= OpenFileDialogProperties[property]; }
  }
  return dialogProperties;
};

const setupSaveDialogProperties = (properties: (keyof typeof SaveFileDialogProperties)[]): number => {
  let dialogProperties = 0;
  for (const property of properties) {
    if (Object.hasOwn(SaveFileDialogProperties, property)) { dialogProperties |= SaveFileDialogProperties[property]; }
  }
  return dialogProperties;
};

const saveDialog = (sync: boolean, window: BaseWindow | null, options?: SaveDialogOptions) => {
  checkAppInitialized();

  if (options == null) options = { title: 'Save' };

  const {
    buttonLabel = '',
    defaultPath = '',
    filters = [],
    properties = [],
    title = '',
    message = '',
    securityScopedBookmarks = false,
    nameFieldLabel = '',
    showsTagField = true
  } = options;

  if (typeof title !== 'string') throw new TypeError('Title must be a string');
  if (typeof buttonLabel !== 'string') throw new TypeError('Button label must be a string');
  if (typeof defaultPath !== 'string') throw new TypeError('Default path must be a string');
  if (typeof message !== 'string') throw new TypeError('Message must be a string');
  if (typeof nameFieldLabel !== 'string') throw new TypeError('Name field label must be a string');

  const settings = {
    buttonLabel,
    defaultPath,
    filters,
    title,
    message,
    securityScopedBookmarks,
    nameFieldLabel,
    showsTagField,
    window,
    properties: setupSaveDialogProperties(properties)
  };

  return sync ? dialogBinding.showSaveDialogSync(settings) : dialogBinding.showSaveDialog(settings);
};

const openDialog = (sync: boolean, window: BaseWindow | null, options?: OpenDialogOptions) => {
  checkAppInitialized();

  if (options == null) {
    options = {
      title: 'Open',
      properties: ['openFile']
    };
  }

  const {
    buttonLabel = '',
    defaultPath = '',
    filters = [],
    properties = ['openFile'],
    title = '',
    message = '',
    securityScopedBookmarks = false
  } = options;

  if (!Array.isArray(properties)) throw new TypeError('Properties must be an array');

  if (typeof title !== 'string') throw new TypeError('Title must be a string');
  if (typeof buttonLabel !== 'string') throw new TypeError('Button label must be a string');
  if (typeof defaultPath !== 'string') throw new TypeError('Default path must be a string');
  if (typeof message !== 'string') throw new TypeError('Message must be a string');

  const settings = {
    title,
    buttonLabel,
    defaultPath,
    filters,
    message,
    securityScopedBookmarks,
    window,
    properties: setupOpenDialogProperties(properties)
  };

  return (sync) ? dialogBinding.showOpenDialogSync(settings) : dialogBinding.showOpenDialog(settings);
};

const messageBox = (sync: boolean, window: BaseWindow | null, options?: MessageBoxOptions) => {
  checkAppInitialized();

  if (options == null) options = { type: 'none', message: '' };

  const messageBoxTypes = ['none', 'info', 'warning', 'error', 'question'];

  let {
    buttons = [],
    cancelId,
    signal,
    checkboxLabel = '',
    checkboxChecked,
    defaultId = -1,
    detail = '',
    icon = null,
    textWidth = 0,
    noLink = false,
    message = '',
    title = '',
    type = 'none'
  } = options;

  const messageBoxType = messageBoxTypes.indexOf(type);
  if (messageBoxType === -1) throw new TypeError('Invalid message box type');
  if (!Array.isArray(buttons)) throw new TypeError('Buttons must be an array');
  if (options.normalizeAccessKeys) buttons = buttons.map(normalizeAccessKey);
  if (typeof title !== 'string') throw new TypeError('Title must be a string');
  if (typeof noLink !== 'boolean') throw new TypeError('noLink must be a boolean');
  if (typeof message !== 'string') throw new TypeError('Message must be a string');
  if (typeof detail !== 'string') throw new TypeError('Detail must be a string');
  if (typeof checkboxLabel !== 'string') throw new TypeError('checkboxLabel must be a string');

  checkboxChecked = !!checkboxChecked;
  if (checkboxChecked && !checkboxLabel) {
    throw new Error('checkboxChecked requires that checkboxLabel also be passed');
  }

  // Choose a default button to get selected when dialog is cancelled.
  if (cancelId == null) {
    // If the defaultId is set to 0, ensure the cancel button is a different index (1)
    cancelId = (defaultId === 0 && buttons.length > 1) ? 1 : 0;
    for (const [i, button] of buttons.entries()) {
      const text = button.toLowerCase();
      if (text === 'cancel' || text === 'no') {
        cancelId = i;
        break;
      }
    }
  }

  // AbortSignal processing.
  let id: number | undefined;
  if (signal) {
    // Generate an ID used for closing the message box.
    id = getNextId();
    // Close the message box when signal is aborted.
    if (signal.aborted) { return Promise.resolve({ cancelId, checkboxChecked }); }
    signal.addEventListener('abort', () => dialogBinding._closeMessageBox(id));
  }

  const settings = {
    window,
    messageBoxType,
    buttons,
    id,
    defaultId,
    cancelId,
    noLink,
    title,
    message,
    detail,
    checkboxLabel,
    checkboxChecked,
    icon,
    textWidth
  };

  if (sync) {
    return dialogBinding.showMessageBoxSync(settings);
  } else {
    return dialogBinding.showMessageBox(settings);
  }
};

export function showOpenDialog(window: BaseWindow, options: OpenDialogOptions): OpenDialogReturnValue;
export function showOpenDialog(options: OpenDialogOptions): OpenDialogReturnValue;
export function showOpenDialog (windowOrOptions: BaseWindow | OpenDialogOptions, maybeOptions?: OpenDialogOptions): OpenDialogReturnValue {
  const window = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? null : windowOrOptions);
  const options = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? windowOrOptions : maybeOptions);
  return openDialog(false, window, options);
}

export function showOpenDialogSync(window: BaseWindow, options: OpenDialogOptions): OpenDialogReturnValue;
export function showOpenDialogSync(options: OpenDialogOptions): OpenDialogReturnValue;
export function showOpenDialogSync (windowOrOptions: BaseWindow | OpenDialogOptions, maybeOptions?: OpenDialogOptions): OpenDialogReturnValue {
  const window = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? null : windowOrOptions);
  const options = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? windowOrOptions : maybeOptions);
  return openDialog(true, window, options);
}

export function showSaveDialog(window: BaseWindow, options: SaveDialogOptions): SaveDialogReturnValue;
export function showSaveDialog(options: SaveDialogOptions): SaveDialogReturnValue;
export function showSaveDialog (windowOrOptions: BaseWindow | SaveDialogOptions, maybeOptions?: SaveDialogOptions): SaveDialogReturnValue {
  const window = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? null : windowOrOptions);
  const options = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? windowOrOptions : maybeOptions);
  return saveDialog(false, window, options);
}

export function showSaveDialogSync(window: BaseWindow, options: SaveDialogOptions): SaveDialogReturnValue;
export function showSaveDialogSync(options: SaveDialogOptions): SaveDialogReturnValue;
export function showSaveDialogSync (windowOrOptions: BaseWindow | SaveDialogOptions, maybeOptions?: SaveDialogOptions): SaveDialogReturnValue {
  const window = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? null : windowOrOptions);
  const options = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? windowOrOptions : maybeOptions);
  return saveDialog(true, window, options);
}

export function showMessageBox(window: BaseWindow, options: MessageBoxOptions): MessageBoxReturnValue;
export function showMessageBox(options: MessageBoxOptions): MessageBoxReturnValue;
export function showMessageBox (windowOrOptions: BaseWindow | MessageBoxOptions, maybeOptions?: MessageBoxOptions): MessageBoxReturnValue {
  const window = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? null : windowOrOptions);
  const options = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? windowOrOptions : maybeOptions);
  return messageBox(false, window, options);
}

export function showMessageBoxSync(window: BaseWindow, options: MessageBoxOptions): MessageBoxReturnValue;
export function showMessageBoxSync(options: MessageBoxOptions): MessageBoxReturnValue;
export function showMessageBoxSync (windowOrOptions: BaseWindow | MessageBoxOptions, maybeOptions?: MessageBoxOptions): MessageBoxReturnValue {
  const window = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? null : windowOrOptions);
  const options = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? windowOrOptions : maybeOptions);
  return messageBox(true, window, options);
}

export function showErrorBox (...args: any[]) {
  return dialogBinding.showErrorBox(...args);
}

export function showCertificateTrustDialog (windowOrOptions: BaseWindow | CertificateTrustDialogOptions, maybeOptions?: CertificateTrustDialogOptions) {
  const window = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? null : windowOrOptions);
  const options = (windowOrOptions && !(windowOrOptions instanceof BaseWindow) ? windowOrOptions : maybeOptions);

  if (options == null || typeof options !== 'object') {
    throw new TypeError('options must be an object');
  }

  const { certificate, message = '' } = options;
  if (certificate == null || typeof certificate !== 'object') {
    throw new TypeError('certificate must be an object');
  }

  if (typeof message !== 'string') throw new TypeError('message must be a string');

  return dialogBinding.showCertificateTrustDialog(window, certificate, message);
}
