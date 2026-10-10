# AmnesiaTDD.FPSFix

An x86 ASI plugin that decouples presentation from the fixed 60 Hz simulation
in compatible Windows builds of *Amnesia: The Dark Descent*.

Version 1.0.0 covers the camera and raw mouse path, first-person hands and
lantern, dynamic props and articulated furniture, cloth, Grunts, ragdolls,
particles, image-trail timing, and presentation-rate GUI cursors. It
validates the host's mapped PE image, resolves every mapped engine boundary
through unique byte signatures, disables only HPL2's presentation
gate, and renders the selected gameplay camera, hands root, dynamic physics
props, Grunt roots and evaluated bone palettes, and particle positions between
completed 60 Hz endpoints. The original fixed-step simulation timer remains
untouched.

## Compatibility

- Architecture: x86/PE32
- Validated GOG build ID: `53742270235201755`
- Validated `Amnesia.exe` SHA-256:
  `E75EE41394B2D20B0F96B74713C40F6C38BAC88E98D0E23383036F22EA1688D0`

The hash above records the validated release target; it is not a startup
allowlist. The plugin does not gate on storefront, path, file size, hash, or
fixed code RVAs. Instead, every required engine function must have exactly one
matching code signature, all required named imports must resolve, and every
hook target must reside in executable memory. An incompatible or ambiguous
mapping fails closed before hooks are installed. This permits another
storefront/revision to load when its relevant binary code and layouts are
compatible, although the GOG build above remains the only validated target.
The scan runs once on the deferred bootstrap thread and is not part of any
simulation, input, or render hot path.

## Build

Configure from an x86 Visual Studio developer environment:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

The paired output is written to `build/bin`:

```text
AmnesiaTDD.FPSFix.asi
AmnesiaTDD.FPSFix.ini
```

The plugin is statically linked to the MSVC runtime and uses only Windows
system libraries at runtime.

Create the canonical plugin-only package with:

```powershell
cmake --build build --target package_plugin_only
```

If the hash-pinned x86 Ultimate ASI Loader research binary is present, create a
convenience bundle with:

```powershell
cmake --build build --target package_ultimate_asi_loader
```

The targets create versioned ZIP files under `dist`. The loader bundle copies
the same plugin pair and packages the verified loader as `wininet.dll`; it also
includes its third-party notice and checksums. Release ZIPs contain only the
plugin/configuration, required loader where applicable, and plain-text
documentation and notices.

## ASI loading

Place the `.asi` and `.ini` together in a directory scanned by a compatible x86
ASI loader. The plugin has no dependency on a loader-specific API and resolves
its configuration relative to its own module path.

Ultimate ASI Loader is available under `research/third_party` for development.
Amnesia imports `WININET.dll`, not `dinput8.dll`, so an Ultimate ASI Loader live
test must use its x86 binary under a validated `wininet.dll` installation. Never
overwrite an existing proxy without checking which mod owns it.

The resulting `AmnesiaTDD.FPSFix.log` is normally written beside the plugin. If
that directory is not writable, it falls back to
`%LOCALAPPDATA%\AmnesiaTDD.FPSFix\AmnesiaTDD.FPSFix.log`. The ASI is normally
kept loaded until process exit; hot unloading is unsupported.

