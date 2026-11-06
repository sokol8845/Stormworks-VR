# Native Stormworks → OpenXR experiment

This is a bounded diagnostic with separate **Menu** and **Gameplay** paths, not
a playable VR mode. Both are restricted to the measured executable SHA and
specific native call sites. Headset tests confirmed image delivery and hands;
gameplay testing exposed tilt and self-head visibility problems. The subsequent
calibration, anchor and shader fixes have passed isolated tests but await another
game/headset test. The user authorized bounded game testing. The current
profile targets Steam build **26294447**, SHA `c38c03ff…`; see
[`game-profile.md`](game-profile.md) for the verified port and
[`status.md`](status.md) for live-test results. Older analysis documents retain
the addresses and executable hashes of their original measurements.

Build `build/capture/Release`, then run from the project root:

```powershell
# Real game renderer, GPU images and explicitly fake OpenXR. No headset used.
.\scripts\run-game-preview.ps1 -FakeRuntime -QuietTrace

# Connected headset and the installed OpenXR runtime, menu scene.
.\scripts\run-game-preview.ps1 -QuietTrace

# Only when game launches are permitted: enter the cockpit normally first.
.\scripts\run-game-preview.ps1 -Scene Gameplay -ManualStart -PreviewSeconds 120 -TimeoutSeconds 600 -QuietTrace -InteractionProbe
# In another shell, after entering the cockpit and putting on the headset:
.\scripts\start-vr-preview.ps1 -GameProcessId <measured-game-pid>
```

The script refuses a second copy of Stormworks. It launches a fresh game with
process-local options, records results under a unique path in `artifacts`, waits
for the selected measurement interval (30 seconds by default), and forcibly closes only its own verified child
after measurements. The user authorized this closing behavior. It restores the
parent environment. `-Loader` overrides the absolute loader path; `-GameExe` and
`-BuildDirectory` allow other locations. Overall timeout includes game startup.

Readiness diagnostics (`#openxr_readiness`) report a continuous wait for the
session, focus, head tracking or a renderable frame after five seconds, then
at most every 30 seconds. The supervisor prints troubleshooting hints and a
recovery message without changing window focus. An `xrCreateInstance` runtime
failure (-2) suggests checking SteamVR, Steam Link and the active OpenXR runtime;
it does not establish a specific cause. This behavior is verified offline.

`SWVR_OPENXR_PREVIEW=1` selects the native bridge and requires an absolute
`SWVR_OPENXR_LOADER`. These variables are normally set by the script. The bridge
initializes OpenXR on the game render thread using its existing HDC/HGLRC. It
calibrates LOCAL to the initial midpoint and yaw heading of both tracked eyes,
only while focused and tracked. Initial pitch/roll are not baked into the world
reference. This preserves LOCAL gravity; it does not flatten the vehicle's bank.
A reference-space revision invalidates calibration and resets pinch detectors.

Each valid frame offsets all three source camera copies in double precision.
In Gameplay, the frame is located before the native camera-owner update, after
an actual world render has armed the path. Matching current/previous look
rotations and the measured native camera backoff are removed from private camera
copies to reconstruct head anchors. Source game cameras remain unchanged.
The temporal eye history is invalidated on empty/suppressed frames. This retains
the observed temporal distinction but is not a proof of all motion-vector state.
Asymmetric FOV is applied to source P/VP and double VP, then again after each of
the renderer's three measured floating-origin camera updates. The hook requires
all three updates for each eye before sending its image. Native depth coefficients
remain unchanged. The frame ends only after both captures have been copied into
the borrowed XR textures. Exceptions discard the frame, close XR on its owning
thread and disable the experiment.

By default, the script enables `SWVR_NATIVE_EYES=1`. The hook captures the actual
resource manager from the game's package initializer, creates two independent
packages from scalar configuration, and waits for the normal manager update to
realize their FBOs. It checks resource and FBO uniqueness against the game and
the other eye before drawing. Source two-attachment G-buffers use the measured
same-size native reconfiguration after initialization. Native references are
explicitly released before the test closes. `-NativeEyeResources:$false` retains
the earlier shared-target path for comparisons; it is not the preferred mode.
Supported configuration changes close and recreate the owned packages before
their next use, with an empty XR frame while native realization completes.
Transient targets with a one-pixel width or height now suspend stereo before
resource recreation or desktop splitting. Existing eye packages are retained,
the current XR frame is ended empty, and history/hand intent are reset. Normal
sizes resume the path (`openxr_target_extent` logs both transitions). Offline
tests cover tiny extents and restoration; a live repeat remains outstanding.

