#!/usr/bin/env python3

import codecs
import os

PATCH_DIR_PREFIX = "Patch-Dir: "
PATCH_FILENAME_PREFIX = "Patch-Filename: "
PATCH_LINE_PREFIXES = (PATCH_DIR_PREFIX, PATCH_FILENAME_PREFIX)


def is_patch_location_line(line):
  return line.startswith(PATCH_LINE_PREFIXES)

def read_patch(patch_dir, patch_filename):
  """Read a patch from |patch_dir/filename| and amend the commit message with
  metadata about the patch file it came from."""
  ret = []
  added_patch_location = False
  patch_path = os.path.join(patch_dir, patch_filename)
  with codecs.open(patch_path, encoding='utf-8') as f:
    for l in f.readlines():
      line_has_correct_start = l.startswith('diff -') or l.startswith('---')
      if not added_patch_location and line_has_correct_start:
        ret.append(f'{PATCH_DIR_PREFIX}{patch_dir}\n')
        ret.append(f'{PATCH_FILENAME_PREFIX}{patch_filename}\n')
        added_patch_location = True
      ret.append(l)
  return ''.join(ret)


def patch_from_dir(patch_dir):
  """Read a directory of patches into a format suitable for passing to
  'git am'"""
  with open(os.path.join(patch_dir, ".patches"), encoding='utf-8') as file_in:
    patch_list = [line.rstrip('\n') for line in file_in.readlines()]

  return ''.join([
    read_patch(patch_dir, patch_filename)
    for patch_filename in patch_list
  ])