With `[Telemetry] Enabled=true`, the log reports completed fixed ticks,
`OnDraw`, `cScene::Render`, and `OnPostRender` rates at the configured interval.
It also reports a 10-us QPC histogram of consecutive `cScene::Render` entries:
sample count, mean, p50, p95, p99, maximum, excluded gaps above 250 ms,
100-ms histogram overflow, and invalid samples. This provides pacing evidence
for the SDL/OpenGL renderer, which PresentMon 2.5.1 did not observe reliably in
live validation.
It also reports interpolation, endpoint-clamp, unavailable, and reset decisions
from the presentation clock, plus camera, hands-root, hands-pose, selected
world-root, and particle outcomes and the latest alpha/reset reason.
When `[Features] MouseOverlay=true`, active smooth-mouse-off gameplay render
frames pump SDL events while the plugin taps the individual relative
`WM_INPUT` packets already registered to the SDL game window. It does not
register a competing raw-input device; SDL retains relative mode, focus,
buttons, and menu behavior. Presentation frames explicitly dispatch only
queued `WM_INPUT` messages before and after the SDL pump so HPL's repeated
60 Hz `SDL_PollEvent` loop cannot acquire a larger packet batch. The next fixed
`cMouseSDL::Update` still retires every delta exactly once. Meanwhile,
the rendered camera receives a temporary angular preview made from the fraction
of the latest authoritative camera command not yet represented by interpolation,
plus still-pending mouse motion. Pending raw counts are sensitivity-scaled in
floating point for presentation instead of reproducing HPL's fixed-tick integer
rounding on every rendered frame. The authoritative player-look call replaces
only the fractional error from HPL's `RoundToInt(raw * sensitivity)` handoff;
state gates, look-speed effects, camera limits, and every other command
component remain engine-owned. HPL's pitch/yaw setters retain camera limits,
and the complete authoritative camera/cache state is restored after rendering.
The same rigid render-space correction is composed into the interpolated hands
root, active hand object, and parent-validated light, billboard, and particle
children, keeping the lantern group aligned with the responsive camera.
The same feature also updates menu, journal, and inventory software cursors at
presentation rate. The plugin observes `cGui::SendMousePos` at the normal fixed
update, then advances the focused GUI immediately before HPL broadcasts
`OnDraw`, using that absolute position plus only the native counts still pending
before the next tick. This ordering lets the standard main- and pause-menu
cursor enter HPL's GUI render queue with the presentation position. Already
presented relative movement is removed from the following fixed GUI message,
preventing doubled hover/drag motion. Clicks and all other actions remain
fixed-update-owned; no cursor history or GUI update loop is introduced.
HPL keeps relative SDL input enabled for gameplay and inventory, so those
states use queued native raw counts. Main and pause menus explicitly disable
relative mode; there the plugin samples the OS cursor directly at `OnDraw` and
converts its screen position to the game client area. This preserves native
desktop pointer acceleration instead of trying to reconstruct it from raw
counts.
When `HandsPoseInterpolation=true`, the active hands mesh also receives its
completed-tick affine skin-palette interpolation. Active hand-object submeshes
retain independent final-model histories, covering attach-bone animation and
the lantern's fixed-tick procedural sway without advancing animation or sway
logic during rendering.
Normal steady gameplay should report zero `multiRead`, `invalid`,
`threadMismatch`, `mergeOverflow`, `previewApplyFail`, and
`handsCorrectionFail`. A state transition in which HPL issues no camera command
may report `commandMissing`/`previewNoCommand`; the overlay fails closed for
those frames.
Menu intervals may report native `overwrite` events because gameplay stops
retiring relative samples. If native packets are unavailable, acquisition
fails closed to the prior SDL accumulator path. On the native path, HPL's
accumulated SDL relative value is kept only as a tick-by-tick diagnostic
cross-check; individual packet counts are the sole movement source. This
prevents `SDL_PollEvent` on the 60 Hz update from collapsing one tick of reports
into a single presentation frame. Normal logs show `rawTap=installed`,
`rawActive=yes`, near-zero `rawTickMismatch`, and nonzero
`rawDirectDrain=<calls>/<messages>` while moving; `presentXY` and `deliveredXY`
match apart from interval boundaries. The fallback path retains the earlier
`deferredXY` conservation contract. During menu or inventory motion,
`guiPresent` should approach `OnDraw`, `guiMoves` should be nonzero, and
`guiThreadMismatch`/`guiInvalid` should remain zero.
`guiModeSamples=relative/absolute` identifies which presentation source was
used; inventory should favor relative samples and main/pause menus absolute
samples.

`[Telemetry] MotionTrace=true` is a diagnostic-only camera probe. It publishes
fixed-size records from the render thread into a bounded single-producer ring;
the telemetry thread writes them in batches to `AmnesiaTDD.FPSFix.motion.csv`.
Each row contains QPC time, tick/presentation serials, raw HPL timer phase,
measured phase headroom and final alpha, authoritative,
interpolated, and final rendered camera poses, the native packet delta acquired
on that frame (or SDL fallback delta), tick-acquired fallback motion deferred
for preview, pending raw motion, and the
completed-command and pending-input preview components. Normal configurations
leave it disabled. A nonzero `dropped`
counter invalidates a trace but does not alter gameplay behavior.
Hooks are installed only after all required in-memory function signatures and
named imports pass; any mismatch fails closed. Set both telemetry and all
behavioral features to `false` to load only the configuration/bootstrap layer
with no engine hooks or compatibility scan.

`[Features] RenderUnlock=true` clears `cEngine::mbLimitFPS` as `cEngine::Run`
begins. This may render as fast as the machine allows: enable VSync or use an
external frame cap. `TargetFPS` and the built-in presentation limiter are not
implemented yet.

