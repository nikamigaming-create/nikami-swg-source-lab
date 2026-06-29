# OG VR HUD Bridge

This is the working target for bringing the OG client HUD into VR from the current flat DX11 x64 baseline.

## Current State

Status on June 7, 2026:

June 27 correction: do not build a spatialized main HUD, full-HUD slab, or generic cropped-HUD panel system. VR should use intentional object-context windows: when the wand/ray targets a person, creature, harvester, lootable object, terminal, or similar object, that object's native contextual/status/action surface appears in that object's 3D space and goes away when focus leaves. Radial/context menus should remain the native SWG left/right mouse interaction path.

* Flat DX11 x64 is the good baseline. Do not treat old white/missing-people or 5 FPS experiment notes as active unless a fresh proof reproduces them.
* The in-game backbuffer/loading view can be submitted to OpenXR as a room/local-space planted quad.
* The quad can be tuned with distance, yaw, lateral, vertical, and width proof-script knobs.
* VR mode forces the ground `FreeChaseCamera` into first-person zoom by default so the headset starts from an in-avatar view instead of desktop third-person. Set `SWG_OG_VR_FORCE_FIRST_PERSON=0` or pass `-DisableVrFirstPerson` to the proof script for comparison/debugging.
* x64 Miles audio initializes through the normal Windows DirectSound/default playback path.
* We are ready to iterate in-game VR from the anchored quad: character presence, first-person view, movement/snap turn, object-context quads, thick wand/pointer, and interaction routing.

The immediate goal is not more flat-renderer triage. The immediate goal is to make the VR menu and in-game flow usable and awesome while preserving flat DX11 x64 when VR is disabled.

For VR proofs, do not start by skipping the menu with `ConfigAutoConnect`. Use `LoginToCharacterSelect` first: capture login, type username/password, click login, capture character select, and stop there. Prove loading/menu/HUD clicks before entering the world.

## Next Gates

1. Login and character-select VR menu proof:
   - Launch with `-EnableVr -SceneDriver LoginToCharacterSelect`.
   - Prove the planted quad shows the login/loading UI in front of the user.
   - Prove username/password entry, login click, loading audio, character-select display, and character/menu click targeting.
   - Stop at character select until VR pointer/menu interaction is trustworthy.

2. First-person VR presence:
   - Force or request an in-game first-person/player-eye view for VR mode instead of staying in desktop third-person.
   - Keep the desktop mirror useful for debugging, but the headset should feel like being in the avatar.
   - Current first proof forces `FreeChaseCamera` to zoom setting `0` when `SWG_OG_VR` or `SWG_D3D11_VR` is set.
   - Proof should record the `ogVrFirstPersonForce` event, camera mode, player position, and whether the headset view is receiving the in-game frame.

3. Recenter planted quad:
   - Add a gated `Direct3d11_VrBridge::requestRecenterQuad(reason)` path that clears the captured anchor.
   - Route a later controller/menu command to it.
   - Proof should log previous and new quad pose with distance/yaw values.

4. Raw OpenXR controller snapshot:
   - `Direct3d11_VrBridge` owns OpenXR action handles, controller aim/grip poses, trigger/grip/thumbstick state, and pointer-ray layer submission.
   - It should not own SWG gameplay semantics.
   - Proof should log controller readiness, per-hand tracking, stick axes, trigger, grip, and pointer dimensions.

5. VR gameplay/HUD router:
   - A UI/client module should consume the raw controller snapshot and decide whether input targets HUD, chat, radial UI, world, or nothing.
   - Movement and snap turn should route through existing player input/message paths.
   - HUD focus must consume clicks before world action routing.

6. Object-context quad proof:
   - Start with native OG target/status/context UI only, anchored to the selected object's 3D position.
   - Do not submit the main HUD, toolbar, radar, chat, or arbitrary cropped UI as world panels.
   - Preserve native SWG left/right mouse semantics so default action and radial/context menus still use the original game paths.

## Source Ideas

Use OpenMW VR as the gold reference for proven VR mechanics:

* OpenXR session lifecycle, frame pacing, and composition behavior.
* Controller aim/grip spaces and thick wand/pointer presentation.
* Comfortable spatial UI interaction rules.

Use the modern client only as the SWG-flavored bridge reference:

