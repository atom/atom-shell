#!/usr/bin/env python3

import argparse
import json
import os
import warnings

from lib import git
from lib.patches import patch_from_dir

THREEWAY = "ELECTRON_USE_THREE_WAY_MERGE_FOR_PATCHES" in os.environ

def apply_patches(target):
  repo = target.get('repo')
  if not os.path.exists(repo):
    warnings.warn(f'repo not found: {repo}')
    return
  patch_dir = target.get('patch_dir')
  git.import_patches(
    committer_email="scripts@electron",
    committer_name="Electron Scripts",
    patch_data=patch_from_dir(patch_dir),
    repo=repo,
    threeway=THREEWAY,
  )

def apply_config(config):
  for target in config:
    apply_patches(target)

def parse_args():
  parser = argparse.ArgumentParser(description='Apply Electron patches')
  parser.add_argument('config', nargs='+',
                      type=argparse.FileType('r'),
                      help='patches\' config(s) in the JSON format')
  return parser.parse_args()


def main():
  for config_json in parse_args().config:
    apply_config(json.load(config_json))


if __name__ == '__main__':
  main()
