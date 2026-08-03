# FMT-AFS — mission scripting source and compiled wordcode

**State:** source ingress, declaration classes, compiler product, and complete
runtime opcode framing are statically recovered; no parser or VM is implemented

**Evidence IDs:** `EV-20260731-001`, `EV-20260731-002`,
`EV-20260803-003`

**Endian:** little-endian on the recovered PE32/i386 implementation

**Version:** no source or wordcode version field was found

## What an AFS member is

AFS is mission-script source text, not a serialized bytecode container. The
mission loader reads the selected package member into one allocation, appends
a NUL byte, and gives the compiler only a `char*`; it does not pass a byte
length. The compiled product is database/object-owned internal state whose
words may contain live object, function, and handler pointers. Processes and
executions own only their mutable cloned variables, stack, IP/SP, delay, queue
links, and a reference to the shared compiled-function record. The wordcode is
therefore neither process-local nor a portable or persistent bytecode format.

No original script, logical member name, asset, binary, or per-file result is
published with this note. Aggregate corpus observations are reused from
[EXP-20260731-079](../experiments/EXP-20260731-079-afs-function-order.md) and
[EXP-20260731-080](../experiments/EXP-20260731-080-afs-mission-outcome-bytecode.md),
not repeated here.

## Recovered source boundary

The forward lexer, grammar parser, and recursive compiler recognize named
objects with declarations including:

| Declaration | Compiler AST tag | Startup rule | Recovered execution envelope |
|---|---:|---|---|
| `event` | `0x385` | Autoexec off; explicit named invocation only | body, then terminal opcode |
| `action` | `0x386` | Autoexec on | initialize declaration state; yield binary32 `0.25` (`0x3E800000`); evaluate condition; branch back while false; set state; body; terminal opcode |
| `timer` | `0x387` | Autoexec on | initialize declaration state; evaluate time expression; yield; set state; body; terminal opcode |

These keywords and AST tags are confirmed facts at confidence 2. The complete
source grammar, every accepted escape/comment form, diagnostic recovery, and
source-compatibility limits are not yet a stable public specification.

Source is tokenized forward. Tokens and AST children retain source order.
Compiled function records are then prepended, so a new process traverses them
in reverse declaration order and appends only `action` and `timer` executions
to its initial FIFO queue. An `event` is accepted by the separate named-call
path only when its Autoexec byte is zero.

The authenticated local corpus previously reported 52 AFS members, 114
events, 328 actions, and no timers. The absence of timers, dynamic `Call`,
`KillProcess`, or `LoadScript` use in that corpus is an observation about that
corpus, not a language restriction.

## Compiled records

A compiled object owns an intrusive list of 0x30-byte function records. The
fields used by the VM are:

| Offset | Width | Confirmed use |
|---:|---:|---|
| `+0x04` | 4 | object/function variable-word count inherited from the base layout |
| `+0x18` | 4 | execution-frame/stack allocation in words; constructor starts at one |
| `+0x1C` | 4 | heap pointer to the first 32-bit wordcode word |
| `+0x20` | 1 | Autoexec byte |
| `+0x24` | 4 | next/older compiled record |
| `+0x28` | 4 | previous/newer compiled record |
| `+0x2C` | 4 | declaration/signature metadata; name at metadata `+0x00`, argument-word count at `+0x08` |

The record exposes no stored wordcode length to the interpreter; allocator
metadata is not consulted as a code bound. After the delay check, a null IP at
interpreter entry returns safely. Once the decoder loop starts it does not
retest IP and continues until yield, terminal, or unsupported opcode; a
malformed branch to null is therefore dereferenced rather than caught.

## Wordcode framing

Every instruction begins with one little-endian 32-bit opcode word. Valid
runtime IDs are `0x01` through `0x46`; the jump table has exactly 70 entries.
Most instructions are one word. Immediate, index, target, handler, and
descriptor forms are two words. Inline text is:

```text
word 0  opcode 0x1A
word 1  payload word count n
word 2  first payload word
...
word 1+n  final payload word, including the source NUL within the payload
```

