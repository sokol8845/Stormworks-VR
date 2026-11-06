# Hand pose to world-ray bridge

`HandInteractionBridge` prepares a single hand's pointing ray and pinch
transitions for a later native input adapter. It makes no game-engine calls,
changes no native fields and dispatches no input or network events. The default
is the right hand; `InteractionHand::left` is an explicit alternative. These
choices do not encode Stormworks' action IDs.

## Geometry and required inputs

Publish one `XrRuntimeFrame`, its LOCAL calibration origin, and the **world-space
anchor T used by the VR camera**. The transform must precede any XR head
offset, per-eye offset or renderer floating-origin rebasing. Supplying the
already adjusted eye camera would apply tracking twice; supplying a rebased
camera would produce coordinates that cannot be compared to the native input
probe's double world origin. If rendering uses `make_head_anchor` to remove the
native look rotation/backoff, publish that same reconstructed anchor. Using the
old uncorrected camera here would make pointing disagree with the corrected VR
image. It still preserves world coordinates and is before any XR pose offset.

The test policy uses the index fingertip as ray origin and the normalized vector
from the index proximal joint to its tip as direction. The vendored OpenXR SDK's
`XR_HAND_JOINT_SET_DEFAULT_EXT` indices are index proximal **7**, index tip **10**
and thumb tip **5**. All three positions must be valid, tracked and finite. Joint
orientation flags are intentionally unnecessary for this point-derived ray.
The proximal-to-tip distance must be at least 1 cm by default. No palm or
controller orientation is substituted when that vector is missing or degenerate.

For a LOCAL point `p`, calibration `(R0,p0)` and direction `d`, the mapping is:

```text
S = diag(1, 1, -1)
p_relative = inverse(R0) * (p - p0)
d_relative = inverse(R0) * d
world_origin = T.translation + T.rotation * S * p_relative
world_direction = normalize(T.rotation * S * d_relative)
```

`S` follows the measured native-camera relation `V*T=S`. Translation and matrix
products use double precision throughout. No large world position is converted
to a float `Pose`. `T` must be finite, affine and orthonormal; both handedness
signs are accepted, consistent with the camera bridge. The output direction is a
unit vector, not a world endpoint. The later adapter decides the native maximum
reach; this module does not extend the game's 3-unit interaction ray.

This pointing direction is a deliberately simple test policy, not validated
gesture ergonomics. Curling the index finger during a pinch can change the ray;
native integration must retain the original held target independently of later
pointing motion.

## Clocks, publication and cancellation

The snapshot stores `xr_time_ns` in the OpenXR time domain, separately from
`published_at` in `std::chrono::steady_clock`. They are never subtracted from one
another. Default maximum publication age is 100 ms. This bounds cached data age;
it does not measure prediction error or headset-to-game tracking latency.

Repeated XR timestamps cannot refresh publication age or replay a gesture.
Decreasing XR timestamps are rejected until an explicit reset establishes a new
source history. A gap between publications longer than the age limit disarms
pinch detection even if the new frame is otherwise valid. A publication clock
regression is rejected as well.

Frames must be running, nonterminal, renderable and focused, with valid/tracked
head position and orientation and an active selected hand. Missing, suppressed,
unfocused or invalid samples publish an invalid ray and cancel the source
gesture. Closed fingers cannot rearm after any loss: the hand must first open
past the 4 cm release threshold, then close within the 2.5 cm press threshold.
These thresholds reuse `PinchDetector` and remain hardware-calibration policy.

`HandCalibration` carries the LOCAL origin and matching `space_revision`; a
mismatch rejects the ray. Changing the origin or reference-space revision
disarms gestures. The convenience `publish(frame, Pose origin, T, now)` overload
assumes the caller already updated that origin for `frame.space_revision`. Use
the explicit calibration overload when the revision token is available.

Call `reset(now)` on session stop, rendering suppression or game-scene changes
when no new frame will be published. It clears XR timestamp/calibration history
and publishes invalid state. An empty frame passed to `publish` also invalidates
the ray, but explicit reset additionally permits a new XR source history.

## Producer, diagnostic reader and logical consumer

All shared state is protected by one mutex. Snapshots are returned by value and
contain no borrowed XR images, joint-array pointers, game pointers or references
to mutable internal storage.

