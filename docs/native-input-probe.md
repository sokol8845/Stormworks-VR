# Read-only native input observation

Aktualizacja 2026-11-02: adresy uzywane przez implementacje przeniesiono na
Steam build 26294447. [Zakres portu i dowody](game-profile.md). Powiazane
starsze zrzuty analizy zachowuja adresy i hash swojego oryginalnego EXE.

`install_native_input_probe(base, sha_matched, absolute_trace_path)` observes the
native input queue and dispatcher. `SWVR_INPUT_PROBE=1` explicitly enables it;
other values leave it disabled. This module emits no key/action events, allocates
no native objects, never pushes a queue, and writes no game object fields. Its
two hooks call the original functions exactly once and preserve all arguments,
the pop boolean result, and C++ exceptions. It can coexist with `native_hand_ray`,
whose hooks use different addresses.

The caller initializes MinHook outside DllMain, verifies the executable's full
SHA, installs the probe before enabling hooks, and keeps its module loaded while
hooks can run. The probe only creates **disabled** hooks and rolls back its own
partial installation; it does not initialize, enable, or tear down global hooks.

## Version and call sites

Supported SHA-256:
`c38c03ff756b4f42bbc9f11cdbb034977ad0f968e45741c5cf410732f22a338d`.
Addresses below are RVAs. Both entry prefixes and all six five-byte CALL
instructions are checked in addition to the caller-supplied full-SHA comparison.
Bytes were read from `artifacts/stormworks-c38c03ff756b.exe`, and checked against the source guards; see the profile migration report. The
input probe was subsequently enabled during the partial Quest Gameplay trial
on 2026-11-02. This does not validate every pause/UI/mode gate; see [status](status.md).

| Hook | Native ABI | Eligible return RVAs |
| --- | --- | --- |
| `694230` | `bool(void* queue, void** event_slot)` | first `86A728`, `86CCEF`; continuation `86A77C`, `86CD4E` |
| `790850` | `void(controller, uint64 arg2, uint64 arg3, uint64 arg4, uint64 arg5, uint64 arg6, event)` | `86A76C`, `86CD3D` |

Dispatch CALL at `86A767` has bytes `E8 E4 60 F2 FF`; CALL at `86CD38` is
`E8 13 3B F2 FF`. Both resolve to `790850`; their **return addresses** are five
bytes later. Unrecognized callers pass directly to the original, without reading
the queue, event slot, context objects, controller, or event. No TLS scope or
cached pop context is reused for later dispatch: the pop frame has already ended.

## Scope of each observation

For a known pop, the owner is derived from the current `queue-CE6B8`. For a known
dispatch, argument 5 minus `CE5D0` must equal argument 6 minus `CE600`, deriving
the current owner independently of pop. Before following further pointers:

1. Owner vtable must equal base + `B0F4A0`.
2. Read owner `CE190` (simulation) and `350` (active state); simulation must be
   nonzero and active must equal simulation **before dereferencing simulation**.
3. Simulation vtable must equal base + `B1A988`.
4. In dispatch, argument 1 must equal simulation + `38`.
5. Dispatch argument 2 must be nonzero with application-data vtable base +
   `B17CC0`, before reading its benchmark byte `233`.

A failed relation records its stage and stops following that chain. Each
before/after snapshot validates the live arguments again. The probe does not
retain native pointers for later dereferencing; numeric addresses in a log are
measurement values, never reusable capabilities. It does not read queue counts,
capacity, storage, synchronization objects, or call a UI handler as a predicate.

Pop copies the current slot value and recognized event fields before and after
the original. A failed pop can leave the previous event in its slot; the probe
preserves and records that behavior. Dispatch copies these live values:

- controller bytes `494`, `49C`, `49F`, `4BC`, `4BD`;
- controller byte `4BF`, named `right_action_active`, which is shared native
  action-20 state and is not identified exclusively with physical mouse input;
