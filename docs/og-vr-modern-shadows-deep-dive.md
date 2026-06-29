# OG VR Modern Shadows Deep Dive

Date: June 27, 2026. The user mentioned July 2026, but that is still in the future from this workspace date, so this plan uses current 2026-era real-time practice without claiming future-specific sources.

## Current Pixel Path

The D3D11 renderer uses inline HLSL in `Direct3d11.cpp`. The normal world draw path binds the synthesized default vertex/pixel shaders, updates `TransformConstants`, binds texture stages, and emits the final lit/fogged pixel. The default pixel shader has material, texture, fog, and directional-light inputs, but it has no shadow-map texture, no light-space matrix, and no depth comparison. That means no modern shadow byte reaches the pixel today.

The only historical shadow path in the ground scene is `ShadowVolume::renderShadowAlpha()`. That path is now disabled for VR unless `[Direct3d11] legacyStencilShadowsInVr=true` is explicitly set. The OG VR runtime profile keeps it false because the desired result is not flat/stencil/blob-era shadows.

## Why Shadows Were Not Working

- `ConfigDirect3d11` exposed modern shadow tuning, but float keys were hardcoded instead of read from cfg. Bias, distance, filter radius, split lambda, fade, and contact distance were not actually tuneable.
- `Direct3d11.cpp` had no modern shadow-map D3D resources. There was no typeless depth texture, DSV, SRV, or comparison sampler for the pixel shader to consume.
- The shader constants do not yet include cascade split distances, light view-projection matrices, or a light-space texture transform.
- The scene is not yet re-rendered from the light into a shadow depth buffer, so even with resources allocated, there is no caster depth data.
- The VR eye loop still called the legacy alpha shadow renderer before this pass. That was at odds with the desired modern-only VR shadow path.

## Chosen Direction

Use a cheap modern hybrid:

1. Cascaded directional shadow map for sun/key-light shadows.
2. Hardware PCF as the baseline filter.
3. PCSS-style variable-radius filtering for quality levels that can afford it.
4. Optional screen/contact shadow term near the receiver for character/object grounding.
5. VR single-pass shadow generation shared by both eyes where camera/light bounds allow it.

This matches the practical D3D11 path: render caster depth from the light, then sample/compare that depth in the pixel shader during the main scene render.

## First Patch

Implemented now:

- `ConfigDirect3d11` owns modern shadow defaults. The shared config float getter is not exported to the D3D11 DLL in this tree, so float tuning remains a follow-up foundation/export fix instead of being pulled into this renderer patch.
- D3D11 allocates modern shadow resources when `modernShadows=true`:
  - `DXGI_FORMAT_R32_TYPELESS` texture array
  - `DXGI_FORMAT_D32_FLOAT` DSV
  - `DXGI_FORMAT_R32_FLOAT` SRV
  - comparison sampler for PCF-style shadow tests
- Startup diagnostics now log map size, cascade count, PCF taps, filter radius, distance, contact-shadow settings, stabilization, and VR single-pass intent.
- VR legacy alpha/stencil shadows are gated by `[Direct3d11] legacyStencilShadowsInVr=false`.

## Next Rendering Work

The next real shadow milestone is the light pass:

1. Add shadow constants for light view-projection matrices, cascade distances, texel size, bias, normal bias, and filter controls.
2. Export or wrap float config reads for the D3D11 DLL so runtime bias/filter/distance tuning can be live.
3. Add a depth-only shadow shader path with a null pixel shader or minimal alpha-test pixel shader for alpha-cutout casters.
4. Render selected terrain/characters/objects into each cascade before the VR eye color pass.
5. Bind the shadow SRV and comparison sampler to the main pixel shader.
6. Apply PCF first, then add PCSS blocker search for high quality.
7. Add proof captures: shadow map occupancy, cascade selection visualization, and before/after eye screenshots.

## References

- Microsoft: Cascaded Shadow Maps, including D3D11 cascade rendering and shader sampling flow: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps
- Microsoft: Common Techniques to Improve Shadow Depth Maps, artifact/bias/stabilization background: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps
- NVIDIA: PCSS Integration whitepaper, including R32 typeless/D32 DSV/R32 SRV resource pattern and scalable soft-shadow filtering: https://developer.download.nvidia.com/assets/gamedev/docs/PCSS_Integration.pdf
- SIGGRAPH Advances in Real-Time Rendering 2025 course index, current real-time lighting/shadow practice context: https://advances.realtimerendering.com/s2025/
