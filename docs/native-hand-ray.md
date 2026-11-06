# Experimental native hand pointing

Aktualizacja 2026-11-04: dodano wybór lewej/prawej ręki oraz natywne Q/E.
Certyfikat zawiera akcję; pola zapisanego hitu, trzymania i zwolnienia są
wybierane dla danej ręki. [Aktualny opis i dowody](left-hand-q.md).
Poniższy opis zachowuje również historyczny etap prawej dłoni.


Aktualizacja 2026-11-02: adresy uzywane przez implementacje przeniesiono na
Steam build 26294447. [Zakres portu i dowody](game-profile.md). Powiazane
starsze zrzuty analizy zachowuja adresy i hash swojego oryginalnego EXE.

Later on 2026-11-02, an authorized Quest Gameplay trial recorded 5766
post-start `hand_ray` rows, including 2595 nonzero hovered-target samples.
The run ended early after a 1x1 target change, so it did not pass the full
120-second experiment. No button actions were enabled. See [status](status.md).

`install_native_hand_ray(base, sha_matched, absolute_trace_path, bridge, callbacks={})` adds an
optional pointing input to the measured Stormworks target-selection function.
It does **not** create button presses, releases, actions, key events, or network
messages. It calls the original native selection once and lets that function
compute its target normally. No target pointer or held-object field is written
by the backend.

This stage is off by default. `SWVR_HAND_RAY=1` opts in; all other values, including
long strings, leave it disabled. The caller must compare the executable SHA to
the supported build, initialize MinHook outside DllMain, install this backend,
and enable hooks afterwards. Installation verifies both 24-byte entry signatures
and creates **disabled** hooks at `74F260` and `4E33A0` only. It never initializes,
enables, or uninitializes MinHook itself. Partial installation removes only its
own successfully created hooks. The parent must choose this backend **or**
`interaction_probe`, since both intercept the same two native functions.

The `HandInteractionBridge` reference must outlive enabled hooks. Parent wiring
uses its default 100 ms freshness policy. This backend is not designed for a
custom shorter freshness deadline hidden inside a differently configured bridge.
Its public header is `include/swvr/native_hand_ray.hpp`; implementation is
`src/native_hand_ray.cpp`. `native_hand_ray_status_name` reports installation
outcomes and `flush_native_hand_ray` flushes diagnostics.
`rearm_native_hand_ray_log()` appends a fresh diagnostic epoch; see below.

## Native call transaction

The guarded Selection ABI is six integer/pointer arguments:

```cpp
void(void* controller, uint64_t context2, uint64_t context3,
     uint64_t context4, uint64_t context5, const int* context6);
```

Only return RVA `745A24` is eligible. One immutable `bridge.inspect()` snapshot
is bound for the Selection call; `consume()` is never used, so pointing does not
steal pinch transitions from a future action consumer. An unrecognized nested
Selection shadows the outer transaction instead of borrowing its ray.

The Origin ABI is:

```cpp
void*(void* character, double* output, uint8_t mode, uint8_t option);
```

The backend calls the original first and always returns its exact pointer result.
It considers an override only inside the eligible TLS Selection transaction and
at return RVA `74F38D`. A null output or a returned pointer different from output
uses the original data unchanged. Forward is not changed before Origin: early
UI/death/native returns do not leave a speculative direction override behind.

At the use point it checks the bound snapshot's age against the steady clock,
then re-inspects the bridge only for validity, space revision and cancellation
generation. Focus/tracking loss or reset during Origin invalidates the bound
pose. A newer valid sequence with the same generation/revision does not replace
the pose halfway through native selection; it takes effect next Selection.
There is necessarily a point-in-time validity boundary: a loss after these checks
cannot undo a target already being computed without another native integration.
This backend dispatches no action based on that target.

## Selection completion observer

An optional `NativeHandRayCallbacks{context, selection_complete}` supplies a
`noexcept` observer for integration with the action backend. Existing
four-argument installer calls remain source compatible and install no observer.
Callback state is published only after a successful installation; a failed or
already-installed result does not replace it. Its function and context must
outlive enabled hooks.

The observer runs only at the known return RVA `745A24`, only for a top-level
Selection (`scope.previous==nullptr`), after the original has returned and after
the exact 12 forward bytes have been restored. It runs before leaving the live
Selection scope, while the caller's arguments remain valid. It receives:

