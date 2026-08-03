# EXP-20260731-080 — AFS mission-outcome bytecode boundary

## Decision

`EV-20260731-002` gives GO for a narrow, fail-closed C++20 recognizer for the
exact five-word call site used by every recovered `MissionFail` and
`MissionSuccess` call:

```text
0x1B, 0x00000001, 0x24, 0x26, descriptor-token
```

It gives NO-GO for a complete AFS VM, live process integration, or invented
ordering between script processes.

## Question

What bytecode does the native AFS compiler emit for a registered server
function, what descriptor fields does the interpreter consume, and which
smallest part can be represented without implementing the whole VM?

## Method

Static analysis used the same hashed, read-only `AfEngine.dll` working copy as
the earlier mission-outcome experiments. No executable, game, script, or GUI
was run.

Ghidra 12.1.2 remains the canonical source for:

- `AfDatabase::NewFunctionType`;
- `AfFunctionCall::SetupAfServerFunctions`; and
- the AFS interpreter.

Rizin 0.9.1 independently recovered exact boundaries and instructions for
those functions and supplied the new token/grammar-registration, compiler,
descriptor-constructor, and parameter-helper evidence. A fresh Ghidra
token/grammar/compiler export was not available in this run because its
Java/OSGi script class failed to load under the managed local profile. Those
emission claims are consequently attributed to Rizin, while native-function
registration and execution are independently supported by the existing Ghidra
reports.

A local aggregate-only corpus scanner inspected the 52 AFS members of the
verified resource working copy. It emitted counts and structural
classifications only. No script text, logical path, identifier, executable,
asset, or analysis database was added to Git.

## Findings

`SetupAfServerFunctions` registers `MissionFail` as native function `0x47` and
`MissionSuccess` as `0x48`. Each descriptor has one formal type-`2`
parameter, named `dummy` in the registration code. The parameter helper adds
one to both the formal-parameter count and argument-word count. The native
mission handler ignores the value, but the AFS calling convention still
allocates and removes that one word.

The recovered descriptor projection is:

| Offset | Meaning used here |
|---:|---|
| `+0x00` | duplicated function name |
| `+0x04` | formal-parameter count |
| `+0x08` | argument-word count |
| `+0x0C` | return/type slot |
| `+0x10` | explicit native handler |
| `+0x14` | native function identifier |
| `+0x18/+0x1C` | parameter-list links |
| `+0x20` | registry/list owner |
| `+0x24/+0x28` | function-list links |

The compiler first resolves a name through
`AfDatabase::GetFunctionTypeByName`. A registered native call compiles its
arguments in source order and then appends exactly:

```text
0x24  push current execution object
0x26  call registered native function
token descriptor token/pointer word
```

A script-function call takes a different `0x23, 0x25, token` path and is not
accepted by the portable recognizer.

The token/grammar registration maps source literal `true` to compiler AST tag
`0x137`; `false` maps to `0x138`, and `NULL` maps to `0x136`. The recursive
compiler subtracts tag base `0x12F` before dispatch. Tag `0x137` reaches the
case that appends:

```text
0x1B  push immediate word
0x01  immediate true value
```

The adjacent `false` case emits `[0x1B, 0x00000000]`, independently confirming
the immediate form. Interpreter opcode `0x1B` reads the next word, advances
the instruction pointer by two words, writes that word to the VM stack, and
advances the stack by four bytes.

The interpreter executes the native triplet as follows:

1. opcode `0x24` pushes the current execution object;
2. opcode `0x26` reads the descriptor token and advances the instruction
   pointer past the remaining two words;
3. it removes `(1 + argumentWordCount) * 4` bytes from the VM stack;
4. it uses the explicit handler at descriptor `+0x10` when present;
5. it calls that handler with the identifier at `+0x14`; and
6. it advances the stack by the handler's returned word count.

For both mission-outcome descriptors, `argumentWordCount` is one, the explicit
handler is present, and the identifiers are `0x47`/`0x48`.

The aggregate corpus result is:

| Measure | Result |
|---|---:|
| AFS members | 52 |
| outcome calls | 67 |
| `MissionFail` shape | 47 |
| `MissionSuccess` shape | 20 |
| distinct argument literals | 1 (`true`) |
| calls in `action` bodies | 67 |
| statement-position calls | 67 |
| terminal-in-block calls | 67 |

All 67 calls use the built-in literal `true`. Combining the aggregate corpus
shape with the literal registration, compiler case, native-call append, and
interpreter cases proves that every recovered outcome site has this exact
five-word shape:

