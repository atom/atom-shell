#!/usr/bin/env python3

import contextlib
import errno
import json
import os
import platform
import shutil
import subprocess
import sys
from urllib.request import urlopen
import zipfile

# from lib.config import is_verbose_mode
def is_verbose_mode():
  return False

ELECTRON_DIR = os.path.abspath(
  os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
)
TS_NODE = os.path.join(ELECTRON_DIR, 'node_modules', '.bin', 'ts-node')
SRC_DIR = os.path.abspath(os.path.join(__file__, '..', '..', '..', '..'))

if sys.platform in ['win32', 'cygwin']:
  TS_NODE += '.cmd'

@contextlib.contextmanager
def scoped_cwd(path):
  cwd = os.getcwd()
  os.chdir(path)
  try:
    yield
  finally:
    os.chdir(cwd)


def download(text, url, path):
  safe_mkdir(os.path.dirname(path))
  with open(path, 'wb') as local_file, urlopen(url) as web_file:
    print(f"Downloading {url} to {path}")
    info = web_file.info()
    if hasattr(info, 'getheader'):
      file_size = int(info.getheaders("Content-Length")[0])
    else:
      file_size = int(info.get("Content-Length")[0])
    downloaded_size = 0
    block_size = 4096

    ci = os.environ.get('CI') is not None

    while True:
      buf = web_file.read(block_size)
      if not buf:
        break

      downloaded_size += len(buf)
      local_file.write(buf)

      if not ci:
        percent = downloaded_size * 100. / file_size
        status = f"\r{text}  {downloaded_size:10d}  [{percent:3.1f}%]"
        print(status, end=' ')

    if ci:
      print(f"{text} done.")
    else:
      print()
  return path


def make_zip(zip_file_path, files, dirs):
  safe_unlink(zip_file_path)
  if sys.platform == 'darwin':
    allfiles = files + dirs
    execute(['zip', '-r', '-y', zip_file_path] + allfiles)
  else:
    with zipfile.ZipFile(zip_file_path, "w",
                         zipfile.ZIP_DEFLATED,
                         allowZip64=True) as zip_file:
      for filename in files:
        zip_file.write(filename, filename)
      for dirname in dirs:
        for root, _, filenames in os.walk(dirname):
          for f in filenames:
            zip_file.write(os.path.join(root, f))
      zip_file.close()


def rm_rf(path):
  try:
    shutil.rmtree(path)
  except OSError:
    pass


def safe_unlink(path):
  try:
    os.unlink(path)
  except OSError as e:
    if e.errno != errno.ENOENT:
      raise


def safe_mkdir(path):
  try:
    os.makedirs(path)
  except OSError as e:
    if e.errno != errno.EEXIST:
      raise


def execute(argv, env=None, cwd=None):
  if env is None:
    env = os.environ
  if is_verbose_mode():
    print(' '.join(argv))
  try:
    output = subprocess.check_output(argv, stderr=subprocess.STDOUT,
                                     env=env, cwd=cwd)
    if is_verbose_mode():
      print(output)
    return output
  except subprocess.CalledProcessError as e:
    print(e.output)
    raise e


def get_electron_branding():
  SOURCE_ROOT = os.path.abspath(os.path.join(__file__, '..', '..', '..'))
  branding_file_path = os.path.join(
    SOURCE_ROOT, 'shell', 'app', 'BRANDING.json')
  with open(branding_file_path, encoding='utf-8') as file_in:
    return json.load(file_in)


cached_electron_version = None
def get_electron_version():
  global cached_electron_version
  if cached_electron_version is None:
    cached_electron_version = str.strip(execute([
      'node',
      '-p',
      'require("./script/lib/get-version").getElectronVersion()'
    ], cwd=ELECTRON_DIR).decode())
  return cached_electron_version

def store_artifact(prefix, key_prefix, files):
  # Azure Storage
  azput(prefix, key_prefix, files)

def azput(prefix, key_prefix, files):
  env = os.environ.copy()
  output = execute([
    'node',
    os.path.join(os.path.dirname(__file__), 'azput.js'),
    '--prefix', prefix,
    '--key_prefix', key_prefix,
  ] + files, env)
  print(output)

def get_out_dir():
  out_dir = 'Default'
  override = os.environ.get('ELECTRON_OUT_DIR')
  if override is not None:
    out_dir = override
  return os.path.join(SRC_DIR, 'out', out_dir)

# NOTE: This path is not created by gn, it is used as a scratch zone by our
#       upload scripts
def get_dist_dir():
  return os.path.join(get_out_dir(), 'gen', 'electron_dist')

def get_electron_exec():
  out_dir = get_out_dir()

  if sys.platform == 'darwin':
    return f'{out_dir}/Electron.app/Contents/MacOS/Electron'
  if sys.platform == 'win32':
    return f'{out_dir}/electron.exe'
  if sys.platform == 'linux':
    return f'{out_dir}/electron'

  raise Exception(
      f"get_electron_exec: unexpected platform '{sys.platform}'")

def get_buildtools_executable(name):
  buildtools = os.path.realpath(os.path.join(ELECTRON_DIR, '..', 'buildtools'))

  if sys.platform == 'darwin':
    chromium_platform = 'mac_arm64' if platform.machine() == 'arm64' else 'mac'
  elif sys.platform in ['win32', 'cygwin']:
    chromium_platform = 'win'
  elif sys.platform in ['linux', 'linux2']:
    chromium_platform = 'linux64'
  else:
    raise Exception(f"Unsupported platform: {sys.platform}")

  if name == 'clang-format':
    path = os.path.join(buildtools, chromium_platform, 'format', name)  
  else:
    path = os.path.join(buildtools, chromium_platform, name)
  if sys.platform == 'win32':
    path += '.exe'
  return path

def get_linux_binaries():
  return [
    'chrome-sandbox',
    'chrome_crashpad_handler',
    get_electron_branding()['project_name'],
    'libEGL.so',
    'libGLESv2.so',
    'libffmpeg.so',
    'libvk_swiftshader.so',
  ]
