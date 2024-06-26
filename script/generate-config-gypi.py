#!/usr/bin/env python3

import ast
import os
import pprint
import re
import subprocess
import sys
from lib.config import get_target_arch

ELECTRON_DIR = os.path.abspath(os.path.join(__file__, '..', '..'))
NODE_DIR = os.path.join(ELECTRON_DIR, '..', 'third_party', 'electron_node')

def run_node_configure(target_cpu):
  configure = os.path.join(NODE_DIR, 'configure.py')
  args = ['--dest-cpu', target_cpu]
  # Enabled in Chromium's V8, will be disabled on 32bit via
  # common.gypi rules
  args += ['--experimental-enable-pointer-compression']

  # Work around "No acceptable ASM compiler found" error on some System,
  # it breaks nothing since Electron does not use OpenSSL.
  args += ['--openssl-no-asm']

  # Enable whole-program optimization for electron native modules.
  if sys.platform == "win32":
    args += ['--with-ltcg']
  subprocess.check_call([sys.executable, configure] + args)

def read_node_config_gypi():
  config_gypi = os.path.join(NODE_DIR, 'config.gypi')
  with open(config_gypi, 'r', encoding='utf-8') as file_in:
    content = file_in.read()
    return ast.literal_eval(content)

def read_electron_args():
  all_gn = os.path.join(ELECTRON_DIR, 'build', 'args', 'all.gn')
  args = {}
  with open(all_gn, 'r', encoding='utf-8') as file_in:
    for line in file_in:
      if line.startswith('#'):
        continue
      m = re.match('(\w+) = (.+)', line)
      if m == None:
        continue
      args[m.group(1)] = m.group(2)
  return args

def main(target_file, target_cpu):
  run_node_configure(target_cpu)
  config = read_node_config_gypi()
  args = read_electron_args()

  # Remove the generated config.gypi to make the parallel/test-process-config
  # test pass.
  os.remove(os.path.join(NODE_DIR, 'config.gypi'))

  v = config['variables']
  # Electron specific variables:
  v['built_with_electron'] = 1
  v['node_module_version'] = int(args['node_module_version'])
  # Used by certain versions of node-gyp.
  v['build_v8_with_gn'] = 'false'

  with open(target_file, 'w+', encoding='utf-8') as file_out:
    file_out.write(pprint.pformat(config, indent=2))

if __name__ == '__main__':
  sys.exit(main(sys.argv[1], sys.argv[2]))