```text
[0x1B, 0x00000001, 0x24, 0x26, descriptor-token]
```

The aggregate scanner published no script text, logical path, or owner-private
content.

The compiler's `action` case creates an auto-executed compiled function with a
symbolic loop:

```text
initialize action state
loop:
    yield 0.25
    evaluate condition
    branch back when false
    mark action fired
    execute body
    terminate execution
```

This bounds the call's surrounding execution model but is not a license to
implement the full scheduler or infer source-order interactions.

## Address ledger

All virtual-address ranges use an exclusive end.

| Function | Range `[start, end)` |
|---|---:|
| AFS token/grammar registration | `0x1005D0A0–0x10060AB7` |
| word-vector append | `0x10061220–0x100612A3` |
| recursive AFS compiler | `0x100612C0–0x10066A15` |
| `AfDatabase::NewFunctionType` | `0x10068310–0x10068345` |
| `SetupAfServerFunctions` | `0x100686C0–0x1006916B` |
| native descriptor constructor | `0x100698D0–0x10069929` |
| add-parameter helper | `0x100699C0–0x10069A2B` |
| AFS interpreter | `0x10069F10–0x1006AC84` |

`EV-20260803-003` later split the older aggregate append extent from padding
and a separate identity helper at `[0x100612B0,0x100612B7)`. This boundary
correction does not change the emitted outcome words or their interpretation.

The source-literal registration calls are at `0x1005D425` (`NULL`, tag
`0x136`), `0x1005D43D` (`true`, tag `0x137`), and `0x1005D455` (`false`, tag
`0x138`). The compiler subtracts tag base `0x12F` at `0x10062463`, establishes
immediate opcode `0x1B` at `0x10062468`, and dispatches at `0x1006247D`. The
`true` case stores the opcode and immediate words at `0x1006321E` and
`0x1006322F`, then reaches the append call at `0x10063255`. The native call
append occupies `[0x10066824, 0x1006684F)`. The descriptor parameter-width
switch is at `0x100699F6–0x10069A28`.

## Portable implementation

`LegacyAfsMissionOutcomeCall` recognizes only:

```text
[0x1B, 0x00000001, 0x24, 0x26, matching nonzero descriptor token]
```

The recognizer requires the exact immediate-`true` producer and native opcode
pair. The caller supplies a portable descriptor view, which must contain:

- an explicit handler;
- exactly one argument word; and
- native identifier `0x47` or `0x48`.

Success returns the existing typed `LegacyMissionOutcomeCall` and consumes
five instruction words. Every mismatch consumes zero words and returns a
specific failure status.

The implementation does not own a bytecode stream, VM stack, descriptor
pointer, scheduler, trigger, process, console, mission state, or platform
presentation. It recognizes but does not execute the argument producer. It
composes with the existing mission-outcome transition only after a successful
decode.

Synthetic tests cover both identifiers, trailing bytecode, all truncation
lengths, wrong producer opcode/immediate values, the script-call opcode pair,
wrong native opcodes, zero/mismatched descriptor tokens, missing explicit
handler, wrong argument widths, unsupported identifiers, and composition with
the existing state transition.

## Confidence

Medium (`2`) and static-only:

- descriptor field projection and one-word type-`2` parameter;
- outcome identifiers and explicit handler;
- source `true` to tag `0x137` to `[0x1B, 1]` emission;
- interpreter immediate-word behavior;
- native and script call-triplet distinction;
- interpreter stack-width calculation;
- the aggregate corpus shape and counts; and
- the symbolic `action` execution envelope.

The complete bytecode for a representative action condition, live stack
contents, multi-process order, and dynamic behavior remain unknown.

## Validation

- complete clean Windows GCC 15.2/Ninja build and `120/120` CTests;
- complete clean WSL2 Ubuntu GCC 13.3/Ninja build and `120/120` CTests;
- complete Windows MSVC 19.51/Ninja build and `120/120` CTests;
- focused WSL2 Clang 18.1 warnings-as-errors build and test;
- focused WSL2 ASan/UBSan build and test;
- aggregate corpus scanner reports 52 files and 67 structurally consistent
  outcome calls;
- exact Rizin function reports were generated from the hashed working copy;
- Rizin exporter `12/12` and PowerShell wrapper tests;
- synthetic public-boundary tests and the `617`-file repository scan;
- `330` unique function-catalog rows with fourteen columns and only the two
  known historical missing references;
- changed-document links, changed-scope local-path scanning, evidence-ID
  uniqueness, and `git diff --check`; and
- no original or derived game content was added to Git.

The read-only formatting check and hosted platform builds remain publication
gates.