Gameplay recomputes visibility immediately around the two eye renders using six
half-spaces covering both frusta. It restores native visibility before returning,
so a later swap-time shutdown cannot retain XR visibility flags in a stale scene.
Auxiliary visibility passes remain unchanged and require separate validation.

`-HeadMask` defaults to enabled. Known animated fragment sources are patched in
memory to add `swvr_vr_head_mask`, defaulting to zero. The flag is set only during
VR eye draws with a nonnegative native head-bone index and the measured 0.25
weight signature. It removes the existing mask's 0.25 m depth limit for that draw;
it does not hide every animated character. Normal draws reset the flag. A failed
patched shader compilation retries the original source. Game files are not edited.
This still relies on the game's local first-person head selector; a third-person
draw with no selected head bone is intentionally not treated as a local head.
See `first-person-head-visibility.md` for the authored-alpha limitation and tests.

`-AvatarPose` is an optional Gameplay-only survivor pose experiment. It implies
HandRay and HeadMask and uses the current local actor to select the verified
male/female rig. Start from the cockpit with tracked head and both hands:

```powershell
.\scripts\run-game-preview.ps1 -Scene Gameplay -AvatarPose -ManualStart -QuietTrace -PreviewSeconds 300 -TimeoutSeconds 600
# After sitting in the cockpit, signal the actual measured game PID:
.\scripts\start-vr-preview.ps1 -GameProcessId <measured-game-pid>
```

The head keeps the calibrated eye mount under rotation/translation. The chest
and partially the stomach follow leaning, while hips/legs retain native animation.
Wrist targets use tracked palm geometry and arm IK preserves limb lengths;
unreachable targets are limited. Finger curl uses the rig's simplified bones,
not a one-to-one mapping of 26 XR joints. Missing tracking falls back to native
arm animation rather than retaining an old wrist target.

The adapter temporarily overrides GL bone uniforms for verified local scene
palettes, including depth-only passes, and restores them after each draw. It
does not write the animation, player physics or network state. Both affected
temporal endpoints use the same current predicted pose to avoid blending with
an untracked native pose; full avatar motion-vector integration is not claimed.
The diagnostic XR hand skeleton remains visible for alignment comparison.

`avatar_pose_totals` records candidate/posed draws, both hands, bounded reach,
rejections, palette bindings, identified eyes, depth draws and cache hits. A
requested run must actually draw the head, both arms and depth pass to succeed;
configuration alone is insufficient. Rig/identity/GL errors fail the experiment.
The first headset AvatarPose trial completed 120 seconds with 6760 stereo pairs,
54120 posed draws and both arms, but no depth-only draw was observed. The run
therefore failed the strict depth-coverage check. Reach limiting affected 47634
posed draws; this is evidence of a mismatch, not a measured human arm length.
The later GPU regression reproduces missing uniform-location observations and
passes after lazy discovery of matrix locations. Live depth coverage and visual
alignment remain unverified; see `status.md`.

Update 2026-11-03 (offline): the only native `depth_anim*` draw is the shadow
cascade pass `0x6de000`, which draws a producer-owned copy of the palette from
a separate list. Those copies are now bound as depth-only after exact mesh,
world and palette equality with a verified local record, so `depth_draws`
counts posed shadow draws. `avatar_pose_totals` also reports rejection reasons
(`skinned_unrebased`, `missing_inputs`, `paired_provenance_misses`,
`clip_rejects`, `bound_depth_palettes`) and `avatar_identity_totals` reports the
identity read stage that failed. Not yet verified in the game.

### Body yaw follows the headset (`-BodyYaw`, Gameplay, opt-in)

Optional `-HandLocomotion` adds a left thumb/middle-finger virtual stick and
implies BodyYaw. Open the hand, pinch at neutral, displace >3 cm to move or
12 cm forward to sprint; release the pinch to stop. It emits native movement
button requests and preserves physical keyboard input. Tested offline only:
[controls, native evidence and limitations](hand-locomotion.md).
Alternative `-ArmSwingRun` selects fist/arm-swing sprint instead of the virtual
stick: open both hands, close both fists and alternate strokes beside the body.
Three opposing half-strokes start forward sprint; opening a fist stops it, while
stationary arms stop after about 250 ms. Choose one movement flag per trial.
This mode is also tested offline only; it requires tracked finger tips and a
first headset trial. `arm_swing_totals` records recognition failures and starts.

