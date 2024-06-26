#!/usr/bin/env python3

import argparse
import datetime
import hashlib
import json
import mmap
import os
import shutil
import subprocess
from struct import Struct
import sys

sys.path.append(
  os.path.abspath(os.path.dirname(os.path.abspath(__file__)) + "/../.."))

from zipfile import ZipFile
from lib.config import PLATFORM, get_target_arch, \
                       get_zip_name, enable_verbose_mode, \
                       is_verbose_mode, get_platform_key
from lib.util import get_electron_branding, execute, get_electron_version, \
                     store_artifact, get_electron_exec, get_out_dir, \
                     SRC_DIR, ELECTRON_DIR, TS_NODE


ELECTRON_VERSION = 'v' + get_electron_version()

PROJECT_NAME = get_electron_branding()['project_name']
PRODUCT_NAME = get_electron_branding()['product_name']

OUT_DIR = get_out_dir()

DIST_NAME = get_zip_name(PROJECT_NAME, ELECTRON_VERSION)
SYMBOLS_NAME = get_zip_name(PROJECT_NAME, ELECTRON_VERSION, 'symbols')
DSYM_NAME = get_zip_name(PROJECT_NAME, ELECTRON_VERSION, 'dsym')
DSYM_SNAPSHOT_NAME = get_zip_name(PROJECT_NAME, ELECTRON_VERSION,
                                  'dsym-snapshot')
PDB_NAME = get_zip_name(PROJECT_NAME, ELECTRON_VERSION, 'pdb')
DEBUG_NAME = get_zip_name(PROJECT_NAME, ELECTRON_VERSION, 'debug')
TOOLCHAIN_PROFILE_NAME = get_zip_name(PROJECT_NAME, ELECTRON_VERSION,
                                      'toolchain-profile')
CXX_OBJECTS_NAME = get_zip_name(PROJECT_NAME, ELECTRON_VERSION,
                                      'libcxx_objects')


