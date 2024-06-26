#!/usr/bin/env python3

import os
import subprocess
import sys

from lib.util import SRC_DIR

PYYAML_LIB_DIR = os.path.join(SRC_DIR, 'third_party', 'pyyaml', 'lib')
sys.path.append(PYYAML_LIB_DIR)
import yaml  #pylint: disable=wrong-import-position,wrong-import-order


class Verbosity:
  CHATTY = 'chatty'  # stdout and stderr
  ERRORS = 'errors'  # stderr only
  SILENT = 'silent'  # no output

  @staticmethod
  def get_all():
    return Verbosity.__get_all_in_order()

  @staticmethod
  def __get_all_in_order():
    return [Verbosity.SILENT, Verbosity.ERRORS, Verbosity.CHATTY]

  @staticmethod
  def __get_indices(*values):
    ordered = Verbosity.__get_all_in_order()
    indices = map(ordered.index, values)
    return indices

  @staticmethod
  def ge(a, b):
    """Greater or equal"""
    a_index, b_index = Verbosity.__get_indices(a, b)
    return a_index >= b_index

  @staticmethod
  def le(a, b):
    """Less or equal"""
    a_index, b_index = Verbosity.__get_indices(a, b)
    return a_index <= b_index


class DisabledTestsPolicy:
  DISABLE = 'disable'  # Disabled tests are disabled. Wow. Much sense.
  ONLY = 'only'  # Only disabled tests should be run.
  INCLUDE = 'include'  # Do not disable any tests.


class Platform:
  LINUX = 'linux'
  MAC = 'mac'
  WINDOWS = 'windows'

  @staticmethod
  def get_current():
    platform = sys.platform
    if platform in ('linux', 'linux2'):
      return Platform.LINUX
    if platform == 'darwin':
      return Platform.MAC
    if platform in ('cygwin', 'win32'):
      return Platform.WINDOWS

    raise AssertionError(
        f"unexpected current platform '{platform}'")

  @staticmethod
  def get_all():
    return [Platform.LINUX, Platform.MAC, Platform.WINDOWS]

  @staticmethod
  def is_valid(platform):
    return platform in Platform.get_all()