The presentation clock is tested at exact and non-integral rates from 60 to
360 Hz. It retains only the immediately previous and current completed 60 Hz
states. Alpha comes from HPL's `cLogicTimer` residual, with QPC used only to
provide sub-millisecond progression on the same SDL time base. Because HPL
tests the fixed-update deadline earlier in the outer loop than it renders, the
phase receives headroom equal to 55% of the measured render interval. This
centers the target within the scheduler window while keeping it inside the
available pair without adding a third state or predicting beyond the current
state. Skipped captures, stalls, invalid timer layouts, and discontinuities
fail closed.

`[Features] CameraInterpolation=true` follows `cScene::mpCurrentListener`, the
same authoritative viewport HPL uses for the gameplay listener. The viewport
must be visible and have a non-null world and Euler-mode camera; its `active`
flag is deliberately not a render gate because `cScene::Render` itself checks
visibility instead. Position is linearly interpolated and orientation uses
normalized shortest-arc quaternion interpolation. The pose is applied only
around `cScene::Render` through a source-mapped camera-field/cache substitution;
the authoritative pose and every cache block dirtied by the override are then
restored byte-for-byte. The attachment node and previous-frame matrices are
never modified. Listener visibility, camera/world changes, clock resets,
same-tick mutations, invalid values, large teleports, or large angular
discontinuities snap and rebuild history instead of being interpolated.

`[Features] HandsRootInterpolation=true` snapshots the exact world root already
computed by `cLuxPlayerHands::PostUpdate` and evaluates it on the camera's
presentation timestamp. At `cSubMeshEntity::GetModelMatrix`, only submeshes
owned by the current hands entity or the current hand object's attached mesh
entity receive
`interpolatedRoot * inverse(authoritativeRoot) * originalModel`. Returned
matrices live in fixed plugin storage for the render scope; HPL transform,
callback, child-dirty, bounding-volume, physics, and animation state are never
written. Direct hand-object child renderables are handled separately: bounded
billboard, particle-system, and light vectors are enumerated from the active
object, every candidate must report the attached mesh as its entity parent,
and its existing world-matrix cache receives the same root correction only for
the scene-render scope. Those cache bytes are restored immediately afterward;
color, fade, flicker, particle simulation, and transform-tree state remain
authoritative. Hands helper/entity changes, invalid rigid transforms, clock resets,
missing pairs, and large discontinuities rebuild history instead of blending
across a coordinate-space transition. Draw/idle/holster states and held-object
changes preserve root history because they share that coordinate space. The
render owner is selected from the active object in the hands object's bounded
list, so a holstering item remains covered after `mpCurrentHandObject` clears.

The experimental `[Features] HandsPoseInterpolation` path snapshots the already-evaluated
`mvBoneMatrices` palette after `cMeshEntity::UpdateGraphicsForFrame`; it never
calls `cMeshEntity::UpdateLogic` or advances an animation. Compatible final
skinning matrices are interpolated affinely on the same presentation clock as
the camera and hands root. This preserves the mild bind-derived scale/shear in
the shipped hands palette and linearly blends fully skinned endpoint vertex
transforms. The
temporary palette is visible only while `cScene::Render` skins the hands
submeshes, then the authoritative bytes are restored. Mesh replacement, vector
relocation, bone-count change, missing tick pairs, malformed matrices, and large
per-bone discontinuities fail closed and rebuild history. This first slice
covers the main first-person hands mesh; attached lantern/item skeletons remain
authoritative, while the separate world-pose path handles Grunts.
The palette is prepared directly at the scene boundary through the mapped,
cache-aware graphics update; no global mesh-update detour remains in the render
hot path. It remains disabled in the default INI: the hands animation used for
the first test is too subtle to establish a useful visual improvement, and an
enemy-pose target has not yet been validated. The controlled pose-off A/B did
not remove the observed judder; disabling world-root discovery did, so the
affine palette path was not the cause of that performance regression.