Branch operands are absolute word indices from the compiled record's
wordcode base, not byte displacements from the current instruction. Descriptor
operands in opcodes `0x25`–`0x27` are native pointers in the original process.
The complete instruction table and stack/IP effects are in
[AFS-VM](../re/systems/AFS-VM.md#complete-opcode-table).

There are no VM registers. The persistent execution state is a binary32 delay,
instruction pointer, stack pointer and allocation, function-local variable
array, process/object variable array, handler, compiled-record pointer, parent
process pointer, and intrusive queue links.

## Type and value representation

The runtime word is untagged. Compiler type checking determines how an opcode
interprets it:

| Recovered source/compiler type ID | Runtime width |
|---:|---:|
| `2` integer/boolean/object word | 1 word |
| `3` binary32 | 1 word |
| `4` retained reference/string handle | 1 word |
| `5` three-component binary32 value | 3 words |
| `6` trigger/handler-derived value | 1 word in the observed call boundary |

This table describes the recovered compiler/runtime contract only. It is not
a safe serialized schema. A portable implementation must use validated typed
values or handles and must never persist or accept the native pointers found
in compiled words.

## Failure behavior of the original

The original behavior is unsafe and must not become the port's acceptance
policy:

- the file size is a 32-bit value; allocation of `size + 1` and the read count
  are unchecked;
- source length, token count, nesting depth, child count, compiler-vector size,
  and allocation arithmetic have no recovered hard limits;
- the loader reports success for any valid package member even when the
  compiler records syntax or semantic errors;
- compile-time mutations are made directly to object/descriptor lists and no
  rollback was found;
- unknown opcode `0` or greater than `0x46` consumes one word, silently returns
  from the current interpreter call, and neither clears active nor unlinks
  state; an otherwise-active execution resumes at the following word when the
  scheduler next reaches it (normally next refresh, but a mutation case can be
  later in the same pass), while a prior `0x46` still causes wrapper teardown;
- branch targets, inline lengths, indices, descriptor pointers, IP, SP, and
  stack capacity are not checked;
- an instruction loop has no budget; integer division exceptional cases and
  several fixed text buffers are unchecked; and
- null input is not a supported parser value—the lexer dereferences its source
  pointer before a null gate.

The temporary source buffer is not leaked on the ordinary control-flow paths:
an invalid member returns before allocation, while normal success and reported
compiler-error paths join after compilation and call the matching delete.
The requested read count is unchecked, so a normal short read still compiles
incomplete or uninitialized contents but reaches ordinary deletion. Overflow,
an unusable/null allocation, or a fault/exception before the join has no proven
cleanup. A compiler-null/nonnull-diagnostic-file combination may also bypass
`fclose`; its feasibility is unknown. Exact allocator, compile, join, and
deallocator VAs are recorded in
[EV-20260803-003](../re/systems/AFS-VM.md#load-and-validation).

Truncated text that was short-read is NUL-terminated at the requested size,
not the actual read count. Parser callbacks can diagnose unexpected end of
file, unknown characters, parse failure, trailing syntax, and semantic errors,
but the mission wrapper does not turn every such error into `LoadScript ==
false`. Behavior after partial compiler mutation is unknown.

Missing references are policy-dependent in the original. An undeclared
function is looked up first among native descriptors and then on the current
compiled object; if both miss, compilation emits an undeclared-function
diagnostic, marks an error, and returns from that compiler node. A missing
inherited object takes the same diagnostic/error-marker class. Despite either
compiler error, `LoadScript` still returns true when the package member itself
was valid. A subsequently missing selected mission object makes `Load` log and
return false; `Start` logs and returns its ordinary zero, which is not a
distinguishing success code. A missing named setup/event function or mission
process is silently skipped. Native ID `0x04` `Call` likewise releases its
references and returns zero when its object/function is absent, while malformed
descriptor tokens in opcodes `0x25`–`0x27` are dereferenced without a guard.
Exact sites and diagnostic constants are recorded in
[EV-20260803-003](../re/systems/AFS-VM.md#missing-reference-behavior).

## Required portable parsing policy

A Windows/iOS parser suitable for untrusted or corrupt input must:

1. accept a byte span with an explicit maximum and require an exact read;
2. check every addition, multiplication, allocation, token, child, nesting,
   string, object, function, variable, instruction, and include limit;
3. parse and compile transactionally, publishing no object or descriptor on
   failure;
4. resolve names to bounded IDs and typed handles rather than native pointers;
5. validate the complete control-flow graph, every instruction width, branch,
   inline payload, stack effect, slot, call signature, and terminal path before
   execution;
6. resolve every declared object, inheritance edge, function, event, process,
   descriptor, and handler before publication, and reject every missing or
   ambiguous reference without consuming runtime state;
7. reject unknown opcodes without consuming state;
8. impose instruction, recursion, include, call-depth, process, and execution
   budgets; and
9. return path-free, bounded diagnostics without echoing private source.

This fail-closed policy is **GO as a new safety contract**. Exact compatibility
with malformed native behavior is **NO-GO** and should not be pursued.

## Decision

| Area | Decision |
|---|---|
| recognize bounded valid AFS source | **CONDITIONAL GO** after the limits and transactional AST/compiler contract are specified and tested |
| accept or reproduce malformed native behavior | **NO-GO** |
| treat internal wordcode as a disk format | **NO-GO** |
| use native descriptor/object pointers in a port | **NO-GO** |
| decode IDs `0x01–0x46` from validated wordcode | static specification **GO**, execution still gated per opcode family |
| complete source/bytecode compatibility | **NO-GO** pending a safe parser, fixtures, and controlled traces |
