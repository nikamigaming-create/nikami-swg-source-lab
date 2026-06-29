# OG VR Hands Process

Use this path for the SWG OG x64 VR hand build. It avoids stale Win32/x64 confusion and does not tail logs after launch.

## Build And Deploy

```powershell
powershell -ExecutionPolicy Bypass -File .\source\client-tools\scripts\dev\Build-Deploy-OG-VR-X64.ps1
```

This rebuilds `Release|x64`, deploys `SwgClient_r.exe` and `gl05_r.dll`, then refuses success unless both built binaries are PE `AMD64`, the deployed x64 client contains the `aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback` marker, and the deployed x64 renderer contains the `rendererHandLayersHardDisabled_noFallback` marker.

## Launch

```powershell
powershell -ExecutionPolicy Bypass -File .\source\client-tools\scripts\dev\START-OG-VR.ps1
```

The launcher validates the deployed runtime before starting the client: runtime exe and renderer must be PE `AMD64`, must contain the current byte markers, and must exist at the client root. It clears old detached-hand offset/twist env vars, keeps head-relative hands off, verifies the renderer has 2D hand layers hard-disabled, and uses real SWG detached hands only.

Read-only validation of the currently deployed runtime:

```powershell
powershell -ExecutionPolicy Bypass -File .\source\client-tools\scripts\dev\START-OG-VR.ps1 -ValidateOnly
```

## Hand Contract

- OpenXR aim pose position is the hand bind pivot target.
- OpenXR aim pose direction is the glove finger-forward direction.
- OpenXR grip pose orientation supplies only the roll/thumb-side frame after being projected onto the aim-forward plane.
- The SWG glove anchor is solved with `anchor = target * inverse(anchor_to_bind)`.
- Mesh proof must report `targetPose=aimPivotAimForwardGripRoll_holdHardpoint_freshPose_noFallback`, `handForwardMinusRayForwardMag` near zero, and `deltaMag` near zero.
- Hardpoint proof must drive `hold_l/hold_r` back to the same ray pivot with `hardpointMinusTargetMag` near zero after correction.
- The yellow ray and glove bind target are fed from the same OpenXR aim pose conversion path.
- Finger curl uses geometry-selected chain direction and rotates the finger bones by default to avoid backwards or stretched crumple.
- Stale pose freeze is disabled. Hands keep only a short `0.5s` real-pose grace before invalid pose data hides the mesh instead of holding a fake hand.
- 2D OpenXR hand layers are hard disabled. The renderer byte marker for this is `rendererHandLayersHardDisabled_noFallback`. The only visible hands are real detached 3D SWG rigs: default menu hands before a creature exists, selected/created creature hands in avatar screens, and the player creature hands in world.

## Optional Offline Analysis After A Run

```powershell
powershell -ExecutionPolicy Bypass -File .\source\client-tools\scripts\dev\verify-og-vr-hands.ps1 -AnalyzeOnly
python .\source\client-tools\scripts\dev\render_vr_hand_math_proof.py --trace .\proofs\og_vr_hands_trace.log --out .\proofs\og_vr_hand_math_proof.html
```

These analyze logs after the run. They are not part of the launch path.
