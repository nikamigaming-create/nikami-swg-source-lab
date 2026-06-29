# Direct3d11.cpp Parity Audit Findings

Date: 2026-06-03

Status note, 2026-06-07: this document is historical renderer-parity evidence from earlier experiments. It must not be treated as the current flat x64 diagnosis by itself. The current working assumption is that flat DX11 x64 on `dx11-x64-flat-baseline` is healthy unless a fresh proof or human test reproduces a problem.

This ledger tracks the line-by-line audit of `Direct3d11.cpp` against the known D3D9 renderer. It is intentionally strict: every item must be tied to source, runtime proof, or pixel proof before it becomes a code fix.

## Current Flat-Frame Proof

Latest paired in-world capture:

- Proof bundle: `D:\code\swg\proofs\og-render-compare-config-autoconnect-world-after-alpha-parity-20260603-114608`
- D3D9 reference: `d3d9-client.png`
- D3D11 candidate: `d3d11-client.png`
- Pixel summary: `pixel-diff\summary.json`

Observed D3D11 differences:

- Hard vertical sky seam and colder blue/purple sky.
- Overbright or flat white buildings compared with the D3D9 warm fogged reference.
- Dark terrain patches and blend seams, especially where bump/normal-mapped terrain layers meet.
- Missing or incorrect lighting/shadow impression even when meshes, smoke, and UI mostly render.
- Mean absolute RGB difference in the latest in-world capture is `54.8967`; thresholded pixel difference is `98.2267%`.

The prior loading-screen alpha-blend fix is proven separately:

- Proof bundle: `D:\code\swg\proofs\og-d3d11-diagnosis-20260603-113851`
- Pixel diff vs D3D9 loading: mean absolute RGB difference `4.3182`.

Latest D3D11 diagnostic-only capture:

- Proof bundle: `D:\code\swg\proofs\d3d11-diagnostic-audit-final-counters`
- Autocapture frame: present `2400`
- Backbuffer: `d3d11-backbuffer.bmp`
- Diagnostic summary:
  - `coordGen=0`
  - `stageReplay=0`
  - `terrainDot3=156205`
  - `fog=14406`, `enabled=7228`
  - `lights=508070`, `nonEmpty=503242`
  - `userVS/PS=5166/0`
  - `gamma=4`
  - `bloom=0`

This means the current Mos Eisley capture does not exercise the D3D9 generated texture-coordinate path, but it heavily exercises terrain DOT3, enabled fog, non-empty light sets, and vertex shader user constants.

Latest terrain DOT3 shader-structure parity capture:

- Proof bundle: `D:\code\swg\proofs\og-render-compare-config-autoconnect-world-terrain-dot3-parity-20260603-122241`
- Change: D3D11 terrain DOT3 shader now follows the original D3D9 terrain shader shape: vertex light, DOT3 light color, transformed terrain light direction, normal-map DOT3, and layer lerp.
- Pixel summary:
  - `meanAbsRgb=50.5996`
  - `thresholdPercent=93.2763`
  - `meanSignedR=20.928`, `meanSignedG=24.8764`, `meanSignedB=27.1623`
- This improves the previous diagnostic pair (`meanAbsRgb=53.545`, `thresholdPercent=94.3071`) but the positive signed RGB delta and contact sheet show D3D11 remains too bright/white and blue-shifted.

## API Table Audit

Generated maps:

- `D:\code\swg\proofs\d3d11-line-audit-20260603\d3d9-glapi-map.csv`
- `D:\code\swg\proofs\d3d11-line-audit-20260603\d3d11-glapi-map.csv`
- `D:\code\swg\proofs\d3d11-line-audit-20260603\glapi-diff.csv`

Release-visible entries that need implementation/proof:

- D3D11 wires `setVertexShaderUserConstants` and `setPixelShaderUserConstants` at `Direct3d11.cpp:3223-3224`, but both functions are empty at `Direct3d11.cpp:4792-4793`.
- D3D9 wires the same callbacks at `Direct3d9.cpp:1063-1064` and forwards them to shader constants at `Direct3d9.cpp:3346-3375`.
- Call sites are not theoretical: water, gradient sky, bloom, and shader sorting call these APIs (`ClientLocalWaterManager.cpp`, `ClientGlobalWaterManager2.cpp`, `GradientSkyAppearance.cpp`, `Bloom.cpp`, `ShaderPrimitiveSorter.cpp`).

