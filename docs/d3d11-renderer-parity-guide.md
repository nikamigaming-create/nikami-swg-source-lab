# SWG D3D11 Renderer Parity Guide

Status note, 2026-06-07: this guide records the earlier flat-renderer parity process. It remains useful for regression triage, but it is not the active diagnosis. The current working assumption is that flat DX11 x64 on `dx11-x64-flat-baseline` is healthy unless a fresh proof or human test says otherwise. Active work has moved to gated VR HUD/controller proof steps.

## Goal

Build a clean Direct3D 11 renderer backend for the original SWG client while keeping the original client behavior intact. The target is flat-screen D3D9 visual parity first, then VR.

Do not change gameplay, networking, object logic, UI logic, targeting, menus, or client data. Work only in the renderer DLL layer and diagnostic scripts around it.

## Current State

The D3D11 raster DLL lives here:

`src/engine/client/application/Direct3d11/src/win32/Direct3d11.cpp`

The built DLL is:

`src/compile/win32/Direct3d11/Release/gl05_r.dll`

The latest proven renderer fix is:

`ac9fe0ea Match D3D9 alpha blend factors in D3D11`

That fix changes D3D11 blend state creation so alpha uses the same source blend, destination blend, and blend operation as color. This matches D3D9 when `D3DRS_SEPARATEALPHABLENDENABLE` is false.

Before that fix, D3D11 hardcoded alpha blending to `ONE/ZERO/ADD`. Loading screens became bright cyan and washed out because stacked translucent loading-screen panels accumulated differently than D3D9.

## Known Good Proof

Loading-screen D3D9 reference:

`D:\code\swg\proofs\og-render-compare-config-autoconnect-loading-early-6s-20260603-112905\d3d9-client.png`

Fixed D3D11 loading capture:

`D:\code\swg\proofs\og-d3d11-diagnosis-20260603-113851\client.png`

Pixel proof output:

`D:\code\swg\proofs\og-d3d11-diagnosis-20260603-113851\pixel-diff-vs-d3d9-loading`

Post-fix loading comparison:

- Mean absolute RGB diff: `4.3182`
- Pixels over threshold 10: `11.5951%`
- Result: D3D11 loading screen is visually close to D3D9 and no longer washed cyan.

## Minimal Repro

Build D3D11:

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' `
  'D:\code\swg\og-client-tools\src\engine\client\application\Direct3d11\build\win32\Direct3d11.vcxproj' `
  /p:Configuration=Release /p:Platform=Win32 /m
```

Capture early D3D11 loading:

```powershell
& 'D:\code\swg\scripts\dev\diagnose-og-client-d3d11.ps1' `
  -Backend D3D11 `
  -SceneName 'config-autoconnect-loading-alpha-parity-6s' `
  -SceneDriver ConfigAutoConnect `
  -TimeoutSeconds 6 `
  -ScreenWidth 1280 `
  -ScreenHeight 960 `
  -D3D11AutocapturePresent 30
```

Generate D3D9 vs D3D11 loading diff:

```powershell
& 'D:\code\swg\scripts\dev\Compare-RenderImages.ps1' `
  -ReferenceImage 'D:\code\swg\proofs\og-render-compare-config-autoconnect-loading-early-6s-20260603-112905\d3d9-client.png' `
  -CandidateImage 'D:\code\swg\proofs\og-d3d11-diagnosis-20260603-113851\client.png' `
  -OutputDirectory 'D:\code\swg\proofs\og-d3d11-diagnosis-20260603-113851\pixel-diff-vs-d3d9-loading'
```

Run a paired D3D9/D3D11 world capture:

```powershell
& 'D:\code\swg\scripts\dev\compare-og-client-renderers.ps1' `
  -SceneName 'config-autoconnect-world-parity' `
  -SceneDriver ConfigAutoConnect `
  -TimeoutSeconds 70 `
  -ScreenWidth 1280 `
  -ScreenHeight 960 `
  -D3D11AutocapturePresent 2400
```

## Diagnostic Rules

Always compare D3D9 and D3D11 with the same scene driver, resolution, timeout, and framing. Do not rely on memory or live headset impressions when a paired screenshot can prove it.

Use these files first:

- `d3d9-client.png`
- `d3d11-client.png`
- `pixel-diff\side-by-side.png`
- `pixel-diff\contact-sheet.png`
- `pixel-diff\summary.json`
- D3D11 `dbwin.txt`

Important D3D11 stats in `dbwin.txt`:

- `stageReplay`: fixed-function texture-stage replay count.
- `terrainDot3`: terrain DOT3 shader fast-path count.
- `transformed`: screen-space/UI/loading draw count.
- `autocapture ok present=N`: confirms early backbuffer capture.

## What Is Solved

Loading screen washout was proven renderer-side, not config-side.

Facts:

- Same original client and same loading asset.
- D3D9 loading art was deep blue and high contrast.
- D3D11 loading art was washed cyan before the fix.
- D3D11 loading art became close to D3D9 after matching D3D9 alpha blend factors.

The fix is renderer-only and mirrors modern-client behavior for parsed client-tools blend states:

```cpp
desc.RenderTarget[0].SrcBlendAlpha = ms_sourceBlend;
desc.RenderTarget[0].DestBlendAlpha = ms_destinationBlend;
desc.RenderTarget[0].BlendOpAlpha = ms_blendOp;
```

## Current Known Issues

In-world D3D11 still has visible parity problems:

- Skybox/sky gradient has weird color banding.
- City walls and terrain still differ from D3D9.
- Terrain DOT3 is improved but not complete.
- Some hard dark regions remain on terrain or ground materials.
- Fog, volume lighting, and sky treatment are not yet D3D9-identical.

The next user-observed issue is skybox banding after the loading fix.

## Next Investigation

Start with sky/fog/material state, not gameplay.

Likely files:

- `src/engine/client/application/Direct3d11/src/win32/Direct3d11.cpp`
- `D:\code\swg\modern-client\src\render\RendererShaders.cpp`
- `D:\code\swg\modern-client\src\render\RendererPipelines.cpp`
- `D:\code\swg\modern-client\src\render\Renderer.cpp`

Relevant D3D11 fallback shader lines in `Direct3d11.cpp`:

- Fog color/control upload.
- `setActiveFogColor()`.
- `setFog()`.
- Pixel shader fog equation.
- Texture format swizzle.
- Screen-space transformed vertex path.

Modern reference points:

- Sky shader: `RendererShaders.cpp`, `kSkyVertexShaderSrc`, `kSkyPixelShaderSrc`.
- Sky pipeline: `RendererPipelines.cpp`, `initSkyShaders()`.
- Client-tools pass blend states: `RendererPipelines.cpp`, client blend state table.

## Commit Discipline

Keep commits small and proven.

Recommended commit shape:

1. Renderer-only code change.
2. Build proof.
3. D3D9/D3D11 or before/after screenshot proof.
4. Pixel summary when possible.
5. Commit message naming the exact parity behavior fixed.

Do not mix a proven loading fix with an unproven skybox fix.