`[Features] WorldRootInterpolation=true` allowlists dynamic physics props
(`mbStaticPhysics == false`) across all known Lux prop subtypes, including
`Object`, `MoveObject`, hinged/slider furniture, doors, and chests, plus Grunt
enemies. Newton-driven `StaticProp` assets have no live Lux wrapper in the
shipped game; otherwise-unowned moving bodies with attached render children
are therefore adopted directly from the transform callback. Known Lux props
and enemies always replace this provisional identity when available. Props may
contain multiple Newton bodies; each body has an independent history;
a bounded `iEntity3D` parent walk maps every rendered submesh to the body that
actually drives it. Grunt history follows the character-driven mesh root.
Candidates are staged after the source-mapped `iLuxProp::OnUpdate` and
`iLuxEnemy::OnUpdate` boundaries, then their completed roots are read only after
global `PostUpdate`. During rendering, matching submeshes receive the same
`interpolatedRoot * inverse(authoritativeRoot) * originalModel` composition as
the hands path. The implementation uses 512 fixed body/root histories, admits
at most 32 bodies per prop, walks at most 16 parent links, and has 512 fixed
render-result slots; only roots that moved during the latest tick enter render
preparation. A fixed, allocation-free owner hash rejects unrelated scene
submeshes in constant time; the body-slot scan is reserved for ambiguous
multi-body props. A second fixed hash resolves body histories during simulation,
and source-matched Newton sleep-state filtering preserves one final endpoint
when a body sleeps. Sleeping props then use an O(1) cached path with a periodic
lifecycle check; the source-matched Newton transform callback immediately wakes
the relevant body without polling the whole prop population. It never calls
physics or entity transform setters.

HPL2 may represent an identity submesh model matrix as a null pointer. The
world-root hook preserves that contract and composes a body correction against
identity only after the submesh has been matched to an eligible prop or
standalone body.
Telemetry reports `propParentModels` for hierarchy-resolved multi-body draws
and `propNullModels` for this null-as-identity path, separately from aggregate
`propModels` and fail-closed `parentMiss`/`invalid` counts. Articulated
submeshes are first resolved through an exact render-ready body-parent hash,
even when their mesh owner also matches the top-level Lux prop;
`propDirectModels` reports that preferred path without scanning the global body
registry in the render hot path. Mesh-owner matching remains a fallback for
renderables without a direct body parent.

For multi-body props whose rendered child does not expose that source-level
body relationship in the GOG runtime, a final-model fallback records the
authoritative rigid submesh matrix once per completed tick and interpolates it
on the same delayed presentation clock. Submeshes are enumerated from the
confirmed `cMeshEntity` pointer vector rather than inferred from render-callback
owner fields. It uses a fixed 512-entry hash with eight bounded snapshots per
visible submesh—the same retention as the presentation clock—never changes
physics state, and cannot admit
single-body props or unrelated scene geometry. Exact current-serial history is
also sufficient to activate the render path when a joint-driven child changes
without placing any body root in the temporary moving-owner index. Confirmed
multi-body props continue final-model capture while their Newton bodies sleep,
because joint-driven drawer and hinge state can still change in that condition;
ordinary single-body sleeping props retain the cached fast path. Telemetry
reports eligible `multiOwners`, `modelCandidates`, `modelCaptured`,
`modelInvalid`, and `modelCapacity`, ready `modelReady`, moving-pair
`modelMovingReady`, plus successful
`propHistoryModels`, so this runtime-specific path can be validated without
conflating it with the preferred body-root paths.
Every interpolated layer uses the same adjacent previous/current serial pair.
If HPL performs several catch-up updates before rendering, each new capture
evicts the older endpoint and only the final two remain. The logic timer then
supplies the accumulator fraction between those endpoints; the clock never
selects an older pair and never alternates with an authoritative fallback merely
because no future state exists.
Diagnostic builds also report `modelHistSeen` and the mutually exclusive
`modelHistBody`, `modelHistOwnerInvalid`, `modelHistOwnerMiss`,
`modelHistNotReady`, `modelHistStale`, `modelHistClock`,
`modelHistGeneration`, `modelHistPair`, and `modelHistInvalid` gates. Applied
moving histories are further divided into `modelHistMovingSame` when the
interpolated result matches the engine's original render matrix and
`modelHistMovingChanged` when the plugin returns a materially different
matrix. These identify whether the captured final-model path is genuinely
moving and changing the renderer input, in addition to why a history was not
substituted at render time.
For a moving final-model child that has no validated body-parent route, the
plugin also replaces the child's cached world-matrix bytes for the duration of
`cScene::Render`. HPL render lists retain a raw pointer to this cache and may
not call `GetModelMatrix` again each presentation. The authoritative bytes are
restored immediately after rendering; `modelCacheApplied`,
`modelCacheInvalid`, and `modelRestoreFail` report this scoped path.

