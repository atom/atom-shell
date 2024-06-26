#!/usr/bin/env python3

import glob
import os
import shutil
import subprocess
import sys
import tempfile

def is_fs_case_sensitive():
  with tempfile.NamedTemporaryFile(prefix='TmP') as tmp_file:
    return(not os.path.exists(tmp_file.name.lower()))

sys.path.append(
  os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + "/../.."))

from lib.config import PLATFORM
from lib.util import get_electron_branding, execute, store_artifact, \
                     get_out_dir, ELECTRON_DIR

RELEASE_DIR = get_out_dir()


PROJECT_NAME = get_electron_branding()['project_name']
PRODUCT_NAME = get_electron_branding()['product_name']
SYMBOLS_DIR = os.path.join(RELEASE_DIR, 'breakpad_symbols')

PDB_LIST = [
  os.path.join(RELEASE_DIR, f'{PROJECT_NAME}.exe.pdb')
]

PDB_LIST += glob.glob(os.path.join(RELEASE_DIR, '*.dll.pdb'))

NPX_CMD = "npx"
if sys.platform == "win32":
    NPX_CMD += ".cmd"


def main():
  os.chdir(ELECTRON_DIR)
  files = []
  if PLATFORM == 'win32':
    for pdb in PDB_LIST:
      run_symstore(pdb, SYMBOLS_DIR, PRODUCT_NAME)
    files = glob.glob(SYMBOLS_DIR + '/*.pdb/*/*.pdb')

  files += glob.glob(SYMBOLS_DIR + '/*/*/*.sym')

  for symbol_file in files:
    print("Generating Sentry src bundle for: " + symbol_file)
    npx_env = os.environ.copy()
    npx_env['npm_config_yes'] = 'true'
    subprocess.check_output([
      NPX_CMD, '@sentry/cli@1.62.0', 'difutil', 'bundle-sources',
      symbol_file], env=npx_env)

  files += glob.glob(SYMBOLS_DIR + '/*/*/*.src.zip')

  # The file upload needs to be symbols/:symbol_name/:hash/:symbol
  os.chdir(SYMBOLS_DIR)
  files = [os.path.relpath(f, os.getcwd()) for f in files]

  # The symbol server needs lowercase paths, it will fail otherwise
  # So lowercase all the file paths here
  if is_fs_case_sensitive():
    for f in files:
      lower_f = f.lower()
      if lower_f != f:
        if os.path.exists(lower_f):
          shutil.rmtree(lower_f)
        lower_dir = os.path.dirname(lower_f)
        if not os.path.exists(lower_dir):
          os.makedirs(lower_dir)
        shutil.copy2(f, lower_f)
  files = [f.lower() for f in files]
  for f in files:
    assert os.path.exists(f)

  upload_symbols(files)


def run_symstore(pdb, dest, product):
  for attempt in range(2):
    try:
      execute(['symstore', 'add', '/r', '/f', pdb, '/s', dest, '/t', product])
      break
    except Exception as e:
      print(f"An error occurred while adding '{pdb}' to SymStore: {str(e)}")
      if attempt == 0:
        print("Retrying...")

def upload_symbols(files):
  store_artifact(SYMBOLS_DIR, 'symbols',
        files)


if __name__ == '__main__':
  sys.exit(main())