```cpp
swvr::HandInteractionBridge hands; // Default: right, 100 ms age.

// On the producer, with the frame's correct calibration and base world camera:
hands.publish(frame, swvr::HandCalibration{origin, revision}, base_game_transform);

// A render-thread diagnostic does not take ownership of gesture transitions:
const auto diagnostic = hands.inspect(); // diagnostic.pinch is always none.

// One logical input consumer; its OS thread ID may change between calls:
const auto input = hands.consume();
if (input.pinch == swvr::PinchEvent::cancelled) {
    // Later native adapter cancels its held interaction. No adapter is included.
}
// input.valid_ray gates use of input.ray; no action is dispatched here.

// After a native selection actually used diagnostic.sequence:
const auto selected = hands.consume_selected(diagnostic.sequence);
// A newer pose does not get consumed using an older selection. Release/cancel
// remain available even if this sequence does not match.

hands.reset(); // Session/scene teardown.
```

`inspect` applies freshness checks but never consumes a transition, disarms the
detector or changes cancellation generations. The no-argument inspect/consume
forms read the clock **after** acquiring the mutex. Explicit timestamps are
available for deterministic tests and already synchronized callers. If an
explicit read time precedes a concurrently published value, the returned ray is
invalid with `clock_reversal`, but that ordering race does not destructively
cancel the producer/consumer. The next properly timed read can still consume it.

`consume` deduplicates each publication sequence and maintains one logical
consumer's pinch state across OS-thread migration. Fresh levels yield began,
held or released transitions; invalid/stale data yields cancelled once for an
active consumer. A separate cancellation generation survives overwritten
invalid publications. Therefore a focus-loss frame followed quickly by a valid
open hand cannot be mistaken for a committing release when the consumer missed
the intermediate frame. Cancellation can be returned alongside a currently valid
ray; the consumer must honor the event, not infer cancellation from validity alone.

This is a latest-state bridge, not a queue of every physical gesture. A complete
short press/release between consumer reads is dropped rather than replayed as a
delayed click. Each source pinch has a monotonic `pinch_gesture_id`; normal
release preserves the ID and sets `pinch_down=false`. New poses in the same hold
preserve the ID, whereas a normal release/repress creates a new one even without
a cancellation-generation change. Invalid/stale diagnostics always report
`pinch_down=false`. Reset does not reuse old gesture IDs.

If the consumer misses a release/repress while holding the old gesture, it
receives one cancellation. The replacement gesture is suppressed until another
opening; it is never merged into the previous hold or started late. The same
rule applies when an overwritten tracking loss is followed by a new closed
gesture. `consume_selected(sequence)` emits began/held only for the exact
published sample used by target selection, without losing them on mismatch.
It still delivers release/cancellation for cleanup. A backend must separately
recheck gesture ID, cancellation generation, bound-pose age and native context
at event receipt; this API does not dispatch or authorize a native action.

Multiple independent native consumers would each need
their own transition state; calling `consume` from diagnostics would steal events.

## Verification and remaining work

`tests/hand_interaction_tests.cpp` passes standalone MSVC x64 C++20 compilation
with `/W4 /WX /permissive-`. Tests exercise:

- reflected native coordinates, rotated calibration/base transforms and a
  roundtrip through a native per-eye camera;
- sub-centimetre offsets at a world position above `1e9`;
- distinct XR/steady clocks, duplicate timestamps, out-of-order samples,
  freshness, source gaps and reference-space changes;
- hysteresis, open-to-rearm behavior, focus/tracking loss and a cancellation
  whose invalid publication was overwritten before the consumer read it;
- invalid joints/transforms, suppressed/empty frames and reset;
- diagnostic reads that do not consume, immutable copies, coherent concurrent
  publication/readback and consumer thread migration without duplicate releases;
- distinct source gesture IDs, missed release/repress cancellation, and deferred
  consumption bound to a completed selection without suppressing cleanup.

These tests use synthetic hand poses and game transforms. They do not prove
alignment with the live input ray, correct native target ownership, ergonomic
pointing, or working cockpit interaction. The optional `native_hand_ray` backend
now substitutes the ray inside a bounded native selection scope, logs the result
and restores direction bytes. It has standalone tests but has not been run in
the game. No module currently dispatches native pinch actions.
