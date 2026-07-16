#!/bin/bash
#
# generate_version_header.sh - Generate core/include/snobol/version.h
#
# Usage:
#   ./dev/generate_version_header.sh
#
# The single source of truth for the library version is the
# `project(libsnobol4 VERSION X.Y.Z)` declaration in the top-level
# CMakeLists.txt.  CMake normally generates version.h via configure_file(),
# but the PHP extension (amalgam) build does not run CMake over the core, so
# this script produces the same header by parsing that declaration and
# substituting core/cmake/version.h.in.  Keep the version in CMakeLists.txt —
# do not hand-edit the generated header.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
CMAKE_LISTS="$PROJECT_ROOT/CMakeLists.txt"
TEMPLATE="$PROJECT_ROOT/core/cmake/version.h.in"
OUT="$PROJECT_ROOT/core/include/snobol/version.h"

if [ ! -f "$CMAKE_LISTS" ]; then
  echo "❌ CMakeLists.txt not found at $CMAKE_LISTS" >&2
  exit 1
fi
if [ ! -f "$TEMPLATE" ]; then
  echo "❌ version.h.in not found at $TEMPLATE" >&2
  exit 1
fi

# Parse `project(libsnobol4 VERSION X.Y.Z ...)`
VERSION_RE='project[[:space:]]*\([[:space:]]*libsnobol4[[:space:]]+VERSION[[:space:]]+([0-9]+)\.([0-9]+)\.([0-9]+)'
if ! grep -Eq "$VERSION_RE" "$CMAKE_LISTS"; then
  echo "❌ Could not find 'project(libsnobol4 VERSION X.Y.Z)' in $CMAKE_LISTS" >&2
  exit 1
fi

MAJOR="$(grep -Eo "$VERSION_RE" "$CMAKE_LISTS" | grep -Eo 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d. -f1)"
MINOR="$(grep -Eo "$VERSION_RE" "$CMAKE_LISTS" | grep -Eo 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d. -f2)"
PATCH="$(grep -Eo "$VERSION_RE" "$CMAKE_LISTS" | grep -Eo 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1 | cut -d. -f3)"

if [ -z "$MAJOR" ] || [ -z "$MINOR" ] || [ -z "$PATCH" ]; then
  echo "❌ Failed to parse version components from $CMAKE_LISTS" >&2
  exit 1
fi

STRING="$MAJOR.$MINOR.$PATCH"

mkdir -p "$(dirname "$OUT")"
sed -e "s/@SNOBOL_VERSION_MAJOR@/$MAJOR/g" \
    -e "s/@SNOBOL_VERSION_MINOR@/$MINOR/g" \
    -e "s/@SNOBOL_VERSION_PATCH@/$PATCH/g" \
    -e "s/@SNOBOL_VERSION_STRING@/$STRING/g" \
    "$TEMPLATE" > "$OUT"

echo "✅ Generated $OUT ($STRING)"
