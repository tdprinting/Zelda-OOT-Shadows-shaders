#!/usr/bin/env bash
# Apply the Realtime Lighting patches to a Ship of Harkinian checkout.
#
#   scripts/apply.sh /path/to/Shipwright
#
# The patches are made against Shipwright 31c0d855 and the libultraship commit it pins (62e973a). They are applied
# to the working tree only (nothing is committed), so you can review with `git diff` / `git -C libultraship diff`.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sw="${1:?usage: apply.sh /path/to/Shipwright}"

[ -d "$sw/soh" ] || { echo "error: $sw doesn't look like a Shipwright checkout" >&2; exit 1; }

# libultraship is a submodule; make sure it is checked out at the pinned commit.
git -C "$sw" submodule update --init libultraship

echo "Applying libultraship patch..."
git -C "$sw/libultraship" apply --index --whitespace=nowarn "$here"/patches/libultraship/*.patch

echo "Applying Shipwright patch..."
git -C "$sw" apply --whitespace=nowarn "$here"/patches/shipwright/*.patch

echo "Done. Build as usual; for Android/GLES builds configure libultraship with -DUSE_OPENGLES=ON."
echo "Enable in game: Settings > Graphics > Realtime Lighting (OpenGL / OpenGL ES renderer only)."
