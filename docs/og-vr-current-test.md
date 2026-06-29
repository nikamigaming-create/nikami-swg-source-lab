# OG VR Current Test

This is the single launch path for the current OG DX11 x64 OpenXR test.

## Command

Run from PowerShell:

```powershell
.\scripts\dev\run-og-vr-test.ps1
```

The script deploys the current x64 `SwgClient_r.exe` and DX11 `gl05_r.dll` into:

```text
..\client\SWGSource Client v3.0
```

It writes proof logs to:

```text
..\proofs\og_vr_current_test.log
```

## Default Test Mode

Defaults are intentional:

- OpenXR VR is on.
- World mode is on after entering the game.
- Main HUD quads are off. There is no full-HUD slab and no generic cropped HUD panel layout.
- Object-context quads are on. The native target/status UI is used only as a source for a window anchored to the object under the VR ray.
- Object-context quads go away when the ray leaves the object.
- The grip-held wand is visible in menus and in game.
- Fake hand/saber visuals are off.
- `SWG_OG_VR=1` always starts with the normal menu/front quad path, then transitions into world mode after game entry.
- TV mode is forced to stay on only when explicitly requested.

Use this command only when testing couch/TV mode:

```powershell
.\scripts\dev\run-og-vr-test.ps1 -TvMode
```

Use this command only for screenshots or panel-only diagnostics:

```powershell
.\scripts\dev\run-og-vr-test.ps1 -HideWand
```

## What To Verify

At the login/menu screens:

- Grip shows the wand.
- The menu/front quad appears in front of you but does not follow your face when you lean or turn your head.
- Trigger clicks exactly where the wand hits.
- Trigger hold and movement behaves like mouse drag.
- The normal SWG cursor should move through the menu path.

After entering the world:

- The game is a real stereo world, not projected on a big TV quad.
- There should not be one giant desktop/HUD slab in world mode.
- Point the grip-held wand at an NPC, player, speeder, mount, harvester, or usable object and verify its contextual target/status window appears around that object in 3D space.
- Move the ray off the object and verify the contextual window disappears.
- Left stick forward moves forward, back moves back.
- Movement ramps in and out instead of binary on/off.
- Interaction path: point the wand at the object in world space, use trigger for normal select/default action, and use the opposite trigger/right-click route for the normal SWG radial/context menu path.

## Current Object Context State

The current object-context path uses the native SWG target/status UI as a texture source, but it is not a main HUD panel system. Only target/status pages with a fresh object anchor are submitted as OpenXR quads.

## Proof Log Checks

Useful quick checks:

```powershell
Select-String -LiteralPath ..\proofs\og_vr_current_test.log -Pattern 'openxrUiMouse|openxrPointerLayer|openxrObjectContext|ogVrHudTargeting|remove' | Select-Object -Last 80
```

Expected events:

- `openxrPointerLayer` while grip is held.
- `openxrUiMouse` down/up while trigger is pressed.
- `ogVrHudTargeting` when the VR ray acquires or clears a world target.
- `openxrObjectContextAnchor` while the ray is on an object.
- `openxrObjectContextPanelRect` when the native target/status window is submitted as an object-context quad.
- `openxrMenuButton` when pressing A, B, X, or Y.