```cpp
struct NativeHandSelection {
    void* controller_live;
    uint64_t a2, a3, a4, a5;
    const int* a6_live;
    HandInteractionSnapshot sample;
    uintptr_t target_token;
    bool used_hand_ray, completed, nested_selection;
    unsigned origin_calls;
};
```

`sample` is the immutable snapshot bound at Selection entry, including sequence,
gesture identity, calibration/cancellation generations, world ray and timestamp.
A newer publication during native Origin never substitutes that sample.
`target_token` copies the actual live `C+520` value after the original, rather
than retaining a previous target. Native live pointers may be used only during
this callback; copied numeric tokens are for equality/identity, not permission
to dereference a saved object later.

Normal native fallback still invokes the observer with `completed=true` and
`used_hand_ray=false`, allowing the backend to invalidate a previous selection
certificate. A native C++ exception restores forward first, then invokes the
observer with `completed=false` and `target_token=0`, and rethrows the original
exception unchanged. `used_hand_ray` remains the literal record of whether the
scope accepted a hand-origin override, even if native selection later failed.

Any inner Selection marks its parent `nested_selection=true`. Inner calls,
including known callers, never invoke this observer themselves. The outer
completion still reports its own bound sample and the actual final target,
which an inner call may have replaced. An action backend must refuse a new press
certificate for that marked result. The marker preserves pointing behavior
instead of claiming the outer sample necessarily produced an inner target.

The completion callback does not consume a gesture in this module or emit an
action. The backend can choose `consume_selected(sample.sequence)` and must
independently validate completeness, origin count, freshness, current target,
gesture identity and game/UI state. Callback execution remains independent of
the diagnostic row cap or a failed log stream, and occurs after the log mutex
has been released. It is still inside the adapter's recursive Selection mutex.
The observer must obey its `noexcept` contract.

Five controller flags block the override at entry and are checked again just
before mutation:

| Controller byte | Diagnostic interpretation |
| --- | --- |
| `494` | Photo mode |
| `49C` | Paused |
| `49F` | Pause/menu |
| `4BC` | Dead |
| `4BD` | Sleeping |

At this exact Selection caller, argument 4 is the current `c_application_data`
object D. Its vtable must equal executable base + `B17CC0`, and byte `D+233`
must be zero. That byte denotes **benchmark mode**, not UI focus. A null or
unexpected object uses native fallback (`application_profile`); a nonzero byte
uses `benchmark_mode`. Both checks are repeated after the original Origin helper,
before any ray mutation. Argument 4 from an unrecognized Selection caller is
never interpreted or dereferenced. The adapter borrows the recognized object only
for this Selection; it does not cache a producer thread's context pointer.

For accepted data, output receives the hand origin as three **double** world
coordinates. Controller `AA0..AA8` receives a finite unit direction as three
floats. The exact original 12 bytes are saved and restored by RAII after
Selection, including C++ exception unwinding. Nested calls on the same controller
temporarily suspend the outer override, execute from the original native bytes,
then restore the outer override before it resumes. No controller/character/target
pointer is retained outside the known call scope. Numeric target values may be
written to the diagnostic log, but are never dereferenced by this backend.

An adapter-owned recursive mutex surrounds each enabled Selection transaction,
including the original call and final byte restoration. TLS alone is insufficient:
two concurrent calls on the same controller could otherwise save each other's
temporary direction and leave a hand ray installed afterwards. The lock serializes
all of this backend's Selection calls, while permitting nested calls on the same
thread. It does **not** synchronize arbitrary native writers elsewhere in the host.
The game's own threading/ownership assumptions still matter; this is not a claim
that all controller memory is globally protected.

## Reach policy and limits

The original head origin O is available from the native Origin helper. Hand origin
H must be finite and within radius 3 of O. Direction must be finite and normalized;
the adapter normalizes tiny representation differences before storing float
components. It does **not** scale forward to change reach.

The observed primary segment builder reads `AA0/AA4/AA8` immediately after Origin
and forms `H + 3*direction`. Direct forward-field reads occur there, but indirect
reads through later native callees have not been excluded. Furthermore, Selection
normalizes the local segment direction and uses a separate **10000-unit** endpoint
in a later branch. Scaling forward would therefore not prove a head-centered
reach limit for every selection branch.

