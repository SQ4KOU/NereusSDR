#!/bin/bash
# NereusSDR VAX HAL plugin — postinstall
# On macOS 14.4+, launchctl kickstart of com.apple.audio.coreaudiod
# returns "Operation not permitted"; fall back to killall.

set -e

launchctl kickstart -kp system/com.apple.audio.coreaudiod 2>/dev/null \
  || sudo killall coreaudiod 2>/dev/null \
  || true

exit 0
