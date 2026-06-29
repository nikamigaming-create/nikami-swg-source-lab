# OG Vulkan Backend Plan

Status note, 2026-06-18: this is the active Vulkan bring-up plan for the original client, not `modern-client`.

## Goal

Add Vulkan as a third original-client renderer backend beside `Direct3d9` and `Direct3d11`.

Keep DX11 intact. Vulkan must prove itself in flat mode before any VR work moves over.

## Current Slice

New backend:

```text
src/engine/client/application/Vulkan
```

Build command:

```powershell
D:\code\swg\scripts\dev\build-og-client.ps1 -Target Vulkan -Platform x64 -Configuration Release -LogPath D:\code\swg\proofs\og_client_build_vulkan_x64.log
```

Deploy or launch flat Vulkan:

```powershell
D:\code\swg\og-client-tools\scripts\dev\START-OG-VULKAN-FLAT.ps1 -DeployOnly
D:\code\swg\og-client-tools\scripts\dev\START-OG-VULKAN-FLAT.ps1
```

Built DLL:

```text
D:\code\swg\og-client-tools\src\compile\x64\Vulkan\Release\gl05_r.dll
```

The first slice exports the OG `GetApi` table, dynamically loads `vulkan-1.dll`, creates a flat Win32 Vulkan path, and implements a clear/present swapchain loop. Mesh/material/UI rendering is deliberately not claimed yet.

## Proven Gates

- Gate A: x64 Vulkan renderer DLL builds.
- Gate B: DLL exports `GetApi` and `SetVrTvModeEnabled`.
- Gate C: `LoadLibrary`, `GetApi`, and `Gl_api.verify()` succeed.
- Gate D: Vulkan runtime is dynamic-loaded from system `vulkan-1.dll`; no `vulkan-1.lib` dependency.
- Gate E0: deploy-only launcher copies the Vulkan x64 renderer into the OG runtime as `gl05_r.dll`.

Proof:

```text
D:\code\swg\proofs\og_client_build_vulkan_x64.log
```

Manual probe result:

```text
LoadLibrary=ok GetApi=<non-null> Verify=True
```

## Next Gates

Gate E: launch the x64 OG flat runtime and prove `install()` creates instance/device/surface/swapchain.

Gate F: prove `present()` clears the client window to the requested color without crashing.

Gate G: add backbuffer screenshot/autocapture for Vulkan, equivalent to `SWG_D3D11_AUTOCAPTURE`.

Gate H: port actual primitive rendering:

- vertex/index buffer upload
- one untextured pipeline
- transformed UI/color vertices
- indexed triangle list
- quad list conversion

Gate I: port texture upload and sampling:

- ARGB/XRGB 8888
- 565/1555/4444 conversion
- DXT formats
- render targets

Gate J: shader/state parity:

- D3D9-style fixed-function fallback pipeline
- blend/depth/cull/scissor state cache
- fog and material constants
- static shader pass replay

Gate K: flat D3D9/DX11/Vulkan comparison scripts and pixel proof.

Gate L: Vulkan OpenXR VR bridge, only after flat rendering has stable world/UI proof.

## Hard Rules

- Do not replace DX11. DX11 remains the reference backend.
- Do not claim Vulkan world parity until paired captures prove it.
- Do not move VR onto Vulkan until flat Vulkan has UI, world geometry, textures, fog, and render-target behavior.
- Keep build/runtime gates independent so a Vulkan regression cannot break DX11 VR work.
