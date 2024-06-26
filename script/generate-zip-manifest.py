#!/usr/bin/env python3

import zipfile
import sys

def main(zip_path, manifest_out):
  with open(manifest_out, 'w', encoding='utf-8') as manifest, \
      zipfile.ZipFile(zip_path, 'r', allowZip64=True) as z:
    for name in sorted(z.namelist()):
      manifest.write(name + '\n')
  return 0

if __name__ == '__main__':
  sys.exit(main(sys.argv[1], sys.argv[2]))