def main():
  args = parse_args()
  if args.verbose:
    enable_verbose_mode()
  if args.upload_to_storage:
    utcnow = datetime.datetime.utcnow()
    args.upload_timestamp = utcnow.strftime('%Y%m%d')

  build_version = get_electron_build_version()
  if not ELECTRON_VERSION.startswith(build_version):
    errmsg = f"Tag ({ELECTRON_VERSION}) should match build ({build_version})\n"
    sys.stderr.write(errmsg)
    sys.stderr.flush()
    return 1

  tag_exists = False
  release = get_release(args.version)
  if not release['draft']:
    tag_exists = True

  if not args.upload_to_storage:
    assert release['exists'], \
          'Release does not exist; cannot upload to GitHub!'
    assert tag_exists == args.overwrite, \
          'You have to pass --overwrite to overwrite a published release'

  # Upload Electron files.
  # Rename dist.zip to  get_zip_name('electron', version, suffix='')
  electron_zip = os.path.join(OUT_DIR, DIST_NAME)
  shutil.copy2(os.path.join(OUT_DIR, 'dist.zip'), electron_zip)
  upload_electron(release, electron_zip, args)

  symbols_zip = os.path.join(OUT_DIR, SYMBOLS_NAME)
  shutil.copy2(os.path.join(OUT_DIR, 'symbols.zip'), symbols_zip)
  upload_electron(release, symbols_zip, args)

  if PLATFORM == 'darwin':
    if get_platform_key() == 'darwin' and get_target_arch() == 'x64':
      api_path = os.path.join(ELECTRON_DIR, 'electron-api.json')
      upload_electron(release, api_path, args)

      ts_defs_path = os.path.join(ELECTRON_DIR, 'electron.d.ts')
      upload_electron(release, ts_defs_path, args)

    dsym_zip = os.path.join(OUT_DIR, DSYM_NAME)
    shutil.copy2(os.path.join(OUT_DIR, 'dsym.zip'), dsym_zip)
    upload_electron(release, dsym_zip, args)

    dsym_snapshot_zip = os.path.join(OUT_DIR, DSYM_SNAPSHOT_NAME)
    shutil.copy2(os.path.join(OUT_DIR, 'dsym-snapshot.zip'), dsym_snapshot_zip)
    upload_electron(release, dsym_snapshot_zip, args)
  elif PLATFORM == 'win32':
    pdb_zip = os.path.join(OUT_DIR, PDB_NAME)
    shutil.copy2(os.path.join(OUT_DIR, 'pdb.zip'), pdb_zip)
    upload_electron(release, pdb_zip, args)
  elif PLATFORM == 'linux':
    debug_zip = os.path.join(OUT_DIR, DEBUG_NAME)
    shutil.copy2(os.path.join(OUT_DIR, 'debug.zip'), debug_zip)
    upload_electron(release, debug_zip, args)

    # Upload libcxx_objects.zip for linux only
    libcxx_objects = get_zip_name('libcxx-objects', ELECTRON_VERSION)
    libcxx_objects_zip = os.path.join(OUT_DIR, libcxx_objects)
    shutil.copy2(os.path.join(OUT_DIR, 'libcxx_objects.zip'),
        libcxx_objects_zip)
    upload_electron(release, libcxx_objects_zip, args)

    # Upload headers.zip and abi_headers.zip as non-platform specific
    if get_target_arch() == "x64":
      cxx_headers_zip = os.path.join(OUT_DIR, 'libcxx_headers.zip')
      upload_electron(release, cxx_headers_zip, args)

      abi_headers_zip = os.path.join(OUT_DIR, 'libcxxabi_headers.zip')
      upload_electron(release, abi_headers_zip, args)

  # Upload free version of ffmpeg.
  ffmpeg = get_zip_name('ffmpeg', ELECTRON_VERSION)
  ffmpeg_zip = os.path.join(OUT_DIR, ffmpeg)
  ffmpeg_build_path = os.path.join(SRC_DIR, 'out', 'ffmpeg', 'ffmpeg.zip')
  shutil.copy2(ffmpeg_build_path, ffmpeg_zip)
  upload_electron(release, ffmpeg_zip, args)

  chromedriver = get_zip_name('chromedriver', ELECTRON_VERSION)
  chromedriver_zip = os.path.join(OUT_DIR, chromedriver)
  shutil.copy2(os.path.join(OUT_DIR, 'chromedriver.zip'), chromedriver_zip)
  upload_electron(release, chromedriver_zip, args)

  mksnapshot = get_zip_name('mksnapshot', ELECTRON_VERSION)
  mksnapshot_zip = os.path.join(OUT_DIR, mksnapshot)
  if get_target_arch().startswith('arm') and PLATFORM != 'darwin':
    # Upload the x64 binary for arm/arm64 mksnapshot
    mksnapshot = get_zip_name('mksnapshot', ELECTRON_VERSION, 'x64')
    mksnapshot_zip = os.path.join(OUT_DIR, mksnapshot)

  shutil.copy2(os.path.join(OUT_DIR, 'mksnapshot.zip'), mksnapshot_zip)
  upload_electron(release, mksnapshot_zip, args)

  if PLATFORM == 'linux' and get_target_arch() == 'x64':
    # Upload the hunspell dictionaries only from the linux x64 build
    hunspell_dictionaries_zip = os.path.join(
      OUT_DIR, 'hunspell_dictionaries.zip')
    upload_electron(release, hunspell_dictionaries_zip, args)

  if not tag_exists and not args.upload_to_storage:
    # Upload symbols to symbol server.
    run_python_upload_script('upload-symbols.py')
    if PLATFORM == 'win32':
      run_python_upload_script('upload-node-headers.py', '-v', args.version)

  if PLATFORM == 'win32':
    toolchain_profile_zip = os.path.join(OUT_DIR, TOOLCHAIN_PROFILE_NAME)
    with ZipFile(toolchain_profile_zip, 'w') as myzip:
      myzip.write(
        os.path.join(OUT_DIR, 'windows_toolchain_profile.json'),
        'toolchain_profile.json')
    upload_electron(release, toolchain_profile_zip, args)

  return 0

