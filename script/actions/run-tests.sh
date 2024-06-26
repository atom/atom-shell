#!/bin/bash
set -euo pipefail

export DISPLAY=:99
Xvfb :99 -screen 0 1024x768x16 -ac &
XVFB_PID=$!
node "$@"