While walking in first person (not seated, no third-person or free camera),
the native look yaw (`C+2A0`) is set so the character and its walking
direction face the headset yaw. The XR view does not depend on that field.
The field-to-look mapping is identified at runtime before any write
(`#body_yaw_calibration`); `#body_yaw_totals` reports frames, writes and total
rotation. Mouse look still adds to the same field. Not yet verified in the game.

HandButtons now also accepts Toggle buttons (`button_type` 1) through the same
native press/release request as Push buttons.

### Seated head limit (Gameplay)

Gameplay limits how far the estimated neck pivot (7.5 cm below and 8 cm behind
the eye centre) may move from its calibrated position. Turning or pitching the
head about the neck is never limited. Leaning inside 60% of the limit is
unchanged; beyond it the offset is compressed smoothly (tanh) toward an
ellipsoid of 0.30 m forward, 0.12 m back, 0.22 m sideways, 0.10 m up and
0.30 m down. The correction shifts the whole tracking space, so the view,
pointing hands and AvatarPose head target stay consistent. `head_limit_totals`
records frames, limited frames, maximum extent and maximum correction. Disable
it with `-NoHeadLimit` (`SWVR_HEAD_LIMIT=0`). Not yet verified in the headset.

For explicit body calibration, hold both tracked hands horizontally out to the
sides, at shoulder height, in a T-pose aligned with the current body direction.
Signal from a separate terminal (or have the assistant signal it when ready):

```powershell
.\scripts\calibrate-avatar.ps1 -GameProcessId <measured-game-pid>
```

`avatar_calibration accepted` confirms a valid tracking sample. Application is
confirmed separately by `body_calibrated=1` and `body_scale` in pose totals.
Wrist span and shoulder height are stored for this actor and rig variant;
recenter preserves those body proportions. The rig derives one fixed upper-body proportion factor (0.65–1.75)
from its bind span and model scale. Unsupported factors leave native proportions
in use. Arms keep these lengths during IK; targets beyond reach remain bounded.
Incomplete/nonhorizontal tracking is rejected and retains the previous valid
calibration. Actor/model changes discard it. Automatic calibration requires
a valid T-pose with wrist span at least 1.2 m, held for 1.5 s. Tracking gaps up
to 300 ms are tolerated; longer gaps, including gaps with no rendered samples,
start a new hold. Actor/model/LOCAL changes and loss of focus also end a hold.
Yellow/green/red frames indicate holding/acceptance/rejection. This is render-only upper-body
calibration, and requires a headset visual trial before acceptance.

The default Gameplay measurement is now 300 seconds, starting when XR initializes;
manual cockpit navigation has a separate startup timeout. Before launch the
supervisor backs up graphics settings and requests 3440×1440 fullscreen. The
native loader resets to 1280×720/windowed if `write_key` is nonzero, so preparation
clears that marker and controlled termination clears it again after process exit.
Other quality settings are preserved. `-WindowWidth`, `-WindowHeight` and
`-WindowMode` override the requested mode. Startup validation checks the first
native eye-source size; a later resize cannot hide an incorrect initial size.
`python tools/verify_preview_startup.py <capture.xr.txt>` replays that check.
The marker regression passes offline; the attempted Menu run exited before XR,
so actual first-frame startup verification is still pending. The user has asked
not to launch the game; further game runs require a new instruction.

`-HandRay` is an optional Gameplay-only pointing experiment. It is mutually
exclusive with `-InteractionProbe`, since both use the same native hooks. The
right index ray replaces the head ray only inside the known target-selection
call, with fresh focused tracking, valid application data, no benchmark mode and
clear native pause/photo/death/sleep flags. Direction bytes are restored before
returning, including nested calls; enabled transactions are serialized. It does
not emit button actions. The headset adds a green three-metre diagnostic ray;
this line is not a hit marker or the native interaction range. The append-only
`.hand-ray-native.tsv` rearms its bounded recording allowance at VR start, so
walking to the cockpit beforehand cannot consume the measurement budget. The
backend and the new ray have only been tested in standalone fixtures; their
alignment and target selection in a moving vehicle remain unverified.