Debug-only wrong entries:

- D3D11 `_DEBUG` maps `setTexturesEnabled` to `setPointSpriteEnable` at `Direct3d11.cpp:3174`.
- D3D11 `_DEBUG` maps `showMipmapLevels` to `setPointSpriteEnable` at `Direct3d11.cpp:3175`.
- D3D11 `_DEBUG` maps `getShowMipmapLevels` to `supportsDynamicTextures` at `Direct3d11.cpp:3176`.
- D3D11 `_DEBUG` leaves `setBadVertexBufferVertexShaderCombination` and `getRenderedVerticesPointsLinesTrianglesCalls` null at `Direct3d11.cpp:3177-3178`.
- D3D9 maps those to real debug functions at `Direct3d9.cpp:1002-1006`.

These are not expected to explain the current Release screenshots, but they are real audit failures.

## Texture Coordinate Generation

Finding: D3D11 records shader texture coordinate generation but does not consume it in shader execution.

D3D9 source behavior:

- `Direct3d9_StaticShaderData.cpp:222-240` maps shader stage coordinate generation to D3D texture coordinate generation:
  - `CG_passThru` -> `D3DTSS_TCI_PASSTHRU`
  - `CG_cameraSpaceNormal` -> `D3DTSS_TCI_CAMERASPACENORMAL`
  - `CG_cameraSpacePosition` -> `D3DTSS_TCI_CAMERASPACEPOSITION`
  - `CG_cameraSpaceReflectionVector` -> `D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR`
- `Direct3d9_StaticShaderData.cpp:244-271` handles `CG_scroll1` and `CG_scroll2`.

D3D11 source behavior:

- `Direct3d11.cpp:3579` copies `stage->m_textureCoordinateGeneration` into `stageTexture.coordinateGeneration`.
- `Direct3d11.cpp:4503` and `Direct3d11.cpp:4512` set only `ms_activeTextureCoordinateSet`.
- `Direct3d11.cpp:1453` stores `ms_activeTextureCoordinateSet` into `textureEnabled[3]`.
- The current inline HLSL selects only `input.tex0`, applies scroll and optional texture transform, and never branches on the D3D9 coordinate-generation mode.
- Stage replay samples every stage using the same UV plus scroll, so generated camera-space normal/position/reflection vector stages cannot match D3D9.

Risk: high. This directly matches the skybox seam and material/environment coordinate symptoms.

Required next proof:

- Add bounded diagnostic logging for any pass/stage where `coordinateGeneration != CG_passThru`.
- Capture Mos Eisley/world proof and list exact shader names, pass numbers, coordinate generation modes, texture tags, and stage counts.
- Only then implement the HLSL coordinate-generation path used by those proven shaders.

2026-06-03 proof update: `D:\code\swg\proofs\d3d11-diagnostic-audit-final-counters` reports `coordGen=0` at present `2400`. This path is still an audit gap, but it is not the first fix for the current Mos Eisley terrain/sky capture.

## Fog

Finding: the D3D11 fog formula is an approximation that must be verified numerically against the D3D9 shader path and actual scene constants.

D3D9 source behavior:

- `Direct3d9.cpp:3233-3260` enables fog, sets `D3DRS_FOGCOLOR`, sets `D3DRS_FOGVERTEXMODE` to `D3DFOG_EXP2`, and in VS/PS mode uploads `{0.0f, 0.0f, density, sqr(density)}` to `VSCR_fog`.
- `Direct3d9_StaticShaderData.cpp:932-944` swaps fog color per shader pass between normal, black, and white.

D3D11 source behavior:

- `Direct3d11.cpp:4707-4717` stores enabled, density, and packed fog color.
- `Direct3d11.cpp:1477-1480` computes `fogControl[2] = density * density * 1.4426950408889634f`.
- Default and terrain HLSL use `1.0f - exp2(-fogControl.z * distance * distance)`.
- D3D11 does implement per-pass normal/black/white fog color through `setActiveFogColor`, but it has not been proven numerically against the original shader assembly for the current scene.

