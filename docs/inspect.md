# inspect

`shckit inspect --release RELEASE --mode MODE --entry LABEL [--end LABEL] [--sz 0|1] [--all] FILE` looks in one function of a text for a witness that SHC didn't compile it from C: an instruction SHC never writes, operands it never writes, or a use of R15 that no C function SHC compiles makes. Each rule holds for the releases and modes its evidence covers, on the assumptions [its evidence](#evidence) names.

A SYSROF object names its writer in its producer record: `C_SH` for SHC's direct object, `A_SH` for asmsh. But asmsh writes `A_SH` for SHC's own assembly output too, so the tag can't tell C from assembly: 565 of the 590 members of the Katana SDK 1.20J's `ninja.lib` carry it, every member the patches rebuild from C among them. So inspect reads the code itself.

For example, `x.src` holds four short functions:

<!-- example kinds: input x.src -->
```
_cache:
          OCBI        @R4
          RTS
          NOP
_first:
          MOV.L       @(0,R4),R0
          RTS
          NOP
_pop:
          MOV.L       @R15+,R1
          RTS
          ADD         R1,R0
_call:
          STS.L       PR,@-R15
          BSR         _pop
          NOP
          LDS.L       @R15+,PR
          RTS
          NOP
```

<!-- example kinds: run -->
```sh
build/shckit inspect --release 5.0R10 --mode asmcode --entry _cache --end _first x.src
build/shckit inspect --release 5.0R10 --mode asmcode --entry _first --end _pop x.src
build/shckit inspect --release 5.0R10 --mode asmcode --entry _pop --end _call x.src
build/shckit inspect --release 5.0R10 --mode asmcode --entry _call x.src
```

<!-- example kinds: output -->
```
x.src:2: _cache: opcode: cache-line
x.src:6: _first: operand: zero-displacement
x.src:10: _pop: abi: stack-above-entry (SP +4)
x.src:13: _call: none
x.src:15: _call: incomplete: call
```