class TestsList():
  def __init__(self, config_path, tests_dir):
    self.config_path = config_path
    self.tests_dir = tests_dir

    # A dict with binary names (e.g. 'base_unittests') as keys
    # and various test data as values of dict type.
    self.tests = TestsList.__get_tests_list(config_path)

  def __len__(self):
    return len(self.tests)

  def get_for_current_platform(self):
    all_binaries = self.tests.keys()
    supported_binaries = filter(self.__platform_supports, all_binaries)
    return supported_binaries

  def run(self, binaries, output_dir=None, verbosity=Verbosity.CHATTY,
      disabled_tests_policy=DisabledTestsPolicy.DISABLE):
    # Don't run anything twice.
    binaries = set(binaries)

    # First check that all names are present in the config.
    for binary_name in binaries:
      if binary_name not in self.tests:
        msg = f"binary {binary_name} not found in config '{self.config_path}'"
        raise Exception(msg)

    # Respect the "platform" setting.
    for binary_name in binaries:
      if not self.__platform_supports(binary_name):
        host = Platform.get_current()
        errmsg = f"binary {binary_name} cannot run on {host}. Check the config"
        raise Exception(errmsg)

    suite_returncode = sum(
        self.__run(binary, output_dir, verbosity, disabled_tests_policy)
        for binary in binaries)
    return suite_returncode

  def run_all(self, output_dir=None, verbosity=Verbosity.CHATTY,
      disabled_tests_policy=DisabledTestsPolicy.DISABLE):
    return self.run(self.get_for_current_platform(), output_dir, verbosity,
                    disabled_tests_policy)

  @staticmethod
  def __get_tests_list(config_path):
    tests_list = {}
    config_data = TestsList.__get_config_data(config_path)

    for data_item in config_data['tests']:
      (binary_name, test_data) = TestsList.__get_test_data(data_item)
      tests_list[binary_name] = test_data

    return tests_list

  @staticmethod
  def __get_config_data(config_path):
    with open(config_path, 'r', encoding='utf-8') as stream:
      return yaml.load(stream)

  @staticmethod
  def __expand_shorthand(value):
    """ Treat a string as {'string_value': None}."""
    if isinstance(value, dict):
      return value

    if isinstance(value, str):
      return {value: None}

    raise AssertionError(f"unexpected shorthand type: {type(value)}")

  @staticmethod
  def __make_a_list(value):
    """Make a list if not already a list."""
    if isinstance(value, list):
        return value
    return [value]

  @staticmethod
  def __merge_nested_lists(value):
    """Converts a dict of lists to a list."""
    if isinstance(value, list):
        return value

    if isinstance(value, dict):
      # It looks ugly as hell, but it does the job.
      return [list_item for key in value for list_item in value[key]]

    raise AssertionError(
        f"unexpected type for list merging: {type(value)}")

  def __platform_supports(self, binary_name):
    return Platform.get_current() in self.tests[binary_name]['platforms']

  @staticmethod
  def __get_test_data(data_item):
    data_item = TestsList.__expand_shorthand(data_item)

    binary_name = data_item.keys()[0]
    test_data = {
      'excluded_tests': [],
      'platforms': Platform.get_all()
    }

    configs = data_item[binary_name]
    if configs is not None:
      # List of excluded tests.
      if 'disabled' in configs:
        excluded_tests = TestsList.__merge_nested_lists(configs['disabled'])
        test_data['excluded_tests'] = excluded_tests

      # List of platforms to run the tests on.
      if 'platform' in configs:
          platforms = TestsList.__make_a_list(configs['platform'])

          for platform in platforms:
            assert Platform.is_valid(platform), \
                f"Unsupported platform {platform}, check {binary_name} config"

          test_data['platforms'] = platforms

    return (binary_name, test_data)

  def __run(self, binary_name, output_dir, verbosity,
      disabled_tests_policy):
    binary_path = os.path.join(self.tests_dir, binary_name)
    test_binary = TestBinary(binary_path)

    test_data = self.tests[binary_name]
    included_tests = []
    excluded_tests = test_data['excluded_tests']

    if disabled_tests_policy == DisabledTestsPolicy.ONLY:
      if len(excluded_tests) == 0:
        # There is nothing to run.
        return 0
      included_tests, excluded_tests = excluded_tests, included_tests

    if disabled_tests_policy == DisabledTestsPolicy.INCLUDE:
      excluded_tests = []

    output_file_path = TestsList.__get_output_path(binary_name, output_dir)

    return test_binary.run(included_tests=included_tests,
                           excluded_tests=excluded_tests,
                           output_file_path=output_file_path,
                           verbosity=verbosity)

  @staticmethod
  def __get_output_path(binary_name, output_dir=None):
    if output_dir is None:
      return None

    return os.path.join(output_dir, f"results_{binary_name}.xml")


class TestBinary():
  # Is only used when writing to a file.
  output_format = 'xml'

  def __init__(self, binary_path):
    self.binary_path = binary_path

  def run(self, included_tests=None, excluded_tests=None,
      output_file_path=None, verbosity=Verbosity.CHATTY):
    gtest_filter = TestBinary.__get_gtest_filter(included_tests,
                                                 excluded_tests)
    gtest_output = TestBinary.__get_gtest_output(output_file_path)

    args = [self.binary_path, gtest_filter, gtest_output]

    returncode = 0

    with open(os.devnull, "w", encoding='utf-8') as devnull:
      stdout = stderr = None
      if Verbosity.le(verbosity, Verbosity.ERRORS):
        stdout = devnull
        if verbosity == Verbosity.SILENT:
          stderr = devnull

      try:
        returncode = subprocess.call(args, stdout=stdout, stderr=stderr)
      except Exception as exception:
        if Verbosity.ge(verbosity, Verbosity.ERRORS):
          print(f"An error occurred while running '{self.binary_path}':",
              '\n', exception, file=sys.stderr)
        returncode = 1

    return returncode

  @staticmethod
  def __get_gtest_filter(included_tests, excluded_tests):
    included_str = TestBinary.__list_tests(included_tests)
    excluded_str = TestBinary.__list_tests(excluded_tests)
    return f"--gtest_filter={included_str}-{excluded_str}"

  @staticmethod
  def __get_gtest_output(output_file_path):
    if output_file_path is None:
      return ""
    return f"--gtest_output={TestBinary.output_format}:{output_file_path}"

  @staticmethod
  def __list_tests(tests):
    if tests is None:
      return ''
    return ':'.join(tests)
