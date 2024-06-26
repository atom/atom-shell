import { Buffer } from 'buffer';
import { Dirent, constants } from 'fs';
import * as path from 'path';
import * as util from 'util';

import type * as Crypto from 'crypto';
import type * as os from 'os';

const asar = process._linkedBinding('electron_common_asar');

const Module = require('module') as NodeJS.ModuleInternal;

const Promise: PromiseConstructor = global.Promise;

const envNoAsar = process.env.ELECTRON_NO_ASAR &&
    process.type !== 'browser' &&
    process.type !== 'renderer';
const isAsarDisabled = () => process.noAsar || envNoAsar;

const internalBinding = process.internalBinding!;
delete process.internalBinding;

const nextTick = (functionToCall: Function, args: any[] = []) => {
  process.nextTick(() => functionToCall(...args));
};

// Cache asar archive objects.
const cachedArchives = new Map<string, NodeJS.AsarArchive>();

const getOrCreateArchive = (archivePath: string) => {
  const isCached = cachedArchives.has(archivePath);
  if (isCached) {
    return cachedArchives.get(archivePath)!;
  }

  try {
    const newArchive = new asar.Archive(archivePath);
    cachedArchives.set(archivePath, newArchive);
    return newArchive;
  } catch {
    return null;
  }
};

process._getOrCreateArchive = getOrCreateArchive;

const asarRe = /\.asar/i;

const {
  getValidatedPath,
  getOptions,
  getDirent
} = __non_webpack_require__('internal/fs/utils');

const {
  validateBoolean,
  validateFunction
} = __non_webpack_require__('internal/validators');

// In the renderer node internals use the node global URL but we do not set that to be
// the global URL instance.  We need to do instanceof checks against the internal URL impl
const { URL: NodeURL } = __non_webpack_require__('internal/url');

// Separate asar package's path from full path.
const splitPath = (archivePathOrBuffer: string | Buffer | URL) => {
  // Shortcut for disabled asar.
  if (isAsarDisabled()) return { isAsar: <const>false };

  // Check for a bad argument type.
  let archivePath = archivePathOrBuffer;
  if (Buffer.isBuffer(archivePathOrBuffer)) {
    archivePath = archivePathOrBuffer.toString();
  }
  if (archivePath instanceof NodeURL) {
    archivePath = getValidatedPath(archivePath);
  }
  if (typeof archivePath !== 'string') return { isAsar: <const>false };
  if (!asarRe.test(archivePath)) return { isAsar: <const>false };

  return asar.splitPath(path.normalize(archivePath));
};

// Convert asar archive's Stats object to fs's Stats object.
let nextInode = 0;

const uid = process.getuid?.() ?? 0;
const gid = process.getgid?.() ?? 0;

const fakeTime = new Date();

function getDirents (p: string, { 0: names, 1: types }: any[][]): Dirent[] {
  for (let i = 0; i < names.length; i++) {
    let type = types[i];
    const info = splitPath(path.join(p, names[i]));
    if (info.isAsar) {
      const archive = getOrCreateArchive(info.asarPath);
      if (!archive) continue;
      const stats = archive.stat(info.filePath);
      if (!stats) continue;
      type = stats.type;
    }
    names[i] = getDirent(p, names[i], type);
  }

  return names;
}

enum AsarFileType {
  kFile = (constants as any).UV_DIRENT_FILE,
  kDirectory = (constants as any).UV_DIRENT_DIR,
  kLink = (constants as any).UV_DIRENT_LINK,
}

const fileTypeToMode = new Map<AsarFileType, number>([
  [AsarFileType.kFile, constants.S_IFREG],
  [AsarFileType.kDirectory, constants.S_IFDIR],
  [AsarFileType.kLink, constants.S_IFLNK]
]);

const asarStatsToFsStats = function (stats: NodeJS.AsarFileStat) {
  const { Stats } = require('fs');

  const mode = constants.S_IROTH | constants.S_IRGRP | constants.S_IRUSR | constants.S_IWUSR | fileTypeToMode.get(stats.type)!;

  return new Stats(
    1, // dev
    mode, // mode
    1, // nlink
    uid,
    gid,
    0, // rdev
    undefined, // blksize
    ++nextInode, // ino
    stats.size,
    undefined, // blocks,
    fakeTime.getTime(), // atim_msec
    fakeTime.getTime(), // mtim_msec
    fakeTime.getTime(), // ctim_msec
    fakeTime.getTime() // birthtim_msec
  );
};