The `0.7.3-dev` live test validated this cache architecture at approximately
119.1--120.1 presentations per second with a 60 Hz simulation. Camera motion
and active props stayed smooth, and a prop left sleeping while other objects
were manipulated woke, interpolated, and could be thrown again correctly.
Steady five-second intervals served roughly 101,600--102,800 sleeping-prop
visits from the cache while performing only about 7,000--8,200 full prop
validations; transform notifications had no overflow, and world apply, parent,
capacity, and render-slot failure counters remained zero.
Object ID, mesh, parent-node, active/static state, clock generation, missed
ticks, invalid transforms, and motion above 0.75 metres or 45 degrees per tick
are fail-closed identity/discontinuity gates. Enemy skeletal pose remains at the
authoritative 60 Hz cadence unless `[Features] WorldPoseInterpolation=true`.
World-root culling at frustum boundaries remains an explicit live-test gate
because HPL's
authoritative bounding volumes are not mutated.

The `0.8.13-dev` live test validated the retained final-model cache path across
desk hinge doors, ordinary drawers, throwable drawers, and wardrobe doors.
All were visibly smooth at approximately 119.3--120.0 presentations per second
over a roughly 60 Hz simulation, alongside smooth camera motion, chairs and
other throwable props, and a Grunt. Active intervals applied 705--2,808 moving
model-cache overrides with zero invalid-cache or restoration failures. World
apply, parent, capacity, render-slot, and callback-overflow failure counters
also remained zero.

`[Features] WorldPoseInterpolation=true` enables bounded skeletal-pose
interpolation for active Grunts and physics-backed articulated props such as
ragdolls. `[Features] RagdollPoseInterpolation=false` can disable only the
articulated-prop class for diagnostic isolation; it defaults to `true` when
omitted and has no effect unless world-pose interpolation is enabled. At the
scene boundary, each target's already-evaluated affine skinning palette is
captured and sampled on the shared presentation clock. Temporary palettes are
restored byte-for-byte after `cScene::Render`; animation logic, events, bone
nodes, and transform trees are never advanced by the plugin. Identity
replacement, stale targets, invalid palettes, missing tick pairs,
discontinuities, and capacity exhaustion fail closed.

`[Features] ParticleInterpolation=true` smooths visible particle positions
without advancing particle simulation. HPL already retains `mvLastPos` and
`mvPos` for every live particle. While each emitter builds its render vertices,
the plugin temporarily exposes their linear presentation-clock midpoint and
then restores every current position byte-for-byte. Emission, lifetime,
collision, velocity, color, size, spin, and all fixed-update behavior remain at
60 Hz. Pointer/vector/layout validation, non-finite data, more than 4,096 live
particles in one emitter, or an unavailable presentation sample fail closed for
that emitter. Telemetry reports emitter calls, applied emitters and positions,
inactive samples, invalid layouts, capacity skips, and restoration failures.

`[Features] ImageTrailTiming=true` corrects the temporal accumulation used by
the startled/low-sanity image-trail effect. HPL's stock calculation divides the
trail amount by the render-frame interval, which makes the previous image decay
progressively more slowly above 60 Hz. The plugin converts the stock 60 Hz
retention into an equivalent weight for the current render interval, temporarily
exposes the converted amount only while `cPostEffect_ImageTrail::RenderEffect`
runs, and immediately restores the gameplay-controlled amount. The effect's
fixed-step fade, radial blur, and all other post-processing remain unchanged.
Invalid amounts or frame intervals fail closed to the original renderer.
Telemetry reports effect calls, corrected and unchanged draws, accumulation
clears, invalid samples, nested calls, and restoration failures.

The presentation-rate mouse preview also detects camera commands that HPL has
intentionally transformed after reading raw input. In particular, the
low-sanity camera sway averages up to 30 fixed-tick look samples, so bypassing
that filter with fresh raw counts made the view alternate between a direct
preview and HPL's filtered result at 60 Hz. While such a transformed command is
active, the plugin suppresses only the not-yet-simulated raw-count preview and
continues interpolating HPL's authoritative filtered camera deltas. Direct
preview resumes after twelve consecutive compatible movement samples; idle
samples cannot change the gate. This preserves the original sanity effect and
keeps fallback latency bounded to one fixed tick.
The direct angular scale is classified before it can be updated and remains
frozen for the entire transformed interval. Consequently the filtered output
cannot move the recovery reference away from the genuine raw-input scale.
Sensitivity and pitch-inversion changes do not invalidate this normalized yaw
scale; their current values are applied separately when predicting each axis.

## Research plan

See `research/docs/amnesia-tdd-high-frame-rate-plan.md` for the evidence gates,
hook strategy, interpolation architecture, validation matrix, and packaging
requirements.