- `_cache` holds `OCBI`, an instruction R10 never writes.
- `_first` loads through `@(0,R4)`. R10 writes a load at offset 0 as `@R4`, never with the displacement.
- `_pop` pops a word it never pushed, so after line 10 R15 is 4 bytes above where it was at the entry. No R10 compile of C found so far does that. The [frame argument](#abi-rules), which would show that none can, is unfinished.
- `_call` shows no witness, but [the stack rule](#the-stack-rule) stopped at the call on line 15: it can't follow a call, so for `stack-above-entry`, `return-below-entry` and `fschg-pairs`, which follow its paths, `none` says nothing about the lines past it. The [instruction rules](#the-rules) read every instruction, reached or not. That command exits 3, for the limit, and the others exit 0. Without `--end`, `_call` runs to the end of the text.

A witness is evidence that the release, under each mode named, compiles no C source to such a function, inline text aside: SHC copies the text of `#pragma inline_asm` and `#pragma asm` into `-code=asmcode` text as written, and it can hold any instruction. `-code=machinecode` refuses both pragmas. Each rule's [evidence](#evidence) is a reading of the compiler's stages, with sweeps of compiled C for R10 and R32, and says what the rule rests on.

C that a release compiles, under a mode a rule holds for in that release, to the rule's witness, outside inline text, would overturn the rule. We believe none exists. If you find one, please send a pull request: we want to grow these rules with the Dreamcast decompilation community.

Without a witness, the function may still not be compiled from C: inspect knows a few rules, and a limit, a place it can't see past, can hide what they would find. inspect doesn't modify files.

## The function

`--entry` and `--end` name labels, each defined once in the text. The function is the lines from the entry's up to the end's, which isn't one of them. Without `--end`, it runs to the end of the entry's `.SECTION` block: up to the next `.SECTION` or `.END` line, or to the text's end. The entry's line is a label or an instruction in a code section.

inspect can't tell where a function starts or ends, or whether calls enter it anywhere but its entry: those are the caller's to know. A label below the entry starts no other function, and a branch to it stays in this one.

Some lines run nothing, and control passes them: blanks, comments, labels, `.EXPORT`, `.IMPORT`, `.EQU` and `.ASSIGN` lines, and the continuations of any of these. Below, these are the lines control passes.

inspect reads a name where a line names it, as asmsh does:

- A `?label` means its first definition in the line's block. A block runs from a `.SECTION` line, or from the first definition of a name other than a `?label`, alone or on an instruction or data, up to the next such line.
- Any other name means its first definition in the whole text. Where that is an `.ASSIGN`, it means the last `.ASSIGN` of it at or above the line, in any section, since asmsh redefines the name at each one.
- asmsh ignores the other definitions, with warning 801.
- A label on a directive other than data, `.POOL`, `.EQU` and `.ASSIGN` defines nothing, nor does a `?label` on `.EQU` or `.ASSIGN`.
- Nor does a name asmsh refuses: one with a `.` or with a `?` past its first character, a `?label` over 16 characters, and `?` or `$` alone.

## What it reports

One line names the witness and its line, or `none` and the entry's line, and one more names the limit, if inspect met one. A `stack-above-entry` or `return-below-entry` witness names R15's offset after its line, and a `small-constant` one its constant:

```
x.src:3: _f: abi: stack-above-entry (SP +8)
x.src:5: _f: operand: small-constant (#0)
x.src:1: _f: none
x.src:1: _f: incomplete: abi-scope
```

With several witnesses, inspect reports one: an opcode's before an operand's before an ABI one, then the earliest line's, then the rule listed first in the [rules table](#the-rules). Of several limits it names the first line's, and of two at one line `continued`, then `rule-scope` or `pool-word`, then the stack rule's. A scope's, which has no line of its own, comes last, at the entry's line. A witness holds even when inspect also reports a limit.

With `--all`, inspect reports every witness, a line each: each rule's at each line where the rule finds one, `stack-above-entry`'s too, not only the first line's. They come in line order, and at one line in the rules table's order. With no witness, inspect prints `none`. The limit follows as without `--all`. The witness inspect reports without `--all` is among them. For `y.src`:

<!-- example all: input y.src -->
```
_f:
          SETT
          MOV.L       @R15+,R1
          MOV.L       @(0,R4),R0
          RTS
          ADD         #-4,R15
```

<!-- example all: run -->
```sh
build/shckit inspect --release 5.0R10 --mode asmcode --entry _f --all y.src
```

<!-- example all: output -->
```
y.src:2: _f: opcode: sett
y.src:3: _f: abi: stack-above-entry (SP +4)
y.src:4: _f: operand: zero-displacement
y.src:4: _f: abi: stack-above-entry (SP +4)
```

The pop on line 3 leaves R15 4 bytes above the entry until the slot on line 6 brings it back, so lines 3 and 4 are `stack-above-entry` witnesses, and the `RTS` on line 5, a return, is none. Without `--all`, inspect reports line 2's alone.

inspect first checks the text as [check](check.md) does. If check refuses the text, inspect prints check's findings instead. R10's `-debug` text is one it refuses: check can't read the `.LINE` lines R10 writes there.

Exit status:

- **0** an inspection that met no limit, witness or not;
- **1** a text shckit refuses: one it can't read, or one check refuses;
- **2** a command-line, I/O or memory error, a label missing or defined twice among them, or a function inspect can't take: an `--end` at or above the entry, or an entry that isn't a label or an instruction in a code section;
- **3** an inspection that met a limit, witness or not.

## The rules

`--release` names the compiler, and `--mode` the code modes a witness has to hold under: `machinecode`, `asmcode` or `both`. The releases are SHC 5.0 R10, R26, R27, R28, R31 and R32 and 5.1 R01, R03, R04, R08, R11 and R13, named `5.0R10` to `5.1R13`, and 6.0C.001, named `6.0C.001`.

| rule | kind | witness | releases | modes |
|---|---|---|---|---|
| [`rotcl`, `rotcr`, `swap`](#rotcl-rotcr-and-swap) | opcode | `ROTCL`, `ROTCR`, `SWAP.B`, `SWAP.W` | every 5.0 and 5.1 release, 6.0C.001 | machinecode, and asmcode in 5.0 R10 and R32 |
| [`dmuls`, `dmulu`](#dmuls-and-dmulu) | opcode | `DMULS.L`, `DMULU.L` | every 5.0 and 5.1 release | both |
| [`movca`](#movca-and-cache-line) | opcode | `MOVCA.L` | 5.0 R10 and R32 | both |
| [`fschg`](#fschg) | opcode | `FSCHG` | 5.0 R32 | both |
| [`cache-line`](#movca-and-cache-line) | opcode | `OCBI`, `OCBP`, `OCBWB` | 5.0 R10 and R32 | both |
| [`sett`](#sett) | opcode | `SETT` | 5.0 R10 and R32 | both |
| [`fipr-registers`](#fipr-ftrv-and-fsca) | operand | `FIPR` other than `FIPR FV4,FV0` | 5.0 R10 and R32 | both |
| [`ftrv-registers`](#fipr-ftrv-and-fsca) | operand | `FTRV` other than `FTRV XMTRX,FV0` | 5.0 R10 and R32 | both |
| [`fsca-registers`](#fipr-ftrv-and-fsca) | operand | `FSCA` other than `FSCA FPUL,DR0` | 5.0 R10 | both |
| [`small-constant`](#small-constant) | operand | a `MOV.L` of a [constant](#constants) from −128 to 127 | 5.0 R10 | both |
| [`zero-displacement`](#zero-displacement) | operand | a `MOV.B`, `MOV.W` or `MOV.L` through `@(0,Rn)` | 5.0 R10 and R32 | both |
| [`fschg-pairs`](#fschg-pairs) | operand | an `FMOV` between memory and FR8, FR10, FR12, FR14 or DR8 to DR14, after an odd number of `FSCHG`s on every path the stack rule follows to it ([more](#pairs-after-fschg)) | 5.0 R10 | both |
| [`stack-written`](#stack-written) | abi | a write to R15 other than a push's or pop's address, `ADD`, `SUB`, `MOV R14,R15` and the stack switch ([more](#writes-to-r15)) | 5.0 R10 and R32 | both |
| [`fpscr-save`](#fpscr-save) | abi | `STS.L FPSCR,@-R15` or `LDS.L @R15+,FPSCR`, in a function that holds an `RTS` ([more](#fpscrs-save)) | 5.0 R10 | both |
| [`stack-above-entry`](#stack-above-entry) | abi | R15 above its value at the entry, after any instruction the stack rule reaches but a branch or a return ([more](#above-the-entry)) | 5.0 R10 | both |
| [`return-below-entry`](#return-below-entry) | abi | R15 below its value at the entry, after the slot of an `RTS` the stack rule reaches ([more](#below-the-entry)) | 5.0 R10 | both |

`stack-above-entry` and `return-below-entry` rest on the [frame argument](#abi-rules), which is unfinished. No R10 compile of C found so far makes either witness.

Every rule but `fschg-pairs`, `stack-above-entry` and `return-below-entry` is an instruction rule, which reads each instruction of the function in a code section, reached or not, as check reads it: in any case and with the sizes asmsh takes. Data, comments and the lines past `.END`, which check passes only blank or commented, hold none. An instruction where a rule would find its witness, or meet `pool-word`, under a release or a mode outside the rule's, is the limit **rule-scope**.

`PREF`, `LDC` and `STC` of a control register and `FRCHG` show nothing, and `FSCHG` shows nothing under R10. R10 writes each of them for a builtin, and R32 each but `FSCHG`. [What has no rule](#what-has-no-rule) says what else has no rule, and why.

### Constants

**A constant** is a number in decimal, `H'`, `B'`, `Q'` or `D'`, with an optional sign, or a symbol whose definition is an `.EQU` or `.ASSIGN` of such a number. It is read as asmsh reads it, a 32-bit word: `H'FFFFFFFF` and `-1` are both −1, and `-H'FFFFFFFF` is 1. An expression is no constant. A symbol `.ASSIGN` defines takes the value of the `.ASSIGN` [it names](#the-function) at its use, and 0 above the first.

**small-constant** reads `MOV.L #x,Rn`, which asmsh expands into a pool load, and a PC-relative `MOV.L label,Rn` or `MOV.L label+n,Rn`, n in decimal, of the `.DATA.L` item n bytes past the label's definition, counted through the `.DATA` and `.RES` lines below it. Above x's first `.ASSIGN`, asmsh gives `#x` the value x has where it writes the pool, not 0, and inspect doesn't place the pool, so x is no constant there.

**zero-displacement** reads an `@(d,Rn)` whose d is the constant 0. asmsh assembles `@(0,Rn)` as the displacement form with 0, not as `@Rn`.

A pool word `small-constant` can't read is the limit **pool-word**:

- one loaded through an expression other than label or label+n, through a label with no definition, or at a displacement from PC;
- one inside an item or a pad, or in a `.DATA.W` or `.DATA.B` item;
- one in or past a data line with a continuation, or past any line but `.DATA`, `.RES`, a label, a blank and a comment, such as `.ALIGN`, `.DATAB`, `.SDATA` or `.FDATA.S`.

So is a word from −128 to 127 whose run of data has a label, up to the run's last data line, that the text names other than as the source of a PC-relative `MOV.W` or `MOV.L`, as an `.EXPORT`, `MOVA`, a `.DATA` item or `#label` does. Such a word may be a variable, and inspect, which computes no addresses, decides that by names alone. The run is the data lines around the word and the lines before and among them other than an instruction, a `.SECTION` and an `.END`.

### Writes to R15

A write to R15 is a `stack-written` witness unless it is a push's or a pop's address, an `ADD` or `SUB`, `MOV R14,R15`, or the stack switch SHC writes for `#pragma interrupt(f(sp=x))`:

```
          MOV.L       R0,@-R15
          MOV.L       L1,R0
          MOV.L       R15,@-R0
          MOV         R0,R15
          ...
          MOV.L       @R15+,R15
          MOV.L       @R15+,R0
          RTE
```

The switch is exempt only whole:

- its head stands as the function's first instructions;
- between `MOV.L R0,@-R15` and `MOV.L R15,@-R0`, the head loads R0 from no other general register: first by a `MOV` of `#imm`, a label, `@(disp,PC)` or `@(disp,GBR)`, or by `STC GBR,R0`, then by any more of those, a `MOV` of `@R0` or an `ADD #imm,R0`;
- in a function that starts so, each `MOV.L @R15+,R15` and `MOV.L @R15+,R0` that `RTE` follows, or `TRAPA` under `tn=`, switches back;
- lines control passes may stand between the switch's instructions, and its `MOV`s may go without a size.

Any other write to R15 is a witness: `MOV R0,R15` after a call, `MOV.L @R15,R15`, or a load of R15 from a pool.

### FPSCR's save

A push of FPSCR through R15, `STS.L FPSCR,@-R15`, or its pop, `LDS.L @R15+,FPSCR`, in a function that holds an `RTS` is an `fpscr-save` witness at its line. Either is enough alone, and it and the `RTS` may stand anywhere in the function, reached or not. R15 may be written `SP`. Here the push on line 2 is a witness, and with `RTE` in place of the `RTS`, neither line would be:

```
_f:
          STS.L       FPSCR,@-R15
          FMOV.S      @R4,FR2
          FMUL        FR2,FR2
          FMOV.S      FR2,@R4
          LDS.L       @R15+,FPSCR
          RTS
          NOP
```

`STS FPSCR,Rn` and `LDS Rm,FPSCR`, which R10 writes for its builtins, show nothing, nor does a copy of FPSCR that a `MOV.L` stores, loads, pushes or pops, through R15 or another register. FPSCR's push and pop through another register are outside the rule, as are other registers' through R15, FPUL's among them. The `RTS` may be another function's: without `--end`, an interrupt function's lines take in each function below it in its block.

## The stack rule

The stack rule follows control through the function and finds three witnesses: [R15 above the entry](#above-the-entry), [R15 below it after a return](#below-the-entry), and [a pair's move after FSCHG](#pairs-after-fschg).

**What it follows:**

- control from the entry, through `BRA`, `BT`, `BF`, `BT/S` and `BF/S` to labels in the function, and through each delay slot;
- to the function's returns, `RTS` and `RTE`;
- and to its branches out. A branch out, as a tail call's `BRA` is, goes to a label in its `.SECTION` block, outside the function's lines, that marks an instruction outside them too, past lines control passes. The rule ends the path at the branch, past its slot where it has one, and a conditional branch's other path goes on. Ending it is no limit: past a branch out, control comes back to the function's lines only through the entry.

It computes no addresses, solves no branch, and assumes nothing of a call: a call, a jump and the rest are [limits](#limits).

**What it knows:** R15's offset from its value at the entry, R14's if R14 holds one, FPSCR.SZ, and whether the `FSCHG`s run since the entry are odd or even in number. Each line changes them so:

| line | what it changes |
|---|---|
| an `ADD` of a signed byte, in any radix | adds to the offset |
| a push or pop through R15 or R14, a `MAC` operand among them | moves the offset by its width: 1, 2 or 4 bytes by the size asmsh takes, `.L` for a `MOV` and `.W` for a `MAC` written without one; 4 for `LDS`, `LDC`, `STS` and `STC`; 4 or 8 for an `FMOV` by SZ, whatever its size |
| `FSCHG` | flips SZ and the count |
| a write to FPSCR, by `LDS` or `LDS.L` | makes SZ and the count unknown |
| a copy of R15 to R14, or back | keeps the offset |
| any other write to R15 or R14 | loses its offset |
| an expansion, `MOV.W`, `MOV.L` or `MOVA` of `#x` | writes its register alone, wherever asmsh puts the literal |

SZ 1 with FPSCR.PR 1 is undefined on the SH-4, and inspect reads an `FMOV`'s width from SZ alone.

A path that joins another keeps the facts both agree on, and a loop runs until each line's facts settle. A line that leaves R15's offset unknown, and a join of paths that disagree on it, is the limit **stack-unknown**. The walk continues, but cannot show a stack witness until it knows R15's offset again; a later copy from R14 can restore it.

`stack-above-entry` and `return-below-entry` rest on SZ as the rule knows it, with `--sz` or without: a write to FPSCR between a function's FR saves and their restores can change the restores' width, and the rule makes SZ unknown there.

### Above the entry

Once the facts settle, R15 above its entry value after any instruction the rule reaches but a branch or a return is a `stack-above-entry` witness, at the first such line, naming R15's offset after it. A branch or a return moves no R15, so R15 above its entry value at one is already a witness at the instruction each path runs before it. The rule reports it wherever the function goes after, even on a path that never runs. That no R10 compile of C does this rests on the [frame argument](#abi-rules), which is unfinished. Here the return's slot leaves R15 8 bytes above the entry, a witness at line 3:

```
_f:
          RTS
          ADD         #8,R15
```

### Below the entry

R15 below its entry value once an `RTS`'s slot has run is a `return-below-entry` witness, at the slot's line, naming R15's offset after it. The rule reports it even on a path that never runs. That no R10 compile of C does this rests on the same unfinished [frame argument](#abi-rules). Before then, R15 below the entry is a frame; after an `RTE`'s slot, or a branch out, it shows nothing. Here the epilogue leaves out a pop, a witness at line 7:

```
_f:
          MOV.L       R14,@-R15
          STS.L       PR,@-R15
          ADD         #-8,R15
          ADD         #8,R15
          RTS
          LDS.L       @R15+,PR
```

### Without --sz

`--sz 0` or `--sz 1` gives FPSCR.SZ at the entry, which sets the width of an `FMOV` through memory. Without it, the rule runs under SZ 0 and under SZ 1, since the entry's SZ is one of them. `stack-above-entry` or `return-below-entry` stands only where both runs find it, naming the offset nearer the entry: under either SZ, R15 is at least that far from it. One that a run finds alone is the limit `stack-unknown` at its line, and a limit either run meets is the inspection's. For `z.src`:

<!-- example sz: input z.src -->
```
_f:
          FMOV.S      @R15+,FR4
          ADD         #-8,R15
          RTS
          ADD         #4,R15
```

<!-- example sz: run -->
```sh
build/shckit inspect --release 5.0R10 --mode asmcode --entry _f --sz 0 --all z.src
build/shckit inspect --release 5.0R10 --mode asmcode --entry _f --sz 1 --all z.src
build/shckit inspect --release 5.0R10 --mode asmcode --entry _f --all z.src
```

<!-- example sz: output -->
```
z.src:2: _f: abi: stack-above-entry (SP +4)
z.src:2: _f: abi: stack-above-entry (SP +8)
z.src:5: _f: abi: stack-above-entry (SP +4)
z.src:2: _f: abi: stack-above-entry (SP +4)
z.src:5: _f: incomplete: stack-unknown
```

The pop on line 2 moves R15 4 bytes with SZ 0 and 8 with SZ 1, so line 2 is a witness under both, and without `--sz` it names +4. The slot on line 5 brings R15 back to the entry with SZ 0 and leaves it 4 bytes above with SZ 1, so without `--sz` line 5 is the limit `stack-unknown`.

### Pairs after FSCHG

An `FMOV` between memory and FR8, FR10, FR12, FR14 or DR8 to DR14, after an odd number of `FSCHG`s on every path the rule follows to it, is an `fschg-pairs` witness at its line.

- The count starts at 0 at the entry, whatever `--sz` gives, and counts an `FSCHG` in a slot too. Past a join of paths that disagree on it, or a write to FPSCR, it is unknown, and an unknown count is no witness.
- Each of those registers' moves through memory has a code word whose register field is 8, 10, 12 or 14: `FMOV.S FR12`, `FMOV FR12` and `FMOV DR12` are one word, which with SZ 1 moves DR12 as a pair, so the rule reads them alike.
- The memory operand is any of `@Rn`, `@Rn+`, `@-Rn` and `@(R0,Rn)`, through any register, R15 among them. XD8 to XD14, fields 9, 11, 13 and 15, and moves between registers are outside the rule.

Here the store on line 3 is a witness, spelled `FMOV.S FR12,@-R4` or not:

```
_f:
          FSCHG
          FMOV        DR12,@-R4
          RTS
          FSCHG
```

## Limits

| limit | where inspect meets it |
|---|---|
| `producer-scope` | a release no rule holds for: every rule stops |
| `abi-scope` | a release other than 5.0 R10: the stack rule stops, and `fschg-pairs` with it |
| `rule-scope` | an instruction where a rule would find its witness, or meet `pool-word`, under a release or mode outside the rule's |
| `call` | `BSR`, `BSRF` or `JSR` |
| `opaque-transfer` | `JMP`, `BRAF`, `TRAPA` or `SLEEP` |
| `branch-target` | a branch to a label in its `.SECTION` block, outside the function's lines, that marks no instruction outside them, or whose target is no label asmsh defines, as the branch reads it, such as one with an offset or `$` |
| `section-change` | a `.SECTION` line, or a branch across one |
| `entered-delay-slot` | a slot reached other than from its branch |
| `delay-slot` | a delayed branch whose slot shckit can't place, as when an `.ALIGN` or a `.SECTION` comes next, or whose slot lies past the function's last line |
| `data-flow` | data |
| `assembler-layout` | `.ALIGN`, `.POOL`, `.NOPOOL` or another directive that writes no data |
| `continued` | an instruction with a continuation below it |
| `fallthrough` | control past the function's last line, or into `.END` |
| `stack-unknown` | a line, or a join, that leaves R15's offset unknown; without `--sz`, one that does under either SZ, or the line of a witness the stack rule finds under one SZ alone |
| `pool-word` | a `MOV.L` of a pool word `small-constant` can't read, or of a small one whose run of data the text names |
| `instruction-effect` | an instruction whose effect the stack rule doesn't know; every one in the [opcode table](opcodes.md) has one |

Every limit but the first two names a line. `rule-scope`, `continued` and `pool-word` are limits wherever the line stands in the function; any other is one only where control reaches its line.

## Evidence

Each rule rests on a reading of the releases its table row names: SHC's front end, shcfrt, its other stages shc and shcmdl, and its back end's stages, SHCGEN, SHCPEP and SHCASM, which turn a function's records into instructions and write them as an object or as text. The rules for R10 and R32 rest on the sweeps of compiled C below too, and some on the probes they cite.

An address cited is a virtual address in one of R10's stages, under image base 0x400000:

- `shcfrt.exe`, SHA-256 `e3223c795644fd137c3ac400440d7e31bd67740bb76890e2f6f50feee5412ec4`;
- `shcmdl.exe`, SHA-256 `00ee4f7220cb65d3bed2e3d7cb5c687a3c06e97b5e432470c7d7cb7584ac377b`;
- `shcgen.exe`, SHA-256 `9963ce29e1c9c1d9b5756a0b703332cbd23cee849805ebea8a967fe48929e789`;
- `shcpep.exe` and `shcasm.exe`, as patches.md's [evidence base](patches.md#evidence-base) gives them.

The readings assume that every allocation the compiler makes succeeds. When one fails, SHCPEP writes out the records it holds, one it is part way through copying among them, and a compile's exit status doesn't show that none failed. Whether such a compile can still end in an object wasn't settled.

A rule that rests on the records SHC's stages write also rests on SHCGEN framing each initializer as shcfrt wrote it, since SHCASM would read a misframed one's data as records. The probes found no misframed initializer, but no reading shows that none can be, and a C source that a misframing turns into a witness refutes the rule it breaks. dmuls, dmulu, movca, cache-line and zero-displacement don't depend on it: they rest on SHCASM's encoder alone.

### The sweeps

2,181 C functions in 100 files, written to exercise control flow, frames, constants and floating point, went through 3,684 compiles. Each compile wrote `-code=asmcode` text, or a `-code=machinecode` object whose listing became text.

| for the SH-4 | R10 | R32 |
|---|---|---|
| optimization | `-optimize=0` and 1, for size and for speed | `-optimize=0` and 1, for size and for speed |
| build | an SDK build's `-double=float -size -extra=a=400` and the options that go with them | `-fpu=single`, `-fpu=double` or no `-fpu` |
| endianness | little and big | little and big |
| one at a time | `-pic=1`, `-inline`, `-loop`, `-macsave=1`, `-rtnext`, `-abs16=all` or `-align16` | `-pic=1`, `-denormalize=on`, `-round=nearest`, `-loop`, `-inline` or `-macsave=1` |

R10 also compiled them for `-cpu=sh1` to `sh3e`, and R32 for `-cpu=sh3`.

The rules cite two populations:

- **The compiles' texts.** check read 3,321 of them, and refused 231 at first: for the `.DATAB` lines SHC writes for a partly initialized aggregate or a `#pragma gbr_base` variable, and for a pool right after the `TRAPA` that ends a `tn=` function. In the constants sweep inspect read copies that write each `.DATAB` line as zero `.DATA` items. inspect read every function of the other 3,090 texts, under the release and mode that wrote it, without `--sz` and with `--sz 0`: none of the 139,278 inspections, 82,112 under R10 and 57,166 under R32, found a witness.
- **The texts in the sweeps' directories.** check now reads both such `.DATAB` lines and a pool right after a `tn=` function's `TRAPA`, and passes all 5,462 texts in the sweeps' directories, copies and cut-down cases among them. 2,901 of them are R10's, and 1,408 have listings. Of the 536 check refused before, inspect read the 4,581 functions the same way, and none of the 8,980 inspections found a witness.

### Opcode rules

In R10 and R32, SHCASM encodes every instruction from its form rows, ORing a row's operand fields into its base word, so a row's fields only add bits to it. In those two, one function of SHCASM writes an instruction's word or its text, printing the mnemonic of the form row the word would come from, and SHCASM's other text is directives, operands, names, comments and the inline text it copies. Under `-code=machinecode`, every 5.x release refuses `#pragma inline_asm` with error 2808 and `#pragma asm` with 2708.

#### rotcl, rotcr and swap

- **Releases.** Under `-code=machinecode` the reading covers every 5.x release and 6.0C.001. Under `-code=asmcode` it covers 5.0 R10 and R32, whose text writer was read.
- **Encoder.** In each release, SHCASM's encoder forms ROTCL's word only for internal op 0x4f, ROTCR's only for 0x5f, and SWAP.B's and SWAP.W's only for 0x46. No other of its form rows can make the four words.
- **Writers.** No stage writes the three ops: not SHCGEN's record writers, template rows or kind mapping, not SHCPEP's node writer, byte constants or shift table, and not SHCASM's builders.
- **Text.** Beyond their form rows, whose mnemonics SHCASM's text writer prints, only SHCASM's and SHCPEP's tables of op names spell ROTCL, ROTCR and SWAP, only debug dumps read those tables, and no other stage holds the names.
- **C.** No front end has a rotate or swap builtin. Byte swaps compile to shifts, masks and ORs, or to byte moves through memory, and a bit shifted from one word into the next to `ROTL` or `ROTR` and `AND #1`.
- **Sweeps.** No `-code=asmcode` text of the sweeps holds any of the four.

#### dmuls and dmulu

- **Releases.** 6.0C.001 wasn't read for them.
- **Encoder.** No release's encoder has a form for either word. R10's form table has 199 rows and each other release's 182, and none names DMULS.L or DMULU.L or can make its word.
- **Text.** No stage of any release holds the text DMUL, so `-code=asmcode` text can't spell them either.
- **C.** Every front end refuses `long long` with error 2102, and none has a builtin for a 64-bit product. Products compile to `MUL.L` or call `__muli`, and divisions call the runtime.

#### movca and cache-line

- **Encoder.** None of R10's 199 rows or R32's 182 can form the word of `MOVCA.L R0,@Rn`, `0000nnnn11000011`, or of `OCBI`, `OCBP` or `OCBWB @Rn`, `0000nnnn10010011`, `10100011` and `10110011`, even with each field taken to set any twelve bits at its shift.
- **Text.** No stage of either release, shc, shcfrt, shcmdl, SHCGEN, SHCPEP or SHCASM, holds the text MOVCA or OCB, so `-code=asmcode` text can't spell them either. They reach it only as inline text.
- **C.** The prefetch builtin's record maps to PREF, whose register operand selects no other cache operation.
- **Sweeps.** No compiled function of the sweeps holds any of them.

#### fschg

- **Releases.** R32 alone: R10 writes `FSCHG` for its vector builtins ([fschg-pairs](#fschg-pairs)).
- **Encoder.** No row of R32's 182 is FSCHG's, as R10's row 187 is. Take each register field as the register's number, 0 to 15, at its shift, and each displacement or immediate field as any twelve bits at its own. Then only FSCA's row, `F0FD` with DRn's number at bit 8, could form FSCHG's word, `F3FD`, and only with an odd number there, which a DRn never has. R32 writes FSCA only as `FSCA FPUL,DR0`.
- **Text.** No stage of R32 holds the text FSCHG, so `-code=asmcode` text can't spell it either.
- **C.** R32's front ends have no `fschg` builtin: R32 compiles `_builtin_fschg()` as a call to an external function of that name.
- **Sweeps.** No text of the sweeps that R32 wrote holds `FSCHG`, against 2,628 in R10's and 376 `FRCHG` in R32's.

#### sett

- **Encoder.** In R10 and R32, SHCASM's encoder forms SETT's word, H'0018, only from SETT's form row, which it reaches only for internal op 0x8f. The one other row whose base word has no bit outside H'0018 is CLRT's, H'0008, which has no fields, as SETT's has none.
- **Writers.** No stage of either release writes op 0x8f: not SHCGEN's record writers, its record constructor, its template rows or its kind mapping, not SHCPEP's node writer or any other store of a record's op, and not SHCASM's record builders or the records it fills a delay slot with. SHCGEN's op table marks 0x8f valid, and SHCASM's reader would encode a record of it, so the rule rests on there being no writer.
- **Text.** The text SETT is in no stage but SETT's row and the tables that name records in a debugging dump, so asmcode text spells `SETT` only as inline text.
- **C.** C that computes with T compiles to other instructions: a signed `(x+y+1)/2` to `MOV #0,R3`, `CMP/GT R0,R3`, `ADDC R3,R0` and `SHAR R0`, `(a+b+1)>>1` to `ADD`, `ADD #1` and a shift, and `_builtin_tas` and `_builtin_gbr_tst_byte` to `TAS.B` or `TST.B` and `MOVT`.
- **Sweeps.** None of the 5,462 texts in the sweeps' directories holds `SETT`, against 151 that hold `ADDC`. In their 1,408 listings H'0018 is a code word only twice, each a `.DATA.W` entry of a switch table.
- **Rests on** every store that could write a record's op. In R10 each was read. In R32 a scan sorted some of SHCPEP's and SHCASM's.

### Operand rules

#### fipr, ftrv and fsca

- **Releases.** R10 and R32 for FIPR and FTRV, R10 alone for FSCA.
- **Templates.** R10 writes FIPR, FTRV and FSCA, and R32 FIPR and FTRV, only for builtins, each from a template that fixes its operands.
  - The FIPR template fixes `FV4,FV0`, and the scalar dispatcher has no FIPR exit.
  - Each of the 15 FTRV template rows fixes `XMTRX,FV0`.
  - The FSCA template fixes `FPUL` and `DR0`: with two results live at once, R10 writes `DR0` twice and stores the first between, never `DR2`.
  - A `#pragma global_register` for an FR register doesn't move these operands.
- **Encoder.** R32's SHCASM forms FIPR's word only for internal op 0xd6 and FTRV's only for 0xd8, and takes the vector fields from the record's operands unchecked.
- **Writers.** In R32 only SHCGEN's template rows write the two ops, and its register map takes FV and XMTRX to themselves. SHCPEP changes operands only in records of other ops, renames only integer registers, and copies a record whole. SHCASM changes no record's registers. SHCGEN's data stream, which reaches SHCASM past SHCPEP, holds only data directives.
- **C.** R32 writes them for `_builtin_fipr`, `_builtin_ftrv`, `_builtin_ftrvadd`, `_builtin_ftrvsub`, `_builtin_mtrx4mul`, `_builtin_mtrx4muladd` and `_builtin_mtrx4mulsub`, and refuses each under `-fpu=double` with error 2804, so the rules hold under every `-fpu`.
- **Probes.** R32 compiled three files of builtin calls in both modes under `-cpu=sh4 -division=cpu -endian=little -round=zero -pic=0 -string=const -comment=nonest -sjis -fpu=single -optimize=1 -speed -nodebug -macsave=0 -extra=a=800`, adding in turn each of `-loop`, `-align16`, `-rtnext`, `-abs16=all`, `-pic=1`, `-denormalize=on`, `-inline=200 -nestinline=4`, `-noinline`, `-double=float`, `-macsave=1`, `-division=peripheral`, `-debug`, `-extra=a=400`, `-extra=a=1000`, `-endian=big` and `-round=nearest`. They write 752 FIPRs and 640 FTRVs, each `FV4,FV0` or `XMTRX,FV0`.

#### small-constant

- **Releases.** The rule doesn't hold for R32. Without `-fpu=single` R32 loads `H'00000000` from a pool by `MOV.L`, as a float 0.0 and as a double's low word. With `-fpu=single -denormalize=on` it keeps a denormal literal, and can load its word the same way: `void s_idx(float *p, int i) { p[i] = 1.78e-43f; }` compiles to `MOV.L L236+2,R1`, whose pool word is `H'0000007F`.
- **Writers.** In the probes, R10's SHCASM writes every PC-relative load itself, expanding the records SHCGEN and SHCPEP hand on for a constant, a call or a jump, a frame slot or its address, a label's address, a function's address under `-pic=1`, and a float constant. Its reader can take a PC-relative operand from SHCPEP's records, but none of the probes' records holds one.
- **Expansion.** SHCASM lowers four kinds of record into constant loads: an integer constant, a frame slot, a frame slot's address, and an address taken PC-relative. The first three go by the value: one from −128 to 127 that names no symbol becomes `MOV #imm,Rn`, one that fits 16 bits a `MOV.W` pool load, and only a wider one or a symbol a `MOV.L` pool load. The fourth skips the first test, so a value from −128 to 127 there would become a `MOV.W` pool load, though no probe's record reaches it with such a value. None of the four loads such a value by `MOV.L`, which is all the rule reads.
- **Options.** `#pragma abs16`, `-abs16` and a GBR section only move a symbol's load to `MOV.W` or `MOV #imm`. A frame slot's offset goes by value the same way once `@(disp,R15)` and `ADD #imm` can't reach the slot. A call, a jump and a function's address load a symbol, or under `-pic=1` a difference of symbols. So no option changes a constant's load.
- **Text.** SHCASM writes no `.EQU` or `.ASSIGN`, so a symbol it loads is no constant in its text. It has no form for `MOV.L #x`, and it writes `-code=asmcode` text from the same expansion and encoder as the object.
- **Sweeps.** In the sweeps R10 loads every integer from −128 to 127 by `MOV #imm,Rn`, or compares it by `CMP/EQ #imm,R0`. Its `MOV.W` items hold 16-bit values, and its `MOV.L` items only wider values, float words outside the range and symbols.
- **Floats.** A float constant is the one load whose value SHCASM doesn't test. Its word goes through `MOVA` and `FMOV.S`, or through `MOV.L`, `LDS` and `FSTS`. It lies from −128 to 127 only for +0.0, a positive denormal, or a NaN with its sign set and a mantissa of H'7FFF80 or more. R10 refuses `-denormalize` with error 3305, and its probes and sweeps load 0.0 and each denormal by `FLDI0`, a denormal literal with warning 1015, fold a NaN to H'7F800001, and load −0.0 as H'80000000.
- **Rests on** the probes and the sweeps, for floats: where SHC turns +0.0, a positive denormal and such a NaN into other loads wasn't found. A pool word SHC writes as a symbol, or as a difference of symbols, can hold any number once linked, and a text that writes such a word as its number is outside this evidence.
- **Refuted by** a float whose word lies from −128 to 127, loaded by `MOV.L`. No probe or sweep compiled one.

#### zero-displacement

- **Encoder.** In R10 and R32, SHCASM's encoder passes both operands through a checker before it picks a form. The checker rewrites `@(disp,Rn)` to `@Rn` when the displacement is 0 and names no symbol, and the form, the code word and the asmcode text all come after it.
- **Writers.** SHCGEN and SHCPEP write `@Rn` itself as `@(0,Rn)` in every probe, so the rewrite produces each `@Rn` of their records. No register displacement names a symbol. Their records hold one as a register and a 16-bit number, with no field for a symbol. SHCASM builds one only for a frame slot, from a positive offset, writing `@R15` for offset 0 itself, and the text writer prints a displacement as its number.
- **Text.** SHC therefore writes neither `@(0,Rn)` nor `@(sym,Rn)`, in either mode, and a symbol `.EQU` or `.ASSIGN` defines as 0 is no displacement SHC writes.
- **Sweeps.** No output of the sweeps holds `@(0,Rn)`: a member or a stack slot at offset 0 is `@Rn`. The GBR builtins write `@(0,GBR)`, which has no `@GBR` form and isn't a register displacement the rule reads.

#### fschg-pairs

- **Writers.** R10 writes `FSCHG` only in the templates SHCGEN expands for its vector builtins, such as `_builtin_ld_ext`, `_builtin_st_ext`, `_builtin_ftrv` and `_builtin_fipr`. SHCASM forms FSCHG's word from one form row, for one internal op, and only template rows write that op. Each template a builtin reaches writes an even number of them, so its runs open at an odd count and close at an even one. `_builtin_fschg()` compiles to a call, in both modes, under every profile and option tried and inside a `#pragma inline` function. SHCGEN's one template with a single `FSCHG` is on no builtin's list.
- **Pairs.** Inside the runs R10 moves only the templates' pairs, DR0 to DR6 and XD0 to XD14. SHCASM forms a paired `FMOV` only for an 8-byte record. SHCGEN gives an `FMOV` record that size only for a DR or XD operand, which it takes only from its table of fixed registers, as template rows and constants name them. Nothing names DR8 to DR14, and the table holds no FR8 to FR15. SHCASM saves and restores FR12 to FR15 one at a time, and FR8 and FR10 singly too, only in an interrupt function's frame. SHCPEP builds no pair, and SHCASM's scheduler keeps each instruction on its side of an `FSCHG`.
- **C.** The front end names FR0 to FR15 alone. `#pragma global_register` takes only FR12 to FR15, refusing FR0 to FR11 with error 2822, so a global in FR12 or FR14 stays a single register, and only the allocator puts a value in FR8 or FR10. R10 has no `-fpu` and computes `double` in software, so no C type lives in a pair.
- **Fields.** SHCASM gives FR12 and DR12 one register field, and FR14 and DR14 another, so `FMOV.S FR12,@-R4` and `FMOV DR12,@-R4` are one word, H'F4CB. R10 writes it between runs, at an even count. FR8 and DR8 share field 8, as FR10 and DR10 share field 10. R10 writes `FMOV.S FR8,@-R4` and `FMOV.S FR10,@-R4` between runs too, for locals it keeps in FR8 and FR10 across `_builtin_st_ext` and `_builtin_ld_ext`.
- **How it counts.** For `_builtin_st_ext(m); *--p = gf12; *--p = gf14; _builtin_ld_ext(m);`, gf12 and gf14 bound to FR12 and FR14, R10 writes `FSCHG`, `FMOV.S FR12,@-R4`, `FMOV.S FR14,@-R4` and `FSCHG`: the close of one run and the open of the next. So the rule:
  - counts from the entry, not by the `FSCHG`s around a move, its spelling or `--sz`;
  - counts along paths: R10 shares a run's closing `FSCHG` between a switch's cases, with a branch inside the run, and a count by line is odd at moves R10 writes at an even count;
  - reads only lines it reaches, where a count holds: at `-optimize=0` R10 writes return blocks that no path reaches, which pop FR12;
  - claims nothing of SZ at run time: after `_builtin_set_fpscr(s | 0x100000)`, which sets SZ, R10 writes `*--p = gf12` as H'F4CB at a count of 0, and it moves DR12 as a pair. A write to FPSCR makes the count unknown.
- **Sweeps.** The rule reaches:
  - in R10's texts in the sweeps, which hold 2,628 `FSCHG`s in 102 texts of both modes: 522 moves of FR12 or FR14 through memory, each at a count of 0, and 2,212 of FR8 or FR10, 1,860 at a count of 0 and 352 past a write to FPSCR;
  - in 192 more that R10 wrote, in both modes and under several sets of options, for C that moves FR12 to FR15 beside the vector builtins and `_builtin_fschg`: 3,284, each at a count of 0, and 3 past a write to FPSCR;
  - in 10 more, for C that keeps locals in FR4 to FR11 across the vector builtins: 20 moves of FR8 or FR10, each at a count of 0.
- **Rests on** SHCPEP renaming no pair's register. Each pass read tests for a pair and an `FSCHG` before it changes a node, but not every pass was read, nor the passes that share code between branches. How shcfrt and shcmdl lower `_builtin_fschg` wasn't read; the compiles show a call. What sets the option under which R10's templates split their stores into single moves wasn't found; under it the singles are FR0 to FR3, at even counts.
- **Refuted by** R10 writing an `FMOV` between memory and FR8, FR10, FR12, FR14 or DR8 to DR14 after an odd number of `FSCHG`s on every path from the entry, for any C source, outside inline text.

### ABI rules

Under SHC's calling interface a function leaves R15 as it found it (SHC compiler manual, table 2.6). No `#pragma global_register` takes R15, and no builtin names it. R10 compiles `_builtin_get_fpscr` and `_builtin_set_fpscr` to `STS FPSCR,Rn` and `LDS Rm,FPSCR` ([why](#fpscr-save)).

**R10's frame.** `stack-above-entry` and `return-below-entry` exclude C by one argument, the frame argument: on every path the stack rule follows, with SZ as the rule knows it, R10 never takes R15 above its entry value, and brings it back to that value by the end of each `RTS`'s slot. The argument is unfinished. Each rule's evidence names the open parts it rests on alone ([stack-above-entry](#stack-above-entry), [return-below-entry](#return-below-entry)). No R10 compile of C found so far makes either rule's witness.

SHCASM builds a function's entry and exit from the same frame record SHCGEN writes (`0x40ae70`, `0x40b910`), with the same saved-register masks:

- The entry pushes the general registers, FR registers, PR, MACH, MACL, FPUL and FPSCR the record names, in that order, and takes the frame by `ADD #-n,R15`, or past 128 bytes by `ADD R0,R15` after a load of −n.
- The exit frees the frame by `ADD #m,R15`, or from 128 bytes by `ADD R1,R15` after a load of m, pops the same registers in the reverse order, and ends in `RTS`, or for an interrupt function in `RTE`, or `TRAPA` under `tn=`.
- A `return` before the end writes a copy of the exit or, with two or more registers saved, PR aside, a branch to it.
- Each push and pop moves 4 bytes, an FR register's the width SZ gives.
- SHCASM writes the entry and the exit alike for `-code=machinecode` and `-code=asmcode`.

The frame alone doesn't settle R15 along a path: the body, SZ and the instructions' order matter too.

Established:

- **No direct `RTS`.** SHCASM passes on any `RTS` record it reads, but SHCGEN's ordinary builders and templates write none. The ordinary emitter's opcode arguments (`0x41fca0`) exclude `0x96`, the `RTS` record. The template interpreter's body roots (`0x4112e0`) lie among 548 static roots: 538 named descriptors (`0x454930..0x455a00`), the unnamed descriptor at `0x4428e8`, and nine direct bodies selected at `0x4131e5`. None of their 2,209 distinct rows emits `0x96`, and the mapped controls (`0x41c8e0`) produce none. The matcher (`0x421b30`) and the builtin selector (`0x40fa80`) read bounded descriptor fields: operand class 0 to 3 and builtin code 0 to 47. The interpreter's skips stay within each body's operand rows and can't skip its terminator (`0x412714..0x412717`). Inline text is outside these builders.
- **One exit label.** SHCGEN puts the exit label right before the exit (`0x40a1cc`, `0x40a505`). Its symbol and label guards (`0x4196fa`, `0x428a90`) keep the label in 181 to 32767, outside the named and case-label ranges. SHCFRT's symbol-count decrements cancel fresh allocations (`0x4259b0`, `0x4259e0`). SHCMDL's recovery discards temporary labels before it restores the counters (`0x41fdd3`, `0x41fde7`) and writes the original labels (`0x401598`). Every `RETURN` names the same exit label (`0x427fe2`, `0x428400`), so SHCPEP counts its reference (`0x40df24`).
- **`ENTER` first.** With optimization on, whenever SHCGEN writes `ENTER`, it puts it right after `FLABEL` (`0x409c99`, `0x409cf6`), and SHCPEP's transformations keep that prefix up to its final writer (`0x40f3c0`). Without optimization, the driver's stage table skips SHCPEP (`shc.exe`, `0x402cef..0x402d0f`, table `0x40f6e0`).
- **The section handoff.** Given that prefix and a valid form stream (open below), SHCASM's section handoff can't carry a stack instruction past it. With optimization on, the unflagged `FLABEL` reaches the slot-state reset (`0x414b46..0x414b65`, `0x414c30`). Without it, the section itself reaches the reset: `0x4146ec` and `0x414b18..0x414b1c` bypass the stashed-section path.
- **A return's slot.** For an `RTS` built from an exit, SHCASM's choice of its own slot instruction (`0x40c148..0x40c19e`) and SHCPEP's admission of a body instruction (`0x416a70..0x416abd`) test the same frame fields and exclude each other. SHCPEP refuses a body instruction that names R15 when the exit has a frame `ADD` or a PR pop (`0x416820`, `0x417570`). Which passes bring a body instruction to that test is open below.

Open, for both rules:

- **The order.** The final order of a frame's instructions is read in part:
  - SHCASM's scheduler keeps every write to R15 in order, inside windows that end at each label, branch and return, and has no cap, where R32's gives up at cycle 200 and moves what it hasn't issued to the top of its window ([stack-above-entry](#stack-above-entry)).
  - SHCPEP's register masks name R15 for every instruction that touches it but `DIV1 Rm,R15`, after which the rule loses R15's offset.
  - SHCPEP moves an instruction within straight-line code, or moves a join's first instruction past code it commutes with, and it can move the instruction before a case table's jump into the slot of a branch past the table. For a `switch` on a call with a stack argument, whose body opens with a `break` before its first case, it moves the call's `ADD #4,R15` into the slot of that `break`'s `BRA`, which no path runs, and the function returns 4 bytes below its entry. Each of its paths runs the call or the table's jump first, and the rule follows neither.
  - SHCASM puts the instruction SHCPEP chose for a conditional branch's slot in that slot, provided the branch forms it sized stay in step with the ones it builds.
- **The form stream, optimized.** SHCASM's form-stream writer (`0x407d60`) and reader (`0x407d00`) pair the local byte counts. The pool-drain argument also needs the source of SHCPEP's operand copy (`0x414e00`) to survive its allocator (`0x40df70`), which can flush and free head units before it retries. Only then do the sized and the written symbolic expressions agree.
- **The form stream, unoptimized.** SHCGEN supplies the stream and pool estimates (`0x428498`, `0x429340`), and its expression lists must be dense: the sizing writer continues past zero entries (`0x418d36..0x418d4e`), while the record writer emits only the nonzero prefix (`0x42fea6..0x42febe`). Its symbol classifier tests bit 0 first (`0x429323`), but its output keeps both low class bits (`0x408367..0x408379`), and SHCASM reads both (`0x4175ce`). SHCASM also gives reserved identifiers up to 180 class 0 (`0x4175b7`), and SHCGEN's classifier has no such guard (`0x4292f0`). Whether a `MOVI` can reach these differing classifications is unread.
- **After a case table.** That a case table always has a returned record after it, through SHCPEP's `str_chg` (`0x4150c8`).
- **Before a case-table jump.** Which R15-moving instruction the slot walk can take before a case-table jump (`0x416ba0`).
- **R15's writers.** That every body writer of R15 is one the rule tracks, or one whose offset it recovers by a later copy from R14 ([the stack rule](#the-stack-rule)). SHCGEN's R15 sentinel (`0x40417f`) can pass through generic operand selection (`0x41affa..0x41b139`), whose local checks don't show it is only read. SHCGEN also writes a `SUB` for a struct argument and a `MOV` for a stack switch ([stack-written](#stack-written)).

#### stack-written

- **Sweeps.** In the sweeps, R10 and R32 write R15 in these forms alone:
  - as a push's or a pop's address: `MOV.L`, `FMOV.S`, and `STS.L` and `LDS.L` of PR, MACH, MACL, FPUL and FPSCR. Both also push R15 itself, as the result's address for a call that returns a struct and for the software double routines, R10's without `-double=float` and R32's under `-cpu=sh3`;
  - by `ADD #imm,R15`, and for a frame over 127 bytes by `ADD R0,R15`, `ADD R1,R15` or `ADD R2,R15` after a `MOV.W` or `MOV.L` of its size;
  - by a `SUB` of R0, R1, R2 or R3, to make room for a struct passed by value;
  - by `MOV R0,R15` and `MOV.L @R15+,R15`, in the stack switch alone.
- **MOV R14,R15.** No output writes it, and the rule lets it by all the same. `MOV R15,R14` only copies R15.
- **The switch.** It loads x by its form, and every switch back is `MOV.L @R15+,R15` and `MOV.L @R15+,R0`, then `RTE` with `NOP` in its slot, or `TRAPA #n` under `tn=`:

| x | the load |
|---|---|
| an address, or a constant beyond 16 bits | `MOV.L L,R0` |
| a 16-bit constant, or a symbol under `#pragma abs16` | `MOV.W L,R0` |
| a constant from −128 to 127 | `MOV #imm,R0` |
| a pointer variable | its address, then `MOV.L @R0,R0` |
| a `#pragma gbr_base` pointer | `MOV.L @(d,GBR),R0` |
| a `gbr_base` variable's address | `STC GBR,R0`, then `ADD #d,R0` |

#### fpscr-save

- **Releases.** R32's functions in the sweeps push and pop FPSCR through R15 only where they return by `RTE` or end in `TRAPA`, but R32 wasn't read.
- **Frame.** R10 pushes and pops FPSCR through R15 only in the frame of a `#pragma interrupt` function, which returns by `RTE` with `NOP` in its slot, or ends in `TRAPA` under `tn=`. SHCASM builds the push as the entry's last, after the general registers, FR registers, PR, MACH, MACL and FPUL and before the frame's `ADD`, and the pop as the exit's first, after its `ADD`, each under one bit of the frame record SHCGEN writes.
- **Writers.** SHCGEN clears that bit for every function but an interrupt function, under `#pragma regsave` too, which sets it. From the same attribute it marks the record an interrupt's, so the exit ends in `RTE`, or `TRAPA` under `tn=`. Only for any other function does it end in `RTS`, and SHCASM builds no other `RTS`. SHCPEP builds FPSCR's pop only in the exit it writes for a tail call, under the same bit, and writes no `RTS`.
- **Builtins.** SHCGEN's own FPSCR operand is bare, in the template rows of `_builtin_get_fpscr` and `_builtin_set_fpscr`, beside an operand that resolves only to a register: that is why the builtins compile to `STS FPSCR,Rn` and `LDS Rm,FPSCR`. SHCPEP writes no memory operand that could merge either with a `MOV.L` through R15.
- **Probes.** The frame records SHCASM reads for 257 functions of the probes bear this out: 80 have the bit, each an interrupt function's; 5, of an interrupt function that neither calls nor uses the FPU, don't; and none of the 172 others does, those under `regsave`, `noregsave` and `noregalloc` among them.
- **Interrupt functions.** An interrupt function pushes FPSCR when it uses the FPU or makes a direct call, on the new stack after a `sp=` switch, and one whose calls all go through a pointer pushes none. R10 writes the pair for `-cpu=sh4` and `sh3e`, and none for `sh1` to `sh3`.
- **C.** C that keeps FPSCR across a call through the builtins keeps the copy in R8, R13 or R14 or in a frame slot, passes it as a fifth argument by `STS FPSCR,R3` and `MOV.L R3,@-R15`, and stores it through `*--p` by `STS FPSCR,R3` and `MOV.L R3,@-R4`. R10 merges none of them into a push of FPSCR, under any option the sweeps name for the SH-4.
- **Sweeps.** In the 2,901 texts R10 wrote in the sweeps' directories, 1,045 functions push and pop FPSCR through R15: 836 return by `RTE`, 209 end in `TRAPA`, and none holds an `RTS`.
- **Operands.** SHCGEN writes no direct `RTS` record in an interrupt function ([R10's frame](#abi-rules)). SHCASM and SHCPEP push and pop FPSCR only through R15, but SHCGEN's operands whose kind comes from a register, and SHCPEP's copies of operands, weren't all read, so the rule reads R15 alone.

#### stack-above-entry

- **Releases.** R32 takes R15 above its entry value under every `-fpu`, so the rule doesn't hold for R32. SHCASM's SH-4 scheduler gives up at cycle 200 and puts what it hasn't issued at the top of its window, above the entry's pushes when the window opens at the entry. In double precision eight chained divisions get there: `double f(double a, double b, double c, double d) { double x = 0.0; x /= a; x /= b; x /= c; x /= d; x /= a; x /= b; x /= c; x /= d; return x; }` compiles under `-speed` and `-size` to a function that starts with the epilogue's first pop, `FMOV.S @R15+,FR14`, a word of its caller's frame; seven keep the order. In single precision, where a division takes half as long, seventeen do. `shc -cpu=sh4 -fpu=single -optimize=1 -speed -code=asmcode` compiles

  ```c
  #pragma interrupt(h)
  extern float g, a;
  void h(void) { float x = g; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; x /= a; g = x; }
  ```

  to a function that pops before it pushes:

  ```
  _h:                              ; function: h
                                   ; frame size=20
            MOV.L       @R15+,R4
            FMOV.S      @R15+,FR4
            FDIV        FR3,FR4
            FMOV.S      @R15+,FR3
  ```
- **Frame.** R10's entry pushes and frame allocation lower R15. The exit builder uses the same saved-register masks, under `#pragma interrupt`, `noregsave` and `regsave` too. The arguments pushed for a call come off after it, by an `ADD`, or by the routine itself for R10's software double routines. The builders don't fix the order along every path through the body ([R10's frame](#abi-rules)).
- **Sweeps.** No R10 output of the sweeps takes R15 above its entry value on a path the stack rule follows. A return above the entry pops what the caller pushed, as runtime helpers written in assembly do.
- **Rests on** the frame argument's open parts ([R10's frame](#abi-rules)), and on two open parts of this rule alone:
  - **Tail calls.** The tail-call marker keeps its meaning through SHCMDL's later transforms. It is set on exit predecessors (`SHCMDL`, `0x40d137`), after inline expansion, but where it starts doesn't show it survives.
  - **Moved pops.** The source and width of each pop SHCPEP can fold or move across a call. SHCPEP's fold uses nominal widths, not SZ (`0x412b3b`, `0x412ba3`).
- **Refuted by** an R10 function compiled from C that takes R15 above its entry value on a path through its lines, with SZ as the rule knows it.

#### return-below-entry

- **Frame.** R10's entry and exit builders use the same saved-register masks. SHCPEP folds into the frame the exit frees an `ADD #n,R15` that runs right before the exit, with no label between: one `ADD #24,R15` frees a 16-byte frame and the last call's 8 bytes of arguments. A copy of the exit frees the folded bytes too, though its path never ran the `ADD`. In every probe with a `return` before the end, the exit keeps its label in front, so nothing folds.
- **C.** R10 uses the stack for call arguments and expression temporaries: SHCGEN can push a temporary (`0x412c77`, `0x412f39`) and restore one (`0x412aa0`) without a call. The rule follows no call. SHCASM writes the same exit for `-code=machinecode` and `-code=asmcode`. Past a call to a software double routine, which pops its own arguments, R15 would read below the entry.
- **SZ.** R10 compiles `_builtin_set_fpscr` to `LDS Rn,FPSCR`, which between a function's FR saves and their restores can change the restores' width: with SZ 1 at the entry and 0 written, four restores take back 16 of the 32 bytes four saves pushed, and the function returns 16 bytes below its entry. The rule loses SZ at the write, and R15's offset at the next FR pop. Nothing else changes SZ between them: `_builtin_fschg()` compiles to a call, and R10 writes `FSCHG` only in pairs inside the expansions of `_builtin_ld_ext`, `_builtin_st_ext`, `_builtin_ftrv` and `_builtin_fipr`, with no push or pop between.
- **Inline text.** R10 writes `.ALIGN 4` below each piece of inline text, so in its `-code=asmcode` text the rule stops there, before the `RTS` that ends a `#pragma inline_asm` function. In a text drop-inline-align has patched, it reaches the `RTS`, and a witness there stands on inline text.
- **Sweeps.** In the 2,901 texts R10 wrote in the sweeps' directories, each of the 55,229 `RTS`s the rule reached with a known offset, under `--sz 0` and under `--sz 1`, was at the entry value. The 149 it reached with an unknown one take or free a frame of 128 bytes or more by `ADD R0,R15` or `ADD R1,R15`.
- **Rests on** the frame argument's open parts ([R10's frame](#abi-rules)), and on one of this rule alone: that the body's pushes, restores and cleanup keep their paired widths under SZ 1. How SHCGEN pairs the temporaries' pushes and restores, and at what widths under SZ 1, isn't fully read. The exit label's range and the absence of direct `RTS` records are established, but they don't settle the body's paths. A return's slot holds one instruction: the exit's own, or one SHCPEP admits only where it crosses nothing that moves R15.
- **Refuted by** an R10 function compiled from C that returns below its entry value on a path the stack rule follows, with SZ as the rule knows it.

### What has no rule

**The entry registers.** C passes nothing in R0 to R3 or FR0 to FR3: arguments go in R4 to R7 and FR4 to FR11, and a struct return's address on the stack. No rule reads them at the entry all the same, because R10 and R32 read them for a variable C reads before it sets it:

- With optimization on, a value C never sets and only reads compiles to a read of R15 or FR15, which SHCGEN names in its records, or of DR14 under R32's double precision, and the software doubles read a stack slot.
- A variable C rewrites in place before it sets it, as in `x += a`, takes the register the allocator gives the result: an argument register the function leaves free, then R0 to R3 or FR0 to FR3, a callee-saved register after its push, or a stack slot. `int f(int a, int b, int c, int d) { int x; x += a; x += b; return x + c + d; }` reads R1 by `ADD R4,R1` before any write of R1, in both releases and modes: as its first instruction in R10, and as its second, after `MOV R6,R0`, in R32.
- In an interrupt function such a variable takes R4 to R7 and FR4 to FR11 first, then R0, FR0 or FR1, which hold the interrupted code's values.
- R32 reads FR0 to FR3 before it writes them in fully defined C too: under double precision it moves `FADD DR2,DR4` above the writes of FR2 and FR3 in `double f(double a, double b) { double x = a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; return x; }`.
- At `-optimize=0` every local lives in a stack slot.

These have no rule because their reading isn't whole:

- **CMP/STR.** In every release the encoder forms its word only for op 0x57, but the writes that could make that op haven't all been read.
- **CLRT and NEGC.** No probe or sweep writes either. CLRT's op, 0x8b, is no immediate, template row or byte constant in any stage of R10 or R32, but the writes that could make it haven't all been read, nor NEGC's.
- **R10's FPUL saves.** In R10, FPUL's push and pop come from the same byte of the frame record as FPSCR's, and in R10's texts in the sweeps appear only in functions that return by `RTE` or end in `TRAPA`, but R10's other writers of FPUL weren't read.
- **R32's stack.** R32's scheduler can move a pop above the entry's pushes under any `-fpu` ([why](#stack-above-entry)). It moves an instruction only within a window, and every label, branch, return and `ADD` to R15 ends one, so return-below-entry may yet hold for R32; the rest of that reading hasn't been done.