const enum AsarError {
  NOT_FOUND = 'NOT_FOUND',
  NOT_DIR = 'NOT_DIR',
  NO_ACCESS = 'NO_ACCESS',
  INVALID_ARCHIVE = 'INVALID_ARCHIVE'
}

type AsarErrorObject = Error & { code?: string, errno?: number };

const createError = (errorType: AsarError, { asarPath, filePath }: { asarPath?: string, filePath?: string } = {}) => {
  let error: AsarErrorObject;
  switch (errorType) {
    case AsarError.NOT_FOUND:
      error = new Error(`ENOENT, ${filePath} not found in ${asarPath}`);
      error.code = 'ENOENT';
      error.errno = -2;
      break;
    case AsarError.NOT_DIR:
      error = new Error('ENOTDIR, not a directory');
      error.code = 'ENOTDIR';
      error.errno = -20;
      break;
    case AsarError.NO_ACCESS:
      error = new Error(`EACCES: permission denied, access '${filePath}'`);
      error.code = 'EACCES';
      error.errno = -13;
      break;
    case AsarError.INVALID_ARCHIVE:
      error = new Error(`Invalid package ${asarPath}`);
      break;
    default:
      throw new Error(`Invalid error type "${errorType}" passed to createError.`);
  }
  return error;
};

const overrideAPISync = function (module: Record<string, any>, name: string, pathArgumentIndex?: number | null, fromAsync: boolean = false) {
  if (pathArgumentIndex == null) pathArgumentIndex = 0;
  const old = module[name];
  const func = function (this: any, ...args: any[]) {
    const pathArgument = args[pathArgumentIndex!];
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return old.apply(this, args);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) throw createError(AsarError.INVALID_ARCHIVE, { asarPath });

    const newPath = archive.copyFileOut(filePath);
    if (!newPath) throw createError(AsarError.NOT_FOUND, { asarPath, filePath });

    args[pathArgumentIndex!] = newPath;
    return old.apply(this, args);
  };
  if (fromAsync) {
    return func;
  }
  module[name] = func;
};

const overrideAPI = function (module: Record<string, any>, name: string, pathArgumentIndex?: number | null) {
  if (pathArgumentIndex == null) pathArgumentIndex = 0;
  const old = module[name];
  module[name] = function (this: any, ...args: any[]) {
    const pathArgument = args[pathArgumentIndex!];
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return old.apply(this, args);
    const { asarPath, filePath } = pathInfo;

    const callback = args[args.length - 1];
    if (typeof callback !== 'function') {
      return overrideAPISync(module, name, pathArgumentIndex!, true)!.apply(this, args);
    }

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      const error = createError(AsarError.INVALID_ARCHIVE, { asarPath });
      nextTick(callback, [error]);
      return;
    }

    const newPath = archive.copyFileOut(filePath);
    if (!newPath) {
      const error = createError(AsarError.NOT_FOUND, { asarPath, filePath });
      nextTick(callback, [error]);
      return;
    }

    args[pathArgumentIndex!] = newPath;
    return old.apply(this, args);
  };

  if (old[util.promisify.custom]) {
    module[name][util.promisify.custom] = makePromiseFunction(old[util.promisify.custom], pathArgumentIndex);
  }

  if (module.promises && module.promises[name]) {
    module.promises[name] = makePromiseFunction(module.promises[name], pathArgumentIndex);
  }
};

let crypto: typeof Crypto;
function validateBufferIntegrity (buffer: Buffer, integrity: NodeJS.AsarFileInfo['integrity']) {
  if (!integrity) return;

  // Delay load crypto to improve app boot performance
  // when integrity protection is not enabled
  crypto = crypto || require('crypto');
  const actual = crypto.createHash(integrity.algorithm).update(buffer).digest('hex');
  if (actual !== integrity.hash) {
    console.error(`ASAR Integrity Violation: got a hash mismatch (${actual} vs ${integrity.hash})`);
    process.exit(1);
  }
}

const makePromiseFunction = function (orig: Function, pathArgumentIndex: number) {
  return function (this: any, ...args: any[]) {
    const pathArgument = args[pathArgumentIndex];
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return orig.apply(this, args);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      return Promise.reject(createError(AsarError.INVALID_ARCHIVE, { asarPath }));
    }

    const newPath = archive.copyFileOut(filePath);
    if (!newPath) {
      return Promise.reject(createError(AsarError.NOT_FOUND, { asarPath, filePath }));
    }

    args[pathArgumentIndex] = newPath;
    return orig.apply(this, args);
  };
};

