#!/usr/bin/env python3
"""Generate stubs.cpp: abort()-ing definitions for every GfxRenderingAPIOGL virtual except ApplyScenePostFx, so the
real gfx_opengl_scenefx.cpp can be linked into a tiny headless test without the rest of the renderer."""
import re, sys

header, out_path = sys.argv[1], sys.argv[2]
h = open(header).read()
body = h[h.index('class GfxRenderingAPIOGL'):]
body = body[:body.index('  private:')]
decls = re.findall(r'^\s{4}([\w:<>,\*\s]+?)\s+(\w+)\(([^;{]*?)\)\s*override;', body, re.S | re.M)
lines = ['#include "fast/backends/gfx_opengl.h"', 'namespace Fast {']
for ret, name, args in decls:
    if name == 'ApplyScenePostFx':
        continue
    lines.append(f'{ret.strip()} GfxRenderingAPIOGL::{name}({" ".join(args.split())}) {{ abort(); }}')
lines.append('}')
open(out_path, 'w').write('\n'.join(lines) + '\n')
