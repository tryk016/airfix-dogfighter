# Controlled aircraft health-gauge capture

## Purpose and status

This is the bounded x32dbg protocol for confirming the live producer and
consumer of the recovered AirCraft health-gauge value. Static reconstruction
and private D3D11 drawing do not depend on this experiment. No dynamic result
has been accepted yet.

The goal is only to confirm that the AirCraft refresh path updates field
`+0x544` from raw actor health `+0x98`, and that the HUD later consumes the same
object and stored value. Do not inspect unrelated process memory.

## Safety boundary

- Work on a disposable copy of the original installation.
- Run the game and `release/x32/x32dbg.exe` as an ordinary user, never as
  administrator.
- Keep network, updates, plugins, symbols, crash reporting, and external URLs
  disabled.
- Use hardware execution breakpoints. Do not insert software breakpoints,
  patch code or memory, save a modified executable, or attach trainers.
- Do not save dumps, traces, databases, screenshots, logical paths, or original
  assets in Git. Record only the bounded hexadecimal fields listed below.
- Close x32dbg and discard its workspace after copying the small observation
  table to ignored local storage.

The exact supported `AirCraft.type` SHA-256 is
`9745842bde40406390b46f935e9f12d6626675f89a8fbb303cb135d76cf7dd1e`.
Stop if the working-copy hash differs.

## Static contract to test

The refresh path at AirCraft RVA `0x00002F60` computes:

```text
candidate = f32(rawHealth * 0.05 + previousDisplayedHealth * 0.95)
displayedHealth = candidate < 0 ? +0.0 : candidate
```

The exact coefficient bits are `0x3D4CCCCD` (`0.05f`) and `0x3F733333`
(`0.95f`). The HUD later reads `displayedHealth` at RVA `0x00006F90` and calls
`GetMaxHealth` before RVA `0x00006FA8`.

## Phase A breakpoints

x86 has four hardware breakpoint slots. Resolve each address as the loaded
`AirCraft.type` module base plus RVA and use hardware execute breakpoints:

| Site | RVA | Pause occurs | Bounded values to record |
|---|---:|---|---|
| `health.before_store` | `0x00003BDC` | before `FST [EBP+0x544]` | thread ID, `EBP`, DWORD `[EBP+0x98]`, DWORD `[EBP+0x544]`, x87 `ST0`, CW, SW |
| `health.after_store` | `0x00003BE2` | after the non-popping store | thread ID, `EBP`, DWORD `[EBP+0x544]`, x87 `ST0`, CW, SW |
| `hud.read_displayed` | `0x00006F90` | before `FLD [ESI+0x544]` | thread ID, `ESI`, DWORD `[ESI+0x98]`, DWORD `[ESI+0x544]`, CW, SW |
| `hud.max_health_return` | `0x00006FA8` | after `GetMaxHealth` returns | thread ID, `ESI`, DWORD `[ESI+0x544]`, DWORD `[ESP+0x3C]`, x87 `ST0`, CW, SW |

Record object registers only as opaque equality tokens such as `owner-A`; do
not publish raw process addresses. Record all float fields as eight hexadecimal
digits, not locale-dependent decimal text. `ST0` may additionally be recorded
as the debugger's exact 80-bit hexadecimal value when available.

## Observation sequence

1. Launch one single-player mission and let the aircraft settle at full
   health. Record one complete producer pair and the following HUD pair.
2. Resume without changing breakpoints. Cause ordinary in-game damage without
   memory or file modification.
3. Record at least five consecutive producer pairs and every interleaved HUD
   pair for the same aircraft.
4. Stop after at most 32 total breakpoint hits. Do not continue into unrelated
   gameplay or network activity.
5. If `EBP` and `ESI` do not identify the same owner, if the thread changes, or
   if the HUD read precedes every observed producer update, label the result
   `NO-GO` and stop.

Use this path-free table shape in ignored local notes:

```text
sequence | site | thread | owner | raw_bits | old_display_bits |
new_display_bits | scaled_health_bits | max_health_st0 | cw | sw
```

Fields not applicable to a site are `-`. Do not include module bases, full
addresses, paths, dumps, or screenshots.

## Acceptance

The observation is sufficient for the next producer-integration decision only
when:

- every before/after pair retains one thread and owner;
- the after-store DWORD equals the x87 candidate rounded by the live policy;
- the HUD owner matches the producer owner;
- the HUD DWORD equals the most recent completed stored value for that owner;
- `[ESP+0x3C]` at the max-health-return site equals displayed health multiplied
  by the recovered `100.0f` operation; and
- CW/SW are reported rather than inferred.

This experiment does not authorize simulation wiring by itself. It does not
establish damage-event ownership, raw-health mutation order, interpolation
between multiple aircraft, multiplayer behaviour, or bit-identical ARM64
arithmetic. Those remain separate decisions.
