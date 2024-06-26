#!/usr/bin/env python3

import argparse
import os
import platform
import shutil
import subprocess
import sys

from lib.util import get_electron_branding, rm_rf

PROJECT_NAME = get_electron_branding()['project_name']
PRODUCT_NAME = get_electron_branding()['product_name']
SOURCE_ROOT = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))

def main():
  args = parse_args()

  source_root = os.path.abspath(args.source_root)
  initial_app_path = os.path.join(source_root, args.build_dir)
  app_path = create_app_copy(initial_app_path)

  if sys.platform == 'darwin':
    electron = os.path.join(app_path, 'Contents', 'MacOS', PRODUCT_NAME)
    ffmpeg_name = 'libffmpeg.dylib'
    ffmpeg_app_path = os.path.join(app_path, 'Contents', 'Frameworks',
                    f'{PRODUCT_NAME} Framework.framework',
                    'Libraries')
  elif sys.platform == 'win32':
    electron = os.path.join(app_path, f'{PROJECT_NAME}.exe')
    ffmpeg_app_path = app_path
    ffmpeg_name = 'ffmpeg.dll'
  else:
    electron = os.path.join(app_path, PROJECT_NAME)
    ffmpeg_app_path = app_path
    ffmpeg_name = 'libffmpeg.so'

  # Copy ffmpeg without proprietary codecs into app.
  ffmpeg_lib_path = os.path.join(source_root, args.ffmpeg_path, ffmpeg_name)
  shutil.copy(ffmpeg_lib_path, ffmpeg_app_path)

  returncode = 0
  try:
    test_path = os.path.join(SOURCE_ROOT, 'spec', 'fixtures',
        'no-proprietary-codecs.js')
    env = dict(os.environ)
    env['ELECTRON_ENABLE_STACK_DUMPING'] = 'true'
    # FIXME: Enable after ELECTRON_ENABLE_LOGGING works again
    # env['ELECTRON_ENABLE_LOGGING'] = 'true'
    testargs = [electron, test_path]
    if sys.platform != 'linux' and (platform.machine() == 'ARM64' or
        os.environ.get('TARGET_ARCH') == 'arm64'):
      testargs.append('--disable-accelerated-video-decode')
    subprocess.check_call(testargs, env=env)
  except subprocess.CalledProcessError as e:
    returncode = e.returncode
  except KeyboardInterrupt:
    returncode = 0

  if returncode == 0:
    print('ok Non proprietary ffmpeg does not contain proprietary codes.')
  return returncode


# Create copy of app to install ffmpeg library without proprietary codecs into
def create_app_copy(initial_app_path):
  app_path = os.path.join(os.path.dirname(initial_app_path),
                          os.path.basename(initial_app_path)
                          + '-no-proprietary-codecs')

  if sys.platform == 'darwin':
    app_name = f'{PRODUCT_NAME}.app'
    initial_app_path = os.path.join(initial_app_path, app_name)
    app_path = os.path.join(app_path, app_name)

  rm_rf(app_path)
  shutil.copytree(initial_app_path, app_path, symlinks=True)
  return app_path

def parse_args():
  parser = argparse.ArgumentParser(description='Test non-proprietary ffmpeg')
  parser.add_argument('-b', '--build-dir',
                      help='Path to an Electron build folder. \
                          Relative to the --source-root.',
                      default=None,
                      required=True)
  parser.add_argument('--source-root',
                      default=SOURCE_ROOT,
                      required=False)
  parser.add_argument('--ffmpeg-path',
                      help='Path to a folder with a ffmpeg lib. \
                          Relative to the --source-root.',
                      default=None,
                      required=True)
  return parser.parse_args()

if __name__ == '__main__':
  sys.exit(main())
