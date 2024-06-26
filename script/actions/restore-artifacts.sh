#!/bin/bash

GENERATED_ARTIFACTS="generated_artifacts_${BUILD_TYPE}_${TARGET_ARCH}"
SRC_ARTIFACTS="src_artifacts_${BUILD_TYPE}_${TARGET_ARCH}"

mv_if_exist() {
  if [ -f "${GENERATED_ARTIFACTS}/$1" ] || [ -d "${GENERATED_ARTIFACTS}/$1" ]; then
    echo Restoring $1 to $2
    mkdir -p $2
    mv $GENERATED_ARTIFACTS/$1 $2
  else
    echo Skipping $1 - It is not present on disk
  fi
}

untar_if_exist() {
  if [ -f "${SRC_ARTIFACTS}/$1" ] || [ -d "${SRC_ARTIFACTS}/$1" ]; then
    echo Restoring $1 to current directory
    tar -xf ${SRC_ARTIFACTS}/$1
  else
    echo Skipping $1 - It is not present on disk
  fi
}

echo Restoring artifacts from $GENERATED_ARTIFACTS

# Restore generated artifacts
mv_if_exist dist.zip src/out/Default
mv_if_exist node_headers.tar.gz src/out/Default/gen
mv_if_exist symbols.zip src/out/Default
mv_if_exist mksnapshot.zip src/out/Default
mv_if_exist chromedriver.zip src/out/Default
mv_if_exist ffmpeg.zip src/out/ffmpeg
mv_if_exist hunspell_dictionaries.zip src/out/Default
mv_if_exist cross-arch-snapshots src

echo Restoring artifacts from $SRC_ARTIFACTS

# Restore src artifacts
untar_if_exist src_artifacts.tar