* OpenXR D3D11 session and swapchain bridge.
* Separate projection layer for the world.
* Separate quad layers for HUD panels.
* Thick controller pointer/wand layers, not a thin flat cursor.
* Controller action routing modeled after the modern `GameHud` VR action audit.

The final UX should step beyond the OpenMW-style baseline toward an AirLink-like panel system:

* Any HUD chunk can be docked beside the user, floated in front, tilted, grabbed, or recentered.
* Loading/login and modal menus appear as clean desktop-like quads.
* A low hovering quick bar provides common actions without blocking the world.
* Temporary menus pop up as spatial panels and can be dismissed or docked.

Use the pasted spatial HUD controller idea only as a shape:

* Spatial panels can be collapsed, moved, and switched between flat/world presentation.
* Actions should be callback-routed.
* HUD modules should be detachable.

Do not copy the pasted code directly. It uses placeholder game concepts and raw `void *` widget handles. In OG, panel ownership must stay typed around `UIPage`, `CuiMediator`, and the existing `SwgCuiHud` mediators.

## Modern Wand Target

The wand/pointer has to feel like the OpenMW/modern path:

* Per-hand OpenXR quad layer rendered from a small pointer-ray texture.
* Default pointer ray texture: `8x8`.
* Default ray length: `2.25m`.
* Default ray width: `0.045m`.
* Configurable width range: `0.01m` to `0.16m`.
* OpenMW-style visual model supports a wider target representation up to `0.32m`.
* Ray is anchored from controller aim/wrist space and should be visible in both eyes.

The OG VR path should not use a fragile single-pixel or screen-space mouse cursor as the primary VR interaction affordance.

## OG HUD Panel Mapping

The full OG ground HUD must remain complete. The VR bridge should group the real OG UI into spatial panel roles instead of inventing new placeholder widgets.

Required panel roles:

* `WorldProjection`: the DX11 world/backbuffer submitted to the VR projection layer.
* `LoadingScreenQuad`: the login/loading screen presented as a clean desktop-like quad centered in front of the headset.
* `HeadsetOverlay`: reticle, target hover/status, system messages, and short quest helper status.
* `RightArmPrimary`: toolbar actions, radial/default action, target status, interaction prompt.
* `LeftArmUtility`: radar/map, journal/quest detail, character/inventory shortcuts, settings.
* `FloatingQuickBar`: a small AirLink-style hovering bar for constant quick actions.
* `ChatPanel`: chat history and chat input, with keyboard focus routed safely.
* `PointerRay`: thick per-hand wand/pointer quad layer.

OG source anchors to use:

* `SwgCuiHud`, `SwgCuiHudGround`, and `SwgCuiHudFactory` for the active ground HUD.
* `SwgCuiAllTargetsGround` and `SwgCuiReticle` for reticle/target affordances.
* `SwgCuiChatWindow` for chat.
* `SwgCuiActions` for radar, quest, toolbar, and radial command routing.
* `CuiManager` pointer and keyboard ownership for interaction focus.

## Bridge Design

The first OG bridge should be additive and gated:

* No VR code should run unless an explicit VR flag or environment variable is set.
* Current bridge flags are `SWG_OG_VR=1` or `SWG_D3D11_VR=1`; optional proof output uses `SWG_OG_VR_PROOF=<path>`.
* Flat DX11 x64 must continue to present exactly as before.
* `Direct3d11` owns the D3D11 device/context/swapchain and is the first practical place to submit the final backbuffer to OpenXR.
* A small `Direct3d11_VrBridge` module should own OpenXR loading, session state, swapchains, submit, and proof logging.
* HUD panel grouping should live outside `Direct3d11.cpp`; the renderer should only provide textures and timing.

The presentation target is closer to AirLink's desktop/panel model than a flat monitor mirror:

* Loading/login appears as a stable quad in front of the user.
* HUD panels can become spatial panels with grab, rotate, recenter, resize, and dock behavior.
* The quick-action bar floats low in front of the user and stays readable without blocking the world.
* Panel motion should feel calm and anchored; no panel should follow head motion as a headset overlay.
* The loading quad must be room/local-space anchored after placement. It must not follow the user's head/eyes; recentering is an explicit future command.
* Loading/login audio must play with the loading quad. Until explicit VR audio-device selection exists, Miles follows the active Windows DirectSound/default playback device, so AirLink/headset audio must be the Windows output before launch.

