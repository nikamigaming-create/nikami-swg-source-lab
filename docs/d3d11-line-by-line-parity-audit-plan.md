# D3D11 Line-By-Line Renderer Parity Audit Plan

Status note, 2026-06-07: this is a historical audit loop for renderer regressions. Do not use it as the default active plan now that flat DX11 x64 is considered healthy. Re-enter this loop only after a fresh proof reproduces a flat-rendering regression.

## Purpose

Bring the original SWG client D3D11 renderer to flat-screen D3D9 visual parity before VR.

This is not a rewrite of client behavior. The audit is renderer-only:

- `src/engine/client/application/Direct3d11/src/win32/Direct3d11.cpp`
- D3D11 build/project files only when required.
- Diagnostic scripts only when required.

No gameplay, networking, object, UI, menu, targeting, or data behavior changes.

## Proof Standard

Every parity change must have evidence at four levels:

1. Source evidence:
   - D3D9 line(s) that define the expected behavior.
   - D3D11 line(s) that currently implement, approximate, stub, or miss it.
2. State evidence:
   - Render state, sampler state, texture format, shader constant, or draw stats proving the runtime path is hit.
3. Pixel evidence:
   - Paired D3D9/D3D11 screenshots using the same scene, resolution, timeout, and framing.
   - Pixel summary JSON and side-by-side/contact sheet.
4. Commit evidence:
   - One clean commit per proven improvement.
   - Commit message names the exact renderer behavior fixed.

Rejected renderer experiments are also evidence. If a source-true change makes pixels worse, back it out, record the proof bundle and pixel numbers, and keep only diagnostics or docs that improve the next pass.

## Magnifying Glass Loop

Use this loop for every suspect line in `Direct3d11.cpp`:

1. Source lock:
   - Identify the exact D3D9 source lines, shader source, or shader assembly that define the behavior.
   - Identify the exact D3D11 line(s) that claim to implement it.
2. Runtime lock:
   - Add bounded logging only if the current proof does not show whether the path is hit.
   - Capture counters and draw/state values in a proof bundle.
3. Pixel lock:
   - Run the paired D3D9/D3D11 capture at the same resolution, scene driver, timeout, and autocapture present.
   - Compare `meanAbsRgb`, `thresholdPercent`, signed channel deltas, and the contact sheet.
4. Decision:
   - Keep and commit only if the result improves pixels or captures necessary diagnostics.
   - Back out behavior changes that regress pixels, and document the rejected path.
5. Next line:
   - Move to the next measured mismatch. Do not tune constants without D3D9 source, extracted shader code, modern renderer source, or captured state proving the value.

## Phase 1: API Surface Audit

Compare every `ms_glApi` hook in D3D9 and D3D11.

For each hook, record:

- API name.
- D3D9 target function and line.
- D3D11 target function and line.
- Whether D3D11 is complete, partial, stubbed, wrong, or intentionally unsupported.
- Whether the hook can affect current flat-client parity.

Immediate high-risk hooks already found:

- `setBrightnessContrastGamma`: D3D11 no-op.
- `setVertexShaderUserConstants`: D3D11 no-op.
- `setPixelShaderUserConstants`: D3D11 no-op.
- `setBadVertexShaderStaticShader`: D3D11 no-op.
- `setMouseCursor` / `showMouseCursor`: D3D11 false.
- `optimizeIndexBuffer`: D3D11 no-op.
- `setBloomEnabled`: D3D11 no-op.
- `createVideoBuffers` / `fillVideoBuffers` / `getVideoBufferData`: D3D11 false/no-op.

Do not fix all stubs blindly. First classify which ones are visible in the current world captures.

## Phase 2: State-By-State D3D9 Parity

Audit D3D11 state construction against D3D9:

- Blend state:
  - Source/destination blend.
  - Blend operation.
  - Separate alpha behavior.
  - Alpha fade opacity.
- Depth/stencil:
  - Depth enable/write/function.
  - Stencil enable/function/masks/ops/reference.
  - Two-sided stencil.
- Rasterizer:
  - Fill mode.
  - Cull mode.
  - Scissor.
  - Depth clip.
- Color write mask.
- Fog:
  - Fog enable.
  - Fog color.
  - Fog density.
  - D3D9 VS/PS constants.
  - D3D11 pixel shader equation.
- Lighting:
  - Ambient.
  - Directional diffuse.
  - Material color sources.
  - Full ambient.
  - Normal transforms.
- Samplers:
  - Address U/V/W.
  - Mip/min/mag filters.
  - Max anisotropy.
  - sRGB texture flag.
- Texture transforms:
  - Stage index.
  - Matrix.
  - Projection.
  - Scroll.

Each state item must map to an exact D3D9 source line and an exact D3D11 source line.

## Phase 3: Shader And Texture Audit