`-HandButtons` is an optional Gameplay-only interaction backend, disabled by
default. It enables HandRay and cannot be combined with InputProbe or
InteractionProbe because it owns the native input hooks. It requires the full
executable SHA and native signatures to match the supported profile. Only the
right hand and the recognized Push button class with `button_type=0` are allowed.
The backend has been integrated and tested with standalone native fixtures;
**it has not been validated in Stormworks or on the headset**. On 2026-11-02
the user allowed a future game trial and asked to be told when to connect Quest.
The installed EXE currently differs from the supported SHA profile; verify the
new executable profile before starting that trial. Once ready, the bounded
manual-start trial can select this backend:

```powershell
.\scripts\run-game-preview.ps1 -Scene Gameplay -ManualStart -PreviewSeconds 120 -TimeoutSeconds 600 -QuietTrace -HandButtons
# After entering the cockpit, use the measured PID printed by the launcher:
.\scripts\start-vr-preview.ps1 -GameProcessId <measured-game-pid>
```

A completed hand-ray selection certifies the gesture, target and scene
generation. The backend enqueues its own native Action20/phase1 event and checks
gesture freshness, focus, target identity, pause/UI/benchmark state and competing
native input again at logical dispatch and immediately before the original
action. Native actor, health and distance checks remain in the original action.
Foreign events retain their original processing path. An accepted press creates
an owned hold. Opening the hand or cancelling tracking uses captured held-target
data and a dedicated type10 release request, followed by attributable local
held/down-state cleanup. Cleanup does not synthesize phase0/short-click.

Live identity includes the current active-character token through the measured
`world+BB9F8` type-4 node/holder chain. A world/character change or loss of live
context cancels older queued presses. The dispatcher resolves from its current
logic-loop owner, and identity is checked again before the action and after
native callbacks, including capture and release handoff. Pause, captured UI and
the known mode gates also cancel an existing owned hold while the hand remains
pinched. Moving the ray to a new hover target keeps the original held target.

A lost/replaced held target or a scene/character transition with an unresolved
hold blocks the backend and retains the hold as metadata. No old target is
dereferenced or release retried after that loss of proof. The first fault is
reported as `last_fault` in `hand_buttons_totals` and the native log, for example
`scene_changed_with_hold`, `held_target_lost` or `cleanup_identity_changed`.
This fails shutdown drain; it does not establish a network release in the old
scene. Pointer comparisons cannot detect every object recreated at the same
address, so these checks do not prove complete native teardown coverage.

As of 2026-11-02, recognized native transitions can release BEFORE this loss:
`7ACA70` handles deactivation from returns `84C6F1/84DFB4`, preserving all four
arguments and deriving live owner from R9 (`owner+358`). The exit caller invokes
deactivation before resetting D+280. `5F2950` handles the known local-character
replacement from `852D06` inside the matching current logic-loop scope; all six
arguments and returned RAX are preserved. Remote players and unknown callers
keep their original path. An unfinished own action, unavailable live scope or
unmatched identity still blocks rather than guessing cleanup.

These callbacks cancel old certificates, submit dedicated release once, and
invalidate scene identity before accepting a fresh open/re-pinch. Active
lifecycle callbacks contribute to pending state, preventing early shutdown
drain. `lifecycle_releases` records successful pre-cleanup handoffs.

The retirement wrapper records ownership before request handoff. After a proven
WRITE terminal clear it frees its payload and request; after localhost typed
clear it frees only its payload, since the native path deletes the request.
Reset preserves unresolved ownership. Unknown teardown, ambiguous handoff or
exceptions can block the backend or retain quarantined records; these cases do
not trigger guessed frees or retries. See `native-button-retirement.md` for the
two terminal paths and remaining teardown limits.

Source-Q reset/destruction now has separate guarded retirement at raw-clear
returns `24005B/23EB68`. It matches the registered Q and an unparented own
allocation ID, observes original native delete, then frees ONLY its payload.
`source_reset`/`source_destroy` totals prove disposal, not delivery to the server;
the interaction validator still requires a WRITE or localhost observation.
Full Release build and 23/23 CTest passed on 2026-11-02, including combined
transition/source-Q cleanup and negative caller/queue/exception tests:
`../artifacts/offline-transitions-tests-20261102.xml`.

