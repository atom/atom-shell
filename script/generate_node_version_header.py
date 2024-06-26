#!/usr/bin/env python3
import re
import sys

node_version_file = sys.argv[1]
out_file = sys.argv[2]
NMV = None
if len(sys.argv) > 3:
  NMV = sys.argv[3]

with open(node_version_file, 'r', encoding='utf-8') as in_file, \
    open(out_file, 'w', encoding='utf-8') as out_file:
  changed = False
  contents = in_file.read()
  new_contents = re.sub(
    r'^#define NODE_MODULE_VERSION [0-9]+$',
    '#define NODE_MODULE_VERSION ' + NMV,
    contents,
    flags=re.MULTILINE)

  changed = contents != new_contents

  if not changed and NMV is not None:
    raise Exception("Did not modify the NMV from nodes value, this value MUST "
                    "differ from node")

  out_file.writelines(new_contents)