Risk: high. The current screenshots show colder sky and under/over-fogged surfaces.

Required next proof:

- Log the fog enable, density, color, and pass fog mode during the paired capture.
- Compare D3D9 shader assembly fog math for the active sky/terrain/wall shaders to D3D11 HLSL before changing constants.

2026-06-03 proof update: `D:\code\swg\proofs\d3d11-diagnostic-audit-final-counters` reports `fog=14406` and `enabled=7228` at the captured frame. Fog is active in the broken scene and should remain in the first-fix set.

## Lighting And Material Constants

Finding: D3D11 collapses D3D9 lighting/material behavior to a small approximation.

D3D9 source behavior:

- `Direct3d9.cpp:3486-3493` forwards lights to `Direct3d9_LightManager::setLights`.
- `Direct3d9_LightManager.cpp:601` and `Direct3d9_LightManager.cpp:624` upload DOT3 pixel light constants.
- `Direct3d9_LightManager.cpp:723-725` uploads full vertex light and extended light data.
- `Direct3d9_LightManager.cpp:768-846` applies DOT3 vertex/pixel light updates using object-local camera position, local light direction, specular power, diffuse/specular colors, alpha fade, bloom, and hemispheric terms.
- `Direct3d9_StaticShaderData.cpp:835-864` uploads material constants and material specular color.
- `Direct3d9_StaticShaderData.cpp:889-922` uploads texture factor and texture scroll constants.
- `Direct3d9_StaticShaderData.cpp:955` sets full-ambient mode per shader.

D3D11 source behavior:

- `Direct3d11.cpp:4806-4859` accumulates ambient and the first parallel light only.
- Default HLSL uses `saturate(lightAmbient.rgb + lightDiffuse.rgb * directionalAmount)`.
- Terrain DOT3 HLSL uses a reconstructed normal influence and the same simplified ambient/directional model.
- D3D11 material handling currently applies diffuse material color, texture factor, and scroll, but does not reproduce the D3D9 material/specular/full light constant layout.

Risk: high. This matches missing/incorrect shadows, flat material response, and terrain DOT3 seams.

Required next proof:

- Log active shader names where D3D9 would use DOT3/light constants.
- Compare original shader constant registers (`VSCR_*`, `PSCR_*`) with D3D11 constant-buffer fields for those shaders.
- Fix one shader family at a time, starting with Mos Eisley terrain DOT3 because that is the visible failing surface.

2026-06-03 proof update: `D:\code\swg\proofs\d3d11-diagnostic-audit-final-counters` reports `terrainDot3=156205`, `lights=508070`, `nonEmpty=503242`, and `userVS/PS=5166/0` at the captured frame. This makes terrain DOT3 plus D3D9 light/material/user vertex constants the current first fix target.

2026-06-03 terrain shader update: D3D11 terrain DOT3 no longer uses sampled normal `.z` as a lighting proxy. It now mirrors the D3D9 shader flow using transformed terrain light direction and DOT3 intensity. Remaining mismatch is the source light/material constants feeding that shader, not the layer sampler order.

2026-06-03 rejected light-slot experiment:

- Tested D3D11 terrain light constants modeled after D3D9 `Direct3d9_LightManager` selection: one `parallelSpecular[0]` slot for DOT3 and two `parallel[0..1]` diffuse slots for terrain vertex light.
- First smoke run failed shader creation because the terrain DOT3 cbuffer did not declare the new terrain light fields:
  - Proof bundle: `D:\code\swg\proofs\d3d11-terrain-light-slots-shaderlog`
  - Logged error: `undeclared identifier 'terrainLightDirection0'`
- After fixing the terrain cbuffer, D3D11 rendered but pixel parity regressed:
  - Proof bundle: `D:\code\swg\proofs\og-render-compare-config-autoconnect-world-terrain-light-slots-20260603-124053`
  - Pixel summary: `meanAbsRgb=54.1564`, `thresholdPercent=99.216`
  - Prior best terrain DOT3 proof remained `meanAbsRgb=50.5996`, `thresholdPercent=93.2763`
