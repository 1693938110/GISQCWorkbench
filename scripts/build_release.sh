#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/dist/GISQCWorkbench-latest"
mkdir -p "$OUT"

if ! command -v cmake >/dev/null 2>&1; then
  echo "ERROR: cmake not found. Install Qt/CMake on Windows or run scripts/build_windows_release.ps1 from a Qt developer shell." >&2
  exit 1
fi

cmake -S "$ROOT" -B "$ROOT/build-release" -DCMAKE_BUILD_TYPE=Release -DGISQC_BUILD_TESTS=OFF -DGISQC_BUILD_QT_APP=ON
cmake --build "$ROOT/build-release" --config Release --target GISQCWorkbench

EXE="$(find "$ROOT/build-release" -name 'GISQCWorkbench.exe' -o -name 'GISQCWorkbench' | head -n 1)"
if [[ -z "$EXE" ]]; then
  echo "ERROR: built executable not found" >&2
  exit 1
fi
cp "$EXE" "$OUT/"
cp -r "$ROOT/data" "$OUT/"
echo "Built output copied to $OUT"