Audit every D3D11 shader path:

- Default fallback shader.
- Stage combiner replay.
- Terrain DOT3 fast path.
- Screen-space/transformed vertex path.
- Alpha-only texture path.
- BGRA swizzle path.
- Fog application.
- Lighting application.

Known high-risk shader issues:

- D3D11 fallback shader receives `textureEnabled[3]` as active texture coordinate set, but does not use D3D9 camera-space generated texture coordinates.
- D3D11 approximates fixed-function lighting instead of fully matching D3D9 material source and light manager behavior.
- D3D11 fog equation may not match the shader path modern uses for equivalent terrain/mesh rendering.
- D3D11 terrain DOT3 is a reconstructed path, not byte-identical to original shader source.
- D3D11 stage combiner replay is partial and must be validated stage-by-stage.

Texture audit:

- Native SWG texture format.
- Runtime D3D11 format.
- Upload byte order.
- Shader sampling swizzle.
- Alpha interpretation.
- DXT block format.
- sRGB/linear assumption.
- Mip generation or lack of it.

## Phase 4: Draw-Level Diagnostics

Add temporary diagnostics only when needed, then keep or remove based on usefulness.

For suspect passes log:

- Shader template name.
- Pass number.
- Texture tag/name.
- Texture format.
- Texture coordinate set.
- Texture coordinate generation mode.
- Material color.
- Texture factor.
- Fog mode/color/density.
- Blend state.
- Depth/write state.
- Stage count and stage ops.
- Vertex format flags and transformed status.

The first draw-level target is the current in-world capture:

`D:\code\swg\proofs\og-render-compare-config-autoconnect-world-after-alpha-parity-20260603-114608`

Visible problems to diagnose:

- Skybox vertical seam and wrong sky color.
- Terrain layer seam blending.
- Missing/incorrect shadows or shading.
- Overbright walls.
- Hard dark terrain regions.
- Minimap/screen-space transform mismatch remains visible in some captures.

## Phase 5: Pixel-By-Pixel Loop

Use paired scenes only.

Required captures:

1. Early loading screen:
   - Confirms screen-space alpha and UI texture blending.
2. Standing Mos Eisley view:
   - Confirms sky, walls, terrain, HUD, and character.
3. Ground-looking terrain view:
   - Confirms terrain DOT3 layer seams and shadowing.
4. Sky-looking view:
   - Confirms skybox seam, fog, clouds, and color ramp.
5. UI-heavy view:
   - Confirms screen-space transforms, alpha, fonts, and minimap.

For every fix:

```powershell
& 'D:\code\swg\scripts\dev\compare-og-client-renderers.ps1' `
  -SceneName '<scene-name>' `
  -SceneDriver ConfigAutoConnect `
  -TimeoutSeconds <seconds> `
  -ScreenWidth 1280 `
  -ScreenHeight 960 `
  -D3D11AutocapturePresent <present>
```

Then inspect:

- `d3d9-client.png`
- `d3d11-client.png`
- `pixel-diff\side-by-side.png`
- `pixel-diff\contact-sheet.png`
- `pixel-diff\summary.json`
- D3D11 `dbwin.txt`

## Phase 6: Fix Order

Fix in this order to avoid compounding false positives:

1. API hooks and renderer states that affect all draws.
2. Texture format/swizzle/sRGB/alpha correctness.
3. Fog equation and fog color mode.
4. Texture coordinate set and generated-coordinate behavior.
5. Fixed-function stage combiner completeness.
6. Lighting/material source parity.
7. Terrain DOT3 layer and normal-map parity.
8. Remaining special effects: sky/clouds/water/shadows/particles.
9. UI/screen-space transform edge cases.

## Current Proven Fixes

`ac9fe0ea Match D3D9 alpha blend factors in D3D11`

Proof:

- D3D11 loading screen changed from washed cyan to D3D9-like deep blue.
- Loading mean absolute RGB diff: `4.3182`.
- Pixels over threshold 10: `11.5951%`.

## Current Next Audit Target

Start with `Direct3d11.cpp` texture coordinate generation and fog/lighting:

- D3D9 has explicit texture coordinate generation:
  - pass-through
  - camera-space normal
  - camera-space position
  - camera-space reflection vector
  - scroll1/scroll2
- D3D11 currently records coordinate set information but does not fully reproduce D3D9 generated coordinate behavior in the fallback shader.

This is likely relevant to:

- Skybox seam.
- Environment texture mismatch.
- Some material/shading mismatches.

Fog is also high priority because D3D11 world capture is under-fogged compared with D3D9:

- D3D9 Mos Eisley reference has warm beige/pink fogged sky and walls.
- D3D11 candidate has colder/darker sky and overbright walls.

Do not commit a fog or texcoord change until paired world pixel proof shows improvement.
