#!/usr/bin/env python3

import argparse
import os
import sys

from lib.native_tests import TestsList, Verbosity, DisabledTestsPolicy


class Command:
  LIST = 'list'
  RUN = 'run'


def parse_args():
  parser = argparse.ArgumentParser(description='Run Google Test binaries')

  parser.add_argument('command',
                      choices=[Command.LIST, Command.RUN],
                      help='command to execute')

  parser.add_argument('-b', '--binary', nargs='+', required=False,
                      help='binaries to run')
  parser.add_argument('-c', '--config', required=True,
                      help='path to a tests config')
  parser.add_argument('-t', '--tests-dir', required=False,
                      help='path to a directory with test binaries')
  parser.add_argument('-o', '--output-dir', required=False,
                      help='path to a folder to save tests results')

  disabled_tests = parser.add_mutually_exclusive_group()
  disabled_tests.add_argument('--only-disabled-tests',
                              dest='disabled_tests_policy',
                              action='store_const',
                              const=DisabledTestsPolicy.ONLY,
                              help='run disabled tests only')
  disabled_tests.add_argument('--include-disabled-tests',
                              dest='disabled_tests_policy',
                              action='store_const',
                              const=DisabledTestsPolicy.INCLUDE,
                              help='if disabled tests should be run as well')
  parser.set_defaults(disabled_tests_policy=DisabledTestsPolicy.DISABLE)

  verbosity = parser.add_mutually_exclusive_group()
  verbosity.add_argument('-v', '--verbosity', required=False,
                         default=Verbosity.CHATTY,
                         choices=Verbosity.get_all(),
                         help='set verbosity level')
  verbosity.add_argument('-q', '--quiet', required=False, action='store_const',
                         const=Verbosity.ERRORS, dest='verbosity',
                         help='suppress stdout from test binaries')
  verbosity.add_argument('-qq', '--quiet-quiet',
                         # https://youtu.be/bXd-zZLV2i0?t=41s
                         required=False, action='store_const',
                         const=Verbosity.SILENT, dest='verbosity',
                         help='suppress stdout and stderr from test binaries')

  args = parser.parse_args()

  # Additional checks.
  if args.command == Command.RUN and args.tests_dir is None:
    parser.error("specify a path to a dir with test binaries via --tests-dir")

  # Absolutize and check paths.
  # 'config' must exist and be a file.
  args.config = os.path.abspath(args.config)
  if not os.path.isfile(args.config):
    parser.error(f"file '{args.config}' doesn't exist")

  # 'tests_dir' must exist and be a directory.
  if args.tests_dir is not None:
    args.tests_dir = os.path.abspath(args.tests_dir)
    if not os.path.isdir(args.tests_dir):
      parser.error(f"directory '{args.tests_dir}' doesn't exist")

  # 'output_dir' must exist and be a directory.
  if args.output_dir is not None:
    args.output_dir = os.path.abspath(args.output_dir)
    if not os.path.isdir(args.output_dir):
      parser.error(f"directory '{args.output_dir}' doesn't exist")

  return args


def main():
  args = parse_args()
  tests_list = TestsList(args.config, args.tests_dir)

  if args.command == Command.LIST:
    all_binaries_names = tests_list.get_for_current_platform()
    print('\n'.join(all_binaries_names))
    return 0

  if args.command == Command.RUN:
    if args.binary is not None:
      return tests_list.run(args.binary, args.output_dir, args.verbosity,
                            args.disabled_tests_policy)

    return tests_list.run_all(args.output_dir, args.verbosity,
                              args.disabled_tests_policy)

  raise AssertionError(f"unexpected command '{args.command}'")


if __name__ == '__main__':
  sys.exit(main())
