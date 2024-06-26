#!/usr/bin/env python3

import os
import sys

PLATFORM = {
  'cygwin': 'win32',
  'msys': 'win32',
  'darwin': 'darwin',
  'linux': 'linux',
  'linux2': 'linux',
  'win32': 'win32',
}[sys.platform]

verbose_mode = False


def get_platform_key():
  if 'MAS_BUILD' in os.environ:
    return 'mas'

  return PLATFORM


def get_target_arch():
  arch = os.environ.get('TARGET_ARCH')
  if arch is None:
    return 'x64'
  return arch


def enable_verbose_mode():
  print('Running in verbose mode')
  global verbose_mode
  verbose_mode = True


def is_verbose_mode():
  return verbose_mode


def get_zip_name(name, version, suffix=''):
  arch = get_target_arch()
  if arch == 'arm':
    arch += 'v7l'
  zip_name = f'{name}-{version}-{get_platform_key()}-{arch}'
  if suffix:
    zip_name += '-' + suffix
  return zip_name + '.zip'