- A follow-up sign-convention smoke test using D3D9-style negative light directions still showed the same harsh dark terrain split:
  - Proof bundle: `D:\code\swg\proofs\d3d11-terrain-light-slots-d3d9-direction-smoke`
- Conclusion: D3D9 light-slot selection is still source-true and must remain in the audit, but that change alone is not a proven renderer fix. It should not be committed as behavior until the remaining terrain normal/material/fog constants are measured at draw level.

## No-Op Stub Sweep

The current D3D11 file contains explicit no-ops. Each must be classified as harmless, diagnostic-only, or parity-blocking.

Likely parity-blocking or scene-visible:

- `Direct3d11.cpp:3983` `setBrightnessContrastGamma(float, float, float) {}`. D3D9 has a real gamma path; loading and in-world color parity must prove whether this is used in the current config.
- `Direct3d11.cpp:4398` `setBadVertexShaderStaticShader(const StaticShader *) {}`. D3D9 has a debug/error path. Mostly diagnostic, but it can hide shader selection failures during parity work.
- `Direct3d11.cpp:4792` `setVertexShaderUserConstants(...) {}`. Proven call sites exist in water and sky code.
- `Direct3d11.cpp:4793` `setPixelShaderUserConstants(...) {}`. Proven call sites exist in bloom and shader sorting code.
- `Direct3d11.cpp:5406` `setBloomEnabled(bool) {}`. D3D9 plumbs bloom through alpha/light constants. This can affect final scene color if bloom is enabled.

2026-06-03 proof update: `D:\code\swg\proofs\d3d11-diagnostic-audit-final-counters` reports `gamma=4`, `bloom=0`, and `userVS/PS=5166/0` at the captured frame. Bloom is not active in this capture; gamma and vertex shader user constants are active.

Probably not current flat-scene visual blockers, but must stay tracked:

- `Direct3d11.cpp:3965` `displayModeChanged() {}`.
- `Direct3d11.cpp:3968` `getOtherAdapterRects(...) {}`.
- `Direct3d11.cpp:3970` `flushResources(bool) {}`.
- `Direct3d11.cpp:3971` `isGdiVisible() { return false; }`.
- `Direct3d11.cpp:3972` `wasDeviceReset() { return false; }`.
- `Direct3d11.cpp:3973-3976` device lost/restored callback registration is empty.
- `Direct3d11.cpp:3979` `supportsHardwareMouseCursor() { return false; }`.
- `Direct3d11.cpp:4021-4026` point size/scale/sprite functions are empty.
- `Direct3d11.cpp:4653-4654` hardware cursor calls return false.
- `Direct3d11.cpp:5404` `optimizeIndexBuffer(WORD *, int) {}`.
- `Direct3d11.cpp:5407-5409` PIX marker/event calls are empty.
- `Direct3d11.cpp:5414-5415` antialias support is false/no-op.
- `Direct3d11.cpp:5418-5421` video buffer functions are false/no-op.

Required next proof:

- Add counters for scene-visible stubs before implementing them.
- If a stub counter is zero in the failing Mos Eisley scene, defer it.
- If a stub counter is nonzero, map it to the D3D9 implementation and either port it or document why it cannot affect the captured pixels.

## Immediate Fix Order

1. Port or emulate the D3D9 terrain/DOT3 light and material constants that are proven active in the captured frame.
2. Implement the active vertex shader user constant path or prove which active shaders consume each register.
3. Verify D3D11 fog math against the active D3D9 shader assembly and the captured fog-enabled calls.
4. Continue terrain DOT3 parity against original shader assembly if light/material constants do not remove the remaining over-bright terrain/wall mismatch.
5. Keep coordinate-generation implementation deferred for this scene because `coordGen=0` in the present-2400 proof, but keep it in the audit backlog.
6. Re-capture the same scene and commit only if the pixel proof improves or explains a necessary parity step.

## Non-Negotiables

- Keep all edits inside renderer DLL/source and diagnostics unless explicitly approved.
- Do not change gameplay, networking, UI behavior, object logic, or client heuristics.
- Each fix must have a before/after proof bundle and a clean commit.
- No unproven numeric tuning. Every constant must come from D3D9 source, shader assembly, modern renderer source when relevant, or measured diagnostic output.