- application `233` (benchmark), owner `CDE80`, and pause UI pointer `CE150`;
- pause UI vtable, and panel int `58` only for exact vtable base + `B07C10`;
- simulation pointers `25B0`, `1D00`, `25A8`, and byte `3010`;
- component `520`, held-right `558`, and held-left `560`.

Target objects are inspected only for their vtable. Definition pointer `58` and
definition int `DA4` (`button_type`) are read only for exact vehicle-button
vtable base + `B75658`; null definitions remain unknown. Unknown component types
are not interpreted through that layout. UI capture/hover pointers are logged
as values and are not dereferenced.

For events, exact key-action vtable `BB2110` identifies known type 23 and permits
reading action/phase ints at `8/C`. Exact mode-event vtables `B59CF0`, `B666B0`
identify types 1, 2 and permit copying the byte at `8`. Photo event vtable
`B86978` identifies type 8 only: the dispatcher does not read its payload, so
the probe does not assume that a byte exists at `8`. Unknown event
vtables are recorded without calling their virtual type function or reading a
guessed payload. The original dispatcher retains responsibility for handling
the event and its native lifetime.

## Log and limits

The installer exclusively creates `<trace>.input-native.tsv`. Each row contains
QPC, thread, caller RVA, all original arguments, first/continuation-pop marker,
boolean result where applicable, and before/after value snapshots. Schema names
include `before_`/`after_`; unknown integer fields use the header's declared
`INT_MIN` sentinel. Unknown addresses remain zero, so the accompanying relation
and vtable columns must be consulted before interpreting them.

`observed_blockers` describes the copied values: controller flags occupy bits
0–4, benchmark bit 5, nonzero/unknown pause panel bit 6, capture/active/hover
pointers bits 7–10, and zero `3010` bit 11. This is an **observation**, not a
complete production gate. Even a zero mask is labelled
`dispatch_observed_ui_unproven`; there is no `input_allowed` result. Missing or
unrecognized pause UI is labelled `pause_ui_unproven`.

The cap is 10000 rows per epoch. After the cap, original work continues through
the hooks with probe reads skipped. `flush_native_input_probe()` flushes the
stream. `bool rearm_native_input_probe_log() noexcept` appends an epoch marker,
resets its row allowance, and preserves the old records and monotonic global
`seq`. It returns false before installation or after I/O failure. The log's own
mutex protects record/epoch ordering across migrated or concurrent callers; it
does not synchronize native UI writers or turn a multi-field snapshot into an
atomic view of the game.

## Verification and remaining evidence

`swvr_native_input_probe_tests` uses fake MinHook lifecycle functions and native
originals, with executable in-memory thunks at the six exact return RVAs. Those
thunks register correct x64 unwind metadata for their stack allocations, so a
native C++ exception crosses them predictably. The initial missing unwind
metadata caused `E06D7363` during an exception case; correcting the **fixture**
resolved that failure. No production exception was swallowed or translated.

Tests cover entry/CALL/SHA/path/opt-in guards, partial-hook rollback, seven
dispatch arguments, pop return/output preservation, unknown-call passthrough,
guard-page failures at each relation, unknown component/event/pause layouts,
UI/benchmark/target types and action-20 state, before/after native transitions,
byte-for-byte absence of extra native writes, exceptions, four concurrent log
writers, cap passthrough and epoch append. Direct MSVC C++20 compilation with
`/W4 /WX /permissive-` passes, as does the standalone fixture (2026-10-29).

These tests establish the adapter contract, not full inventory/chat/modal
coverage in Stormworks. Native object validity still depends on the recognized
call's host lifetime; profile checks do not make arbitrary memory safe. A future
authorized game test must measure ordinary cockpit use, pause, chat/text,
inventory and character/mode transitions. Event ordering, hand-ray alignment,
gesture/selection identity and eventual press/release ownership remain separate
requirements. Stormworks was not launched for this implementation.

Related evidence: [native-input-queue.md](native-input-queue.md),
[hand-input-gates.md](hand-input-gates.md),
`artifacts/native-input-queue-signatures.json`,
`artifacts/cockpit-input-functions-03.json`.
