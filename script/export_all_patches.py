#!/usr/bin/env python3

import argparse
import json
import os
import warnings

from lib import git


def export_patches(target, dry_run):
  repo = target.get('repo')
  if not os.path.exists(repo):
    warnings.warn(f'repo not found: {repo}')
    return
  git.export_patches(
    dry_run=dry_run,
    grep=target.get('grep'),
    out_dir=target.get('patch_dir'),
    repo=repo
  )


def export_config(config, dry_run):
  for target in config:
    export_patches(target, dry_run)


def parse_args():
  parser = argparse.ArgumentParser(description='Export Electron patches')
  parser.add_argument('config', nargs='+',
                      type=argparse.FileType('r'),
                      help='patches\' config(s) in the JSON format')
  parser.add_argument("-d", "--dry-run",
    help="Checks whether the exported patches need to be updated.",
    default=False, action='store_true')
  return parser.parse_args()


def main():
  configs = parse_args().config
  dry_run = parse_args().dry_run
  for config_json in configs:
    export_config(json.load(config_json), dry_run)


if __name__ == '__main__':
  main()
