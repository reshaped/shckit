# inspect

`shckit inspect --entry LABEL [--end LABEL] [--sz 0|1] [--all] FILE` looks in one function of a text for a witness: a pattern no Dreamcast SHC compiler is known to write from C. The pattern is an instruction, operands, or a use of R15.

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
build/shckit inspect --entry _cache --end _first x.src
build/shckit inspect --entry _first --end _pop x.src
build/shckit inspect --entry _pop --end _call x.src
build/shckit inspect --entry _call x.src
```

<!-- example kinds: output -->
```
x.src:2: _cache: opcode: cache-line
x.src:6: _first: operand: zero-displacement
x.src:12: _pop: abi: return-above-entry (SP +4)
x.src:13: _call: none
x.src:15: _call: incomplete: call
```

- `_cache` holds `OCBI`, an instruction no SHC release is known to write.
- `_first` loads through `@(0,R4)`. SHC is known to write a load at offset 0 only as `@R4`.
- `_pop` pops a word it never pushed, so its return leaves R15 4 bytes above where it was at the entry. No compile of C found so far does that. The [frame argument](#abi-rules), which would show that SHC 5.0 R10's can't, is unfinished.
- `_call` shows no witness, but [the stack rule](#the-stack-rule) stopped at the call on line 15: it can't follow a call, so for `return-above-entry`, `return-below-entry` and `fschg-pairs`, which follow its paths, `none` says nothing about the lines past it. The [instruction rules](#the-rules) read every instruction, reached or not. That command exits 3, for the limit, and the others exit 0. Without `--end`, `_call` runs to the end of the text.

## What a witness claims

A witness says one thing: no SHC compiler is known to write this pattern from C. It says so of the family, not of one release. A text comes with no release to ask about, so inspect takes none, and it takes no code mode for the same reason. A rule stands only while no release is known to write its witness from C, under `-code=machinecode` or `-code=asmcode`: it rests on the union of everything known.

The family is the SHC the Dreamcast's SDKs shipped: 5.0 and 5.1, and 6.0 AD, whose back end is 5.1 R13's. Later SHC is outside the claim: from 7.0 it has builtins for `SWAP` and for a 64-bit product.

"Not known" is the claim, and it is weaker than "can't". [Evidence](#evidence) says, for each rule, what was read for which release, what was compiled, and what is inference. Most rules were read for one or two releases and only compiled in the others, and some releases weren't read or compiled at all. A rule applies to a text from an unread release of the family all the same: the gap is stated, and it is no scope on the rule.

Inline text is outside the claim. SHC copies the text of `#pragma inline_asm` and `#pragma asm` into `-code=asmcode` text as written, and it can hold any instruction. `-code=machinecode` refuses both pragmas.

C that any release of the family compiles to a rule's witness, outside inline text, refutes the rule: it is dropped, or narrowed to what stays true. Each rule's evidence ends with what would refute it. If you find such C, please send a pull request: we want to grow these rules with the Dreamcast decompilation community.

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

One line names the witness and its line, or `none` and the entry's line, and one more names the limit, if inspect met one. A `return-above-entry` or `return-below-entry` witness names R15's offset after its line, and a `small-constant` or `integer-constant` one its constant:

```
x.src:3: _f: abi: return-above-entry (SP +8)
x.src:5: _f: operand: small-constant (#-1)
x.src:1: _f: none
x.src:4: _f: incomplete: call
```

With several witnesses, inspect reports one: an opcode's before an operand's before an ABI one, then the earliest line's, then the rule listed first in the [rules table](#the-rules). Of several limits it names the first line's, and of two at one line `continued`, then `pool-word`, then `integer-constant`'s, then the stack rule's. A witness holds even when inspect also reports a limit.

With `--all`, inspect reports every witness, a line each: each rule's at each line where the rule finds one. They come in line order, and at one line in the rules table's order. With no witness, inspect prints `none`. The limit follows as without `--all`. The witness inspect reports without `--all` is among them. For `y.src`:

<!-- example all: input y.src -->
```
_f:
          SETT
          MOV.L       @R15+,R1
          MOV.L       @(0,R4),R0
          RTS
          ADD         #4,R15
```

<!-- example all: run -->
```sh
build/shckit inspect --entry _f --all y.src
```

<!-- example all: output -->
```
y.src:2: _f: opcode: sett
y.src:4: _f: operand: zero-displacement
y.src:6: _f: abi: return-above-entry (SP +8)
```

The pop on line 3 takes R15 4 bytes above the entry, which shows nothing by itself, and the slot on line 6 leaves it 8 bytes above, a `return-above-entry` witness. Without `--all`, inspect reports line 2's alone.

inspect first checks the text as [check](check.md) does. If check refuses the text, inspect prints check's findings instead. R10's `-debug` text is one it refuses: check can't read the `.LINE` lines R10 writes there.

Exit status:

- **0** an inspection that met no limit, witness or not;
- **1** a text shckit refuses: one it can't read, or one check refuses;
- **2** a command-line, I/O or memory error, a label missing or defined twice among them, or a function inspect can't take: an `--end` at or above the entry, or an entry that isn't a label or an instruction in a code section;
- **3** an inspection that met a limit, witness or not.

## The rules

