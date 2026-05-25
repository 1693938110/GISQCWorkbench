#!/usr/bin/env bash
set -euo pipefail

# GIS QC Workbench development SDK bootstrap for Ubuntu/WSL.
# Installs CMake/Ninja, Qt 6 Widgets, and GDAL/PROJ development packages.
# If the default Ubuntu archive is unreachable, re-run after switching to a reachable mirror.

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  qt6-base-dev \
  qt6-tools-dev \
  qt6-tools-dev-tools \
  libgdal-dev \
  gdal-bin \
  proj-bin

printf '\nInstalled versions:\n'
cmake --version | head -1
qmake6 --version | head -2 || true
gdal-config --version || true
pkg-config --modversion Qt6Widgets || true
