#!/bin/bash

set -eo pipefail

buildtools=$HOME/.electron_build_tools
gclient_root=/workspaces/gclient
buildtools_configs=/workspaces/buildtools-configs

export PATH="$PATH:$buildtools/src"

# Create the persisted buildtools config folder
mkdir -p $buildtools_configs
mkdir -p $gclient_root/.git-cache
rm -f $buildtools/configs
ln -s $buildtools_configs $buildtools/configs

# Write the gclient config if it does not already exist
if [ ! -f $gclient_root/.gclient ]; then
  echo "Creating gclient config"

  echo "solutions = [
      { \"name\"        : \"src/electron\",
          \"url\"         : \"https://github.com/electron/electron\",
          \"deps_file\"   : \"DEPS\",
          \"managed\"     : False,
          \"custom_deps\" : {
          },
          \"custom_vars\": {},
      },
    ]
  " >$gclient_root/.gclient
fi

# Write the default buildtools config file if it does
# not already exist
if [ ! -f $buildtools/configs/evm.testing.json ]; then
  echo "Creating build-tools testing config"

  write_config() {
    echo "
        {
            \"root\": \"/workspaces/gclient\",
            \"remotes\": {
                \"electron\": {
                    \"origin\": \"https://github.com/electron/electron.git\"
                }
            },
            \"gen\": {
                \"args\": [
                    \"import(\\\"//electron/build/args/testing.gn\\\")\",
                    \"use_remoteexec = true\"
                ],
                \"out\": \"Testing\"
            },
            \"env\": {
                \"CHROMIUM_BUILDTOOLS_PATH\": \"/workspaces/gclient/src/buildtools\",
                \"GIT_CACHE_PATH\": \"/workspaces/gclient/.git-cache\"
            },
            \"\$schema\": \"file:///home/builduser/.electron_build_tools/evm-config.schema.json\",
            \"configValidationLevel\": \"strict\",
            \"reclient\": \"$1\",
            \"goma\": \"none\",
            \"preserveXcode\": 5
        }
    " >$buildtools/configs/evm.testing.json
  }

  write_config remote_exec

  e use testing 
else
  echo "build-tools testing config already exists"
fi