The `.hand-buttons.tsv` and `.button-retirement.tsv` sidecars record input and
ownership operations. XR shutdown cancels intent and waits up to five seconds
for live logic scopes to clear owned holds, pending events and all retained
ownership records. A timeout, backend fault or quarantine fails the experiment.
The supervisor requires `hand_buttons_drain=complete`, at least one accepted
press, one release and one WRITE or localhost retirement before reporting
success. Merely enabling hooks or submitting stereo frames is insufficient.

The `native_hand_buttons_integration` fixture exercises the real bridge, ray,
buttons, capture, bindings, builder and retirement modules with fake native
functions and MinHook. It covers normal-open WRITE, focus-loss localhost, pause
FIFO ordering, foreign passthrough and balanced allocations without phase0.
The saved 19/19 CTest report predates the three added button-backend/retirement
test targets. Offline validation on 2026-11-01 completed a full Release build,
23/23 CTest, 13/13 Python tests and the standalone GL tracer/DLL check. See
`../artifacts/offline-tests-20261101.xml`. A deterministic unit regression first
exposed an event leak when reset cancelled registration immediately before
`mark_submitted()`. The backend now destroys its unsubmitted event and clears
pending state; the regression also verifies recovery for the next gesture.
The test-only synchronization callback is absent from production builds.
No Stormworks or headset trial was performed.

The later lifecycle validation on the same date added 18 isolated unit scenarios
and extended the real-module integration with queued-press shutdown, hold
shutdown through both retirement routes, and pause delivered during a hold.
Retired button memory is decommitted in the lost-target tests. The full Release
build and 23/23 CTest passed again; the new subcases execute inside existing test
programs. See `../artifacts/offline-lifecycle-tests-20261101.xml`.

`-InputProbe` is an optional Gameplay-only observation module, compatible with
HandRay when HandButtons is disabled. It records the known native queue-pop and dispatcher paths, current
owner/simulation/controller relationships, before/after event data, native
pause/UI/benchmark fields and button categories to `.input-native.tsv`. It does
not emit or suppress actions or claim that the observed UI fields form a complete
input gate. It checks full-profile agreement, function entries and six CALL
sites before installing hooks. Recording rearms at VR start; the existing
`scripts/capture-scene.ps1` request can also append a new epoch for both enabled
HandRay/InputProbe logs at the next desktop swap. These calls do not launch a game.

The desktop shows both captured views side by side. Menu UI is drawn afterwards
on the desktop and is not included in the headset layer. Valid tracked hand
segments are drawn as cyan/orange diagnostic skeletons in the final XR textures;
pinches are counted. The skeletons use LOCAL joint positions and the same eye
pose/FOV as the submitted projection layer. They have no scene-depth occlusion
and do not themselves route gestures to game controls; HandButtons is a separate
opt-in backend. Data from the fake loader is synthetic
and cannot prove optical tracking. See `docs/hand-overlay.md`.

The `.xr.txt` sidecar records runtime identity, calibration, submitted frame and
hand/gesture counts, CPU stage timings, stop reason and completion. CPU timings
include driver calls and possible stalls; they are not GPU timestamp queries or
a headset latency measurement. `-QuietTrace` disables detailed GL events to avoid
their diagnostic overhead. The original event trace remains available without
that switch. Two diagnostic eye images are saved as PPM after ten stereo frames.

The fake loader's `.fake.json` validates submitted pose/FOV/rectangles, image
release order, nonblack/different pixels, both diagnostic hand colors in each eye,
and swapchain destruction. The first submitted textures are also saved beside
that report as `.left.ppm`/`.right.ppm`, including hands. The script requires
successful validation and a full `duration_elapsed` stop. A passing fake
run establishes integration with a test double, not delivery to a compositor.

Known limitations: independent target packages do not isolate the whole renderer
state, historical camera values or shader state. Earlier scene construction and
auxiliary views may still have camera-dependent limits. The scripted
menu camera moves independently of the headset. Periodic native maintenance
frames remain mono on the desktop and do not submit XR frames. Water, temporal
effects, color correctness, resolution and comfortable performance remain to be
verified. A runtime failure or unexpected menu transition ends this bounded test;
it does not automatically restart the game or session.
