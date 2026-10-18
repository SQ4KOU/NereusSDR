#!/bin/bash
set -e
sudo rm -rf "/Library/Audio/Plug-Ins/HAL/NereusSDRVAX.driver"
rm -f /dev/shm/nereussdr-vax-* /dev/shm/nereussdr-tx 2>/dev/null || true
sudo killall coreaudiod 2>/dev/null || true
echo "NereusSDR VAX HAL plugin uninstalled."