def parse_args():
  parser = argparse.ArgumentParser(description='upload distribution file')
  parser.add_argument('-v', '--version', help='Specify the version',
                      default=ELECTRON_VERSION)
  parser.add_argument('-o', '--overwrite',
                      help='Overwrite a published release',
                      action='store_true')
  parser.add_argument('-p', '--publish-release',
                      help='Publish the release',
                      action='store_true')
  parser.add_argument('-s', '--upload_to_storage',
                      help='Upload assets to azure bucket',
                      dest='upload_to_storage',
                      action='store_true',
                      default=False,
                      required=False)
  parser.add_argument('--verbose',
                      action='store_true',
                      help='Mooooorreee logs')
  return parser.parse_args()


def run_python_upload_script(script, *args):
  script_path = os.path.join(
    ELECTRON_DIR, 'script', 'release', 'uploaders', script)
  print(execute([sys.executable, script_path] + list(args)))


def get_electron_build_version():
  if get_target_arch().startswith('arm') or 'CI' in os.environ:
    # In CI we just build as told.
    return ELECTRON_VERSION
  electron = get_electron_exec()
  return subprocess.check_output([electron, '--version']).strip()


class NonZipFileError(ValueError):
  """Raised when a given file does not appear to be a zip"""


def zero_zip_date_time(fname):
  """ Wrap strip-zip zero_zip_date_time within a file opening operation """
  try:
    with open(fname, 'r+b') as f:
      _zero_zip_date_time(f)
  except Exception:
    # pylint: disable=W0707
    raise NonZipFileError(fname)


def _zero_zip_date_time(zip_):
  def purify_extra_data(mm, offset, length, compressed_size=0):
    extra_header_struct = Struct("<HH")
    # 0. id
    # 1. length
    STRIPZIP_OPTION_HEADER = 0xFFFF
    EXTENDED_TIME_DATA = 0x5455
    # Some sort of extended time data, see
    # ftp://ftp.info-zip.org/pub/infozip/src/zip30.zip ./proginfo/extrafld.txt
    # fallthrough
    UNIX_EXTRA_DATA = 0x7875
    # Unix extra data; UID / GID stuff, see
    # ftp://ftp.info-zip.org/pub/infozip/src/zip30.zip ./proginfo/extrafld.txt
    ZIP64_EXTRA_HEADER = 0x0001
    zip64_extra_struct = Struct("<HHQQ")
    # ZIP64.
    # When a ZIP64 extra field is present this 8byte length
    # will override the 4byte length defined in canonical zips.
    # This is in the form:
    # - 0x0001 (header_id)
    # - 0x0010 [16] (header_length)
    # - ... (8byte uncompressed_length)
    # - ... (8byte compressed_length)
    mlen = offset + length

    while offset < mlen:
      values = list(extra_header_struct.unpack_from(mm, offset))
      _, header_length = values
      extra_struct = Struct("<HH" + "B" * header_length)
      values = list(extra_struct.unpack_from(mm, offset))
      header_id, header_length = values[:2]

      if header_id in (EXTENDED_TIME_DATA, UNIX_EXTRA_DATA):
        values[0] = STRIPZIP_OPTION_HEADER
        for i in range(2, len(values)):
          values[i] = 0xff
        extra_struct.pack_into(mm, offset, *values)
      if header_id == ZIP64_EXTRA_HEADER:
        assert header_length == 16
        values = list(zip64_extra_struct.unpack_from(mm, offset))
        header_id, header_length, _, compressed_size = values

      offset += extra_header_struct.size + header_length

    return compressed_size

  FILE_HEADER_SIGNATURE = 0x04034b50
  CENDIR_HEADER_SIGNATURE = 0x02014b50

  archive_size = os.fstat(zip_.fileno()).st_size
  signature_struct = Struct("<L")
  local_file_header_struct = Struct("<LHHHHHLLLHH")
  # 0. L signature
  # 1. H version_needed
  # 2. H gp_bits
  # 3. H compression_method
  # 4. H last_mod_time
  # 5. H last_mod_date
  # 6. L crc32
  # 7. L compressed_size
  # 8. L uncompressed_size
  # 9. H name_length
  # 10. H extra_field_length
  central_directory_header_struct = Struct("<LHHHHHHLLLHHHHHLL")
  # 0. L signature
  # 1. H version_made_by
  # 2. H version_needed
  # 3. H gp_bits
  # 4. H compression_method
  # 5. H last_mod_time
  # 6. H last_mod_date
  # 7. L crc32
  # 8. L compressed_size
  # 9. L uncompressed_size
  # 10. H file_name_length
  # 11. H extra_field_length
  # 12. H file_comment_length
  # 13. H disk_number_start
  # 14. H internal_attr
  # 15. L external_attr
  # 16. L rel_offset_local_header
  offset = 0

  mm = mmap.mmap(zip_.fileno(), 0)
  while offset < archive_size:
    if signature_struct.unpack_from(mm, offset) != (FILE_HEADER_SIGNATURE,):
      break
    values = list(local_file_header_struct.unpack_from(mm, offset))
    compressed_size, _, name_length, extra_field_length = values[7:11]
    # reset last_mod_time
    values[4] = 0
    # reset last_mod_date
    values[5] = 0x21
    local_file_header_struct.pack_into(mm, offset, *values)
    offset += local_file_header_struct.size + name_length
    if extra_field_length != 0:
      compressed_size = purify_extra_data(mm, offset, extra_field_length,
                                          compressed_size)
    offset += compressed_size + extra_field_length

  while offset < archive_size:
    if signature_struct.unpack_from(mm, offset) != (CENDIR_HEADER_SIGNATURE,):
      break
    values = list(central_directory_header_struct.unpack_from(mm, offset))
    file_name_length, extra_field_length, file_comment_length = values[10:13]
    # reset last_mod_time
    values[5] = 0
    # reset last_mod_date
    values[6] = 0x21
    central_directory_header_struct.pack_into(mm, offset, *values)
    offset += central_directory_header_struct.size
    offset += file_name_length + extra_field_length + file_comment_length
    if extra_field_length != 0:
      purify_extra_data(mm, offset - extra_field_length, extra_field_length)

  if offset == 0:
    raise NonZipFileError(zip_.name)