// Override fs APIs.
export const wrapFsWithAsar = (fs: Record<string, any>) => {
  const logFDs = new Map<string, number>();
  const logASARAccess = (asarPath: string, filePath: string, offset: number) => {
    if (!process.env.ELECTRON_LOG_ASAR_READS) return;
    if (!logFDs.has(asarPath)) {
      const logFilename = `${path.basename(asarPath, '.asar')}-access-log.txt`;
      const logPath = path.join((require('os') as typeof os).tmpdir(), logFilename);
      logFDs.set(asarPath, fs.openSync(logPath, 'a'));
    }
    fs.writeSync(logFDs.get(asarPath), `${offset}: ${filePath}\n`);
  };

  const shouldThrowStatError = (options: any) => {
    if (options && typeof options === 'object' && options.throwIfNoEntry === false) {
      return false;
    }

    return true;
  };

  const { lstatSync } = fs;
  fs.lstatSync = (pathArgument: string, options: any) => {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return lstatSync(pathArgument, options);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      if (shouldThrowStatError(options)) throw createError(AsarError.INVALID_ARCHIVE, { asarPath });
      return null;
    }

    const stats = archive.stat(filePath);
    if (!stats) {
      if (shouldThrowStatError(options)) throw createError(AsarError.NOT_FOUND, { asarPath, filePath });
      return null;
    }

    return asarStatsToFsStats(stats);
  };

  const { lstat } = fs;
  fs.lstat = (pathArgument: string, options: any, callback: any) => {
    const pathInfo = splitPath(pathArgument);
    if (typeof options === 'function') {
      callback = options;
      options = {};
    }
    if (!pathInfo.isAsar) return lstat(pathArgument, options, callback);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      const error = createError(AsarError.INVALID_ARCHIVE, { asarPath });
      nextTick(callback, [error]);
      return;
    }

    const stats = archive.stat(filePath);
    if (!stats) {
      const error = createError(AsarError.NOT_FOUND, { asarPath, filePath });
      nextTick(callback, [error]);
      return;
    }

    const fsStats = asarStatsToFsStats(stats);
    nextTick(callback, [null, fsStats]);
  };

  fs.promises.lstat = util.promisify(fs.lstat);

  const { statSync } = fs;
  fs.statSync = (pathArgument: string, options: any) => {
    const { isAsar } = splitPath(pathArgument);
    if (!isAsar) return statSync(pathArgument, options);

    // Do not distinguish links for now.
    return fs.lstatSync(pathArgument, options);
  };

  const { stat } = fs;
  fs.stat = (pathArgument: string, options: any, callback: any) => {
    const { isAsar } = splitPath(pathArgument);
    if (typeof options === 'function') {
      callback = options;
      options = {};
    }
    if (!isAsar) return stat(pathArgument, options, callback);

    // Do not distinguish links for now.
    process.nextTick(() => fs.lstat(pathArgument, options, callback));
  };

  fs.promises.stat = util.promisify(fs.stat);

  const wrapRealpathSync = function (realpathSync: Function) {
    return function (this: any, pathArgument: string, options: any) {
      const pathInfo = splitPath(pathArgument);
      if (!pathInfo.isAsar) return realpathSync.apply(this, arguments);
      const { asarPath, filePath } = pathInfo;

      const archive = getOrCreateArchive(asarPath);
      if (!archive) {
        throw createError(AsarError.INVALID_ARCHIVE, { asarPath });
      }

      const fileRealPath = archive.realpath(filePath);
      if (fileRealPath === false) {
        throw createError(AsarError.NOT_FOUND, { asarPath, filePath });
      }

      return path.join(realpathSync(asarPath, options), fileRealPath);
    };
  };

  const { realpathSync } = fs;
  fs.realpathSync = wrapRealpathSync(realpathSync);
  fs.realpathSync.native = wrapRealpathSync(realpathSync.native);

  const wrapRealpath = function (realpath: Function) {
    return function (this: any, pathArgument: string, options: any, callback: any) {
      const pathInfo = splitPath(pathArgument);
      if (!pathInfo.isAsar) return realpath.apply(this, arguments);
      const { asarPath, filePath } = pathInfo;

      if (arguments.length < 3) {
        callback = options;
        options = {};
      }

      const archive = getOrCreateArchive(asarPath);
      if (!archive) {
        const error = createError(AsarError.INVALID_ARCHIVE, { asarPath });
        nextTick(callback, [error]);
        return;
      }

      const fileRealPath = archive.realpath(filePath);
      if (fileRealPath === false) {
        const error = createError(AsarError.NOT_FOUND, { asarPath, filePath });
        nextTick(callback, [error]);
        return;
      }

      realpath(asarPath, options, (error: Error | null, archiveRealPath: string) => {
        if (error === null) {
          const fullPath = path.join(archiveRealPath, fileRealPath);
          callback(null, fullPath);
        } else {
          callback(error);
        }
      });
    };
  };

  const { realpath } = fs;
  fs.realpath = wrapRealpath(realpath);
  fs.realpath.native = wrapRealpath(realpath.native);

  fs.promises.realpath = util.promisify(fs.realpath.native);

  const { exists: nativeExists } = fs;
  fs.exists = function exists (pathArgument: string, callback: any) {
    let pathInfo: ReturnType<typeof splitPath>;
    try {
      pathInfo = splitPath(pathArgument);
    } catch {
      nextTick(callback, [false]);
      return;
    }
    if (!pathInfo.isAsar) return nativeExists(pathArgument, callback);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      const error = createError(AsarError.INVALID_ARCHIVE, { asarPath });
      nextTick(callback, [error]);
      return;
    }

    const pathExists = (archive.stat(filePath) !== false);
    nextTick(callback, [pathExists]);
  };

  fs.exists[util.promisify.custom] = function exists (pathArgument: string) {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return nativeExists[util.promisify.custom](pathArgument);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      const error = createError(AsarError.INVALID_ARCHIVE, { asarPath });
      return Promise.reject(error);
    }

    return Promise.resolve(archive.stat(filePath) !== false);
  };

  const { existsSync } = fs;
  fs.existsSync = (pathArgument: string) => {
    let pathInfo: ReturnType<typeof splitPath>;
    try {
      pathInfo = splitPath(pathArgument);
    } catch {
      return false;
    }
    if (!pathInfo.isAsar) return existsSync(pathArgument);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) return false;

    return archive.stat(filePath) !== false;
  };

  const { access } = fs;
  fs.access = function (pathArgument: string, mode: number, callback: any) {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return access.apply(this, arguments);
    const { asarPath, filePath } = pathInfo;

    if (typeof mode === 'function') {
      callback = mode;
      mode = fs.constants.F_OK;
    }

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      const error = createError(AsarError.INVALID_ARCHIVE, { asarPath });
      nextTick(callback, [error]);
      return;
    }

    const info = archive.getFileInfo(filePath);
    if (!info) {
      const error = createError(AsarError.NOT_FOUND, { asarPath, filePath });
      nextTick(callback, [error]);
      return;
    }

    if (info.unpacked) {
      const realPath = archive.copyFileOut(filePath);
      return fs.access(realPath, mode, callback);
    }

    const stats = archive.stat(filePath);
    if (!stats) {
      const error = createError(AsarError.NOT_FOUND, { asarPath, filePath });
      nextTick(callback, [error]);
      return;
    }

    if (mode & fs.constants.W_OK) {
      const error = createError(AsarError.NO_ACCESS, { asarPath, filePath });
      nextTick(callback, [error]);
      return;
    }

    nextTick(callback);
  };

  const { access: accessPromise } = fs.promises;
  fs.promises.access = function (pathArgument: string, mode: number) {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) {
      return accessPromise.apply(this, arguments);
    }

    const p = util.promisify(fs.access);
    return p(pathArgument, mode);
  };

  const { accessSync } = fs;
  fs.accessSync = function (pathArgument: string, mode: any) {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return accessSync.apply(this, arguments);
    const { asarPath, filePath } = pathInfo;

    if (mode == null) mode = fs.constants.F_OK;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      throw createError(AsarError.INVALID_ARCHIVE, { asarPath });
    }

    const info = archive.getFileInfo(filePath);
    if (!info) {
      throw createError(AsarError.NOT_FOUND, { asarPath, filePath });
    }

    if (info.unpacked) {
      const realPath = archive.copyFileOut(filePath);
      return fs.accessSync(realPath, mode);
    }

    const stats = archive.stat(filePath);
    if (!stats) {
      throw createError(AsarError.NOT_FOUND, { asarPath, filePath });
    }

    if (mode & fs.constants.W_OK) {
      throw createError(AsarError.NO_ACCESS, { asarPath, filePath });
    }
  };

  function fsReadFileAsar (pathArgument: string, options: any, callback: any) {
    const pathInfo = splitPath(pathArgument);
    if (pathInfo.isAsar) {
      const { asarPath, filePath } = pathInfo;

      if (typeof options === 'function') {
        callback = options;
        options = { encoding: null };
      } else if (typeof options === 'string') {
        options = { encoding: options };
      } else if (options === null || options === undefined) {
        options = { encoding: null };
      } else if (typeof options !== 'object') {
        throw new TypeError('Bad arguments');
      }

      const { encoding } = options;
      const archive = getOrCreateArchive(asarPath);
      if (!archive) {
        const error = createError(AsarError.INVALID_ARCHIVE, { asarPath });
        nextTick(callback, [error]);
        return;
      }

      const info = archive.getFileInfo(filePath);
      if (!info) {
        const error = createError(AsarError.NOT_FOUND, { asarPath, filePath });
        nextTick(callback, [error]);
        return;
      }

      if (info.size === 0) {
        nextTick(callback, [null, encoding ? '' : Buffer.alloc(0)]);
        return;
      }

      if (info.unpacked) {
        const realPath = archive.copyFileOut(filePath);
        return fs.readFile(realPath, options, callback);
      }

      const buffer = Buffer.alloc(info.size);
      const fd = archive.getFdAndValidateIntegrityLater();
      if (!(fd >= 0)) {
        const error = createError(AsarError.NOT_FOUND, { asarPath, filePath });
        nextTick(callback, [error]);
        return;
      }

      logASARAccess(asarPath, filePath, info.offset);
      fs.read(fd, buffer, 0, info.size, info.offset, (error: Error) => {
        validateBufferIntegrity(buffer, info.integrity);
        callback(error, encoding ? buffer.toString(encoding) : buffer);
      });
    }
  }

  const { readFile } = fs;
  fs.readFile = function (pathArgument: string, options: any, callback: any) {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) {
      return readFile.apply(this, arguments);
    }

    return fsReadFileAsar(pathArgument, options, callback);
  };

  const { readFile: readFilePromise } = fs.promises;
  fs.promises.readFile = function (pathArgument: string, options: any) {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) {
      return readFilePromise.apply(this, arguments);
    }

    const p = util.promisify(fsReadFileAsar);
    return p(pathArgument, options);
  };

  const { readFileSync } = fs;
  fs.readFileSync = function (pathArgument: string, options: any) {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return readFileSync.apply(this, arguments);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) throw createError(AsarError.INVALID_ARCHIVE, { asarPath });

    const info = archive.getFileInfo(filePath);
    if (!info) throw createError(AsarError.NOT_FOUND, { asarPath, filePath });

    if (info.size === 0) return (options) ? '' : Buffer.alloc(0);
    if (info.unpacked) {
      const realPath = archive.copyFileOut(filePath);
      return fs.readFileSync(realPath, options);
    }

    if (!options) {
      options = { encoding: null };
    } else if (typeof options === 'string') {
      options = { encoding: options };
    } else if (typeof options !== 'object') {
      throw new TypeError('Bad arguments');
    }

    const { encoding } = options;
    const buffer = Buffer.alloc(info.size);
    const fd = archive.getFdAndValidateIntegrityLater();
    if (!(fd >= 0)) throw createError(AsarError.NOT_FOUND, { asarPath, filePath });

    logASARAccess(asarPath, filePath, info.offset);
    fs.readSync(fd, buffer, 0, info.size, info.offset);
    validateBufferIntegrity(buffer, info.integrity);
    return (encoding) ? buffer.toString(encoding) : buffer;
  };

  type ReaddirOptions = { encoding: BufferEncoding | null; withFileTypes?: false, recursive?: false } | undefined | null;
  type ReaddirCallback = (err: NodeJS.ErrnoException | null, files: string[]) => void;

  const { readdir } = fs;
  fs.readdir = function (pathArgument: string, options: ReaddirOptions, callback: ReaddirCallback) {
    callback = typeof options === 'function' ? options : callback;
    validateFunction(callback, 'callback');

    options = getOptions(options);
    pathArgument = getValidatedPath(pathArgument);

    if (options?.recursive != null) {
      validateBoolean(options?.recursive, 'options.recursive');
    }

    if (options?.recursive) {
      nextTick(callback!, [null, readdirSyncRecursive(pathArgument, options)]);
      return;
    }

    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return readdir.apply(this, arguments);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      const error = createError(AsarError.INVALID_ARCHIVE, { asarPath });
      nextTick(callback!, [error]);
      return;
    }

    const files = archive.readdir(filePath);
    if (!files) {
      const error = createError(AsarError.NOT_FOUND, { asarPath, filePath });
      nextTick(callback!, [error]);
      return;
    }

    if (options?.withFileTypes) {
      const dirents = [];
      for (const file of files) {
        const childPath = path.join(filePath, file);
        const stats = archive.stat(childPath);
        if (!stats) {
          const error = createError(AsarError.NOT_FOUND, { asarPath, filePath: childPath });
          nextTick(callback!, [error]);
          return;
        }
        dirents.push(new fs.Dirent(file, stats.type));
      }
      nextTick(callback!, [null, dirents]);
      return;
    }

    nextTick(callback!, [null, files]);
  };

  const { readdir: readdirPromise } = require('fs').promises;
  fs.promises.readdir = async function (pathArgument: string, options: ReaddirOptions) {
    options = getOptions(options);
    pathArgument = getValidatedPath(pathArgument);

    if (options?.recursive != null) {
      validateBoolean(options?.recursive, 'options.recursive');
    }

    if (options?.recursive) {
      return readdirRecursive(pathArgument, options);
    }

    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return readdirPromise(pathArgument, options);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      return Promise.reject(createError(AsarError.INVALID_ARCHIVE, { asarPath }));
    }

    const files = archive.readdir(filePath);
    if (!files) {
      return Promise.reject(createError(AsarError.NOT_FOUND, { asarPath, filePath }));
    }

    if (options?.withFileTypes) {
      const dirents = [];
      for (const file of files) {
        const childPath = path.join(filePath, file);
        const stats = archive.stat(childPath);
        if (!stats) {
          throw createError(AsarError.NOT_FOUND, { asarPath, filePath: childPath });
        }
        dirents.push(new fs.Dirent(file, stats.type));
      }
      return Promise.resolve(dirents);
    }

    return Promise.resolve(files);
  };

  const { readdirSync } = fs;
  fs.readdirSync = function (pathArgument: string, options: ReaddirOptions) {
    options = getOptions(options);
    pathArgument = getValidatedPath(pathArgument);

    if (options?.recursive != null) {
      validateBoolean(options?.recursive, 'options.recursive');
    }

    if (options?.recursive) {
      return readdirSyncRecursive(pathArgument, options);
    }

    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return readdirSync.apply(this, arguments);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) {
      throw createError(AsarError.INVALID_ARCHIVE, { asarPath });
    }

    const files = archive.readdir(filePath);
    if (!files) {
      throw createError(AsarError.NOT_FOUND, { asarPath, filePath });
    }

    if (options?.withFileTypes) {
      const dirents = [];
      for (const file of files) {
        const childPath = path.join(filePath, file);
        const stats = archive.stat(childPath);
        if (!stats) {
          throw createError(AsarError.NOT_FOUND, { asarPath, filePath: childPath });
        }
        dirents.push(new fs.Dirent(file, stats.type));
      }
      return dirents;
    }

    return files;
  };

  const binding = internalBinding('fs');
  const { internalModuleReadJSON, kUsePromises } = binding;
  internalBinding('fs').internalModuleReadJSON = (pathArgument: string) => {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return internalModuleReadJSON(pathArgument);
    const { asarPath, filePath } = pathInfo;

    const archive = getOrCreateArchive(asarPath);
    if (!archive) return [];

    const info = archive.getFileInfo(filePath);
    if (!info) return [];
    if (info.size === 0) return ['', false];
    if (info.unpacked) {
      const realPath = archive.copyFileOut(filePath);
      const str = fs.readFileSync(realPath, { encoding: 'utf8' });
      return [str, str.length > 0];
    }

    const buffer = Buffer.alloc(info.size);
    const fd = archive.getFdAndValidateIntegrityLater();
    if (!(fd >= 0)) return [];

    logASARAccess(asarPath, filePath, info.offset);
    fs.readSync(fd, buffer, 0, info.size, info.offset);
    validateBufferIntegrity(buffer, info.integrity);
    const str = buffer.toString('utf8');
    return [str, str.length > 0];
  };

  const { internalModuleStat } = internalBinding('fs');
  internalBinding('fs').internalModuleStat = (pathArgument: string) => {
    const pathInfo = splitPath(pathArgument);
    if (!pathInfo.isAsar) return internalModuleStat(pathArgument);
    const { asarPath, filePath } = pathInfo;

    // -ENOENT
    const archive = getOrCreateArchive(asarPath);
    if (!archive) return -34;

    // -ENOENT
    const stats = archive.stat(filePath);
    if (!stats) return -34;

    return (stats.type === AsarFileType.kDirectory) ? 1 : 0;
  };

  async function readdirRecursive (originalPath: string, options: ReaddirOptions) {
    const result: any[] = [];

    const pathInfo = splitPath(originalPath);
    let queue: [string, string[]][] = [];
    const withFileTypes = Boolean(options?.withFileTypes);

    let initialItem = [];
    if (pathInfo.isAsar) {
      const archive = getOrCreateArchive(pathInfo.asarPath);
      if (!archive) return result;
      const files = archive.readdir(pathInfo.filePath);
      if (!files) return result;

      // If we're in an asar dir, we need to ensure the result is in the same format as the
      // native call to readdir withFileTypes i.e. an array of arrays.
      initialItem = files;
      if (withFileTypes) {
        initialItem = [
          [...initialItem], initialItem.map((p: string) => {
            return internalBinding('fs').internalModuleStat(path.join(originalPath, p));
          })
        ];
      }
    } else {
      initialItem = await binding.readdir(
        path.toNamespacedPath(originalPath),
        options!.encoding,
        withFileTypes,
        kUsePromises
      );
    }

    queue = [[originalPath, initialItem]];

    if (withFileTypes) {
      while (queue.length > 0) {
        // @ts-expect-error this is a valid array destructure assignment.
        const { 0: pathArg, 1: readDir } = queue.pop();
        for (const dirent of getDirents(pathArg, readDir)) {
          result.push(dirent);
          if (dirent.isDirectory()) {
            const direntPath = path.join(pathArg, dirent.name);
            const info = splitPath(direntPath);
            let readdirResult;
            if (info.isAsar) {
              const archive = getOrCreateArchive(info.asarPath);
              if (!archive) continue;
              const files = archive.readdir(info.filePath);
              if (!files) continue;

              readdirResult = [
                [...files], files.map((p: string) => {
                  return internalBinding('fs').internalModuleStat(path.join(direntPath, p));
                })
              ];
            } else {
              readdirResult = await binding.readdir(
                direntPath,
                options!.encoding,
                true,
                kUsePromises
              );
            }
            queue.push([direntPath, readdirResult]);
          }
        }
      }
    } else {
      while (queue.length > 0) {
        // @ts-expect-error this is a valid array destructure assignment.
        const { 0: pathArg, 1: readDir } = queue.pop();
        for (const ent of readDir) {
          const direntPath = path.join(pathArg, ent);
          const stat = internalBinding('fs').internalModuleStat(direntPath);
          result.push(path.relative(originalPath, direntPath));

          if (stat === 1) {
            const subPathInfo = splitPath(direntPath);
            let item = [];
            if (subPathInfo.isAsar) {
              const archive = getOrCreateArchive(subPathInfo.asarPath);
              if (!archive) return;
              const files = archive.readdir(subPathInfo.filePath);
              if (!files) return result;
              item = files;
            } else {
              item = await binding.readdir(
                path.toNamespacedPath(direntPath),
                options!.encoding,
                false,
                kUsePromises
              );
            }
            queue.push([direntPath, item]);
          }
        }
      }
    }

    return result;
  }

  function readdirSyncRecursive (basePath: string, options: ReaddirOptions) {
    const withFileTypes = Boolean(options!.withFileTypes);
    const encoding = options!.encoding;

    const readdirResults: string[] = [];
    const pathsQueue = [basePath];

    function read (pathArg: string) {
      let readdirResult;

      const pathInfo = splitPath(pathArg);
      if (pathInfo.isAsar) {
        const { asarPath, filePath } = pathInfo;
        const archive = getOrCreateArchive(asarPath);
        if (!archive) return;

        readdirResult = archive.readdir(filePath);
        if (!readdirResult) return;
        // If we're in an asar dir, we need to ensure the result is in the same format as the
        // native call to readdir withFileTypes i.e. an array of arrays.
        if (withFileTypes) {
          readdirResult = [
            [...readdirResult], readdirResult.map((p: string) => {
              return internalBinding('fs').internalModuleStat(path.join(pathArg, p));
            })
          ];
        }
      } else {
        readdirResult = binding.readdir(
          path.toNamespacedPath(pathArg),
          encoding,
          withFileTypes
        );
      }

      if (readdirResult === undefined) return;

      if (withFileTypes) {
        const length = readdirResult[0].length;
        for (let i = 0; i < length; i++) {
          const resultPath = path.join(pathArg, readdirResult[0][i]);
          const info = splitPath(resultPath);

          let type = readdirResult[1][i];
          if (info.isAsar) {
            const archive = getOrCreateArchive(info.asarPath);
            if (!archive) return;
            const stats = archive.stat(info.filePath);
            if (!stats) continue;
            type = stats.type;
          }

          const dirent = getDirent(pathArg, readdirResult[0][i], type);

          readdirResults.push(dirent);
          if (dirent.isDirectory()) {
            pathsQueue.push(path.join(dirent.path, dirent.name));
          }
        }
      } else {
        for (let i = 0; i < readdirResult.length; i++) {
          const resultPath = path.join(pathArg, readdirResult[i]);
          const relativeResultPath = path.relative(basePath, resultPath);
          const stat = internalBinding('fs').internalModuleStat(resultPath);

          readdirResults.push(relativeResultPath);
          if (stat === 1) pathsQueue.push(resultPath);
        }
      }
    }

    for (let i = 0; i < pathsQueue.length; i++) {
      read(pathsQueue[i]);
    }

    return readdirResults;
  }

  // Calling mkdir for directory inside asar archive should throw ENOTDIR
  // error, but on Windows it throws ENOENT.
  if (process.platform === 'win32') {
    const { mkdir } = fs;
    fs.mkdir = (pathArgument: string, options: any, callback: any) => {
      if (typeof options === 'function') {
        callback = options;
        options = {};
      }

      const pathInfo = splitPath(pathArgument);
      if (pathInfo.isAsar && pathInfo.filePath.length > 0) {
        const error = createError(AsarError.NOT_DIR);
        nextTick(callback, [error]);
        return;
      }

      mkdir(pathArgument, options, callback);
    };

    fs.promises.mkdir = util.promisify(fs.mkdir);

    const { mkdirSync } = fs;
    fs.mkdirSync = function (pathArgument: string, options: any) {
      const pathInfo = splitPath(pathArgument);
      if (pathInfo.isAsar && pathInfo.filePath.length) throw createError(AsarError.NOT_DIR);
      return mkdirSync(pathArgument, options);
    };
  }

  function invokeWithNoAsar (func: Function) {
    return function (this: any) {
      const processNoAsarOriginalValue = process.noAsar;
      process.noAsar = true;
      try {
        return func.apply(this, arguments);
      } finally {
        process.noAsar = processNoAsarOriginalValue;
      }
    };
  }

  // Strictly implementing the flags of fs.copyFile is hard, just do a simple
  // implementation for now. Doing 2 copies won't spend much time more as OS
  // has filesystem caching.
  overrideAPI(fs, 'copyFile');
  overrideAPISync(fs, 'copyFileSync');

  overrideAPI(fs, 'open');
  overrideAPISync(process, 'dlopen', 1);
  overrideAPISync(Module._extensions, '.node', 1);
  overrideAPISync(fs, 'openSync');

  const overrideChildProcess = (childProcess: Record<string, any>) => {
    // Executing a command string containing a path to an asar archive
    // confuses `childProcess.execFile`, which is internally called by
    // `childProcess.{exec,execSync}`, causing Electron to consider the full
    // command as a single path to an archive.
    const { exec, execSync } = childProcess;
    childProcess.exec = invokeWithNoAsar(exec);
    childProcess.exec[util.promisify.custom] = invokeWithNoAsar(exec[util.promisify.custom]);
    childProcess.execSync = invokeWithNoAsar(execSync);

    overrideAPI(childProcess, 'execFile');
    overrideAPISync(childProcess, 'execFileSync');
  };

  const asarReady = new WeakSet();

  // Lazily override the child_process APIs only when child_process is
  // fetched the first time.  We will eagerly override the child_process APIs
  // when this env var is set so that stack traces generated inside node unit
  // tests will match. This env var will only slow things down in users apps
  // and should not be used.
  if (process.env.ELECTRON_EAGER_ASAR_HOOK_FOR_TESTING) {
    overrideChildProcess(require('child_process'));
  } else {
    const originalModuleLoad = Module._load;
    Module._load = (request: string, ...args: any[]) => {
      const loadResult = originalModuleLoad(request, ...args);
      if (request === 'child_process' || request === 'node:child_process') {
        if (!asarReady.has(loadResult)) {
          asarReady.add(loadResult);
          // Just to make it obvious what we are dealing with here
          const childProcess = loadResult;

          overrideChildProcess(childProcess);
        }
      }
      return loadResult;
    };
  }
};