| rule | kind | witness |
|---|---|---|
| [`rotcl`, `rotcr`, `swap`](#rotcl-rotcr-and-swap) | opcode | `ROTCL`, `ROTCR`, `SWAP.B`, `SWAP.W` |
| [`dmuls`, `dmulu`](#dmuls-and-dmulu) | opcode | `DMULS.L`, `DMULU.L` |
| [`movca`](#movca-and-cache-line) | opcode | `MOVCA.L` |
| [`cache-line`](#movca-and-cache-line) | opcode | `OCBI`, `OCBP`, `OCBWB` |
| [`sett`](#sett) | opcode | `SETT` |
| [`fipr-registers`](#fipr-ftrv-and-fsca) | operand | `FIPR` other than `FIPR FV4,FV0` |
| [`ftrv-registers`](#fipr-ftrv-and-fsca) | operand | `FTRV` other than `FTRV XMTRX,FV0` |
| [`fsca-registers`](#fipr-ftrv-and-fsca) | operand | `FSCA` other than `FSCA FPUL,DR0` |
| [`small-constant`](#small-constant) | operand | a `MOV.L` of a [constant](#constants) from −128 to −1 |
| [`integer-constant`](#integer-constant) | operand | a `MOV.L` of a [constant](#constants) from 0 to 32767 or from −32768 to −129 that each path from it reads first as an integer, or passes unread to a call ([more](#integer-constants)) |
| [`zero-displacement`](#zero-displacement) | operand | a `MOV.B`, `MOV.W` or `MOV.L` through `@(0,Rn)` |
| [`fschg-pairs`](#fschg-pairs) | operand | an `FMOV` between memory and FR8, FR10, FR12, FR14 or DR8 to DR14, after an odd number of `FSCHG`s on every path the stack rule follows to it ([more](#pairs-after-fschg)) |
| [`stack-written`](#stack-written) | abi | a write to R15 other than a push's or pop's address, `ADD`, `SUB`, `MOV R14,R15` and the stack switch ([more](#writes-to-r15)) |
| [`fpscr-save`](#fpscr-save) | abi | `STS.L FPSCR,@-R15` or `LDS.L @R15+,FPSCR`, in a function that holds an `RTS` ([more](#fpscrs-save)) |
| [`return-above-entry`](#return-above-entry) | abi | R15 above its value at the entry, after the slot of an `RTS` or `RTE` the stack rule reaches ([more](#above-the-entry)) |
| [`return-below-entry`](#return-below-entry) | abi | R15 below its value at the entry, after the slot of an `RTS` the stack rule reaches ([more](#below-the-entry)) |

`return-above-entry` and `return-below-entry` rest on the [frame argument](#abi-rules), which is unfinished and was read for R10 alone. No compile of C found so far makes either witness.

Every rule but `fschg-pairs`, `return-above-entry` and `return-below-entry` is an instruction rule, which reads each instruction of the function in a code section, reached or not, as check reads it: in any case and with the sizes asmsh takes. Data, comments and the lines past `.END`, which check passes only blank or commented, hold none. `integer-constant` reads each load so, and from it follows the paths the stack rule follows, past calls too ([more](#integer-constants)).

`PREF`, `LDC` and `STC` of a control register, `FRCHG` and `FSCHG` show nothing: R10 writes each of them for a builtin, and R32 each but `FSCHG`. [What has no rule](#what-has-no-rule) says what else has no rule, and why.

### Constants

**A constant** is a number in decimal, `H'`, `B'`, `Q'` or `D'`, with an optional sign, or a symbol whose definition is an `.EQU` or `.ASSIGN` of such a number. It is read as asmsh reads it, a 32-bit word: `H'FFFFFFFF` and `-1` are both −1, and `-H'FFFFFFFF` is 1. An expression is no constant. A symbol `.ASSIGN` defines takes the value of the `.ASSIGN` [it names](#the-function) at its use, and 0 above the first.

**small-constant** and **integer-constant** read `MOV.L #x,Rn`, which asmsh expands into a pool load, and a PC-relative `MOV.L label,Rn` or `MOV.L label+n,Rn`, n in decimal, of the `.DATA.L` item n bytes past the label's definition, counted through the `.DATA` and `.RES` lines below it. Above x's first `.ASSIGN`, asmsh gives `#x` the value x has where it writes the pool, not 0, and inspect doesn't place the pool, so x is no constant there.

**zero-displacement** reads an `@(d,Rn)` whose d is the constant 0. asmsh assembles `@(0,Rn)` as the displacement form with 0, not as `@Rn`.

A pool word they can't read is the limit **pool-word**:

- one loaded through an expression other than label or label+n, through a label with no definition, or at a displacement from PC;
- one inside an item or a pad, or in a `.DATA.W` or `.DATA.B` item;
- one in or past a data line with a continuation, or past any line but `.DATA`, `.RES`, a label, a blank and a comment, such as `.ALIGN`, `.DATAB`, `.SDATA` or `.FDATA.S`.

So is a word in a run of data the text names, where the word is from −128 to −1 or its use makes `integer-constant`'s witness. The run is the data lines around the word and the lines before and among them other than an instruction, a `.SECTION` and an `.END`. The text names it where the run has a label, up to its last data line, that a line names other than as the source of a PC-relative `MOV.W` or `MOV.L`, as an `.EXPORT`, `MOVA`, a `.DATA` item or `#label` does. Such a word may be a variable, and inspect, which computes no addresses, decides that by names alone.

### Integer constants

**integer-constant** reads a load as `small-constant` does, of a constant from 0 to 32767 or from −32768 to −129: a word `MOV #imm` or `MOV.W` could load. SHC is known to load such a word by `MOV.L` only as a float's, for `LDS` ([evidence](#integer-constant)), so the rule reads what the word is used for. From the load it follows the paths the [stack rule](#the-stack-rule) follows, and past calls, knowing at each line which registers hold the word. A copy, `MOV Rm,Rn`, carries the word to Rn, and a write of a register takes it away. The first line on a path that reads a register holding the word settles the path:

- as the witness where it reads the word as an integer: by `ADD`, `SUB`, `AND`, `OR`, `XOR`, `NOT` or `NEG`, a shift or a rotate, `CMP/xx`, `TST` or `DT`, `EXTS` or `EXTU`, `MUL`, `MULS`, `MULU`, `DMULS`, `DMULU`, `DIV0S` or `DIV1`, or as the address or the index of any instruction, a load, a store, `JMP` and `JSR` among them;
- as none where it reads it any other way: by an `LDS` or `LDC` of a system register, `LDS Rn,FPUL` among them, as the data of a store or a push, or by `ADDC`, `ADDV`, `SUBC`, `SUBV`, `NEGC`, `XTRCT`, `SWAP`, `BRAF` or `BSRF`.

Where control leaves a line, past its slot:

- a `JSR`, `BSR` or `JMP` with the word unread in R4 to R7 settles the path as the witness;
- any other call, `JSR`, `BSR` or `BSRF`, takes R0 to R7, and the path goes on with the word in R8 to R15;
- a return, `RTS` or `RTE`, a `TRAPA`, a `BRAF`, any other `JMP` and a branch out settle the path as none.

A write that leaves no register holding the word settles the path as none too. So does an `LDC` to SR, or an `RTE` ahead of its slot, with the word in R0 to R7: either can switch the bank those names mean, and the SH-4 runs an `RTE`'s slot with SR already restored. Where paths join, a register holds the word only where it does on each of them: a read of one that holds it on some alone settles the path as none, and so does a join that leaves no register holding it on all. A loop runs until each line's facts settle, and a path that never reads the word settles nothing.

The load is a witness where a path settles as the witness and no path settles as none or meets a [limit](#limits). Where a path meets a limit and no path settles as none, the limit is the inspection's. A word whose run of data the text names is the limit `pool-word` in place of the witness, as for `small-constant`. The words from −128 to −1 are `small-constant`'s, whatever their use. For `w.src`:

<!-- example integers: input w.src -->
```
_clear:
          MOV         #0,R0
          MOV.L       #H'00000080,R7
?loop:
          MOV.L       R0,@R1
          DT          R7
          BF/S        ?loop
          ADD         #4,R1
          RTS
          NOP
_store:
          MOV.L       L1,R1
          LDS         R1,FPUL
          FSTS        FPUL,FR3
          RTS
          FMOV.S      FR3,@R4
L1:
          .DATA.L     H'00000080
```

<!-- example integers: run -->
```sh
build/shckit inspect --entry _clear --end _store w.src
build/shckit inspect --entry _store w.src
```

<!-- example integers: output -->
```
w.src:3: _clear: operand: integer-constant (#128)
w.src:11: _store: none
```

`DT` reads `_clear`'s count first on every path. In `_store`, `LDS` reads the same word first, as SHC writes `p[i] = 1.794e-43f` under `-denormalize=on`. SHC loads the integer 128 by `MOV.W`.

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

The switch is exempt only with its head whole:

- its head stands above the function's first branch, call, jump or return, though other instructions may stand above it: SHC's scheduler [puts them there](#stack-written);
- between `MOV.L R0,@-R15` and `MOV.L R15,@-R0`, the head loads R0 from no other general register: first by a `MOV` of `#imm`, a label, `@(disp,PC)` or `@(disp,GBR)`, or by `STC GBR,R0`, then by any more of those, a `MOV` of `@R0`, an `ADD #imm,R0`, an `EXTU.B` or a `NOT` of R0 into itself, a `SHLL2 R0` or a `SHLL8 R0`;
- in a function with such a head, its `MOV R0,R15` is exempt, and so is each `MOV.L @R15+,R15`, wherever it stands;
- lines control passes may stand between the head's instructions, and its `MOV`s may go without a size.

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

The stack rule follows control through the function and finds three witnesses: [R15 above the entry after a return](#above-the-entry), [R15 below it after a return](#below-the-entry), and [a pair's move after FSCHG](#pairs-after-fschg).

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

`return-above-entry` and `return-below-entry` rest on SZ as the rule knows it, with `--sz` or without: a write to FPSCR between a function's FR saves and their restores can change the restores' width, and the rule makes SZ unknown there.

### Above the entry

R15 above its entry value once a return's slot has run, an `RTS`'s or an `RTE`'s, is a `return-above-entry` witness, at the slot's line, naming R15's offset after it. The rule reports it even on a path that never runs. That no compile of C does this rests on the [frame argument](#abi-rules), which is unfinished. R15 above the entry anywhere else shows nothing: SHC's scheduler can put a pop above the entry's pushes ([why](#return-above-entry)). A branch out shows nothing either. Here the return's slot leaves R15 8 bytes above the entry, a witness at line 3:

```
_f:
          RTS
          ADD         #8,R15
```

### Below the entry

R15 below its entry value once an `RTS`'s slot has run is a `return-below-entry` witness, at the slot's line, naming R15's offset after it. The rule reports it even on a path that never runs. That no compile of C does this on a path the rule follows rests on the same unfinished [frame argument](#abi-rules). Before then, R15 below the entry is a frame; after an `RTE`'s slot, or a branch out, it shows nothing. Here the epilogue leaves out a pop, a witness at line 7:

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

`--sz 0` or `--sz 1` gives FPSCR.SZ at the entry, which sets the width of an `FMOV` through memory. Without it, the rule runs under SZ 0 and under SZ 1, since the entry's SZ is one of them. `return-above-entry` or `return-below-entry` stands only where both runs find it, naming the offset nearer the entry: under either SZ, R15 is at least that far from it. One that a run finds alone is the limit `stack-unknown` at its line, and a limit either run meets is the inspection's. For `z.src`:

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
build/shckit inspect --entry _f --sz 0 --all z.src
build/shckit inspect --entry _f --sz 1 --all z.src
build/shckit inspect --entry _f --all z.src
```

<!-- example sz: output -->
```
z.src:1: _f: none
z.src:5: _f: abi: return-above-entry (SP +4)
z.src:1: _f: none
z.src:5: _f: incomplete: stack-unknown
```

The pop on line 2 moves R15 4 bytes with SZ 0 and 8 with SZ 1. The slot on line 5 brings R15 back to the entry with SZ 0 and leaves it 4 bytes above with SZ 1, so with `--sz 1` line 5 is a witness, and without `--sz` it is the limit `stack-unknown`.

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
| `pool-word` | a `MOV.L` of a pool word inspect can't read, or of one in a run of data the text names, where the word is from −128 to −1 or its use makes `integer-constant`'s witness |
| `instruction-effect` | an instruction whose effect the stack rule or `integer-constant` doesn't know; every one in the [opcode table](opcodes.md) has one |

Every limit names a line. `continued` and `pool-word` are limits wherever the line stands in the function; any other is one only where control reaches its line. `integer-constant` meets each of them but `call`, `opaque-transfer` and `stack-unknown` where a path from a load it follows reaches it: it follows calls and `SLEEP`, and a `JMP`, `BRAF` or `TRAPA` settles its path.

## Evidence

What is known differs by release, and each rule says so. A **reading** is of SHC's stages: its front end, shcfrt, its other stages shc and shcmdl, and its back end's stages, SHCGEN, SHCPEP and SHCASM, which turn a function's records into instructions and write them as an object or as text. A **compile** is C put through a release, with inspect reading what it wrote.

| release | read | compiled |
|---|---|---|
| 5.0 R10 | every rule, the frame argument unfinished, and of `integer-constant` only how integer constants are loaded | [the sweeps](#the-sweeps), [the family compiles](#the-family-compiles) |
| 5.0 R32 | the opcode rules, `fipr-registers`, `ftrv-registers`, `zero-displacement`, and the scheduler | [the sweeps](#the-sweeps), [the family compiles](#the-family-compiles) |
| 5.0 R26, R27, R28, R31 and 5.1 R01, R03, R04, R08, R11, R13 | the encoder for `rotcl`, `rotcr`, `swap`, `dmuls` and `dmulu`, and each stage's text | [the family compiles](#the-family-compiles) |
| 6.0C.001 | the encoder for `rotcl`, `rotcr` and `swap` | the stack switch under `-extra=a=1000` ([stack-written](#stack-written)) |
| any other release, 6.0 AD among them | nothing | nothing |

`integer-constant` rests on [its census](#integer-constant) of the family compiles and of probes of its own, beside what R10's reading shows of integer constants. A reading of one release says nothing by itself of another. A compile shows what a release writes for the C tried, and nothing of other C. What carries a rule across releases is inference: the releases from R26 to 5.1 R13 name the same builtins, R10 differs only in `_builtin_fschg`, `_builtin_frchg`, `_builtin_fabs` and `_builtin_sqrt`, and no compile of any of them outside inline text writes a witness. A release can still differ in code nobody read, and a later release can add a builtin. That is why a witness says "not known" and no more.

An address cited is a virtual address in one of R10's stages, under image base 0x400000:

- `shcfrt.exe`, SHA-256 `e3223c795644fd137c3ac400440d7e31bd67740bb76890e2f6f50feee5412ec4`;
- `shcmdl.exe`, SHA-256 `00ee4f7220cb65d3bed2e3d7cb5c687a3c06e97b5e432470c7d7cb7584ac377b`;
- `shcgen.exe`, SHA-256 `9963ce29e1c9c1d9b5756a0b703332cbd23cee849805ebea8a967fe48929e789`;
- `shcpep.exe` and `shcasm.exe`, as patches.md's [evidence base](patches.md#evidence-base) gives them.

The readings assume that every allocation the compiler makes succeeds. When one fails, SHCPEP writes out the records it holds, one it is part way through copying among them, and a compile's exit status doesn't show that none failed. Whether such a compile can still end in an object wasn't settled.

A rule that rests on the records SHC's stages write also rests on SHCGEN framing each initializer as shcfrt wrote it, since SHCASM would read a misframed one's data as records. The probes found no misframed initializer, but no reading shows that none can be, and a C source that a misframing turns into a witness refutes the rule it breaks. dmuls, dmulu, movca, cache-line and zero-displacement don't depend on it: they rest on SHCASM's encoder alone.

### The sweeps

2,181 C functions in 100 files, written to exercise control flow, frames, constants and floating point, went through 3,684 compiles by R10 and R32. Each compile wrote `-code=asmcode` text, or a `-code=machinecode` object whose listing became text.

| for the SH-4 | R10 | R32 |
|---|---|---|
| optimization | `-optimize=0` and 1, for size and for speed | `-optimize=0` and 1, for size and for speed |
| build | an SDK build's `-double=float -size -extra=a=400` and the options that go with them | `-fpu=single`, `-fpu=double` or no `-fpu` |
| endianness | little and big | little and big |
| one at a time | `-pic=1`, `-inline`, `-loop`, `-macsave=1`, `-rtnext`, `-abs16=all` or `-align16` | `-pic=1`, `-denormalize=on`, `-round=nearest`, `-loop`, `-inline` or `-macsave=1` |

R10 also compiled them for `-cpu=sh1` to `sh3e`, and R32 for `-cpu=sh3`.

The rules cite two populations:

- **The compiles' texts.** check read 3,321 of them, and refused 231 at first: for the `.DATAB` lines SHC writes for a partly initialized aggregate or a `#pragma gbr_base` variable, and for a pool right after the `TRAPA` that ends a `tn=` function. In the constants sweep inspect read copies that write each `.DATAB` line as zero `.DATA` items. inspect read every function of the other 3,090 texts, without `--sz` and with `--sz 0`: none of the 139,278 inspections, 82,112 of R10's texts and 57,166 of R32's, found a witness. Those inspections ran each rule only on the releases it was read for, so the stack rule read R10's texts alone, and `small-constant` and `fpscr-save` too. The family compiles run those rules on every release.
- **The texts in the sweeps' directories.** check now reads both such `.DATAB` lines and a pool right after a `tn=` function's `TRAPA`, and passes all 5,462 texts in the sweeps' directories, copies and cut-down cases among them. 2,901 of them are R10's, and 1,408 have listings. Of the 536 check refused before, inspect read the 4,581 functions the same way, and none of the 8,980 inspections found a witness.

### The family compiles

C put through each release from 5.0 R10 to 5.1 R13, and every function read with every rule but `integer-constant`, without `--sz` and with `--sz 0`. 6.0C.001 and 6.0 AD aren't among them. There are two populations:

- **Probes.** C written to reach each rule: the vector, FSCA and system builtins, float words and constants, chained divisions, interrupt functions and their stack switches, frames and mixed float and double code. It went through `-optimize=0` and 1, for size and for speed, under no `-fpu`, `-fpu=single`, `-fpu=double`, `-fpu=single -denormalize=on` and `-double=float`, in both modes, R10 under the first and the last alone: it has no `-fpu`, `-denormalize` or `-round`. One at a time, it also went through `-cpu=sh1` to `sh3e`, `-pic=1`, `-macsave=1`, `-round=nearest`, `-round=zero`, `-loop`, `-inline`, `-rtnext`, `-abs16=all`, `-align16`, `-endian=big`, `-division=cpu`, `-extra=a=400`, `-extra=a=800` and `-debug`.
- **Retained compiles.** C nobody wrote to reach a rule: `-code=machinecode` listings, which became text, and `-code=asmcode` text.

| release | retained texts | functions | probe texts | functions |
|---|---|---|---|---|
| 5.0 R10 | 530 | 4,509 | 307 | 3,418 |
| 5.0 R26 | 15 | 202 | 658 | 6,666 |
| 5.0 R27 | 3 | 70 | 668 | 6,774 |
| 5.0 R28 | 28 | 233 | 661 | 6,683 |
| 5.0 R31 | 120 | 326 | 663 | 6,737 |
| 5.0 R32 | 3,172 | 23,754 | 669 | 6,806 |
| 5.1 R01 | 92 | 1,298 | 616 | 6,700 |
| 5.1 R03 | 85 | 1,304 | 661 | 6,798 |
| 5.1 R04 | 716 | 9,207 | 672 | 6,878 |
| 5.1 R08 | 118 | 1,345 | 637 | 6,481 |
| 5.1 R11 | 998 | 12,419 | 651 | 6,577 |
| 5.1 R13 | 2,065 | 21,383 | 650 | 6,633 |

Outside inline text, none of these 153,201 functions holds a witness of any of those rules. `integer-constant` came later, and it finds its witness only in inline text: [its evidence](#integer-constant) says which of them it read. They hold 3,654 `FIPR`s, 10,083 `FTRV`s and 3,213 `FSCA`s with the templates' operands alone, and every `FSCHG` among them is R10's. Four rules took their present form from these compiles: `small-constant`, `integer-constant`, `stack-written` and `return-above-entry`. For those four the compiles confirm the form rather than test it, and C nobody tried can still refute it. What shaped them is C written to reach them: no retained compile loads a word from 0 to 127 by `MOV.L`, none of those still on disk loads one up to 32767 outside inline text, and none moves a stack switch or takes R15 above the entry on a path the stack rule reads.

The stack rule reads less than the instruction rules do. It read 80,762 of the 153,201 functions with no limit. It stopped at a call in 61,182 and at another limit in 11,257, and for those the stack witnesses say nothing past that line. So `return-above-entry` and `return-below-entry` rest mostly on the leaf functions and the paths before a first call, where `stack-written` and `zero-displacement` read every instruction.

### Opcode rules

In R10 and R32, SHCASM encodes every instruction from its form rows, ORing a row's operand fields into its base word, so a row's fields only add bits to it. In those two, one function of SHCASM writes an instruction's word or its text, printing the mnemonic of the form row the word would come from, and SHCASM's other text is directives, operands, names, comments and the inline text it copies. Under `-code=machinecode`, every 5.x release refuses `#pragma inline_asm` with error 2808 and `#pragma asm` with 2708.

#### rotcl, rotcr and swap

- **Read.** Under `-code=machinecode` the reading covers every 5.x release and 6.0C.001. Under `-code=asmcode` it covers 5.0 R10 and R32, whose text writer was read. For the others' text it is inference.
- **Encoder.** In each release, SHCASM's encoder forms ROTCL's word only for internal op 0x4f, ROTCR's only for 0x5f, and SWAP.B's and SWAP.W's only for 0x46. No other of its form rows can make the four words.
- **Writers.** No stage writes the three ops: not SHCGEN's record writers, template rows or kind mapping, not SHCPEP's node writer, byte constants or shift table, and not SHCASM's builders.
- **Text.** Beyond their form rows, whose mnemonics SHCASM's text writer prints, only SHCASM's and SHCPEP's tables of op names spell ROTCL, ROTCR and SWAP, only debug dumps read those tables, and no other stage holds the names.
- **C.** No front end has a rotate or swap builtin. Byte swaps compile to shifts, masks and ORs, or to byte moves through memory, and a bit shifted from one word into the next to `ROTL` or `ROTR` and `AND #1`.
- **Sweeps.** No `-code=asmcode` text of the sweeps holds any of the four.
- **Refuted by** any release writing `ROTCL`, `ROTCR`, `SWAP.B` or `SWAP.W` for C, in either mode, outside inline text.

#### dmuls and dmulu

- **Read.** Every 5.x release. 6.0C.001 wasn't read for them.
- **Encoder.** No release's encoder has a form for either word. R10's form table has 199 rows and each other release's 182, and none names DMULS.L or DMULU.L or can make its word.
- **Text.** No stage of any release holds the text DMUL, so `-code=asmcode` text can't spell them either.
- **C.** Every front end refuses `long long` with error 2102, and none has a builtin for a 64-bit product. Products compile to `MUL.L` or call `__muli`, and divisions call the runtime.
- **Refuted by** any release writing `DMULS.L` or `DMULU.L` for C, in either mode, outside inline text.

#### movca and cache-line

- **Read.** R10 and R32. For the others it is inference from the text below and the compiles.
- **Encoder.** None of R10's 199 rows or R32's 182 can form the word of `MOVCA.L R0,@Rn`, `0000nnnn11000011`, or of `OCBI`, `OCBP` or `OCBWB @Rn`, `0000nnnn10010011`, `10100011` and `10110011`, even with each field taken to set any twelve bits at its shift.
- **Text.** No stage of any release from R10 to 5.1 R13, shc, shcfrt, shcmdl, SHCGEN, SHCPEP or SHCASM, holds the text MOVCA or OCB, so `-code=asmcode` text can't spell them either. They reach it only as inline text.
- **C.** The prefetch builtin's record maps to PREF, whose register operand selects no other cache operation.
- **Sweeps.** No compiled function of the sweeps holds any of them.
- **Refuted by** any release writing `MOVCA.L`, `OCBI`, `OCBP` or `OCBWB` for C, in either mode, outside inline text.

#### sett

- **Read.** R10 and R32. For the others it is inference from the compiles.
- **Encoder.** In R10 and R32, SHCASM's encoder forms SETT's word, H'0018, only from SETT's form row, which it reaches only for internal op 0x8f. The one other row whose base word has no bit outside H'0018 is CLRT's, H'0008, which has no fields, as SETT's has none.
- **Writers.** No stage of either release writes op 0x8f: not SHCGEN's record writers, its record constructor, its template rows or its kind mapping, not SHCPEP's node writer or any other store of a record's op, and not SHCASM's record builders or the records it fills a delay slot with. SHCGEN's op table marks 0x8f valid, and SHCASM's reader would encode a record of it, so the rule rests on there being no writer.
- **Text.** The text SETT is in no stage but SETT's row and the tables that name records in a debugging dump, so asmcode text spells `SETT` only as inline text.
- **C.** C that computes with T compiles to other instructions: a signed `(x+y+1)/2` to `MOV #0,R3`, `CMP/GT R0,R3`, `ADDC R3,R0` and `SHAR R0`, `(a+b+1)>>1` to `ADD`, `ADD #1` and a shift, and `_builtin_tas` and `_builtin_gbr_tst_byte` to `TAS.B` or `TST.B` and `MOVT`.
- **Sweeps.** None of the 5,462 texts in the sweeps' directories holds `SETT`, against 151 that hold `ADDC`. In their 1,408 listings H'0018 is a code word only twice, each a `.DATA.W` entry of a switch table.
- **Rests on** every store that could write a record's op. In R10 each was read. In R32 a scan sorted some of SHCPEP's and SHCASM's.
- **Refuted by** any release writing `SETT` for C, in either mode, outside inline text.

### Operand rules

#### fipr, ftrv and fsca

- **Read.** R10 and R32 for FIPR and FTRV, R10 alone for FSCA. For the others it is inference from the compiles.
- **Templates.** R10 writes FIPR, FTRV and FSCA, and R32 FIPR and FTRV, only for builtins, each from a template that fixes its operands.
  - The FIPR template fixes `FV4,FV0`, and the scalar dispatcher has no FIPR exit.
  - Each of the 15 FTRV template rows fixes `XMTRX,FV0`.
  - The FSCA template fixes `FPUL` and `DR0`: with two results live at once, R10 writes `DR0` twice and stores the first between, never `DR2`.
  - A `#pragma global_register` for an FR register doesn't move these operands.
- **Encoder.** R32's SHCASM forms FIPR's word only for internal op 0xd6 and FTRV's only for 0xd8, and takes the vector fields from the record's operands unchecked.
- **Writers.** In R32 only SHCGEN's template rows write the two ops, and its register map takes FV and XMTRX to themselves. SHCPEP changes operands only in records of other ops, renames only integer registers, and copies a record whole. SHCASM changes no record's registers. SHCGEN's data stream, which reaches SHCASM past SHCPEP, holds only data directives.
- **C.** R32 writes them for `_builtin_fipr`, `_builtin_ftrv`, `_builtin_ftrvadd`, `_builtin_ftrvsub`, `_builtin_mtrx4mul`, `_builtin_mtrx4muladd` and `_builtin_mtrx4mulsub`, and refuses each under `-fpu=double` with error 2804, so the rules hold under every `-fpu`.
- **Probes.** R32 compiled three files of builtin calls in both modes under `-cpu=sh4 -division=cpu -endian=little -round=zero -pic=0 -string=const -comment=nonest -sjis -fpu=single -optimize=1 -speed -nodebug -macsave=0 -extra=a=800`, adding in turn each of `-loop`, `-align16`, `-rtnext`, `-abs16=all`, `-pic=1`, `-denormalize=on`, `-inline=200 -nestinline=4`, `-noinline`, `-double=float`, `-macsave=1`, `-division=peripheral`, `-debug`, `-extra=a=400`, `-extra=a=1000`, `-endian=big` and `-round=nearest`. They write 752 FIPRs and 640 FTRVs, each `FV4,FV0` or `XMTRX,FV0`.
- **Compiles.** In the family compiles every release from R10 to 5.1 R13 writes these builtins' FIPR as `FV4,FV0`, FTRV as `XMTRX,FV0` and FSCA as `FPUL,DR0`, with the same pointer passed twice, with results live together, and with two FSCA results live at once.
- **Refuted by** any release writing FIPR, FTRV or FSCA with other operands for C, in either mode, outside inline text.

#### small-constant

- **Read.** R10. For the others it is inference from the compiles.
- **Writers.** In the probes, R10's SHCASM writes every PC-relative load itself, expanding the records SHCGEN and SHCPEP hand on for a constant, a call or a jump, a frame slot or its address, a label's address, a function's address under `-pic=1`, and a float constant. Its reader can take a PC-relative operand from SHCPEP's records, but none of the probes' records holds one.
- **Expansion.** SHCASM lowers four kinds of record into constant loads: an integer constant, a frame slot, a frame slot's address, and an address taken PC-relative. The first three go by the value: one from −128 to 127 that names no symbol becomes `MOV #imm,Rn`, one that fits 16 bits a `MOV.W` pool load, and only a wider one or a symbol a `MOV.L` pool load. The fourth skips the first test, so a value from −128 to 127 there would become a `MOV.W` pool load, though no probe's record reaches it with such a value. None of the four loads such a value by `MOV.L`, which is all the rule reads.
- **Options.** `#pragma abs16`, `-abs16` and a GBR section only move a symbol's load to `MOV.W` or `MOV #imm`. A frame slot's offset goes by value the same way once `@(disp,R15)` and `ADD #imm` can't reach the slot. A call, a jump and a function's address load a symbol, or under `-pic=1` a difference of symbols. So no option changes a constant's load.
- **Text.** SHCASM writes no `.EQU` or `.ASSIGN`, so a symbol it loads is no constant in its text. It has no form for `MOV.L #x`, and it writes `-code=asmcode` text from the same expansion and encoder as the object.
- **Sweeps.** In the sweeps R10 loads every integer from −128 to 127 by `MOV #imm,Rn`, or compares it by `CMP/EQ #imm,R0`. Its `MOV.W` items hold 16-bit values, and its `MOV.L` items only wider values, float words outside the range and symbols.
- **Floats.** A float constant is the one load whose value SHCASM doesn't test. Its word goes through `MOVA` and `FMOV.S`, or through `MOV.L`, `LDS` and `FSTS`, or under `-cpu=sh3` through `MOV.L` and a store. That is why small-constant leaves 0 and the words above it to [integer-constant](#integer-constant), which reads their use. A word from −128 to −1 as a float is a NaN with its sign set and a mantissa of H'7FFF80 or more, and C has no literal for one.
- **Compiles.** In the family compiles every release from R10 to 5.1 R13 folds `0.0f/0.0f` and its negation to H'7F800001, and R10 folds a `double` NaN to H'7FF00000. A `double` whose low word is H'FFFFFFFF or H'FFFFFF80 loads that word by `MOVA` and `FMOV.S`, or under `-cpu=sh3` by `MOV #imm,Rn`. None loads a word from −128 to −1 by `MOV.L`.
- **Rests on** the probes and the compiles, for floats: where SHC turns such a NaN into other loads wasn't found. A pool word SHC writes as a symbol, or as a difference of symbols, can hold any number once linked, and a text that writes such a word as its number is outside this evidence.
- **Refuted by** any release loading a word from −128 to −1 by `MOV.L` for C, in either mode, outside inline text.

#### integer-constant

- **Read.** R10's expansion of constants, as [small-constant](#small-constant) gives it: of the records SHCASM lowers into constant loads, it loads none whose value fits 16 bits by `MOV.L`, and it doesn't test a float constant's value. How a float's word is loaded, and what reads it, wasn't read in any release. For the rest, and for every other release, the rule rests on the compiles.
- **Census.** A census read each PC-relative `MOV.L`, resolved its pool word and checked it against SHC's own `; H'...` comment, with no mismatch, and followed the loaded register to its first read on every path, through slots, branches and copies. It left inline text out. It read three populations:
  - the retained compiles still on disk: 7,085 texts of 65,810 functions, of every release from R10 to 5.1 R13;
  - the family compiles' probes: 7,513 texts of 77,151 functions;
  - probes of its own, in 8,186 compiles: 5,643 texts of 129,804 functions. Their C passes integer constants as arguments and returns them, switches on them densely, sparsely and widely, compares, masks, shifts, multiplies and divides by them, stores them in fields and bitfields, copies, initializes and returns structs, passes varargs, uses absolute pointers, large offsets and frames, 128-count loops, enums and tables, and calls a function through an absolute pointer, with constants among its arguments. It calls the integer builtins, `set_fpscr` with 0, 1, 127 and 200 among them, and compiles interrupt functions with `sp=` 0x7f, 0x80 and 0x100, and `#pragma abs16` and `gbr_base`. It stores float 0.0, −0.0 and the denormal words 1, 32, 127, 128, H'7FF3 and H'7FFE, and doubles with the low words 0, 1 and H'7F, through plain and indexed stores, globals, returns, arguments, more than eight float arguments, varargs, K&R calls, union and pointer punning, struct and union arguments and returns, `gbr_base`, `abs16` and `volatile` floats, arrays and loops, and an integer and a float of one word in one function. Every release refuses its `long long`. It went through every release from R10 to 5.1 R13 in both modes, under `-optimize=0` and `-optimize=1` for speed and for size, each with no `-fpu`, `-double=float`, `-fpu=single`, `-fpu=double`, and `-denormalize=on` with `-fpu=single`, `-fpu=double` and no `-fpu`, R10 under the first two alone; and one at a time `-cpu=sh1` to `sh3e`, and for the SH-4 `-pic=1`, `-abs16=all`, `-loop`, `-inline`, `-rtnext`, `-endian=big`, `-division=cpu`, `-macsave=1`, `-round=nearest` and `-extra=a=800`, and a few of them together.
- **Loads.** Of 635,457 loads, 2,645 load a word from −32768 to 32767, each of them in a probe: 0, a float 0.0 with no `-fpu`, at every optimization level, in R31 to 5.1 R13, 1,443 times; 127, 128 and 32766, denormal floats under `-denormalize=on` with `-fpu=single` or no `-fpu`, in R26 to 5.1 R13, 1,170 times; and 32 and 32755 in 5.1 R03 alone, which the others load by `MOVA` and `FMOV.S`. `LDS Rn,FPUL`, then `FSTS FPUL,FRm`, reads each first on every path. None stands in a slot, and none is read first as an integer, stored, pushed, or left unread at a call, a jump or a return. No load of such a word by `MOV.L` appears under R10, `-fpu=double`, `-double=float` or a `-cpu` other than `sh4`, and none of a word from −32768 to −1.
- **Integers.** Every release from R10 to 5.1 R13 loads an integer by value: by `MOV #imm` from −128 to 127, and by `MOV.W` for the rest of 16 bits. 5.1 R13 loads the count of `for (i = 0; i < 128; i++)` by `MOV.W L547,R6`. In `void w_128i(float *p, int *q, int i) { p[i] = 1.794e-43f; q[i] = 128; }` under `-fpu=single -denormalize=on`, every release from R26 to 5.1 R13 loads the float's word H'00000080 by `MOV.L` for `LDS`, and the integer 128 by `MOV.W`. Under `-extra=a=1000` the 5.1 releases load the rest of 16 bits without a pool, building the word in its register as they build the [stack switch's](#stack-written): under `-cpu=sh4 -extra=a=1000`, 5.1 R13 passes and masks each integer from −32768 to 32767 with no `MOV.W`, and no `MOV.L` of such a word.
- **Calls.** Under `-cpu=sh1`, `sh2` and `sh3`, floats travel in R4 to R7 and R0, but their words go by value too: 5.1 R13 compiles `hf(1.78e-43f)` to `MOV #127,R4` and `JMP @R2`, and so do R10, R26 and R32. Under `sh2e`, `sh3e` and `sh4`, float arguments go in FR4 to FR11 or on the stack, and R10's software doubles, and `sh3e`'s, load their words by value and pass them on the stack.
- **Compiles.** inspect, run on every function of the 788 texts that hold those loads or ones in inline text, finds the witness only in inline text: 34 loads, in hand-written `#pragma inline_asm` bodies.
- **Rests on** the compiles, beside R10's expansion: no stage of R26 to 5.1 R13 was read for how a float's word is loaded. 855 of the retained compiles, of 5.1 R04, R11 and R13 but one, are no longer on disk: the family compiles read them for words from −128 to 127, but nobody read them for words up to 32767 or for uses. 6.0 AD compiled nothing. A pool word SHC writes as a symbol, or as a difference of symbols, is no constant: under `-pic=1`, SHC adds a function's distance to an address, `MOV.L L543+40,R14 ; _gs-L549` then `ADD R0,R14`, and a text that writes such a word as its number shows the witness for SHC's own code.
- **Refuted by** any release loading a word from 0 to 32767 or from −32768 to −129 by `MOV.L` for C, in either mode, outside inline text, where each path from the load reads the word first as an integer, or leaves it unread in R4 to R7 at a `JSR`, `BSR` or `JMP`. The rule was narrowed around the compiles that refute a rule blind to the use. Float 0.0 with no `-fpu`, in R31 to 5.1 R13, refutes a `MOV.L` of a word from 0 to 127: `p[i] = 0.0f;` compiles to `MOV.L L349,R1 ; H'00000000`, then `LDS R1,FPUL` and `FSTS FPUL,FR3`. So do denormal words under `-denormalize=on`, in R26 to 5.1 R13: `p[i] = 1.78e-43f;` gives `MOV.L L349+4,R1 ; H'0000007F`, and in 5.1 R03 `p[i+1] = 4.5e-44f;` gives H'00000020. `w_128i` above refutes a `MOV.L` of any word `MOV.W` could load, and so do `p[i] = 4.5916e-41f;`, H'00007FFE, in R26 to 5.1 R13, and H'00007FF3 in 5.1 R03. Each of the 2,645 loads refutes a rule that takes `LDS Rn,FPUL` as an integer read, or ignores the read.

#### zero-displacement

- **Read.** R10 and R32. For the others it is inference from the compiles.
- **Encoder.** In R10 and R32, SHCASM's encoder passes both operands through a checker before it picks a form. The checker rewrites `@(disp,Rn)` to `@Rn` when the displacement is 0 and names no symbol, and the form, the code word and the asmcode text all come after it.
- **Writers.** SHCGEN and SHCPEP write `@Rn` itself as `@(0,Rn)` in every probe, so the rewrite produces each `@Rn` of their records. No register displacement names a symbol. Their records hold one as a register and a 16-bit number, with no field for a symbol. SHCASM builds one only for a frame slot, from a positive offset, writing `@R15` for offset 0 itself, and the text writer prints a displacement as its number.
- **Text.** SHC therefore writes neither `@(0,Rn)` nor `@(sym,Rn)`, in either mode, and a symbol `.EQU` or `.ASSIGN` defines as 0 is no displacement SHC writes.
- **Sweeps.** No output of the sweeps holds `@(0,Rn)`: a member or a stack slot at offset 0 is `@Rn`. The GBR builtins write `@(0,GBR)`, which has no `@GBR` form and isn't a register displacement the rule reads.
- **Refuted by** any release writing a `MOV.B`, `MOV.W` or `MOV.L` through `@(0,Rn)` for C, in either mode, outside inline text.

#### fschg-pairs

- **Read.** R10, the one release known to write `FSCHG`. No stage of any release from R26 to 5.1 R13 holds the text FSCHG, and in the family compiles none writes it. R32 has no form for it either: no row of its 182 is FSCHG's, as R10's row 187 is, and `_builtin_fschg()` compiles to a call to an external function of that name. Take each register field as the register's number, 0 to 15, at its shift, and each displacement or immediate field as any twelve bits at its own. Then only FSCA's row, `F0FD` with DRn's number at bit 8, could form FSCHG's word, `F3FD`, and only with an odd number there, which a DRn never has.
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
- **Refuted by** any release writing an `FMOV` between memory and FR8, FR10, FR12, FR14 or DR8 to DR14 after an odd number of `FSCHG`s on every path from the entry, for any C source, outside inline text.

### ABI rules

Under SHC's calling interface a function leaves R15 as it found it (SHC compiler manual, table 2.6). No `#pragma global_register` takes R15, and no builtin names it. R10 compiles `_builtin_get_fpscr` and `_builtin_set_fpscr` to `STS FPSCR,Rn` and `LDS Rm,FPSCR` ([why](#fpscr-save)).

**R10's frame.** `return-above-entry` and `return-below-entry` exclude C by one argument, the frame argument: on every path the stack rule follows, with SZ as the rule knows it, R10 brings R15 back to its entry value by the end of each return's slot. The argument is unfinished, and it was read for R10 alone. Each rule's evidence names the open parts it rests on alone ([return-above-entry](#return-above-entry), [return-below-entry](#return-below-entry)). For every other release these two rules are inference from compiles: no release's frame builders but R10's were read. No compile of C found so far, by any release, makes either rule's witness.

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
  - SHCASM's scheduler keeps every write to R15 in order, inside windows that end at each label, branch and return, and has no cap, where R32's gives up at cycle 200 and moves what it hasn't issued to the top of its window ([return-above-entry](#return-above-entry)).
  - SHCPEP's register masks name R15 for every instruction that touches it but `DIV1 Rm,R15`, after which the rule loses R15's offset.
  - SHCPEP moves an instruction within straight-line code, or moves a join's first instruction past code it commutes with, and it can move the instruction before a case table's jump into the slot of a branch past the table. For a `switch` on a call with a stack argument, whose body opens with a `break` before its first case, it moves the call's `ADD #4,R15` into the slot of that `break`'s `BRA`, which no path runs, and the function returns 4 bytes below its entry. Each of its paths runs the call or the table's jump first, and the rule follows neither.
  - SHCASM puts the instruction SHCPEP chose for a conditional branch's slot in that slot, provided the branch forms it sized stay in step with the ones it builds.
- **The form stream, optimized.** SHCASM's form-stream writer (`0x407d60`) and reader (`0x407d00`) pair the local byte counts. The pool-drain argument also needs the source of SHCPEP's operand copy (`0x414e00`) to survive its allocator (`0x40df70`), which can flush and free head units before it retries. Only then do the sized and the written symbolic expressions agree.
- **The form stream, unoptimized.** SHCGEN supplies the stream and pool estimates (`0x428498`, `0x429340`), and its expression lists must be dense: the sizing writer continues past zero entries (`0x418d36..0x418d4e`), while the record writer emits only the nonzero prefix (`0x42fea6..0x42febe`). Its symbol classifier tests bit 0 first (`0x429323`), but its output keeps both low class bits (`0x408367..0x408379`), and SHCASM reads both (`0x4175ce`). SHCASM also gives reserved identifiers up to 180 class 0 (`0x4175b7`), and SHCGEN's classifier has no such guard (`0x4292f0`). Whether a `MOVI` can reach these differing classifications is unread.
- **After a case table.** That a case table always has a returned record after it, through SHCPEP's `str_chg` (`0x4150c8`).
- **Before a case-table jump.** Which R15-moving instruction the slot walk can take before a case-table jump (`0x416ba0`).
- **R15's writers.** That every body writer of R15 is one the rule tracks, or one whose offset it recovers by a later copy from R14 ([the stack rule](#the-stack-rule)). SHCGEN's R15 sentinel (`0x40417f`) can pass through generic operand selection (`0x41affa..0x41b139`), whose local checks don't show it is only read. SHCGEN also writes a `SUB` for a struct argument and a `MOV` for a stack switch ([stack-written](#stack-written)).

#### stack-written

- **Read.** R10's frame builders, with R15's other writers open ([R10's frame](#abi-rules)). The rest is compiles.
- **Sweeps.** In the sweeps, R10 and R32 write R15 in these forms alone:
  - as a push's or a pop's address: `MOV.L`, `FMOV.S`, and `STS.L` and `LDS.L` of PR, MACH, MACL, FPUL and FPSCR. Both also push R15 itself, as the result's address for a call that returns a struct and for the software double routines, R10's without `-double=float` and R32's under `-cpu=sh3`;
  - by `ADD #imm,R15`, and for a frame over 127 bytes by `ADD R0,R15`, `ADD R1,R15` or `ADD R2,R15` after a `MOV.W` or `MOV.L` of its size;
  - by a `SUB` of R0, R1, R2 or R3, to make room for a struct passed by value;
  - by `MOV R0,R15` and `MOV.L @R15+,R15`, in the stack switch alone.
- **MOV R14,R15.** No output writes it, and the rule lets it by all the same. `MOV R15,R14` only copies R15.
- **The switch.** It loads x by its form. Unscheduled, every switch back is `MOV.L @R15+,R15` and `MOV.L @R15+,R0`, then `RTE` with `NOP` in its slot, or `TRAPA #n` under `tn=`:

| x | the load |
|---|---|
| an address, or a constant beyond 16 bits | `MOV.L L,R0` |
| a 16-bit constant, or a symbol under `#pragma abs16` | `MOV.W L,R0` |
| a 16-bit constant under `-extra=a=1000`, in 5.1 R01 to R13 and 6.0C.001 | `MOV #imm,R0`, then `EXTU.B R0,R0`, or `SHLL2 R0`, or `SHLL8 R0` and after it nothing, one or two `ADD #imm,R0`, or `NOT R0,R0` |
| a constant from −128 to 127 | `MOV #imm,R0` |
| a pointer variable | its address, then `MOV.L @R0,R0` |
| a `#pragma gbr_base` pointer | `MOV.L @(d,GBR),R0` |
| a `gbr_base` variable's address | `STC GBR,R0`, then `ADD #d,R0` |

- **`-extra=a=1000`.** Under this option the 5.1 releases and 6.0C.001 build a 16-bit constant from `MOV #imm` where 5.0 R10, R26 and R32 keep `MOV.W`. 5.1 R13 compiles `#pragma interrupt(h(sp=0x100))` over `void h(void) { g = a; }` under `-cpu=sh4 -extra=a=1000` to a head of `MOV.L R0,@-R15`, `MOV #1,R0`, `SHLL8 R0`, `MOV.L R15,@-R0` and `MOV R0,R15`. For each x from −32768 to 32767 outside −128 to 127, under the same options, R13 writes one of six loads after `MOV #imm,R0`: `SHLL8` and an `ADD` 64,706 times, `SHLL8` alone 255, `EXTU.B` 128, `SHLL8` and two `ADD`s 127, `SHLL2` 63, and `SHLL8` and `NOT` once, for H'7FFF. 5.1 R01, R03, R04, R08 and R11 and 6.0C.001 write the same loads for the 7,520 values of x tried, a wider x by `MOV.L` among them. Each such head refutes an exemption that knows the other loads alone.
- **The scheduler.** SHC's scheduler moves the switch as it moves a pop ([return-above-entry](#return-above-entry)). From R26 to 5.1 R13, a function with a stack switch and fourteen or more float divisions has other instructions above the head: `FDIV`s, a store, or a lifted pop, `LDS.L @R15+,FPSCR` among them. From thirty-four divisions on, `MOV.L @R15+,R15` stands among the exit's pops, after `MOV.L @R15+,R5` and `MOV.L @R15+,R0` and before `LDS.L @R15+,FPSCR`, so the pops past it read the interrupted stack. In 7,035 such functions, of every release from R10 to 5.1 R13 and from 4 to 256 divisions, the head stands whole and in order above the first branch, and one `MOV.L @R15+,R15` stands below it. 5.1 R01 stops with no message on an `sp=` that names a variable, so its functions hold a constant stack alone. The heads `-extra=a=1000` builds stay whole too: in 16,416 functions of R26, R32, 5.1 R01, R03, R04, R08, R11 and R13 and 6.0C.001, with 1 to 100 divisions and eight values of x, with `-extra=a=1000` and without, the rule finds no witness.
- **Rests on** the head staying whole and loading x by one of these forms, which the compiles show and no reading does. A compile that parts its instructions, or loads x another way, refutes the exemption's shape, and with it the rule as it stands.
- **Refuted by** any release writing R15 otherwise for C, in either mode, outside inline text.

#### fpscr-save

- **Read.** R10. R32's functions in the sweeps push and pop FPSCR through R15 only where they return by `RTE` or end in `TRAPA`, but R32 wasn't read, and neither was any other release. For them it is inference from the compiles.
- **Frame.** R10 pushes and pops FPSCR through R15 only in the frame of a `#pragma interrupt` function, which returns by `RTE` with `NOP` in its slot, or ends in `TRAPA` under `tn=`. SHCASM builds the push as the entry's last, after the general registers, FR registers, PR, MACH, MACL and FPUL and before the frame's `ADD`, and the pop as the exit's first, after its `ADD`, each under one bit of the frame record SHCGEN writes.
- **Writers.** SHCGEN clears that bit for every function but an interrupt function, under `#pragma regsave` too, which sets it. From the same attribute it marks the record an interrupt's, so the exit ends in `RTE`, or `TRAPA` under `tn=`. Only for any other function does it end in `RTS`, and SHCASM builds no other `RTS`. SHCPEP builds FPSCR's pop only in the exit it writes for a tail call, under the same bit, and writes no `RTS`.
- **Builtins.** SHCGEN's own FPSCR operand is bare, in the template rows of `_builtin_get_fpscr` and `_builtin_set_fpscr`, beside an operand that resolves only to a register: that is why the builtins compile to `STS FPSCR,Rn` and `LDS Rm,FPSCR`. SHCPEP writes no memory operand that could merge either with a `MOV.L` through R15.
- **Probes.** The frame records SHCASM reads for 257 functions of the probes bear this out: 80 have the bit, each an interrupt function's; 5, of an interrupt function that neither calls nor uses the FPU, don't; and none of the 172 others does, those under `regsave`, `noregsave` and `noregalloc` among them.
- **Interrupt functions.** An interrupt function pushes FPSCR when it uses the FPU or makes a direct call, on the new stack after a `sp=` switch, and one whose calls all go through a pointer pushes none. R10 writes the pair for `-cpu=sh4` and `sh3e`, and none for `sh1` to `sh3`.
- **C.** C that keeps FPSCR across a call through the builtins keeps the copy in R8, R13 or R14 or in a frame slot, passes it as a fifth argument by `STS FPSCR,R3` and `MOV.L R3,@-R15`, and stores it through `*--p` by `STS FPSCR,R3` and `MOV.L R3,@-R4`. R10 merges none of them into a push of FPSCR, under any option the sweeps name for the SH-4.
- **Sweeps.** In the 2,901 texts R10 wrote in the sweeps' directories, 1,045 functions push and pop FPSCR through R15: 836 return by `RTE`, 209 end in `TRAPA`, and none holds an `RTS`.
- **Operands.** SHCGEN writes no direct `RTS` record in an interrupt function ([R10's frame](#abi-rules)). SHCASM and SHCPEP push and pop FPSCR only through R15, but SHCGEN's operands whose kind comes from a register, and SHCPEP's copies of operands, weren't all read, so the rule reads R15 alone.
- **Refuted by** any release pushing or popping FPSCR through R15 for C in a function that holds an `RTS`, in either mode, outside inline text.

#### return-above-entry

- **Read.** R10's frame, unfinished, and R32's scheduler. Every other release rests on compiles alone.
- **The scheduler.** R15 above the entry on the way shows nothing, because from R26 to 5.1 R13 SHC can compile C to it. R32's SHCASM's SH-4 scheduler gives up at cycle 200 and puts what it hasn't issued at the top of its window, above the entry's pushes when the window opens at the entry. In double precision eight chained divisions get there: `double f(double a, double b, double c, double d) { double x = 0.0; x /= a; x /= b; x /= c; x /= d; x /= a; x /= b; x /= c; x /= d; return x; }` compiles under `-speed` and `-size` to a function that starts with the epilogue's first pop, `FMOV.S @R15+,FR14`, a word of its caller's frame; seven keep the order. In single precision, where a division takes half as long, seventeen do. `shc -cpu=sh4 -fpu=single -optimize=1 -speed -code=asmcode` compiles

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

  Each release from R26 to 5.1 R13 writes these two functions so, with its pops above its pushes. Every label, branch, return and `ADD` to R15 ends a window, so the pops stay on their path, and each such function is back at its entry value by the return's slot. The rule reads that slot. Where a lifted `LDS.L @R15+,FPSCR` makes SZ unknown, as in the interrupt function, the rule loses R15's offset at the next FR pop, with `stack-unknown`.
- **Frame.** R10's entry pushes and frame allocation lower R15. The exit builder uses the same saved-register masks, under `#pragma interrupt`, `noregsave` and `regsave` too. The arguments pushed for a call come off after it, by an `ADD`, or by the routine itself for R10's software double routines. The builders don't fix the order along every path through the body ([R10's frame](#abi-rules)).
- **Sweeps.** No R10 output of the sweeps takes R15 above its entry value on a path the stack rule follows. A return above the entry pops what the caller pushed, as runtime helpers written in assembly do.
- **Compiles.** No text of the family compiles returns above its entry value on a path the stack rule follows.
- **Rests on** the frame argument's open parts ([R10's frame](#abi-rules)), and on three open parts of this rule alone:
  - **Tail calls.** The tail-call marker keeps its meaning through SHCMDL's later transforms. It is set on exit predecessors (`SHCMDL`, `0x40d137`), after inline expansion, but where it starts doesn't show it survives.
  - **Moved pops.** The source and width of each pop SHCPEP can fold or move across a call. SHCPEP's fold uses nominal widths, not SZ (`0x412b3b`, `0x412ba3`).
  - **Copies of an exit.** A copy of an exit that SHCPEP folded an `ADD #n,R15` into frees those bytes too, though its path never ran the `ADD` ([return-below-entry](#return-below-entry)). In every probe with a `return` before the end, the exit keeps its label in front, so nothing folds.
- **Refuted by** a function any release compiled from C whose return's slot leaves R15 above its entry value on a path the stack rule follows, with SZ as the rule knows it.

#### return-below-entry

- **Read.** R10's frame, unfinished. R32's scheduler moves an instruction only within a window, and every label, branch, return and `ADD` to R15 ends one, so it doesn't reach this rule, but the rest of R32's reading hasn't been done. Every other release rests on compiles alone.
- **Frame.** R10's entry and exit builders use the same saved-register masks. SHCPEP folds into the frame the exit frees an `ADD #n,R15` that runs right before the exit, with no label between: one `ADD #24,R15` frees a 16-byte frame and the last call's 8 bytes of arguments. A copy of the exit frees the folded bytes too, though its path never ran the `ADD`. In every probe with a `return` before the end, the exit keeps its label in front, so nothing folds.
- **C.** R10 uses the stack for call arguments and expression temporaries: SHCGEN can push a temporary (`0x412c77`, `0x412f39`) and restore one (`0x412aa0`) without a call. The rule follows no call. SHCASM writes the same exit for `-code=machinecode` and `-code=asmcode`. Past a call to a software double routine, which pops its own arguments, R15 would read below the entry.
- **SZ.** R10 compiles `_builtin_set_fpscr` to `LDS Rn,FPSCR`, which between a function's FR saves and their restores can change the restores' width: with SZ 1 at the entry and 0 written, four restores take back 16 of the 32 bytes four saves pushed, and the function returns 16 bytes below its entry. The rule loses SZ at the write, and R15's offset at the next FR pop. Nothing else changes SZ between them: `_builtin_fschg()` compiles to a call, and R10 writes `FSCHG` only in pairs inside the expansions of `_builtin_ld_ext`, `_builtin_st_ext`, `_builtin_ftrv` and `_builtin_fipr`, with no push or pop between.
- **Inline text.** R10 writes `.ALIGN 4` below each piece of inline text, so in its `-code=asmcode` text the rule stops there, before the `RTS` that ends a `#pragma inline_asm` function. In a text drop-inline-align has patched, it reaches the `RTS`, and a witness there stands on inline text.
- **Sweeps.** In the 2,901 texts R10 wrote in the sweeps' directories, each of the 55,229 `RTS`s the rule reached with a known offset, under `--sz 0` and under `--sz 1`, was at the entry value. The 149 it reached with an unknown one take or free a frame of 128 bytes or more by `ADD R0,R15` or `ADD R1,R15`.
- **Compiles.** No text of the family compiles returns below its entry value on a path the stack rule follows.
- **Rests on** the frame argument's open parts ([R10's frame](#abi-rules)), and on one of this rule alone: that the body's pushes, restores and cleanup keep their paired widths under SZ 1. How SHCGEN pairs the temporaries' pushes and restores, and at what widths under SZ 1, isn't fully read. The exit label's range and the absence of direct `RTS` records are established, but they don't settle the body's paths. A return's slot holds one instruction: the exit's own, or one SHCPEP admits only where it crosses nothing that moves R15.
- **Refuted by** a function any release compiled from C that returns below its entry value on a path the stack rule follows, with SZ as the rule knows it.

### What has no rule

What SHC is known to write has no rule, however odd it looks:

- **FSCHG.** R10 writes it in the templates of its vector builtins, 2,628 times in the sweeps. A pattern after it can still be a witness ([fschg-pairs](#fschg-pairs)).
- **A 16-bit word by `MOV.L` for `LDS`.** With no `-fpu`, every release from R31 to 5.1 R13 loads a float 0.0 as H'00000000 by `MOV.L`, `LDS` and `FSTS`. Under `-denormalize=on`, with `-fpu=single` or no `-fpu`, every release from R26 to 5.1 R13 loads a denormal word the same way: `void s_idx(float *p, int i) { p[i] = 1.78e-43f; }` compiles to `MOV.L L236+2,R1`, whose pool word is `H'0000007F`. So `integer-constant` reads the word's use, and `LDS` shows nothing.
- **R15 above the entry before a return.** Every release from R26 to 5.1 R13 can [put a pop above the entry's pushes](#return-above-entry).

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