def upload_electron(release, file_path, args):
  filename = os.path.basename(file_path)

  # Strip zip non determinism before upload, in-place operation
  try:
    zero_zip_date_time(file_path)
  except NonZipFileError:
    pass

  # if upload_to_storage is set, skip github upload.
  # todo (vertedinde): migrate this variable to upload_to_storage
  if args.upload_to_storage:
    key_prefix = f'release-builds/{args.version}_{args.upload_timestamp}'
    store_artifact(os.path.dirname(file_path), key_prefix, [file_path])
    upload_sha256_checksum(args.version, file_path, key_prefix)
    return

  # Upload the file.
  upload_io_to_github(release, filename, file_path, args.version)

  # Upload the checksum file.
  upload_sha256_checksum(args.version, file_path)


def upload_io_to_github(release, filename, filepath, version):
  print(f'Uploading {filename} to GitHub')
  script_path = os.path.join(
    ELECTRON_DIR, 'script', 'release', 'uploaders', 'upload-to-github.ts')
  with subprocess.Popen([TS_NODE, script_path, filepath,
                         filename, str(release['id']), version],
                        stdout=subprocess.PIPE, 
                        stderr=subprocess.STDOUT) as upload_process:
    if is_verbose_mode():
      for c in iter(lambda: upload_process.stdout.read(1), b""):
        sys.stdout.buffer.write(c)
        sys.stdout.flush()


def upload_sha256_checksum(version, file_path, key_prefix=None):
  checksum_path = f'{file_path}.sha256sum'
  if key_prefix is None:
    key_prefix = f'checksums-scratchpad/{version}'
  sha256 = hashlib.sha256()
  with open(file_path, 'rb') as f:
    sha256.update(f.read())

  filename = os.path.basename(file_path)
  with open(checksum_path, 'w', encoding='utf-8') as checksum:
    checksum.write(f'{sha256.hexdigest()} *{filename}')
  store_artifact(os.path.dirname(checksum_path), key_prefix, [checksum_path])


def get_release(version):
  script_path = os.path.join(
    ELECTRON_DIR, 'script', 'release', 'find-github-release.js')

  # Strip warnings from stdout to ensure the only output is the desired object
  release_env = os.environ.copy()
  release_env['NODE_NO_WARNINGS'] = '1'
  release_info = execute(['node', script_path, version], release_env)
  if is_verbose_mode():
    print(f'Release info for version: {version}:\n')
    print(release_info)
  release = json.loads(release_info)
  return release

if __name__ == '__main__':
  sys.exit(main())