The existing native selection and press-distance checks are left unchanged.
Pointing/hover can reach beyond a radius-3 sphere around O because the ray now
starts at H, and some existing selection branches already use larger distances.
The hand-origin check is not a claim that every hover target is within 3 units of
the head. Future action integration must retain the native press-distance guard
and verify target ownership and cancellation separately. This module contains
no action-function hook or invocation.

Mapping a render-time hand pose to the logic-time character origin on moving
vehicles remains unverified. The adapter does not guess an orientation rebase or
force the ray to fit a moving controller. Out-of-range or invalid data falls back
to the native head ray. User-facing ergonomics, highlighting and world alignment
still need a real game/HMD test when game launches are authorized again.

## Bounded diagnostics

The installer creates `trace_path + ".hand-ray-native.tsv"` with exclusive-create
semantics and never overwrites a previous file. Up to 10000 Selection records per epoch
include QPC/thread/caller, bound hand sequence and XR frame, original head origin,
used origin/direction, native target fields after Selection, the read-only source
pinch gesture ID/down state and the blocking
reason. The header declares the flags and QPC frequency. The row cap or logging
failure stops logging only; it does not suppress original native work or silently
change pointing behavior. Native fallback reasons include caller mismatch,
unfocused/stale/reset data, UI flags, unexpected Origin return, invalid geometry
and hand origin beyond the head-radius check.

`bool rearm_native_hand_ray_log() noexcept` appends an `#epoch` marker and resets
only the current epoch's row allowance. It preserves the existing file and the
monotonic `seq` column; trailing `epoch` and `epoch_row` columns identify each
record's capture epoch and position. The installer starts epoch 0. Manual VR
startup can rearm after desktop navigation has filled its allowance, so the
upcoming VR measurement is still recorded. Rearming never resets tracking,
consumes a pinch, changes the ray or touches controller memory. It returns false
before installation or after an I/O failure; it cannot revive a failed stream.

## Verification

`tests/native_hand_ray_tests.cpp` uses fake MinHook lifecycle functions and
executable in-memory caller thunks with the exact measured return RVAs. The game
is not started and executable entry points are not patched. Tests cover:

- opt-in/SHA/signature/path guards, occupied logs and partial-hook rollback;
- the six-argument Selection ABI, four-argument Origin ABI and returned pointer;
- correct double world coordinates near 1e9 and a unit, unscaled direction;
- exact forward-byte restoration after normal returns, exceptions and nesting;
- two threads attempting overlapping Selection on the same controller, with
  delayed fake native work, serialized callbacks and exact final-byte restoration;
- unexpected caller, no TLS scope and a different thread;
- all five blocking flags, new valid publication during Origin, focus loss/reset
  during Origin, cancellation followed by immediate recovery, stale entry/use;
- benchmark and application-vtable gates at entry and after original Origin,
  null application fallback, and an opaque invalid arg4 from an unknown caller;
- hand origin outside and exactly at radius 3, and nonfinite native head origin;
- native target calculation, retained pinch-consumer transitions, no action hooks;
- bounded logs while original native calls and later valid pointing continue;
- cap followed by rearm and append, preserving 10000 old records, monotonic
  sequences, and a fresh epoch counter without changing hand pointing;
- exact forward-byte restoration inside the completion observer, all live ABI
  arguments and immutable bound sample across a newer publication;
- known/top-level-only completion, fallback invalidation, exception invalidation
  before rethrow, and an inner target overwrite marked separately from ray use;
- callbacks continuing after the diagnostic cap, two migrated logic threads,
  installation not replacing callbacks, and a fresh child fixture confirming
  the original four-argument API has no observer even after a failed callback
  installation attempt.

Standalone MSVC C++20 `/W4 /WX /permissive-` compilation and these tests pass
(2026-10-29). Adding the benchmark tests first produced the expected failure
`Blocked call changed native ray inputs` on the earlier implementation. A
separately compiled negative-control copy with only the transaction lock omitted
is rejected by `Concurrent native Selection transactions overlapped`; the
production source retains the lock. The CMake `swvr_native_hand_ray_tests` target
also passed. The current trace fixture includes 10002 records across epochs 0/1.
They verify the adapter contract with test doubles, not working gameplay or
cockpit hand actions. Stormworks was not launched during those original offline
tests. The later authorized live-trial result is described above.
