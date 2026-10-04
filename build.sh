#!/usr/bin/env bash
# Compila ascii-maker en ./build y deja el binario listo.
#
#   ./build.sh            Release (por defecto)
#   ./build.sh Debug      con símbolos de depuración
#   ./build.sh Release -j # pasarle flags a cmake --build

set -euo pipefail

BUILD_TYPE="${1:-Release}"
shift || true

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$DIR/build"

# -j con el número de núcleos, por defecto
JOBS="$(nproc 2>/dev/null || echo 4)"

cmake -S "$DIR" -B "$BUILD" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD" -j "$JOBS" "$@"

echo
echo "Listo:  $BUILD/ascii-maker"
echo "Ejecuta: $BUILD/ascii-maker"
