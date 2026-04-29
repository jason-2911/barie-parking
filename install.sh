#!/usr/bin/env bash

set -euo pipefail

if [[ $(uname -s) != "Linux" ]]; then
    echo "Error: install.sh currently supports Debian/Ubuntu Linux only."
    echo "Use your platform's package manager for Qt6, qrencode, mosquitto, and libgpiod."
    exit 1
fi

if [[ $EUID -ne 0 ]]; then
    echo "This script must be run as root or with sudo."
    echo "Example: sudo ./install.sh"
    exit 1
fi

apt update

apt install -y \
    build-essential \
    cmake \
    pkg-config \
    qt6-base-dev \
    libqrencode-dev \
    libmosquitto-dev \
    libgpiod-dev \
    alsa-utils

cat <<'EOF'
Success! Dependencies are installed.
Next steps:
  mkdir -p build
  cd build
  cmake ..
  make
EOF