Current loading/backbuffer quad tuning is environment-driven and VR-only:

* `SWG_OG_VR_QUAD_DISTANCE_METERS`: distance from the headset at anchor capture, default `1.75`.
* `SWG_OG_VR_QUAD_YAW_DEGREES`: yaw offset applied when the quad is anchored, default `0`.
* `SWG_OG_VR_QUAD_LATERAL_OFFSET_METERS`: side offset after yaw is applied, default `0`.
* `SWG_OG_VR_QUAD_VERTICAL_OFFSET_METERS`: height offset from the headset pose at anchor capture, default `0`.
* `SWG_OG_VR_QUAD_WIDTH_METERS`: quad width, default `1.65`.

The bridge logs these values in the `openxrQuadAnchor` proof event so headset feedback can be turned into exact launch settings.

Minimum bridge API shape:

```cpp
class Direct3d11_VrBridge
{
public:
    static void install(ID3D11Device *device, ID3D11DeviceContext *context);
    static void remove();
    static bool isEnabled();
    static void beginFrame();
    static bool submitBackBuffer(ID3D11Texture2D *backBuffer);
    static void submitHudPanel(char const *panelName, ID3D11Texture2D *texture, int width, int height);
    static void submitPointerRay(bool leftHand);
};
```

That API is deliberately small. It lets OG first prove the final backbuffer/HUD can reach VR, then later split true HUD panels into separate textures.

## Interaction Rules

Controller routing should follow the modern contract:

* The wand replaces the flat in-game interaction mode in VR. It is the primary way to interact with both HUD panels and the world.
* Flat mouse-style interaction remains available only for desktop mirror/debug fallback, not as the VR-first model.
* Each wand ray resolves a focus target in this order: arm/headset HUD panel, modal UI/chat input, radial/context UI, then world target.
* Grip plus trigger submits that hand's pointer ray.
* Pointer focus on an arm HUD panel consumes the click before world targeting.
* No-grip right trigger performs the default target action.
* No-grip left trigger tabs targets.
* Radial/menu opens context actions.
* Chat input can take keyboard focus without permanently stealing game movement.
* The reticle/cursor position in VR should be driven by the wand focus result, not by the desktop mouse position.

The pasted callback idea maps to OG as command dispatch callbacks, not generic menu callbacks:

* Toolbar slot -> `SwgCuiActions`/command queue intent.
* Radial action -> existing target/radial route.
* Radar/map buttons -> existing radar/map actions.
* Chat submit -> existing chat input path.
* Inventory/character/journal -> existing mediator activation actions.

## Wand Focus Pipeline

The VR interaction path should be explicit so HUD and world input cannot fight each other:

1. Read controller aim/grip pose and trigger state.
2. Build the thick modern pointer layer for the active hand.
3. Project the ray against spatial HUD panels.
4. If a HUD panel is hit, route trigger/click/drag/scroll to that panel and consume the input.
5. If no HUD panel is hit, run OG world target picking/default action/radial action.
6. Emit proof diagnostics showing the resolved focus target: `hud`, `chat`, `radial`, `world`, or `none`.

World interaction targets:

* Look-at/hover target.
* Default action target.
* Radial/context target.
* Target selection/tab target.
* Object examine/conversation requests.

HUD interaction targets:

* Toolbar slots.
* Chat history/input.
* Radar/map controls.
* Quest/journal controls.
* Character/inventory shortcuts.
* System modal buttons.

## Proof Requirements

A VR HUD proof is not complete unless it records:

* Flat DX11 x64 proof still passes after the VR code is present but disabled.
* VR mode creates an OpenXR D3D11 session and submits frames.
* Projection layer shows the in-game world.
* HUD is visible and readable in VR: chat, toolbar, radar, target/status widgets, quest helper, reticle/cursor, and system messages.
* Loading/login sounds, UI sounds, ambient audio, and in-world effects are audible in the headset.
* Thick wand/pointer layer is visible for both hands and uses the modern width/length model.
* The wand can interact with arm/headset HUD panels and the in-world target/action/radial path.
* Pointer interactions hit the arm HUD and do not accidentally click the world while the HUD has focus.
* Logs or JSON proof include layer submission, pointer dimensions, HUD panel list, and action routing results.
