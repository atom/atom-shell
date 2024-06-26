#!/bin/bash

if [ "`uname`" == "Darwin" ]; then
  if [ -z "$MAS_BUILD" ]; then
    BUILD_TYPE="darwin"
  else
    BUILD_TYPE="mas"
  fi
elif [ "`uname`" == "Linux" ]; then
  BUILD_TYPE="linux"
else
  echo "Unsupported platform"
  exit 1
fi

GENERATED_ARTIFACTS="generated_artifacts_${BUILD_TYPE}_${TARGET_ARCH}"

echo Creating $GENERATED_ARTIFACTS...
rm -rf $GENERATED_ARTIFACTS
mkdir $GENERATED_ARTIFACTS

SRC_ARTIFACTS="src_artifacts_${BUILD_TYPE}_${TARGET_ARCH}"

echo Creating $SRC_ARTIFACTS...
rm -rf $SRC_ARTIFACTS
mkdir $SRC_ARTIFACTS

mv_if_exist() {
  if [ -f "$1" ] || [ -d "$1" ]; then
    echo Storing $1
    mv $1 $GENERATED_ARTIFACTS
  else
    echo Skipping $1 - It is not present on disk
  fi
}

cp_if_exist() {
  if [ -f "$1" ] || [ -d "$1" ]; then
    echo Storing $1
    cp $1 $GENERATED_ARTIFACTS
  else
    echo Skipping $1 - It is not present on disk
  fi
}

move_src_dirs_if_exist() {
  mkdir src_artifacts

  for dir in \
    src/out/Default/gen/node_headers \
    src/out/Default/overlapped-checker \
    src/out/Default/ffmpeg \
    src/out/Default/hunspell_dictionaries \
    src/third_party/electron_node \
    src/third_party/nan \
    src/cross-arch-snapshots \
    src/third_party/llvm-build \
    src/build/linux \
    src/buildtools/mac \
    src/buildtools/third_party/libc++ \
    src/buildtools/third_party/libc++abi \
    src/third_party/libc++ \
    src/third_party/libc++abi \
    src/out/Default/obj/buildtools/third_party \
    src/v8/tools/builtins-pgo
  do
    if [ -d "$dir" ]; then
      mkdir -p src_artifacts/$(dirname $dir)
      cp -r $dir/ src_artifacts/$dir
    fi      
  done

  tar -C src_artifacts -cf src_artifacts.tar ./

  echo Storing src_artifacts.tar
  mv src_artifacts.tar $SRC_ARTIFACTS
}

# Generated Artifacts
mv_if_exist src/out/Default/dist.zip
mv_if_exist src/out/Default/gen/node_headers.tar.gz
mv_if_exist src/out/Default/symbols.zip
mv_if_exist src/out/Default/mksnapshot.zip
mv_if_exist src/out/Default/chromedriver.zip
mv_if_exist src/out/ffmpeg/ffmpeg.zip
mv_if_exist src/out/Default/hunspell_dictionaries.zip
mv_if_exist src/cross-arch-snapshots
cp_if_exist src/out/electron_ninja_log
cp_if_exist src/out/Default/.ninja_log

move_src_dirs_if_exist
