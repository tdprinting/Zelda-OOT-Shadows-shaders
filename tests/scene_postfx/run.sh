#!/usr/bin/env bash
# Headless test for the scene post-process, using Mesa software rendering (no GPU, no game, no ROM needed).
#
#   LUS=/path/to/libultraship DEPS=/path/to/deps tests/scene_postfx/run.sh
#
# DEPS must contain checkouts named: thread-pool, prism, imgui (the same versions libultraship fetches).
# Needs: g++ (C++20), libEGL, libGLESv2, libGL, libSDL2-dev, libspdlog-dev, libfmt-dev, mesa (llvmpipe).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${LUS:?set LUS to the patched libultraship checkout}"
: "${DEPS:?set DEPS to the directory holding thread-pool, prism and imgui}"
out="${OUT:-$(mktemp -d)}"

INC="-I$LUS/include -I$LUS/src -I$DEPS/thread-pool/include -I$DEPS/prism/src -I$DEPS/imgui -I$DEPS/imgui/backends -I/usr/include/SDL2"
python3 "$here/gen_stubs.py" "$LUS/include/fast/backends/gfx_opengl.h" "$out/stubs.cpp"

build() { # name, defines, libs
    g++ -std=c++20 -O1 -DENABLE_OPENGL $2 $INC -include imgui.h "$here/main.cpp" "$out/stubs.cpp" \
        "$LUS/src/fast/backends/gfx_opengl_scenefx.cpp" -o "$out/$1" $3 -lEGL -lspdlog -lfmt
}
build fx_gles "-DUSE_OPENGLES" "-lGLESv2"
build fx_gl "" "-lGL"

export LIBGL_ALWAYS_SOFTWARE=1
echo "##### OpenGL ES 3"; "$out/fx_gles"
echo "##### Desktop OpenGL"; "$out/fx_gl"
# Sanity check that the test can actually fail: with the light direction flipped every case must fail.
echo "##### negative control (must report failures)"
if FX_NEG=1 "$out/fx_gles" >/dev/null; then echo "ERROR: negative control passed, test is not discriminating"; exit 1; else echo "ok: negative control fails as expected"; fi
