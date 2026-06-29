# OG VR Audit

Scope for this pass: core VR, detached 3D hands, and controller rays. HUD and wrist-panel polish are intentionally deferred unless they break those three surfaces.

## Launch Contract

- `scripts/dev/Build-Deploy-OG-VR-X64.ps1` is the build path for this work.
- It builds `Release|x64` for `clientGame`, `Direct3d11`, and `SwgClient`.
- It refuses stale runtime binaries by checking PE machine type, SHA match after deploy, and byte markers.
- Required client marker: `aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback`.
- Required renderer marker: `rendererHandLayersHardDisabled_noFallback`.
- `scripts/dev/START-OG-VR.ps1 -ValidateOnly` is the fast no-launch runtime check.
- `scripts/dev/START-OG-VR.ps1 -EnableLogs -PhysicsTrace` is the launch path when headset testing is needed.

## Core VR Owners

- `src/engine/client/application/Direct3d11/src/win32/Direct3d11_VrBridge.cpp`
  - Owns OpenXR session startup, swapchains, frame submit, projection, recenter, controller actions, pointer rays, wand visuals, and hand-state publication.
  - Projection and quad swapchains must prefer `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`.
  - Scene hands must not be renderer 2D hand layers; `SWG_OG_VR_HAND_VISUALS=0` and the renderer marker keep this hard-disabled.
  - Menu-time controller poses may publish before world recenter using menu/head pose space; world-time poses publish only after world recenter is settled.
- `src/engine/client/library/clientGraphics/src/shared/Camera.cpp` and `Camera.h`
  - Own the game-side camera/projection hooks used by the VR bridge.
- `src/engine/client/library/clientGraphics/src/win32/Graphics.cpp` and `Gl_dll.def`
  - Own exported renderer bridge functions and runtime DLL wiring.

## Hand/Ray Bridge

- `src/engine/client/application/Direct3d11/src/win32/VRPhysicsBridge.h` and `.cpp`
  - Own the ABI between renderer/OpenXR and clientGame.
  - `HandState.aimFromWorld` is the ray/selection pose.
  - `HandState.gripFromWorld` is the controller grip pose.
  - `SWGVRPhysics_GetAimRay` feeds game ray selection.
  - `SWGVRPhysics_PublishHandState` feeds detached hands.
- Invariant: hands and rays share the same aim pose origin.
- Invariant: aim ray is independent from grip position; grip may not move the hand pivot away from the ray origin.

## Detached Hands

- `src/engine/client/library/clientGame/src/shared/object/VrDetachedHands.cpp`
  - Owns 3D hand rig creation, species mesh selection, bind solving, pose smoothing, hardpoint correction, finger curl, weapon clone attach, and proof logs.
  - Hand mesh source now prefers `appearance/mesh/<species>_<gender>_hands.lmg`, then falls back to LOD `.mgn` files.
  - Default menu hands prefer `appearance/mesh/hum_m_hands.lmg`, then LOD fallback.
  - `getTrackedHandTarget()` contract:
    - position = OpenXR aim/ray origin,
    - hand finger-forward = OpenXR aim/ray forward,
    - hand roll/thumb-side = projected OpenXR grip `I`,
    - target offset remains zero by launcher default,
    - collision and smoothing remain off by launcher default.
  - Proof must log `handMinusRayOriginMag <= 0.005` and `handForwardMinusRayForwardMag <= 0.005`.
- Avatar menu hooks:
  - `SwgCuiAvatarSelection.cpp`, `SwgCuiAvatarSimple.cpp`, and `SwgCuiAvatarCreation.cpp` publish selected/created creature pointers.
  - `SwgCuiAvatarCreationHelper.cpp` now updates the preview creature during creation helper changes.
- Real hands are expected in three states:
  - first menu: default 3D SWG hands,
  - avatar selection/creation: selected/created creature hands,
  - world: player creature hands and equipped hand wearables when available.

## Rays And Selection

- Renderer/controller side:
  - `Direct3d11_VrBridge.cpp` samples OpenXR aim/grip spaces each frame and publishes hand state.
  - Native wand visuals are renderer-side debug/aim visuals only.
- Game selection side:
  - `src/game/client/library/swgClientUserInterface/src/shared/page/SwgCuiHud.cpp` calls `SWGVRPhysics_GetAimRay` and converts the native VR ray into SWG world collision.
  - Center cursor remains only a fallback; VR wand ray is the owned path for headset targeting.
- Invariant: the yellow ray origin and detached hand pivot must be the same point in the same coordinate space.

## HUD Boundary

- HUD work is not part of this pass except for avoiding interference with hands/rays.
- HUD/env owners include `CuiIoWin.cpp`, `SwgCuiHud.cpp`, and the Direct3d11 quad/panel code.
- Current boundary:
  - HUD quads may exist.
  - HUD may occlude or capture input by explicit settings.
  - HUD code must not own or rewrite hand pivots, hand orientation, ray origin, world recenter, or controller pose publication.

## Verification

Run after a build:

```powershell
powershell -ExecutionPolicy Bypass -File .\source\client-tools\scripts\dev\START-OG-VR.ps1 -ValidateOnly
```

Run after a headset session:

```powershell
powershell -ExecutionPolicy Bypass -File .\source\client-tools\scripts\dev\verify-og-vr-hands.ps1 -AnalyzeOnly
python .\source\client-tools\scripts\dev\render_vr_hand_math_proof.py --trace .\proofs\og_vr_hands_trace.log --out .\proofs\og_vr_hand_math_proof.html
```

Required green checks:

- x64 built/runtime binaries match.
- renderer marker is present.
- hand marker is present.
- OpenXR session begins and tracked controller poses publish.
- no `openxrHandLayers` submit.
- no head-relative hand publish.
- controller proof covers both hands.
- hand pivot equals ray origin.
- hand forward equals ray forward.
- mesh bind pivot solves onto target.
- rendered `hold_l`/`hold_r` hardpoint correction solves onto the shared pivot.

## Current Patch Result

- Rebuilt and deployed x64 `SwgClient_r.exe` and `gl05_r.dll`.
- Runtime hashes match built hashes.
- Client marker: `aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback`.
- Renderer marker: `rendererHandLayersHardDisabled_noFallback`.
- Next headset check should focus on first visible hands, hand rotation, and whether the selected Trandoshan uses the logical hand mesh.
