# Realtime lighting for Ship of Harkinian

Adds realtime **sun/moon shadows**, **bump mapping** and optional **specular highlights** to Ship of Harkinian
(Shipwright), built to run on **OpenGL ES 3.0** so it can work on Android handhelds such as the Odin 2 Portal.

> **Status: renderer verified in isolation, not yet seen in the actual game.**
> See [What has and hasn't been tested](#what-has-and-hasnt-been-tested) before relying on it.

## Using it

```sh
git clone https://github.com/HarbourMasters/Shipwright && cd Shipwright   # patches target 31c0d855
/path/to/this/repo/scripts/apply.sh .                                      # applies both patches, commits nothing
```

Build Shipwright as usual (OpenGL renderer). In game: **Settings → Graphics → Realtime Lighting**.
Settings are stored as CVars under `gScenePostFx.*`.

| Setting | CVar | Default |
|---|---|---|
| Enable | `gScenePostFx.Enabled` | off |
| Shadow Strength | `gScenePostFx.ShadowStrength` | 60% |
| Shadow Distance (game units) | `gScenePostFx.ShadowDistance` | 200 |
| Shadow Quality (samples/pixel) | `gScenePostFx.ShadowSteps` | 16 |
| Bump Mapping | `gScenePostFx.BumpStrength` | 60% |
| Specular Highlights | `gScenePostFx.SpecularStrength` | 0% |
| Flip Light Direction (troubleshooting) | `gScenePostFx.FlipLight` | off |

## How it works

Fast3D (the N64 renderer) does the RSP vertex transform on the CPU and throws away world positions and normals
before anything reaches the GPU. A classic shadow-map pass would therefore mean running every display list a second
time, which is a poor trade on a handheld. Instead this is **one fullscreen pass over the finished world**:

1. The game emits a new display-list command, `gSPScenePostFx` (`G_SCENEPOSTFX`, 0x4b), at the end of the opaque
   world and before the HUD. It carries the sun direction and settings. OoT multiplies the view matrix into the
   projection, so the light direction the game already uses is in the same world space the renderer sees.
2. The interpreter remembers the latest perspective view-projection matrix. The GL backend snapshots colour + depth
   into a scratch target, inverts that matrix, and for every pixel rebuilds the world-space position from depth.
3. From those positions it derives:
   * **Shadows** — a short ray march toward the light through the depth buffer.
   * **Bump mapping** — the surface normal is perturbed by the luminance gradient of the rendered texels.
   * **Specular** — Blinn-Phong from the perturbed normal.

It sits on top of every existing N64 shader permutation, adds no geometry passes, and the backend interface has a
default no-op so the Direct3D 11 and Metal backends are unaffected.

Changes:
* `patches/libultraship/` — opcode, interpreter hook, `GfxRenderingAPI::ApplyScenePostFx`, and the GL/GLES
  implementation (`src/fast/backends/gfx_opengl_scenefx.cpp`).
* `patches/shipwright/` — emits the command from `Play_Draw` (using the brighter of OoT's two directional lights, so
  it follows day/night) and adds the menu section.

## Android / Odin 2 Portal

The shader is GLSL ES 3.00 with explicit `highp`, uses only ES 3.0 features (`texelFetch`, depth-stencil texture
sampling, depth+colour `glBlitFramebuffer`), and is exercised on Mesa's GLES 3.2 in the test below. libultraship
already has a GLES path (`-DUSE_OPENGLES=ON`, which also applies its existing `z *= 0.3` clip-space adjustment — the
pass accounts for it).

Two caveats:
* **Shipwright has no official Android project.** This change makes the *renderer* Android-ready; getting an APK
  needs an Android port build (SDL2 + `USE_OPENGLES`), which is separate work.
* **Not measured on hardware.** The Odin 2 Portal's Adreno 740 should handle it, but I couldn't run it there. Cost
  is one colour+depth copy plus one fullscreen shader. If it is too heavy, lower *Shadow Quality* (try 8) or the
  internal resolution, or set Shadow Strength to 0 to keep only bump/specular (much cheaper).

## What has and hasn't been tested

Verified:
* The fragment/vertex shaders validate with `glslangValidator` as GLSL ES 3.00, 130 and 410 core.
* All changed C++ compiles (libultraship `interpreter.cpp`, the GL backend, the new file) for GLES and desktop GL.
* `tests/scene_postfx/run.sh` runs the **real** `ApplyScenePostFx` headless (Mesa llvmpipe, GLES 3.2 and desktop GL
  4.5) on a synthetic scene drawn through a Fast3D-style row-vector pipeline, covering a normal viewport, a flipped-Y
  framebuffer, a widescreen aspect squeeze and an offset letterbox viewport. It compares the shadow against an
  analytic ray/box test: **100% of true shadow pixels are shadowed, ~80% precision** (I haven't characterised where the
  extra ~20% of darkened pixels fall), and it **fails with 0% recall when the light direction is flipped**, so it can
  tell right from wrong.
* Both patches apply cleanly to fresh clones of the pinned upstream commits.

**Not** verified:
* The full Shipwright build and the game itself were never built or run (no ROM or GPU here). The `z_play.c` and menu
  changes are unbuilt.
* **Visual quality and tuning** of bump mapping and shadow defaults in real scenes. Bump is a heuristic.
* **OoT's light-vector sign convention** is assumed (points toward the light). If shadows fall toward the sun, tick
  *Flip Light Direction*.
* Performance on Android hardware.

## Limitations (by design)

* Screen-space: only on-screen casters cast shadows, and shadows can vary at screen edges / thin geometry.
* Opaque world only; translucent surfaces (water, particles) are drawn after and are not relit.
* Bump mapping is *derived* from baked N64 texture brightness, not real normal maps — those would need asset support.
* One light (the brighter of sun/moon). Point lights (torches) are not shadow-casting.
* Skipped while MSAA is on, and for non-OpenGL backends.

## Testing

```sh
LUS=/path/to/patched/libultraship DEPS=/path/to/deps tests/scene_postfx/run.sh
```
`DEPS` holds `thread-pool`, `prism` and `imgui` checkouts (versions pinned in libultraship's
`cmake/dependencies/common.cmake`). Needs `g++`, EGL/GLES/GL, SDL2, spdlog, fmt, and Mesa.
