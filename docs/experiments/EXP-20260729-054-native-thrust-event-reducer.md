# EXP-20260729-054: native thrust-event reducer

- Date: 2026-07-29
- Status: implemented as an unwired typed-write decoder
- Scope: already-formed native `THRUST_SET` and `THRUST_APPLY` events

## Question

Can the statically confirmed `AfVehicle::ProcessEvent` writes feeding the
slot-45 thrust transition be preserved in portable C++20 without inventing the
still-unknown Q15-to-event timing or coupling the reducer to a scheduler?

## Evidence boundary

`AirCraft::ProcessEvent` at `0x10008530` delegates both relevant events to
`AfVehicle::ProcessEvent` at VA `0x1001B6C0`, RVA `0x1B6C0`. The payload is a
signed native `int32` at packed event offset `+0x11`.

| Event | Native field | Recovered operation |
|---|---|---|
| `0x63 THRUST_SET` | `targetThrust`, `+0x444` | `payload * f32(1/255)` |
| `0x64 THRUST_APPLY` | `thrustApply`, `+0x440` | `payload * f32(0.02) * f32(1/255)` |

The inactive latch at `+0x460` is tested before the payload conversion and
field store. A successful nonzero result then clears both DWORDs of the signed
64-bit rest duration at `+0x458/+0x45C`.

The native storage type permits any signed `int32`. Recovered command bindings
and the AI control map establish `[-255, 255]`; the analog formula has no local
clamp and its upstream raw-axis domain is not yet independently proven. The
portable decoder conservatively admits `[-255, 255]` and rejects other active
values. This is a fail-closed reconstruction boundary, not a claim that the
native dispatcher contains the same range check or that every producer is
fully bounded. A recognized inactive event remains an accepted no-op even when
its payload is outside the admitted range, because the native branch does not
inspect that payload.

## Portable contract

`legacyAircraftDecodeNativeThrustEvent` returns either:

- one `LegacyAircraftThrustWrite` selecting exactly `targetThrust` or
  `thrustApply`;
- `ignoredInactive`, with no write;
- `unsupportedEvent`, with no write; or
- `payloadOutsideEvidenceRange`, with no write.

The write owns only the selected field value and a `clearRestDuration`
directive. It does not own the thrust state or the rest counter. The separate
simulation-thread-confined control-event state owner now commits the typed
write and optional rest clear together, but remains unwired from live event
producers and scheduling.

An active zero payload is still a decoded write, because it can release a
persistent apply value. It does not clear rest duration. Repeating the same
nonzero value still requests a rest clear; the decision follows the decoded
value, not whether the destination field changed.

## Numeric policy

Recovered binary32 constants are:

```text
1/255f = 0x3B808081
0.02f  = 0x3CA3D70A
```

The native x87 path retains wider intermediates until the final field store.
The portable implementation performs the bounded calculation in `double` and
casts to `float` once. Exact tests include the distinguishing cases:

```text
THRUST_APPLY(249) = 0x3C9FFC25
THRUST_APPLY(255) = 0x3CA3D70B
```

Sequential binary32 rounding would instead produce `0x3C9FFC26` for payload
`249`; simply returning the `0.02f` literal would be one ULP low for `255`.
These vectors protect the recovered store behavior without claiming complete
x87 parity for the flight law.

## Deliberate exclusions

The decoder has no:

- `InputFrame`, Q15 mapping, device, or command binding;
- input clock, event queue, timestamp, or sample-and-hold policy;
- 12 ms scheduler or sleep-step invocation;
- target add/clamp/smoothing;
- health, engine-start, force, torque, collision, or rigid-body state;
- pose, camera, audio, renderer, or backend dependency.

It does not consume or clear `thrustApply`. Last-write-wins ordering and the
number of slot-45 calls between apply and release events remain the
responsibility of a future trace-driven runtime.

## Tests

Synthetic tests verify:

- exact binary32 vectors for both events at
  `-255, -249, -127, -1, 0, 1, 127, 249, 255`;
- all 511 admitted values in `[-255, 255]` against an independent
  integer-product/RNE32 oracle, plus monotonicity, sign symmetry, selected
  field, and rest-clear predicate;
- active rejection of `-256`, `256`, and signed `int32` extrema;
- inactive acceptance for ordinary and out-of-range payloads before payload
  validation;
- rejection of both misleading `THROTTLE_*` values and another unsupported
  event before the inactive gate;
- zero release, same-value replacement, explicit last-write-wins sequences,
  and typed single-field composition;
- composition with the separate slot-45 helper without moving clamp or
  smoothing into the event decoder; and
- portable `noexcept` and trivially-copyable result properties.

No original executable, resource, logical name, checksum, trace, or private
path is needed by these tests.

## Acceptance rule

This experiment accepts only the typed native-event write. It does not claim
the 60 Hz-to-12 ms timing join, a complete control reducer, moving flight,
rendered movement, or playable behavior.

## Related

- [Isolated slot-45 thrust control](EXP-20260729-053-isolated-slot45-thrust-control.md)
- [TURN_SET and control-event state owner](EXP-20260730-061-native-turn-event-control-state.md)
- [AirCraft controls and scheduler](../re/systems/AIRCRAFT-FLIGHT.md)
- [AirCraft flight law](../re/systems/AIRCRAFT-FLIGHT-LAW.md)
