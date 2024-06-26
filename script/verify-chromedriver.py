#!/usr/bin/env python3

import argparse
import os
import re
import subprocess
import sys

SOURCE_ROOT = os.path.abspath(os.path.dirname(os.path.dirname(__file__)))


def main():
  args = parse_args()

  chromedriver_name = {
    'darwin': 'chromedriver',
    'win32': 'chromedriver.exe',
    'linux': 'chromedriver',
    'linux2': 'chromedriver'
}

  chromedriver_path = os.path.join(
    args.source_root, args.build_dir, chromedriver_name[sys.platform])
  with subprocess.Popen([chromedriver_path],
                        stdout=subprocess.PIPE,
                        universal_newlines=True) as proc:
    try:
      output = proc.stdout.readline()
    except KeyboardInterrupt:
      returncode = 0
    finally:
      proc.terminate()

  returncode = 0
  match = re.search(
    '^Starting ChromeDriver [0-9]+.[0-9]+.[0-9]+.[0-9]+ .* on port [0-9]+$',
    output
  )

  if match is None:
    returncode = 1

  if returncode == 0:
    print('ok ChromeDriver is able to be initialized.')

  return returncode


def parse_args():
    parser=argparse.ArgumentParser(description='Test ChromeDriver')
    parser.add_argument('--source-root',
                        default=SOURCE_ROOT,
                        required=False)
    parser.add_argument('--build-dir',
                        default=None,
                        required=True)
    return parser.parse_args()


if __name__ == '__main__':
    sys.exit(main())
