#!/bin/bash
# Configure + build every Palette Seed3 target. Needs an Arm GNU toolchain:
#   ARM_GCC=/path/to/arm-gnu-toolchain/bin ./build.sh      (default: first arm-none-eabi-gcc on PATH,
#   else ~/local_deps/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi/bin)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -z "${ARM_GCC:-}" ]; then
  if command -v arm-none-eabi-gcc >/dev/null 2>&1; then ARM_GCC=$(dirname "$(command -v arm-none-eabi-gcc)")
  else ARM_GCC=$HOME/local_deps/arm-gnu-toolchain-15.3.rel1-darwin-arm64-arm-none-eabi/bin; fi
fi
python3 "$HERE/tools/gen_engine_table.py" --check
python3 "$HERE/tools/make_lds.py" >/dev/null
cmake -S "$HERE" -B "$HERE/build" -G Ninja -DCMAKE_C_COMPILER="$ARM_GCC/arm-none-eabi-gcc" ${@:-}
cmake --build "$HERE/build"
echo; echo "== images =="; ls -la "$HERE"/build/*.bin
