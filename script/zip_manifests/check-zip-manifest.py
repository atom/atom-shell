#!/usr/bin/env python3

import zipfile
import sys

def main(zip_path, manifest_in):
  with open(manifest_in, 'r', encoding='utf-8') as manifest, \
      zipfile.ZipFile(zip_path, 'r', allowZip64=True) as z:
    files_in_zip = set(z.namelist())
    files_in_manifest = {l.strip() for l in manifest.readlines()}
  added_files = files_in_zip - files_in_manifest
  removed_files = files_in_manifest - files_in_zip
  if added_files:
    print("Files added to bundle:")
    for f in sorted(list(added_files)):
      print('+' + f)
  if removed_files:
    print("Files removed from bundle:")
    for f in sorted(list(removed_files)):
      print('-' + f)

  return 1 if added_files or removed_files else 0

if __name__ == '__main__':
  sys.exit(main(sys.argv[1], sys.argv[2]))
