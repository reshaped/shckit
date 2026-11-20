# inspect

`shckit inspect --entry LABEL [--end LABEL] [--sz 0|1] [--all] FILE` looks in one function of a text for a witness: a pattern no Dreamcast SHC compiler is known to write from C. The pattern is an instruction, its operands or where it stands, or a use of R15.

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
- `_call` shows no witness, but [the stack rule](#the-stack-rule) stopped at the call on line 15: it can't follow a call, so for the rules that follow its paths, `return-above-entry`, `return-below-entry` and those that count `FSCHG`s, `none` says nothing about the lines past it. The [instruction rules](#the-rules) read every instruction, reached or not. That command exits 3, for the limit, and the others exit 0. Without `--end`, `_call` runs to the end of the text.

## What a witness claims

A witness says one thing: no SHC compiler is known to write this pattern from C. It says so of the family, not of one release. A text comes with no release to ask about, so inspect takes none, and it takes no code mode for the same reason. A rule stands only while no release is known to write its witness from C, under `-code=machinecode` or `-code=asmcode`: it rests on the union of everything known.

The family is the SHC the Dreamcast's SDKs shipped: 5.0 and 5.1, and 6.0 AD, whose back end is 5.1 R13's. Later SHC is outside the claim: from 7.0 it has builtins for `SWAP` and for a 64-bit product.

"Not known" is the claim, and it is weaker than "can't". [Evidence](#evidence) says, for each rule, what was read for which release, what was compiled, and what is inference. Most rules were read for one or two releases and only compiled in the others, and some releases weren't read or compiled at all. A rule applies to a text from an unread release of the family all the same: the gap is stated, and it is no scope on the rule.

Inline text is outside the claim. SHC copies the text of `#pragma inline_asm` and `#pragma asm` into `-code=asmcode` text as written, and it can hold any instruction. `-code=machinecode` refuses both pragmas.

Two more things are outside it, and both are the caller's to know, since no text shows either:

- **A part of a function.** A witness is of the function whose lines inspect is given ([the function](#the-function)). Lines that hold less than the function can show a witness the function doesn't: [`fipr-unloaded`](#fiprs-loads), which counts a function's loads as [`fipr-shared`](#shared-loads) does, shows one from the label SHC puts before a `FIPR` that two arms share, and the stack rule's witnesses rest on the entry too.
- **A module the inter-module optimizer rewrote.** 5.1 R08, R11 and R13 ship an optimizer that runs at link time, and under `-optimize=same_code` it moves a block the compiler wrote into a routine of its own, which no compiler wrote ([evidence](#the-inter-module-optimizer)). 6.0 AD's own linker lists the same option, and wasn't tried. A text written from such a module's bytes holds that routine as a function, and `fipr-unloaded` and `fipr-shared` each show their witness in one, for C alone. In place of each copy of the block it holds a push of PR, a `BSR` and a pop, and where the block stood right below a conditional branch or its slot, `pr-save-below-branch` shows its witness at that push, for C alone. No other rule showed a witness in the modules that were tried.

C that any release of the family compiles to a rule's witness, outside inline text, refutes the rule: it is dropped, or narrowed to what stays true. Each rule's evidence ends with what would refute it. If you find such C, please send a pull request: we want to grow these rules with the Dreamcast decompilation community.

Without a witness, the function may still not be compiled from C: inspect knows a few rules, and a limit, a place it can't see past, can hide what they would find. inspect doesn't modify files.

## The function

`--entry` and `--end` name labels, each defined once in the text. The function is the lines from the entry's up to the end's, which isn't one of them. Without `--end`, it runs to the end of the entry's `.SECTION` block: up to the next `.SECTION` or `.END` line, or to the text's end. The entry's line is a label or an instruction in a code section.

inspect can't tell where a function starts or ends, or whether calls enter it anywhere but its entry: those are the caller's to know. A label below the entry starts no other function, and a branch to it stays in this one. One rule takes less on trust: `pr-save-below-branch` reads a branch and a push as one function's only where no label stands between them ([why](#prs-save)). Two take more: `fipr-unloaded` and `fipr-shared` count the loads among the function's lines, so they need each line of the function among them ([why](#fiprs-loads)).

Some lines run nothing, and control passes them: blanks, comments, labels, `.EXPORT`, `.IMPORT`, `.EQU` and `.ASSIGN` lines, and the continuations of any of these. Below, these are the lines control passes.

inspect reads a name where a line names it, as asmsh does:

- A `?label` means its first definition in the line's block. A block runs from a `.SECTION` line, or from the first definition of a name other than a `?label`, alone or on an instruction or data, up to the next such line.
- Any other name means its first definition in the whole text. Where that is an `.ASSIGN`, it means the last `.ASSIGN` of it at or above the line, in any section, since asmsh redefines the name at each one.
- asmsh ignores the other definitions, with warning 801.
- A label on a directive other than data, `.POOL`, `.EQU` and `.ASSIGN` defines nothing, nor does a `?label` on `.EQU` or `.ASSIGN`.
- Nor does a name asmsh refuses: one with a `.` or with a `?` past its first character, a `?label` over 16 characters, and `?` or `$` alone.

## What it reports

One line names the witness and its line, or `none` and the entry's line, and one more names the limit, if inspect met one. A `return-above-entry` or `return-below-entry` witness names R15's offset after its line, a `small-constant` or `integer-constant` one its constant, a `fipr-unloaded` one the registers its function doesn't load, and a `fipr-shared` one those it loads too seldom:

```
x.src:3: _f: abi: return-above-entry (SP +8)
x.src:5: _f: operand: small-constant (#-1)
x.src:9: _f: operand: fipr-unloaded (FR3 FR7)
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
| [`cmp-str`](#cmp-str) | opcode | `CMP/STR` ([more](#cmpstr)) |
| [`fipr-registers`](#fipr-ftrv-and-fsca) | operand | `FIPR` other than `FIPR FV4,FV0` |
| [`fipr-unloaded`](#fipr-unloaded) | operand | `FIPR FV4,FV0` in a function that holds no load of one of FR0 to FR7 ([more](#fiprs-loads)) |
| [`fipr-shared`](#fipr-shared) | operand | `FIPR FV4,FV0` past the loads of one of FR0 to FR7, in a function that holds fewer loads of it than `FIPR FV4,FV0`, and one at least ([more](#shared-loads)) |
| [`ftrv-registers`](#fipr-ftrv-and-fsca) | operand | `FTRV` other than `FTRV XMTRX,FV0` |
| [`fsca-registers`](#fipr-ftrv-and-fsca) | operand | `FSCA` other than `FSCA FPUL,DR0` |
| [`small-constant`](#small-constant) | operand | a `MOV.L` of a [constant](#constants) from −128 to −1 |
| [`integer-constant`](#integer-constant) | operand | a `MOV.L` of a [constant](#constants) from 0 to 32767 or from −32768 to −129 that each path from it reads first as an integer, or passes unread to a call ([more](#integer-constants)) |
| [`zero-displacement`](#zero-displacement) | operand | a `MOV.B`, `MOV.W` or `MOV.L` through `@(0,Rn)` |
| [`fschg-slot`](#fschg-slot) | operand | an `FSCHG` in the delay slot of `RTS`, `RTE`, `BRA`, `BRAF`, `JMP`, `BSR`, `BSRF`, `JSR`, `BT/S` or `BF/S` ([more](#fschg-in-a-slot)) |
| [`fschg-pairs`](#fschg-pairs) | operand | an `FMOV` between memory and FR8, FR10, FR12, FR14 or DR8 to DR14, after an odd number of `FSCHG`s on every path the stack rule follows to it ([more](#pairs-after-fschg)) |
| [`fschg-dt`](#fschg-dt) | operand | a `DT` after an odd number of `FSCHG`s on every path the stack rule follows to it ([more](#dt-after-fschg)) |
| [`fschg-stores`](#fschg-stores) | operand | an `FMOV` to memory from FR4, FR6, DR4 or DR6, after an odd number of `FSCHG`s on every path the stack rule follows to it ([more](#stores-after-fschg)) |
| [`fschg-register-moves`](#fschg-register-moves) | operand | an `FMOV` between two FPU registers, after an odd number of `FSCHG`s on every path the stack rule follows to it ([more](#register-moves-after-fschg)) |
| [`stack-written`](#stack-written) | abi | a write to R15 other than a push's or pop's address, `ADD`, `SUB`, `MOV R14,R15` and the stack switch ([more](#writes-to-r15)) |
| [`fpscr-save`](#fpscr-save) | abi | `STS.L FPSCR,@-R15` or `LDS.L @R15+,FPSCR`, in a function that holds an `RTS` ([more](#fpscrs-save)) |
| [`pr-save-below-branch`](#pr-save-below-branch) | abi | `STS.L PR,@-R15` below a `BT`, `BF`, `BT/S` or `BF/S` and its slot, with no label, no `BRA`, `BRAF`, `JMP`, `RTS`, `RTE` or `TRAPA`, and no data or directive but `.EXPORT` and `.IMPORT` between them ([more](#prs-save)) |
| [`return-above-entry`](#return-above-entry) | abi | R15 above its value at the entry, after the slot of an `RTS` or `RTE` the stack rule reaches ([more](#above-the-entry)) |
| [`return-below-entry`](#return-below-entry) | abi | R15 below its value at the entry, after the slot of an `RTS` the stack rule reaches ([more](#below-the-entry)) |

`return-above-entry` and `return-below-entry` rest on the [frame argument](#abi-rules), which is unfinished and was read for R10 alone. No compile of C found so far makes either witness. `pr-save-below-branch` reads the text's labels, which the code's bytes don't hold, so in a text written from bytes its witness shows less ([more](#prs-save)).

Every rule but `fschg-pairs`, `fschg-dt`, `fschg-stores`, `fschg-register-moves`, `return-above-entry` and `return-below-entry` is an instruction rule, which reads each instruction of the function in a code section, reached or not, as check reads it: in any case and with the sizes asmsh takes. Data, comments and the lines past `.END`, which check passes only blank or commented, hold none. `integer-constant` reads each load so, and from it follows the paths the stack rule follows, past calls too ([more](#integer-constants)). `pr-save-below-branch` reads each push so, and the lines above it in the order of the text ([more](#prs-save)). `fschg-slot` reads each `FSCHG` so, and the lines above it up to the first that control doesn't pass ([more](#fschg-in-a-slot)). `fipr-unloaded` and `fipr-shared` read each `FIPR FV4,FV0` so, and count the loads of the whole function ([more](#fiprs-loads)).

`PREF`, `LDC` and `STC` of a control register, `FRCHG` and `FSCHG` show nothing by themselves: R10 writes each of them for a builtin, and R32 each but `FSCHG`. An `FSCHG` shows something by where it stands ([fschg-slot](#fschg-in-a-slot)) or by what follows it ([the stack rule](#the-stack-rule)). [What has no rule](#what-has-no-rule) says what else has no rule, and why.

### CMP/STR

An instruction whose mnemonic is `CMP/STR` is a `cmp-str` witness at its line, whichever two registers it names, with its size, `CMP/STR.L`, or without. `CMP/STR Rm,Rn` sets T when a byte of Rn equals the byte of Rm in the same place, so against a register of zero it finds a zero byte in a word. SHC's C runtime ends a string a word at a time that way, and SHC compiles `strcmp` to a call of the runtime: no release is known to write the instruction itself for C ([evidence](#cmp-str)).

The rule reads the mnemonic of each instruction of the function, reached or not, and nothing more: no operand, no line around it and no name. For `q.src`:

<!-- example strings: input q.src -->
```
_zero:
          MOV         #0,R0
          CMP/STR     R0,R4
          RTS
          MOVT        R0
_text:
          MOV.L       L248+2,R0  ; L239
          RTS
          NOP
L248:
          .RES.W      1
          .DATA.L     L239
L239:
          .SDATA      "x"
          .DATA.B     H'0A
          .SDATA      "          CMP/STR     R4,R5"
          .DATA.B     H'0A,H'00
```

<!-- example strings: run -->
```sh
build/shckit inspect --entry _zero --end _text q.src
build/shckit inspect --entry _text q.src
```

<!-- example strings: output -->
```
q.src:3: _zero: opcode: cmp-str
q.src:6: _text: none
```

`_zero` tests its argument for a zero byte on line 3. `_text` is what 5.1 R13 writes under `-cpu=sh4 -fpu=single -optimize=1 -speed` for `const char *q4(void) { return "x\n          CMP/STR     R4,R5\n"; }`, cut down to its own pool word, with its string, which SHC writes in a section of constants, set below it. SHC parts a string at each line end, so a line of assembly in one stays the operand of an `.SDATA`.

What shows nothing:

- the name in a comment or in a string;
- a data word of the instruction's form, such as `.DATA.W H'212C`, which SHC writes for a 16-bit constant and in a switch table;
- a call of the runtime's `__quick_strcmp1` or `__slow_strcmp1`, or of another routine that holds the instruction: inspect reads no label's name;
- the compares SHC writes, `CMP/EQ`, `CMP/GE`, `CMP/GT`, `CMP/HI`, `CMP/HS`, `CMP/PL` and `CMP/PZ`, and what it writes for a test of bytes, `TST`, `EXTU.B` and `TAS.B`.

### FIPR's loads

`FIPR FV4,FV0` reads FR0 to FR7. SHC is known to write it only for `_builtin_fipr`, from a template that first loads each of the eight from memory, and no pass that was read takes such a load away ([evidence](#fipr-unloaded)). So a `FIPR FV4,FV0` is a `fipr-unloaded` witness, at its line, where its function holds no load of one of the eight. The witness names each such register.

**A load of FRn** is an `FMOV` from memory, `@Rm`, `@Rm+` or `@(R0,Rm)`, into FRn, with a size or without, anywhere in the function's code, reached or not:

- A load of DRn is one of FRn and of FRn+1, and a load of XDn one of FRn+1: `FMOV @Rm+,XD2` and `FMOV.S @Rm+,FR3` are one word, which with SZ 0 moves FR3.
- `FMOV.S @Rm+,FR2` and `FMOV @Rm+,DR2` are one word, which with SZ 1 moves the pair. So a load of FR0, FR2, FR4 or FR6 is one of the register above it too where the caller or the text gives a reason to think SZ may be 1 at it: under `--sz 1`, or in a function that holds an `FSCHG` or a write of FPSCR, `LDS Rm,FPSCR` or `LDS.L @Rm+,FPSCR`, anywhere. Elsewhere it is read as written, without `--sz` as under `--sz 0`: these rules don't run under both as [the stack rule](#without---sz) does. R10's template loads four pairs between two `FSCHG`s, and a text written from bytes may spell them as singles.
- The memory can be a literal pool, a frame slot or the stack, and an `FMOV` with a continuation below it, which inspect can't read, counts as a load of each register. Each of these errs toward no witness.

The rule follows no path, and it doesn't ask what filled a register in place of a load: `FLDI0`, `FSUB`, `FLOAT`, a move from another register, or nothing, so that the `FIPR` reads it as the caller left it. For `o.src`:

<!-- example products: input o.src -->
```
_dot:
          FMOV.S      @R4+,FR0
          FLDI0       FR3
          FMOV.S      @R4+,FR1
          FMOV.S      @R4+,FR2
          FMOV.S      @R5+,FR4
          FMOV.S      @R5+,FR5
          FMOV.S      @R5+,FR6
          FIPR        FV4,FV0
          RTS
          FMOV.S      FR3,FR0
_late:
          FMOV.S      FR15,@-R15
          ADD         #-8,R15
          FIPR        FV4,FV0
          FMOV.S      FR3,FR0
          FADD        FR15,FR0
          FMOV.S      FR4,FR15
          FDIV        FR5,FR15
          MOV         R5,R2
          MOV.L       R4,@R15
          MOV.L       R5,@(4,R15)
          MOV.L       @R15,R3
          FMOV.S      @R2+,FR4
          FMOV.S      @R3+,FR0
          FMOV.S      @R3+,FR1
          FMOV.S      @R3+,FR2
          FMOV.S      @R3+,FR3
          FDIV        FR5,FR15
          FMOV.S      @R2+,FR5
          FMOV.S      @R2+,FR6
          FMOV.S      @R2+,FR7
          ADD         #8,R15
          RTS
          FMOV.S      @R15+,FR15
```

<!-- example products: run -->
```sh
build/shckit inspect --entry _dot --end _late o.src
build/shckit inspect --entry _late o.src
```

<!-- example products: output -->
```
o.src:9: _dot: operand: fipr-unloaded (FR3 FR7)
o.src:12: _late: none
```

`_dot` loads FR0 to FR2 and FR4 to FR6, fills FR3 by `FLDI0` and reads FR7 as its caller left it. `_late` is what 5.1 R13 writes under `-cpu=sh4 -optimize=1 -speed -fpu=single` for `float q_before16(float *a, float *b, float x, float y)`, which divides `x` by `y` sixteen times and returns `_builtin_fipr(a, b) + x`, cut down to two of its divisions. SHC's scheduler [gives up](#return-above-entry) there and leaves the `FIPR` above its own loads, on line 15, where it reads eight registers as the entry left them. The function holds a load of each all the same, so it shows nothing.

[`fipr-registers`](#fipr-ftrv-and-fsca) reads a `FIPR`'s operands and nothing else, and takes every `FIPR` but `FIPR FV4,FV0`. `fipr-unloaded` and [`fipr-shared`](#shared-loads) read `FIPR FV4,FV0` alone, the one form SHC is known to write, and what its function loads: `fipr-unloaded` whether it loads each register at all, and `fipr-shared` whether it loads each as often as it holds such `FIPR`s. No `FIPR` is a witness of `fipr-registers` and of one of them.

What shows nothing:

- a register that the function loads anywhere, above the `FIPR` or below, and that the `FIPR` reads as the entry left it, as in `_late`, or as something else filled it: SHC's scheduler writes both from C ([what has no rule](#what-has-no-rule));
- which registers the function reads as its caller left them. A function that takes a vector in FR4 to FR7 shows the witness because it loads none of them, and not because it reads them ([the entry registers](#what-has-no-rule));
- an `FTRV` whose vector no load fills: no rule reads an `FTRV`'s loads ([what has no rule](#what-has-no-rule)).

**The rule needs the function's lines whole.** It counts over [the function's lines](#the-function), so lines that hold less than the function can make its witness:

- Lines that begin below the function's entry can leave its loads out. SHC puts a label before a `FIPR` that two arms share, each arm with loads of its own above it. For `void r_two(float *a, float *b, int k) { if (k) { _builtin_fipr(a, b); return; } _builtin_fipr(b, a); }`, 5.0 R26 under `-cpu=sh4 -optimize=1 -speed -fpu=single` ends:

  ```
            FMOV.S      @R5+,FR3
            FMOV.S      @R4+,FR7
  L354:
            FIPR        FV4,FV0
            RTS
            NOP
  ```

  Read from `_r_two`, the function shows nothing. Read from `L354`, it shows the witness, naming all eight registers, for code SHC compiled from C. inspect can't tell `L354` from a function's entry: it reads no label's name, SHC starts a static function at such a label too, and the function above can branch to that one or run into it ([what SHC writes](#what-has-no-rule)). So the rule takes the entry on trust, as the stack rule's witnesses do, and inspect neither refuses such lines nor marks them.
- Lines that run past the function's end, as they do without `--end`, take in the loads of each function below, which can hide the witness. So can an `FSCHG` or a write of FPSCR in a function below, with which each load of FR0, FR2, FR4 and FR6 among the lines counts for the register above it too.
- A routine that the inter-module optimizer made from a block of a compiled function holds that block and none of the function's other lines, and no text shows where it came from ([evidence](#the-inter-module-optimizer)).

### Shared loads

A function holds a `fipr-shared` witness where it holds more `FIPR FV4,FV0` than [loads](#fiprs-loads) of one of FR0 to FR7, and at least one load of it. Each run of SHC's template writes a load of each register beside its `FIPR`, and nothing that was read copies a `FIPR` without its loads, or merges the loads of two templates and leaves both `FIPR`s. So no function SHC is known to compile holds fewer loads of a register than `FIPR`s ([evidence](#fipr-shared)).

The witness is the function's count, and the lines inspect names are a convention. With c loads of a register, each `FIPR FV4,FV0` below the function's first c, in the order of the text, reached or not, is a witness that names the register, whichever of them the function runs first. A register with no load is [`fipr-unloaded`](#fiprs-loads)'s, at every `FIPR`, and this rule doesn't name it. For `p.src`:

<!-- example shares: input p.src -->
```
_blend:
          FMOV.S      @R6+,FR4
          FMOV.S      @R6+,FR5
          FMOV.S      @R6+,FR6
          FMOV.S      @R6+,FR7
          FMOV.S      @R4+,FR0
          FMOV.S      @R4+,FR1
          FMOV.S      @R4+,FR2
          FMOV.S      @R4+,FR3
          FIPR        FV4,FV0
          FMOV.S      FR3,FR8
          FMOV.S      @R5+,FR0
          FMOV.S      @R5+,FR1
          FMOV.S      @R5+,FR2
          FMOV.S      @R5+,FR3
          FIPR        FV4,FV0
          FADD        FR3,FR8
          RTS
          FMOV.S      FR8,FR0
_twice:
          MOV         R5,R2
          MOV         R4,R3
          FMOV.S      @R3+,FR0
          MOV         R4,R1
          FMOV.S      @R2+,FR4
          MOV         R5,R0
          FMOV.S      @R3+,FR1
          FMOV.S      @R2+,FR5
          FMOV.S      @R3+,FR2
          FMOV.S      @R2+,FR6
          FMOV.S      @R3+,FR3
          FMOV.S      @R2+,FR7
          FIPR        FV4,FV0
          FMOV.S      @R0+,FR4
          FMOV.S      @R1+,FR0
          FMOV.S      @R0+,FR5
          FMOV.S      @R1+,FR1
          FMOV.S      @R0+,FR6
          FMOV.S      @R1+,FR2
          FMOV.S      FR3,FR8
          FMOV.S      @R1+,FR3
          FMOV.S      @R0+,FR7
          FIPR        FV4,FV0
          FADD        FR3,FR8
          RTS
          FMOV.S      FR8,FR0
```

<!-- example shares: run -->
```sh
build/shckit inspect --entry _blend --end _twice p.src
build/shckit inspect --entry _twice p.src
```

<!-- example shares: output -->
```
p.src:16: _blend: operand: fipr-shared (FR4 FR5 FR6 FR7)
p.src:20: _twice: none
```

`_blend` loads FV4 once and keeps it across two `FIPR`s, so the second, on line 16, is past the one load of each of FR4 to FR7. `_twice` is what 5.0 R26 writes under `-cpu=sh4 -optimize=1 -speed` for `float s_twice(float *a, float *b) { return _builtin_fipr(a, b) + _builtin_fipr(a, b); }`: the same call twice loads sixteen registers.

A `FIPR` with other operands is [`fipr-registers`](#fipr-ftrv-and-fsca)' and isn't counted here. As many loads of a register as `FIPR`s show nothing, wherever they stand, and neither do more.

This rule needs the function's lines whole as `fipr-unloaded` does. Lines that run past the function's end count the `FIPR`s of each function below with its loads, and inline text counts with the C around it:

- Two functions that show nothing apart show nothing read as one: their loads and their `FIPR`s add up. But a function that holds a `FIPR` and no load, read with a compiled one below it, shows this rule's witness at the lower function's `FIPR`, in place of `fipr-unloaded`'s at its own.
- In a function with C beside inline text, the count takes in what the C part loads. A `#pragma inline_asm` body that keeps FV4 across three `FIPR`s shows the witness in a function whose C loads FR5, FR6 and FR7 once each, and a different compile of the C around the body can take it away.

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

### FSCHG in a slot

An `FSCHG` in the delay slot of `RTS`, `RTE`, `BRA`, `BRAF`, `JMP`, `BSR`, `BSRF`, `JSR`, `BT/S` or `BF/S` is an `fschg-slot` witness at its line. R10, the one release known to write `FSCHG`, fills a slot only with an instruction one of its stages marked for one, and no stage marks an `FSCHG`: it writes the `FSCHG` above the branch and leaves the slot to another instruction or a `NOP` ([evidence](#fschg-slot)).

The slot is the first line below the branch that control doesn't pass, so [lines control passes](#the-function) may stand between the two, a label among them. The branch is one of the function's lines. The rule follows no path and keeps no count: it reads each `FSCHG` of the function, reached or not. Its kind is `operand`, the nearest of the three: SHC writes the instruction, and the witness is where it stands. For `u.src`:

<!-- example slots: input u.src -->
```
_copy:
          FSCHG
          FMOV        @R5+,DR0
          FMOV        DR0,@R4
          RTS
          FSCHG
_store:
          ADD         #64,R4
          FSCHG
          FMOV        XD14,@-R4
          FMOV        XD0,@-R4
          FSCHG
          RTS
          NOP
```

<!-- example slots: run -->
```sh
build/shckit inspect --entry _copy --end _store u.src
build/shckit inspect --entry _store u.src
```

<!-- example slots: output -->
```
u.src:6: _copy: operand: fschg-slot
u.src:7: _store: none
```

`_copy` writes its second `FSCHG` in the slot of its `RTS`, on line 6. `_store` writes it above the `RTS`, on line 12, and leaves the slot a `NOP`: it is what R10 writes for `void e_st(float m[4][4]) { _builtin_st_ext(m); }`, cut down to two of its eight stores.

What shows nothing:

- an `FSCHG` right below `BT` or `BF`, which have no slot: R10 writes `TST R5,R5`, `BT L413`, `FSCHG` for `if (n) { _builtin_ld_ext(m); return; }`;
- an `FSCHG` at a branch's target, with no delayed branch right above its label, or below a slot;
- an `FSCHG` below a branch with an `.ALIGN`, a `.SECTION` or another line that is no instruction and that control doesn't pass between them. The rule places no slot past such a line, and where the stack rule reaches the branch it meets the limit `delay-slot`. asmsh can still assemble the `FSCHG` into the slot, past an `.ALIGN` that writes no fill, a `.POOL` with no literal waiting or a `.SECTION` that resumes the branch's section, and the rule doesn't read it there. R10 writes `.ALIGN 4` below each piece of inline text, one that ends in a branch among them;
- `FRCHG` in a slot, which is outside the rule.

The branch can be inline text where the `FSCHG` isn't. In a text drop-inline-align has patched, an `FSCHG` that SHC compiled from C is a witness where the branch above it is inline text: the witness then shows that the function holds a delayed branch SHC didn't write, and it stands on inline text ([evidence](#fschg-slot)).

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

### PR's save

A push of PR through R15, `STS.L PR,@-R15`, is a `pr-save-below-branch` witness where it stands in the run of a `BT`, `BF`, `BT/S` or `BF/S` above it, below that branch's slot. SHC is known to save PR only in a function's entry, which it writes above the function's first branch: a path that returns before it calls pays for the save too ([evidence](#pr-save-below-branch)).

**A run** is the lines below a conditional branch, in the order of the text, reached or not, up to the first that ends it:

- a label: on a line of its own, on an instruction, the push's own among them, or on a directive, as every `.EQU` and `.ASSIGN` has one;
- `BRA`, `BRAF`, `JMP`, `RTS`, `RTE` or `TRAPA`;
- data, or a directive other than `.EXPORT` and `.IMPORT`.

Any other instruction stays in the run, a call and its slot among them, and so do blanks, comments, and `.EXPORT` and `.IMPORT` lines with no label, with their continuations. The first instruction below a `BT/S` or `BF/S` is its slot, which runs on both paths, so a push there is no witness of that branch. The branch is one of the function's lines. R15 may be written `SP`, and the push may go without its size. For `v.src`:

<!-- example saves: input v.src -->
```
_late:
          TST         R4,R4
          BT          L1
          STS.L       PR,@-R15
          JSR         @R5
          NOP
          LDS.L       @R15+,PR
L1:
          RTS
          NOP
_early:
          TST         R4,R4
          BT          L2
          RTS
          NOP
L2:
          STS.L       PR,@-R15
          JSR         @R5
          NOP
          LDS.L       @R15+,PR
          RTS
          NOP
```

<!-- example saves: run -->
```sh
build/shckit inspect --entry _late --end _early v.src
build/shckit inspect --entry _early v.src
```

<!-- example saves: output -->
```
v.src:4: _late: abi: pr-save-below-branch
v.src:5: _late: incomplete: call
v.src:11: _early: none
v.src:18: _early: incomplete: call
```

`_late` saves PR on line 4, below the `BT` on line 3, for the one path that calls. `_early`'s push stands below a `BT` too, but past an `RTS` and the label `L2`, so it shows nothing: SHC writes that shape for a function that branches to the static function below it.

**Why a label ends a run.** SHC starts every function at a label, a static one at a label of the form `Lnn`, like any branch target. It can write a static function right below a branch of the function above, with no return between them, and nothing but SHC's `; function:` comment, which inspect doesn't read, tells that label from the others ([what SHC writes](#what-has-no-rule)). So this rule doesn't take [the function](#the-function)'s lines as one function's: it reads a branch and a push together only where no label stands between them, so it needs no `--end` to keep one function's branch from another's push. It still reads the function's lines alone: without `--end` they take in each function below the entry in its block, and a witness there is reported under the entry's name. It reads that a line holds a label, never the label's name.

**The witness is in the text, not in the code.** A label is not in the code's bytes. The other rules' witnesses are in what the bytes hold too, an instruction's word, its operands or a pool's word, though `small-constant` and `integer-constant` also read whether the text names a pool word's run of data: the same bytes show their witness where nothing names the run, and the limit `pool-word` where a name does ([constants](#constants)). In a text SHC wrote, the labels are SHC's, and the witness says what a witness says. In a text someone wrote from bytes, the labels are the writer's: there the witness shows that the branch and the push aren't one C function's, and it doesn't show that no C function starts between them. The same bytes under the label SHC writes there show none ([evidence](#pr-save-below-branch)). In a module the inter-module optimizer rewrote, which is outside the claim, the branch and the push can be one C function's all the same: the optimizer writes the push ([evidence](#the-inter-module-optimizer)).

A push below `BRA`, a call, a jump or a return with no conditional branch above it in its run is outside the rule. So are PR saved another way, by `STS PR,Rn`, by a copy's `MOV.L` or through another register, and PR's pop, `LDS.L @R15+,PR`, which SHC writes in each exit.

## The stack rule

The stack rule follows control through the function and finds six witnesses: [R15 above the entry after a return](#above-the-entry), [R15 below it after a return](#below-the-entry), and after an odd number of `FSCHG`s [a pair's move](#pairs-after-fschg), [a `DT`](#dt-after-fschg), [a store of FR4, FR6, DR4 or DR6](#stores-after-fschg) and [a move between FPU registers](#register-moves-after-fschg).

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
- The memory operand is any of `@Rn`, `@Rn+`, `@-Rn` and `@(R0,Rn)`, through any register, R15 among them. XD8 to XD14, fields 9, 11, 13 and 15, are outside the rule, and so are moves between registers, which are [fschg-register-moves'](#register-moves-after-fschg).
- The `FSCHG`s can be inline text where the move isn't. In a text drop-inline-align has patched, a move SHC compiled from C is a witness where the `FSCHG` above it is inline text, and it stands on that inline text ([evidence](#fschg-dt)).

Here the store on line 3 is a witness, spelled `FMOV.S FR12,@-R4` or not:

```
_f:
          FSCHG
          FMOV        DR12,@-R4
          RTS
          FSCHG
```

### DT after FSCHG

A `DT` after an odd number of `FSCHG`s on every path the rule follows to it is an `fschg-dt` witness at its line. The count is [fschg-pairs'](#pairs-after-fschg): 0 at the entry, an `FSCHG` in a slot counted, and unknown past a join of paths that disagree or a write to FPSCR. The `DT`'s register isn't read.

R10 writes a `DT` only in place of a compare, and C's own compares stand outside the runs its builtins open and close. So a loop R10 compiles counts down below the `FSCHG` that closes a run ([evidence](#fschg-dt)). For `t.src`:

<!-- example counts: input t.src -->
```
_copy:
          FSCHG
?loop:
          FMOV        @R5+,DR0
          DT          R6
          FMOV        DR0,@R4
          BF/S        ?loop
          ADD         #8,R4
          FSCHG
          RTS
          NOP
_store:
L527:
          MOV         R4,R3
          ADD         #64,R3
          FSCHG
          FMOV        XD14,@-R3
          FMOV        XD0,@-R3
          FSCHG
          DT          R5
          BF/S        L527
          ADD         #64,R4
          RTS
          NOP
```

<!-- example counts: run -->
```sh
build/shckit inspect --entry _copy --end _store t.src
build/shckit inspect --entry _store t.src
```

<!-- example counts: output -->
```
t.src:5: _copy: operand: fschg-dt
t.src:12: _store: none
```

`_copy` counts its loop down on line 5, between the `FSCHG`s on lines 2 and 9. `_store` is what R10 writes for `void l_1(M *m, int n) { do { _builtin_st_ext(*m); m++; } while (--n); }`, with `M` a `float[4][4]`, cut down to two of its eight stores: each pass opens a run and closes it, and the `DT` on line 20 stands below the closing `FSCHG`.

The rule reads the `DT` alone, not the loop. A compare, a conditional branch, a label and an `ADD` after an odd number of `FSCHG`s show nothing ([what has no rule](#what-has-no-rule)). A [limit](#limits) hides the witness: past a call, a jump or an `.ALIGN` the rule knows no count.

The `FSCHG`s can be inline text where the `DT` isn't. In a text drop-inline-align has patched, a `DT` that SHC compiled from C is a witness where the `FSCHG` above it is inline text: the witness then shows that the function holds an `FSCHG` no template wrote, and it stands on inline text. In the text as R10 writes it, the `.ALIGN 4` below the inline text stops the count ([evidence](#fschg-dt)).

### Stores after FSCHG

An `FMOV` to memory from FR4, FR6, DR4 or DR6, after an odd number of `FSCHG`s on every path the rule follows to it, is an `fschg-stores` witness at its line. The count is [fschg-pairs'](#pairs-after-fschg).

- Each of those stores has a code word whose register field is 4 or 6: `FMOV.S FR4,@-R1`, `FMOV FR4,@-R1` and `FMOV DR4,@-R1` are one word, which with SZ 1 stores DR4 as a pair, so the rule reads them alike.
- The memory operand is any of `@Rn`, `@-Rn` and `@(R0,Rn)`, through any register, R15 among them.
- A load into those registers is no witness, nor is a store of DR0, DR2 or an XD pair: R10's templates write them after an odd `FSCHG` ([evidence](#fschg-stores)). XD4 and XD6 have fields 5 and 7, as FR5 and FR7 have.

For `s.src`:

<!-- example stores: input s.src -->
```
_clear:
          FLDI0       FR4
          FLDI0       FR5
          FSCHG
          FMOV        DR4,@-R1
          FMOV        DR4,@-R1
          FSCHG
          RTS
          NOP
_push:
          ADD         #64,R4
          FMOV.S      FR4,@-R5
          FMOV.S      FR5,@-R5
          FSCHG
          FMOV        XD14,@-R4
          FMOV        XD0,@-R4
          FSCHG
          FMOV.S      FR6,@-R5
          RTS
          FMOV.S      FR7,@-R5
```

<!-- example stores: run -->
```sh
build/shckit inspect --entry _clear --end _push --all s.src
build/shckit inspect --entry _push s.src
```

<!-- example stores: output -->
```
s.src:5: _clear: operand: fschg-stores
s.src:6: _clear: operand: fschg-stores
s.src:10: _push: none
```

`_clear` stores DR4 twice between its `FSCHG`s. `_push` is what R10 writes for `void s_2(M m, float *p, float x, float y, float z, float w) { *--p = x; *--p = y; _builtin_st_ext(m); *--p = z; *--p = w; }`, with `M` a `float[4][4]`, cut down to two of its eight stores of pairs: C's own stores of FR4 to FR7 stand above the run and below it.

A [limit](#limits) hides the witness as it hides `fschg-dt`'s, and as there the `FSCHG`s can be inline text where the store isn't: in a text drop-inline-align has patched, a store SHC compiled from C is a witness where the `FSCHG` above it is inline text, and it stands on that inline text ([evidence](#fschg-stores)).

### Register moves after FSCHG

An `FMOV` between two FPU registers, after an odd number of `FSCHG`s on every path the rule follows to it, is an `fschg-register-moves` witness at its line. The count is [fschg-pairs'](#pairs-after-fschg).

- The registers are FRm and FRn, or a DR or an XD register on each side, as asmsh takes them, with a size or without: `FMOV DR0,XD0`, `FMOV.S FR0,FR1` and `FMOV FR0,FR1` are one word, which with SZ 1 moves a pair, so the rule reads them alike.
- A move through memory is no witness of this rule. After an odd `FSCHG` R10 moves a pair only between a register and memory, and C's own copies of floats stand at even counts ([evidence](#fschg-register-moves)).

For `r.src`:

<!-- example moves: input r.src -->
```
_unit:
          FLDI1       FR0
          FLDI0       FR1
          FSCHG
          FMOV        DR0,XD0
          FSCHG
          RTS
          NOP
_swap:
          ADD         #-4,R15
          ADD         #64,R4
          FMOV.S      FR5,@R15
          FMOV.S      FR4,FR5
          FMOV.S      @R15,FR4
          FSCHG
          FMOV        XD14,@-R4
          FMOV        XD0,@-R4
          FSCHG
          FMOV.S      FR4,FR0
          FSUB        FR5,FR0
          RTS
          ADD         #4,R15
```

<!-- example moves: run -->
```sh
build/shckit inspect --entry _unit --end _swap r.src
build/shckit inspect --entry _swap r.src
```

<!-- example moves: output -->
```
r.src:5: _unit: operand: fschg-register-moves
r.src:9: _swap: none
```

`_unit` moves DR0 to XD0 between its `FSCHG`s. `_swap` is what R10 writes for `float m_1(M m, float x, float y) { float t = x; x = y; y = t; _builtin_st_ext(m); return x - y; }`, with `M` a `float[4][4]`, cut down to two of its eight stores of pairs: C's own copies, `FMOV.S FR4,FR5` and `FMOV.S FR4,FR0`, stand above the run and below it.

A [limit](#limits) hides the witness as it hides `fschg-dt`'s, and as there the `FSCHG`s can be inline text where the move isn't. In a text drop-inline-align has patched, a copy SHC compiled from C is a witness where the `FSCHG` above it is inline text: it then shows that the function holds an `FSCHG` no template wrote, and it stands on inline text.

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
| 5.0 R10 | every rule, the frame argument unfinished, of `integer-constant` only how integer constants are loaded, and of `pr-save-below-branch` the entry and the scheduler's windows | [the sweeps](#the-sweeps), [the family compiles](#the-family-compiles), [the FSCHG compiles](#the-fschg-compiles), [the FIPR compiles](#the-fipr-compiles) |
| 5.0 R32 | the opcode rules, of [`cmp-str`](#cmp-str) only what scripts read, `fipr-registers`, `ftrv-registers`, `zero-displacement`, the scheduler, and for `fipr-unloaded` and `fipr-shared` the template's rows and the tests of four of SHCPEP's passes | [the sweeps](#the-sweeps), [the family compiles](#the-family-compiles), [the FIPR compiles](#the-fipr-compiles) |
| 5.0 R26, R27, R28, R31 and 5.1 R01, R03, R04, R08, R11, R13 | the encoder for `rotcl`, `rotcr`, `swap`, `dmuls` and `dmulu`, the form rows for `FSCHG`'s word, each stage's text, by script what [`cmp-str`](#cmp-str) names, in 5.1 R11 part of `pr-save-below-branch`'s entry and windows, for `fipr-unloaded` and `fipr-shared` the template's rows and the tests of four of SHCPEP's passes, and for `fipr-shared` the routine that fills a conditional branch's slot in R26 and 5.1 R13 | [the family compiles](#the-family-compiles), [the FSCHG compiles](#the-fschg-compiles), [the FIPR compiles](#the-fipr-compiles) |
| 6.0 AD | the form rows for `FSCHG`'s word, in a `shcasm.exe` that is 5.1 R13's file, of [`cmp-str`](#cmp-str) what scripts read in 5.1 R13's `shcgen.exe`, `shcpep.exe` and `shcasm.exe`, which are its files too, and for `fipr-unloaded` and `fipr-shared` what was read in those three | nothing |
| any other release | nothing | nothing |

`integer-constant` rests on [its census](#integer-constant) of the family compiles and of probes of its own, beside what R10's reading shows of integer constants. `pr-save-below-branch` rests on R10's reading, on a scan of the other releases for the places it names, and on [compiles of its own](#pr-save-below-branch). `fschg-slot`, `fschg-dt`, `fschg-stores` and `fschg-register-moves` rest on R10's reading and on [the FSCHG compiles](#the-fschg-compiles): of the form rows read, R10's alone hold `FSCHG`'s word. `cmp-str` rests on R10's reading, on what scripts read in each release from R26 to 5.1 R13, and on [compiles of its own](#cmp-str). `fipr-unloaded` and `fipr-shared` rest on R10's reading, on a scan of the other releases for the rows and tests they name, and on [the FIPR compiles](#the-fipr-compiles). A reading of one release says nothing by itself of another. A compile shows what a release writes for the C tried, and nothing of other C. What carries a rule across releases is inference: the releases from R26 to 5.1 R13 name the same builtins, R10 differs only in `_builtin_fschg`, `_builtin_frchg`, `_builtin_fabs` and `_builtin_sqrt`, and no compile of any of them outside inline text writes a witness. A release can still differ in code nobody read, and a later release can add a builtin. That is why a witness says "not known" and no more.

An address cited is a virtual address in one of R10's stages, under image base 0x400000:

- `shcfrt.exe`, SHA-256 `e3223c795644fd137c3ac400440d7e31bd67740bb76890e2f6f50feee5412ec4`;
- `shcmdl.exe`, SHA-256 `00ee4f7220cb65d3bed2e3d7cb5c687a3c06e97b5e432470c7d7cb7584ac377b`;
- `shcgen.exe`, SHA-256 `9963ce29e1c9c1d9b5756a0b703332cbd23cee849805ebea8a967fe48929e789`;
- `shcpep.exe` and `shcasm.exe`, as patches.md's [evidence base](patches.md#evidence-base) gives them.

[pr-save-below-branch](#pr-save-below-branch) also cites three of 5.1 R11's stages, under the same base:

- `shcgen.exe`, SHA-256 `113c15b4e89b1ac4dd1d1c0d381c2e8378e98e04e54c5d98bad1950f45915ac3`;
- `shcpep.exe`, SHA-256 `1844dde96e8f4ccf9e1ade6f66db0cea0e656e8ca51224387f4facb55041c744`;
- `shcasm.exe`, SHA-256 `f2435048eedf28b64813c128a5f5395c2ca7a49d00d0859233a47f5f100d859b`.

[What has no rule](#what-has-no-rule) cites one address in that `shcgen.exe` and in 5.1 R13's, SHA-256 `98882d4f2019dab173a5c485211f359f30bb5320494cacbcc6bdd1f0b424f5d3`, under the same base.

[fipr-unloaded](#fipr-unloaded) and [fipr-shared](#fipr-shared) also cite 5.1 R13's `shcpep.exe`, SHA-256 `a1c2b7f8c47b8839dff5dfce3d975159ab6e34392815bfe9834c8cad174ce0ba`, and fipr-shared R26's, SHA-256 `b0e84ed65a4b493cc83a11feef89da97bae14c4680b93e68423752f3f4cdc2de`, under the same base.

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

C put through each release from 5.0 R10 to 5.1 R13, and every function read with every rule but `cmp-str`, `integer-constant`, `pr-save-below-branch`, `fschg-slot`, `fschg-dt`, `fschg-stores`, `fschg-register-moves`, `fipr-unloaded` and `fipr-shared`, without `--sz` and with `--sz 0`. 6.0 AD isn't among them. There are two populations:

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

Outside inline text, none of these 153,201 functions holds a witness of any of those rules. `cmp-str`, `integer-constant`, `pr-save-below-branch`, `fschg-slot`, `fschg-dt`, `fschg-stores`, `fschg-register-moves`, `fipr-unloaded` and `fipr-shared` came later, and each finds its witness only in inline text: its evidence says which of them it read ([cmp-str](#cmp-str), [integer-constant](#integer-constant), [pr-save-below-branch](#pr-save-below-branch), [the FSCHG compiles](#the-fschg-compiles), [the FIPR compiles](#the-fipr-compiles)). They hold 3,654 `FIPR`s, 10,083 `FTRV`s and 3,213 `FSCA`s with the templates' operands alone, and every `FSCHG` among them is R10's. Four rules took their present form from these compiles: `small-constant`, `integer-constant`, `stack-written` and `return-above-entry`. For those four the compiles confirm the form rather than test it, and C nobody tried can still refute it. What shaped them is C written to reach them: no retained compile loads a word from 0 to 127 by `MOV.L`, none of those still on disk loads one up to 32767 outside inline text, and none moves a stack switch or takes R15 above the entry on a path the stack rule reads.

The stack rule reads less than the instruction rules do. It read 80,762 of the 153,201 functions with no limit. It stopped at a call in 61,182 and at another limit in 11,257, and for those the stack witnesses say nothing past that line. So `return-above-entry` and `return-below-entry` rest mostly on the leaf functions and the paths before a first call, where `stack-written` and `zero-displacement` read every instruction.

### The FSCHG compiles

C written to put an instruction in or beside a run, and compiled by R10, the one release known to write `FSCHG` ([fschg-pairs](#fschg-pairs)). A run is what R10's templates open and close: the lines after an odd number of `FSCHG`s. `fschg-slot`, `fschg-dt`, `fschg-stores` and `fschg-register-moves` rest on these compiles beside R10's reading.

- **Probes.** 160 functions in four files. They call each of the eleven builtins whose templates write `FSCHG`:
  - as a function's last statement, before a return of an `int`, a `float`, a pointer and a global, with a frame and saved registers to free, before and after a call and before a tail call;
  - in both arms of an `if`, under `&&` and `||`, in a `switch` whose cases end alike, and around `goto`;
  - in `do`, `while` and `for` loops, nested and with `break` and `continue`, whose counter is stepped before, between and after the builtins, in a register, on the stack, in a global and through a pointer;
  - beside float arguments that live across a run, stores through `*--p`, loads and copies of floats before, between and after runs, and FPSCR written around one;
  - in interrupt functions and `#pragma inline` wrappers, with frames over 128 bytes and structs passed and returned.
- **Matrices.** R10 compiled the probes under the options of [the family compiles'](#the-family-compiles) probes, 124 texts, and under 27 values of `-extra=a=`, each single bit from 1 to 80000 among them, at `-optimize=0`, `-optimize=1 -speed` and `-optimize=1 -size -double=float` in both modes, 648 texts. Their 30,880 functions hold 106,049 `FSCHG`s. Each release from R26 to 5.1 R13 compiled the probes under the first options too, 180 texts of 7,200 functions a release, with no `FSCHG`.
- **Random C.** 150 files of 20 to 30 functions, each of two to five statements drawn from the eleven builtins with 38 pointer and 18 matrix operand forms, float and integer statements, arms of an `if` that end alike, counted loops in ten forms, `switch`, early returns, `goto` and calls. 90 of the files add runs of 5 to 13 builtins, large arms that close on one builtin, loops that end in a builtin whose pointer needs a set-up, statements right below a run, and globals bound to FR12 to FR14. R10 compiled them under 69 sets of options: `-optimize=0` and 1, for size and for speed, with `-loop`, `-inline`, `-pic=1`, `-double=float`, thirteen values of `-extra=a=`, and bits of `-extra`'s other keys alone and beside them. The 3,030 texts hold 76,920 functions and 5,338,564 `FSCHG`s. 30 `-code=machinecode` listings of six of the files hold 600 functions and 43,786 more.
- **`-extra`'s keys.** `-extra` takes nine keys, `reg`, `temp`, `asm`, `pep`, `gen`, `back`, `mdl`, `frt` and `cnt`, and `a` is `asm`. The probes, 28 functions that vary one builtin's pointer, and one random file went through `-optimize=1 -speed` with each single bit of each key: 1,728 compiles, 1,492 of which wrote a text, with 439,530 `FSCHG`s.
- **The other releases.** Three more random files went through each release from R26 to 5.1 R13 under three sets of options, in both modes: 360 functions a release, with no `FSCHG`.

**What inspect read.** Each function that holds an `FSCHG`, cut at SHC's `; function:` comments, without `--sz`, in:

- the texts above, and those of the probes each rule's evidence names;
- 87 more of R10's that were compiled for other rules;
- the probes of [the family compiles](#the-family-compiles);
- the retained compiles still on disk on 2026-11-19, 8,579 texts of every release from R10 to 5.1 R13, among them every text of DreamComp's cache as it stood that day: each compile's listing or text, and each text shckit's patches or another step of its builds made of one.

That is 21,031 distinct texts of 307,178 functions, 91,754 of which hold an `FSCHG`, 4,811,243 `FSCHG`s in all. 4 of the texts, compiled under `-debug`, went through as copies without the `.LINE` lines check refuses. `fschg-slot` shows 45 times in inline text, hand-written assembly that the retained compiles copy, and once at an `FSCHG` that R10 compiled below an inline `BRA`, in a text drop-inline-align patched ([fschg-slot](#fschg-slot)). `fschg-dt` shows 72 times in inline text, and once at a `DT` that R10 compiled between two inline `FSCHG`s, in a text drop-inline-align patched ([fschg-dt](#fschg-dt)). `fschg-stores` shows 386 times in inline text, and twice at stores that R10 compiled between two inline `FSCHG`s, in a text drop-inline-align patched ([fschg-stores](#fschg-stores)). `fschg-register-moves` shows 541 times in inline text, and 6 times at copies that R10 compiled behind an inline `FSCHG`, in the two texts drop-inline-align patched ([fschg-register-moves](#fschg-register-moves)).

The stack rule reads less of these functions than `fschg-slot` does. Leaving out `stack-unknown`, which doesn't stop the count and which is the first limit of every function of the random C, it read 23,188 of the 25,450 functions outside the random C with no limit, and stopped at a call in 1,791 and at another limit in 471. In the random C's 30 listings and every twelfth of its texts, 250 texts in all, it read 4,112 of the 6,080 functions with no limit, and stopped at a call in 1,344 and at a jump in 624. For those `fschg-dt`, `fschg-stores` and `fschg-register-moves` say nothing past that line.

### The FIPR compiles

C written to part a `FIPR FV4,FV0` from its template's loads, compiled by each release from 5.0 R10 to 5.1 R13. `fipr-unloaded` and `fipr-shared` rest on these compiles beside the readings.

- **Probes.** 121 functions in four files:
  - float arguments already in FR4 to FR11 copied into the builtin's arrays; a vector of two differences and a zero, as arrays, `register` arrays, structs, walks, statics and globals; constant vectors; inline functions; results dropped or chained;
  - the builtin twice with one or both pointers the same; in arms of `if`, `?:` and `switch`; in loops and around `goto`; after `_builtin_ftrv`, `_builtin_ftrvadd`, `_builtin_ftrvsub`, `_builtin_add4`, `_builtin_sub4`, `_builtin_ld_ext`, `_builtin_st_ext`, `_builtin_mtrx4mul`, `_builtin_fsca` and `_builtin_fsrra`; and with calls around it;
  - sixteen to thirty-six `FDIV`s or twenty `FSQRT`s before, after and between `FIPR`s, and an interrupt function;
  - a pre-decrement store and the template's load through one pointer; plain stores through the pointer the template loads through; loads the template repeats; one vector shared by three products; arms that open or close alike; dead and unreachable calls; functions long enough for a pool to break them; and float arguments live across the template.
- **Options.** `-optimize=1 -speed`, `-optimize=1 -size` and `-optimize=0`, each under no `-fpu`, `-fpu=single`, `-fpu=double`, `-fpu=single -denormalize=on` and `-double=float`, in both modes, R10 under the first and the last alone. One at a time over `-optimize=1 -speed`: `-pic=1`, `-macsave=1`, `-round=nearest`, `-round=zero`, `-loop`, `-inline`, `-inline=200 -nestinline=4`, `-noinline`, `-rtnext`, `-abs16=all`, `-align16`, `-endian=big`, `-division=cpu`, `-extra=a=400`, `800`, `1000` and `1800`, `-debug`, and `-cpu=sh3e`, `sh3` and `sh2`. Six option sets of games' builds, in both modes. And each single bit of `-extra=a` from 1 to 8000 but 8, SHCASM's dump bit: under it R26, R27 and R28 wrote no text, and each release from R31 to 5.1 R08 ran to its time limit on the file of chained divisions. Of the other fifteen bits' compiles one timed out, R28's of one file under bit 80. Every release refuses the builtin under `-fpu=double` and under `-cpu=sh2`, `sh3` and `sh3e`, with error 2804, and R10 has no `-round`. The 4,559 compiles left 3,211 texts.
- **A second round**, written to break the rule after the first found nothing. 14 functions put a `FIPR` before a return, with other returns, joins, loops, a `switch`, frames and saved registers in the function. 44 use volatile vectors, absolute addresses, constant tables, struct and double operands, `noregsave`, `noregalloc`, `regsave`, three kinds of interrupt function, `global_register` for FR12, FR13 and R14, `gbr_base`, `abs16`, tail calls, a call through a pointer, recursion, and the result in the conditions of `if`, `while`, `do`, `for`, `switch`, `&&`, `||` and `?:`. 90 make a `FIPR` the slot candidate of branches over 4 to 700 statements. 22 advance the pointers in C as the template does. 18 use `setjmp`, frames of 1,200 and 4,400 bytes, double and struct arguments and results, a union and a hand-walked argument list. And 360 generated functions nest arms, loops, switches, `goto`, early returns, calls, stores through the vectors and up to 24 divisions or 20 square roots around the builtin, which some call several dozen times. They went through 30 sets of options, not every file under each: `-optimize=0` and `-optimize=1` with `-speed`, `-size` and `-nospeed`; no `-fpu`, `-fpu=single` and `-double=float`; `-loop`, `-inline=200 -nestinline=4` and `-inline=400 -nestinline=8`; `-pic=1 -rtnext -macsave=1`; `-align16 -abs16=all`; `-endian=big -denormalize=on -round=nearest`; `-division=peripheral -extra=a=1000`; thirteen values of `-extra=a=`, never bit 8; `-goptimize`, `-case=table` and `-case=ifthen` where a release takes them; and two sets as `-code=machinecode` listings. R10, R26 and R13 refuse the builtin under `-cpu=sh1`, `sh2`, `sh2e`, `sh3` and `sh3e`. The 1,986 compiles left 1,923 texts.

**What inspect read.** Each function that holds a `FIPR`, cut at SHC's `; function:` comments, without `--sz`:

| population | texts | functions | compiled functions with a `FIPR FV4,FV0` | `FIPR FV4,FV0` in them |
|---|---|---|---|---|
| the probes | 3,211 | 93,159 | 90,260 | 263,537 |
| the second round | 1,923 | 72,610 | 72,397 | 996,387 |
| the probes of [the FSCHG compiles](#the-fschg-compiles), as each release compiled them | 3,092 | 109,880 | 6,174 | 6,860 |
| the probes of [the family compiles](#the-family-compiles) | 7,513 | 77,151 | 2,576 | 3,542 |
| the retained compiles still on disk on 2026-11-19, every text of DreamComp's cache as it stood that day among them | 8,579 | 75,843 | 26 | 100 |

A compiled function is one that holds no inline text. None of the 171,433 compiled functions with a `FIPR FV4,FV0` shows `fipr-unloaded` or `fipr-shared`. In the retained compiles `fipr-unloaded` shows in 40 functions and `fipr-shared` in 41, 31 of them the same, each at a `FIPR` that is inline text.

### The inter-module optimizer

5.1 R08, R11 and R13 ship `optlnksh.exe` and `optsh.exe`, the "SH SERIES INTER-MODULE OPTIMIZER Ver. 1.1": Release03 with R08, and Release05 with R11 and R13, the same two files in both. 5.0 R10 to R32 and 5.1 R01 to R04 ship neither. The 6.0 AD files here hold R13's two, byte for byte, beside a linker of 6.0 AD's own, `optlnk.exe`, the "Optimizing Linkage Editor Ver7.0", whose usage lists `-optimize=same_code`, "Unify common code", too. That linker refuses a 5.1 R13 object with error L2200, and 6.0 AD compiled nothing here, so it wasn't tried. The optimizer links objects compiled with `-goptimize`, which makes the compiler write a supplementary file for each, `shciop/NAME.iop`.

Under `-optimize=same_code`, "unify common code", it moves a block that stands twice or more in a module into a routine of its own, `_com_optN`, and in place of each copy writes `STS.L PR,@-R15`, `BSR`, `NOP` and `LDS.L @R15+,PR`. No compiler wrote that routine, and the linked module says so only by its name: the module's symbols and its map hold `_com_optN`, and an image converted from the module holds no name. A block that opens with a template's `FIPR` becomes a function with the `FIPR` and none of its loads. For `o10.c`, with `ext` defined in another file:

```c
extern void ext(void);
float t(float *a, float *b, float *c, int k) {
  float r = 0.0f, s;
  ext();
  s = k ? _builtin_fipr(a, b) : _builtin_fipr(a, c); r += s * s * s + s;
  ext();
  s = k ? _builtin_fipr(b, c) : _builtin_fipr(c, a); r += s * s * s + s;
  ext();
  s = k ? _builtin_fipr(c, c) : _builtin_fipr(b, a); r += s * s * s + s;
  ext();
  return r;
}
```

5.1 R13 under `shc o10.c -objectfile=o10.obj -code=machinecode -cpu=sh4 -endian=little -optimize=1 -speed -fpu=single -goptimize` keeps the loads of each `?:`'s two arms and shares one `FIPR` under a label, three times alike:

```
          BRA         L258
          FMOV.S      @R2+,FR3
L250:
          MOV         R14,R0
          MOV         R12,R1
          FMOV.S      @R1+,FR0
          ...
          FMOV.S      @R0+,FR7
L258:
          FIPR        FV4,FV0
          FMOV.S      FR3,FR4
          FMUL        FR4,FR3
          FMOV.S      FR4,FR0
          FMOV.S      FR4,FR2
          FMAC        FR0,FR3,FR2
          JSR         @R13
          FADD        FR2,FR15
```

`optlnksh -optimize=same_code -samesize=8 -information -subcommand=o10.sub`, whose file names the two objects and places `P` at `H'8C010000`, prints `** 0220 SAME_CODE OPTIMIZE:o10._com_opt1 IS CREATED` and `** 0240 o10._t IS OPTIMIZED`. The linked module holds one `FIPR` where the object held three, in a routine of six instructions and a return:

```
8c01010c  FIPR FV4,FV0              ; _com_opt1
8c01010e  FMOV.S FR3,FR4
8c010110  FMUL FR4,FR3
8c010112  FMOV.S FR4,FR0
8c010114  FMOV.S FR4,FR2
8c010116  FMAC FR0,FR3,FR2
8c010118  RTS
8c01011a  NOP
```

`_com_opt1` holds `FIPR FV4,FV0` and no load: as a text, it shows `fipr-unloaded`, naming all eight registers, for a module compiled from C alone.

`o11.c` is `o10.c` with a fourth pointer `d`, `float t(float *a, float *b, float *c, float *d, int k)`, and `r += s * _builtin_fipr(d, d);` in place of each `r += s * s * s + s;`, so the block below the shared `FIPR` holds a whole template too. Under the same commands the optimizer makes:

```
8c010120  MOV R14,R2                ; _com_opt1
8c010122  MOV R14,R3
8c010124  FIPR FV4,FV0
8c010126  FMOV.S @R2+,FR0
8c010128  FMOV.S @R3+,FR4
8c01012a  FMOV.S @R2+,FR1
8c01012c  FMOV.S @R3+,FR5
8c01012e  FMOV.S @R2+,FR2
8c010130  FMOV.S @R3+,FR6
8c010132  FMOV.S FR3,FR14
8c010134  FMOV.S @R2+,FR3
8c010136  FMOV.S @R3+,FR7
8c010138  FIPR FV4,FV0
8c01013a  FMOV.S FR3,FR0
8c01013c  RTS
8c01013e  NOP
```

Two `FIPR FV4,FV0` and one load of each register: as a text, `fipr-shared`, naming all eight.

- **Which releases.** Each 5.0 release, R10 to R32, refuses `-goptimize` with error 3305, and objects that R10, R26 and R32 compiled without it gave no routine. 5.1 R01 takes the option and writes no supplementary file, and gave no routine. 5.1 R03 writes a file that both optimizers refuse, with error 3060. The objects of 5.1 R04, R08, R11 and R13 each gave `_com_opt1` through Release03, through R11's Release05 and through R13's.
- **What it was seen to do**, in eleven probes. None of it was read in `optsh.exe`:
  - It needs the supplementary file: a 5.1 R13 object compiled without the option gave no routine.
  - It took whole blocks, from a call, a branch or a label to the next. Four blocks that end in the same eight loads and `FIPR` and differ above them stayed where they were.
  - It took blocks only from functions that save PR, and made one routine a run.
  - Each of the seven routines it made ends in `RTS` with `NOP` in the slot. Two hold a template whole, eight loads and their `FIPR`, and one holds eight loads alone: those three show no witness.
  - It takes `-samesize=8` and refuses 4 with error 2730, and with no `-samesize` it left the 12-byte block above alone. With no `-optimize` option it made no routine.
- **What it means for a text.** inspect reads no linked module and no label's name, so no text shows it that a function is such a routine. Between the compiler and a text SHC wrote, or the listing of an object it wrote, no link stands. A text written from a linked image's bytes needs a reason of its own that the image went through no such link. Each rule was run over ten modules the optimizer rewrote, the two above among them, of objects that 5.1 R04, R08, R11 and R13 compiled, each written as text from its bytes with a label at each branch's target and at each function's entry. `fipr-unloaded` and `fipr-shared` show their witness in a routine, as above. `pr-save-below-branch` shows its witness in the function a block was taken from: for `void f(int k, int *p)`, which runs `p[0] = p[1] * 3 + p[2] * 5; p[3] += p[0]; p[4] ^= p[5];` under `if (k & 1)`, under `if (k & 2)` and under `if (k & 4)` with a call between them, each of the four releases' modules holds `TST #H'02,R0`, `BT`, `STS.L PR,@-R15`, `BSR`, `NOP` and `LDS.L @R15+,PR` where the second block stood. No other rule showed a witness: no routine in them names R15 or PR, each ends in `RTS` and `NOP`, and no 5.1 release writes `FSCHG`.

### Opcode rules

In R10 and R32, SHCASM encodes every instruction from its form rows, ORing a row's operand fields into its base word, so a row's fields only add bits to it. In those two, one function of SHCASM writes an instruction's word or its text, printing the mnemonic of the form row the word would come from, and SHCASM's other text is directives, operands, names, comments and the inline text it copies. Under `-code=machinecode`, every 5.x release refuses `#pragma inline_asm` with error 2808 and `#pragma asm` with 2708.

#### rotcl, rotcr and swap

- **Read.** Under `-code=machinecode` the reading covers every 5.x release. Under `-code=asmcode` it covers 5.0 R10 and R32, whose text writer was read. For the others' text it is inference.
- **Encoder.** In each release, SHCASM's encoder forms ROTCL's word only for internal op 0x4f, ROTCR's only for 0x5f, and SWAP.B's and SWAP.W's only for 0x46. No other of its form rows can make the four words.
- **Writers.** No stage writes the three ops: not SHCGEN's record writers, template rows or kind mapping, not SHCPEP's node writer, byte constants or shift table, and not SHCASM's builders.
- **Text.** Beyond their form rows, whose mnemonics SHCASM's text writer prints, only SHCASM's and SHCPEP's tables of op names spell ROTCL, ROTCR and SWAP, only debug dumps read those tables, and no other stage holds the names.
- **C.** No front end has a rotate or swap builtin. Byte swaps compile to shifts, masks and ORs, or to byte moves through memory, and a bit shifted from one word into the next to `ROTL` or `ROTR` and `AND #1`.
- **Sweeps.** No `-code=asmcode` text of the sweeps holds any of the four.
- **Refuted by** any release writing `ROTCL`, `ROTCR`, `SWAP.B` or `SWAP.W` for C, in either mode, outside inline text.

#### dmuls and dmulu

- **Read.** Every 5.x release.
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

#### cmp-str

- **Read.** Each release from 5.0 R10 to 5.1 R13: R10 line by line, the other eleven by script. In R10, SHCASM's encoder and its field functions, each record filler of SHCGEN with the op at each of its calls, the template interpreter and the kind mapping, SHCPEP's node writer, reader, copies and rewrites in place, SHCASM's node writer with the op at each of its calls, and each byte store that arithmetic feeds. In each of the others, the encoder's tables, every immediate and every data byte equal to 0x57 in SHCGEN, SHCPEP and SHCASM, every select of two constants, every template row, the table of builtins, the op at each call of the fillers and node writers, and the path of SHCGEN's record buffer. 6.0 AD's `shcgen.exe`, `shcpep.exe` and `shcasm.exe` are 5.1 R13's files. Of its front end only the builtin names were read.
- **Encoder.** In each of the twelve, SHCASM's encoder forms `CMP/STR`'s word, `0010nnnnmmmm1100`, only from form row 51, base word H'200C with a register field at bit 4 and one at bit 8, and it reaches that row only for internal op 0x57. Eight other rows have a base word with no bit outside H'2FFC, and each lacks a bit of H'200C that its fields can't add. Seven lack bit 2 or bit 3 and have no field below bit 4: `MOV.B Rm,@Rn`, `MOV.B Rm,@-Rn`, `MOV.B Rm,@(R0,Rn)`, `TST Rm,Rn`, `CLRT`, `CLRMAC` and `SETT`. The eighth, `MOV.B @(R0,Rm),Rn`, lacks bit 13, and R10 forms its memory field from the register byte, sign-extended and shifted to bit 4 (`0x403f67`), so the field's bits from 12 up are all clear or all set. The other releases' field functions weren't read.
- **Constants.** No stage of any of the twelve holds 0x57 to write as an op. Of the 2,277,517 instructions a walk of control reaches in the 36 stage files, those with a byte 0x57 in an immediate are compares that read an op, the bound of a switch over ops and the end of the range 0x50 to 0x57 of the ops that set T; a message number, `push 0x1357`; the C runtime's digit conversion, `add bl,0x57`; and the address of its number buffer, `[esp+0x257]`. Each data byte 0x57 is text, a table of the C runtime, the import table, a byte of an address, SHCASM's form row 87, `SHLL`'s, for op 0x4a, or an operand word of a template row. No select of two constants, which the stages' own compiler wrote with `sbb` or `setcc`, makes a low byte of 0x57 at any step a script follows. It follows none past a `lea`, which one or two selects of SHCASM reach in nine of the releases: each adds the select to an address, of a stack buffer or of the text SHCASM is writing.
- **SHCGEN.** In each of the twelve, SHCGEN builds its records in one fixed buffer, and every mention of the buffer in the image is a push of its address before a call: 129 in R10, 139 from R26 to 5.1 R01, 143 in R03 and 144 from R04. Nine functions receive it, the record writer, seven fillers and one that reads a record, and from them the pointer reaches 19 functions, 20 from R04, through no indirect call. Through it eight instructions write a record's first byte, its op: each filler's store of its second argument, and a store of zero that discards a record (R10 `0x427e5c`). The writer builds two more records on its own stack, a `BRA` and a label, to step over a pool. So an op of SHCGEN's is a filler's argument. In R10 each argument is an immediate, none of them 0x57, or one of three things:
  - a choice between immediates, such as `EXTS` or `EXTU`, `AND` or `TST`, a conditional branch or its opposite, a shift left or right, and one of three labels by its node's kind;
  - a template row's own op word. Of the 6,319 rows of R10's 538 named templates and 155 unnamed ones, none has an op word whose low byte is 0x57;
  - the result of the kind mapping (`0x41c8e0`): an immediate, one of those choices, or on an error path the mapping's own argument, whose low byte is zero. For C's orderings it returns 0x51 to 0x56, `CMP/HS`, `CMP/GE`, `CMP/HI`, `CMP/GT`, `CMP/PZ` and `CMP/PL`, and `CMP/EQ`, 0x50, is an immediate. Those seven are the compares C reaches. `CMP/STR` is the eighth in the encoder, and no path to it from a tree node was found.

  In the other eleven a script found the fillers' calls by their shape. No immediate there is 0x57, and the arguments that aren't immediates were read only in 5.1 R13, in part.
- **SHCPEP and SHCASM.** R10's SHCPEP builds a node at 13 calls of its node writer (`0x40b5f0`), each with an immediate op. It rewrites an op in place to an immediate, or to one of six shift ops from a table, and every byte immediate it stores is one of 35 values, none 0x57. Otherwise it copies: its reader stores the op that SHCGEN's stream holds (`0x40db82`). R10's SHCASM builds a record at 143 calls of its node writer (`0x4106a0`), where the op is one of 32 immediates, a select of two, or the op of another record, which it copies for a slot or an exit. The byte stores that arithmetic feeds, 53 in each of the two stages, write a byte of a record or an operand past its first, an operand's kind, a flag, a character of text, a size or a length, and none a record's op. In each of the other eleven a script found the two node writers by their shape, and no immediate op at their calls is 0x57.
- **A record of it would be encoded.** In R10, each stage's table of record formats gives op 0x57 the format of the other compares, SHCGEN's record writer writes any op its table doesn't mark zero, and SHCPEP's reader stores whatever op its input holds. So the rule rests on there being no writer, as [sett](#sett) does.
- **Text.** In each of the twelve, of the compiler's files only `shcasm.exe` and `shcpep.exe` hold the text CMP/STR: SHCASM, whose form row names it, and SHCPEP, in the table of op names from which it prints a dump of its node list. In R10 and R32, whose text writer prints the mnemonic of the form row a record's word would come from, `-code=asmcode` text therefore spells `CMP/STR` only for op 0x57, or as inline text. For the others that is inference from the compiles.
- **C.** SHC's `<string.h>`, as the Katana SDK 1.20J has it, defines `strcmp` and `strcpy` as `_builtin_strcmp` and `_builtin_strcpy`, and declares `memcmp`, `strlen`, `strncmp`, `strchr` and `memchr` as plain functions. In each of the twelve, SHCGEN's table of builtins gives `strcmp` code 0, the one name with it, and SHCGEN pushes the numbers of two runtime routines, `_quick_strcmp1` and `_slow_strcmp1`, where R10's call generator compares a callee's name with `_builtin_strcmp` (`0x405a2b`, `0x405b4f`, `0x405b5f`). So `strcmp` is a call: under `-cpu=sh4 -optimize=1 -speed`, R10 and 5.1 R13 compile `int c_ll(void) { return strcmp("abcdefg", "abcdefh"); }` to `STS.L PR,@-R15`, three `MOV.L`s of the literals and of `__quick_strcmp1`, `JSR @R3`, `NOP`, `LDS.L @R15+,PR` and `RTS`. `strcpy` has a template: with optimization on it is pairs of `MOV.B` for a short literal and a call of `__slow_strcpy` for the rest, and at `-optimize=0` a call of `__slow_strcpy` or `__quick_strcpy`. Of the other builtins only `tas` and `gbr_tst_byte` test a byte, by `TAS.B` and `TST.B` ([sett](#sett)). `CMP/STR` stands in the runtime's routines themselves: in the Katana SDK 1.20J's `sh4nlfzz.lib`, `__quick_strcmp1` and `_quick_strcpy1` hold four each.
- **Compiles.** C written to reach the instruction went through each release from R10 to 5.1 R13, in both modes:
  - `strcmp`, `_builtin_strcmp` and `strcpy` of each kind of operand, pointers, literals, arrays of `char` and of `long`, structs and members among them, compared with itself, through a pointer to `strcmp`, and with one builtin's result as the other's argument, and `memcmp`, `strlen`, `strncmp`, `strchr` and `memchr`;
  - a zero byte in a word, and a byte of one word equal to the byte of another in the same place, each by masks, shifts, casts, byte views, unions and bitfields, with `||`, `&&`, `|` and `?:`, in loops and without;
  - string routines by hand, a byte at a time and a word at a time in the shape of the runtime's own;
  - each compare with zero and each ordering, as a value and as a branch, after `x - 1`, `x + 1`, `-x` and `x--`, in counting loops and switches: the compares one `OR` or one increment away from 0x57, where the back end rewrites, inverts and merges them;
  - initialized data whose bytes are 0x57, in arrays, strings, structs and bitfields, and of 253 to 32,766 bytes around each power of two: a record misframed out of such data has op 0x57 ([Evidence](#evidence)).

  They went through the options of [the family compiles'](#the-family-compiles) probes, 38 option sets beyond those, `-aggressive`, `-structreg`, `-goptimize`, `-case=table` and `-lang=cpp` among them, `-cpu=7000` and `-cpu=7600`, and each single bit of `-extra`'s keys `a`, `b`, `c`, `g`, `m`, `p`, `r` and `t`, the bits under `-code=machinecode` in R10 and part of R26 alone. Of 25,289 compiles 22,050 wrote a text or a listing that holds code, 1,167,731 functions in all, and none holds `CMP/STR`. Nor do the texts of a function of 3,000 statements, which compiles to 4,142 compares or more, or of the string builtins called with the wrong arguments, which the front end refuses or compiles to the same calls. `hz_mask`, `!(x & 0xff) || !(x & 0xff00) || !(x & 0xff0000) || !(x & 0xff000000)`, compiles in 5.1 R13 to `EXTU.B R4,R0`, `TST R0,R0` and `BT`, then three times a `MOV.L` of a mask, `TST R4,R0` and `BT`. A control, `CMP/STR R0,R4` in a `#pragma inline_asm` body, is copied into the text under `-code=asmcode` by R10, R32 and 5.1 R13, and refused under `-code=machinecode` with error 2808.
- **The dump.** Under `-extra=p=80` SHCPEP prints its node list, and under `-extra=p=2000` at each pass, with each record's op by the name its table gives it: `CMP/EQ` to `CMP/STR` for 0x50 to 0x57. So a record of op 0x57 that SHCPEP held at any pass would be printed, whether or not it lived to the text. 1,865 compiles of the probes above, by each release, and of 581 C files written for other rules, by R10, R32 and 5.1 R13, printed 421,688,550 names, 234,313,392 of them no discarded record's and 2,971,181 of them compares. None is `CMP/STR`.
- **What inspect read.** Each code section whole, as one function, in every text of these populations still on disk on 2026-11-19: the retained compiles, every text of DreamComp's cache as it stood that day among them, 8,030 distinct texts of 68,269 functions; the 6,113 distinct texts of the family compiles' probes; 1,613 of this rule's probes; the 733 distinct texts its refutation kept; and 10,376 under other rules' research: their probes and refutations, their targets' texts, and 82 `#pragma inline_asm` bodies cut from DreamComp's sources, which no release compiled. That is 26,807 distinct texts of 538,840 functions, 246 of them compiled under `-debug` and read as copies without the `.LINE` lines check refuses. The witness shows in one, the control, at the line of its inline text. check refuses 14 more of the other rules' texts, and none of them holds the text CMP/STR. The 22,476 listings among the texts hold 16,999,666 instruction words, none of `CMP/STR`'s form, and 80 `.DATA.W` words that have it. In the refutation's texts, 63 lines hold the name inside the string of an `.SDATA`. The texts of [the sweeps](#the-sweeps) aren't among these: they weren't found as a population of their own.
- **Rests on** what wasn't read:
  - SHCGEN's record pointer was followed, in each of the twelve, by a script that reads registers in address order and not along every path;
  - SHCPEP's and SHCASM's records live on the heap, and their stores were read by value. R10's were read one by one, each store's source found by a walk back along the listing and not by a flow analysis. Those of the other releases were sorted by script: of the 3,869 byte writes at offset 0 of a pointer in their SHCPEP and SHCASM, 295 store arithmetic that can give 0x57, and 204 couldn't be sorted. A store through a pointer nobody typed could be a record's;
  - each stage reads its records from a file the stage before wrote, and SHCASM reads back a stream of its own. That framing wasn't read;
  - the op arguments that aren't immediates, in every release but R10;
  - the encoder's field functions, in every release but R10;
  - two or more bits of `-extra` together, a bit of `frt`, the one of [its nine keys](#the-fschg-compiles) not tried, a compile whose allocation fails ([Evidence](#evidence)), and 6.0 AD's front end, which names one builtin 5.1 R13's doesn't, `trace`.
- **Refuted by** any release writing `CMP/STR` for C, in either mode, outside inline text. The rule reads no operand and no line around the instruction, so it has no narrower form to fall back on. In the compilers, a writer would be a store of 0x57, or of a value that can be 0x57, into a record's first byte: an op argument of 0x57 at a builder's call, a template row whose op word is 0x0057, or arithmetic on an op, as `op + 1` on `CMP/PL`'s or the assignment of a 4-bit field through a pointer to a record. In the encoder, a form row other than 51 that can form the word would refute it too: a ninth row whose base word has no bit outside H'2FFC, or a field function that sets bit 13 for `MOV.B @(R0,Rm),Rn`.

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

#### fipr-unloaded

- **Read.** R10: SHCGEN's templates and their interpreter, and in SHCPEP the four passes that delete loads and stores, the operand compare they share, and each other function that tests for an `FMOV`. In each release from R26 to 5.1 R13: the template's rows, and the same tests of the four passes, found by the passes' debug strings and matched by shape. Of 5.1 R13 the operand compare and the head and scan of one pass were read too. 6.0 AD's `shcgen.exe`, `shcpep.exe`, `shcasm.exe` and `shcmdl.exe` are 5.1 R13's files, and its `shcfrt.exe` wasn't read. No release's `shcfrt` or `shcmdl` was read: what they do with a call of `_builtin_fipr` before SHCGEN sees it rests on the compiles.
- **One template.** In each of the twelve releases, one row of SHCGEN's named templates holds `FIPR`'s op, 0xd6: in R10's `ainlin033` (`0x43f7d0`), among 538 templates of 2,723 rows, and in the others' `ainlin030`, among 597 to 617 templates of 3,077 to 3,253 rows. Each `shcgen.exe` holds the word `d6 00 00 00` once in its data, in that row, and no instruction with the immediate 0xd6, and `shcpep.exe` and `shcasm.exe` only compare with it. A scan of every byte 0xd6 whose twelve-byte grid leads to a terminator row, in a named template or not, finds that row alone. So no other builtin's template ends in a `FIPR`.
- **Its loads.** R10's template is two set-up rows, `FSCHG`, four rows that load DR4, DR6, DR0 and DR2 through an operand's register with post-increment, the `FIPR FV4,FV0` row and `FSCHG`. From R26 on it is two set-up rows, eight rows that load FR4 to FR7 and FR0 to FR3 the same way, a command row and the `FIPR` row, the same bytes in all eleven releases but one command number. R10's interpreter (`0x4112e0`) walks a template twelve bytes a row up to its terminator, and skips a row only on the conditions in the row's third and fourth bytes (`0x411324`, `0x412714`), which are zero in every row of this template. `_builtin_fipr` is builtin code 35, which names this one descriptor. The later releases' rows have the same zero bytes, and their interpreters weren't read.
- **Operands that change their register.** SHCPEP gives each operand a kind: 1 is a register, 2 is `@Rn`, 8 to 12 are the other memory operands, and 3 and 4 are the two that change their register, `@Rn+` and `@-Rn` (`0x4088c0`, `0x408998` to `0x4089e1`). Its operand compare (`0x409d80`) never matches a kind 3 or 4: it leaves at once for either, and answers equal only for two null operands (`0x409dc1` to `0x409dd0`).
- **The passes that take loads.** Four passes of SHCPEP delete a load or a store, or make a load a register move. None can take a template's load, which goes through `@Rn+`:
  - `delload` (`0x40c210`) deletes a load to a register when a later move to that register comes before any use. Its candidate must have a source of a kind other than 3 and 4 (`0x40c2aa` to `0x40c2b6`). `FIPR` is also on the list of ops whose destination is read as well as written (`0x405ec0`, `0x41050a`), and SHCPEP's mask table gives FV0 the bits of FR0 to FR3 and FV4 those of FR4 to FR7, in all twelve releases, so each template load has a use at the `FIPR`.
  - `delmemld` (`0x40c540`) takes a first load whose source is of kind 2 or 8 to 12 (`0x40c5db` to `0x40c5fb`), and a second whose source the compare matches (`0x40c7d3`).
  - `delstld` (`0x40c970`) takes a store to a destination of a kind other than 3 and 4 (`0x40ca24` to `0x40ca33`), and a later load whose source the compare matches (`0x40cbf8`).
  - `delstore` (`0x40cdc0`) skips kinds 3 and 4 (`0x40ce3b`).

  R10 has a second guard: its pair loads are eight-byte nodes, and each of the four passes stops its scan at one (`0x40c306`, `0x40c675`, `0x40caad`, `0x40ceb6`). The later releases load four-byte singles and have no such stop.
- **The other releases.** Each of the twelve keeps those tests: `delload` leaves a candidate of kind 3 or 4, `delmemld` takes a first load of kind 2 or 8 to 12 alone, `delstld` leaves a store to kind 3 or 4, `delstore` skips both, and the compare answers not equal for both. 5.1 R13's compare (`0x40bf90`, `0x40bfd3` to `0x40bfdf`) and its `delload` (`0x4137f9` to `0x413954`) were read. The others are matched by shape.
- **Other functions that change an `FMOV`.** In R10's SHCPEP four more functions test for an `FMOV` and change nodes:
  - one folds an `ADD` into the address of a load through `@Rn` or `@(0,Rn)` (`0x4114f0`), which a template's load isn't;
  - one makes a second load of one constant a register move (`0x4117f0`), and its scan stops at a `FIPR`;
  - one gives a load the destination of a register move of what it loaded, and deletes the move (`0x411d70`): a template register's first use is the `FIPR`, which is no move;
  - one makes `@Rn+` and `@-Rn` out of an `ADD` beside a load or a store through `@Rn` (`0x4129b0`): it makes kinds 3 and 4, and doesn't take them.

  R32 has the same four by their compares, which weren't read, and two more that make a push and a pop out of frame moves.
- **The scheduler.** SHCASM's scheduler knows the loads feed the `FIPR`: a dump of its dependence list, by 5.1 R11 or R13, names the eight loads as the `FIPR`'s parents. From R26 on it [gives up at cycle 200](#return-above-entry) all the same and puts what it hasn't issued at the top of its window, which can leave a `FIPR` above its own loads ([what has no rule](#what-has-no-rule)). A compare of a counter with 200 stands in the SHCASM of each release from R26 on, and in none of R10's; what it guards wasn't read again here. A window ends at each label, branch and return and holds 32 instructions at most ([pr-save-below-branch](#pr-save-below-branch)), and a function starts at a label. So the give-up reorders the instructions of one function, and a count over the function doesn't see it.
- **Compiles.** In [the FIPR compiles](#the-fipr-compiles):
  - A store right above the load stays a store and a load. `float p_one(float *p, float *b, float x) { p[0] = x; return _builtin_fipr(p, b); }` under R26 is `FMOV.S FR4,@R4`, then `FMOV.S @R4+,FR0`. Four stores through `*--p` and the builtin on `p`, under 5.1 R13, are four `FMOV.S FRn,@-R1` and then `FMOV.S @R1+,FR0` to FR3: a push and a pop through one register, both kept.
  - A vector built in registers goes through the frame. Two differences and a zero, as the arrays of `_builtin_fipr(u, v)`, are stored to a 32-byte frame and loaded back, in every release, and so are float arguments and a struct.
  - After `FTRV` the result is stored and loaded again. For `float t_loc(float *a) { float v[4]; _builtin_ftrv(a, v); return _builtin_fipr(v, v); }` 5.1 R13 stores FR3 to FR0 through `@-R3` and at once loads FR0 to FR7 through `@R2+` and `@R1+`.
  - A result C drops keeps its loads and its `FIPR`, and a call C can't reach loses both.
  - Two arms that open with the same call keep two templates whole. Two that end alike keep each arm's loads and share one `FIPR` under a label ([FIPR's loads](#fiprs-loads)).
  - A literal pool can part a template, with a `BRA` and a label before the `FIPR`, and its loads stay in the function.
  - R10 writes its template alike under each single bit of `-extra=a` from 1 to 8000 but 8, `a=80` and `a=40` among them: `FSCHG`, four `FMOV @Rn+,DRm`, `FIPR FV4,FV0` and `FSCHG`.
  - No template load loses its post-increment: in the 90,260 functions with a `FIPR` of the probes, and in the 68,123 of the second round's `-code=asmcode` texts, the loads through `@Rm+` alone are at least as many as the `FIPR`s, for each of FR0 to FR7.
- **Sweeps.** inspect, without `--sz`, read each function that holds a `FIPR` in the 24,318 texts listed under [the FIPR compiles](#the-fipr-compiles): 428,643 functions, of which 171,433 are compiled functions with a `FIPR FV4,FV0`, 1,270,426 such `FIPR`s in all. None of them shows the witness. It shows in 40 functions of the retained compiles, texts of R10, R32 and 5.1 R11, at 112 `FIPR`s, each of them inline text: hand-written `#pragma inline_asm` bodies that those compiles copy.
- **Kept as assembly.** Fourteen functions that DreamComp, the decompilation shckit grew out of, keeps as assembly show the witness:
  - Ten are C functions of the Katana SDK's Ninja library, 1.00J and 1.20J, each with a `#pragma inline_asm` body expanded in it, and each read in R10's own `-code=asmcode` text. No link stands between the compiler and that text.
  - Four are functions of Sonic Adventure and Sonic Adventure 2, one of the second and three of the first, in texts written from the images' bytes. So the claim needs a reason that no [`same_code` link](#the-inter-module-optimizer) made them, and what there is has weak points:
    - Sonic Adventure's `IP.BIN` dates its disc 1998-12-10: before 5.1 R03, the first release here that writes a supplementary file, whose `shc.exe` was linked on 1999-02-08, and before R08's `optlnksh.exe` and `optsh.exe`, linked on 1999-06-29. That covers its three and not Sonic Adventure 2's one, whose disc is dated 2001-05-29.
    - Each of the four ends in `RTS` with `FMOV.S FR3,FR0` in its slot, where each of the seven routines the optimizer made ends in `RTS` and `NOP`. That rests on seven routines, not on a reading.
    - Sonic Adventure 2's function and one of Sonic Adventure's are each called twice in their `1ST_READ.BIN`, by `JSR` through a pool word, with no `STS.L PR,@-R15` right above the call and no `LDS.L @R15+,PR` right below its slot: each caller pushes PR at its own entry, 9 or 28 instructions above the call. The other two have no pool word in `1ST_READ.BIN`, and no other file was read for a call, so their case is thinner.
    - DreamComp rebuilds both images' C functions from the compiler's direct objects, with no optimizer in its link, and a function the optimizer had taken a block from would not match.
    - A scan of an image can't settle it: the four words of the optimizer's call stand 6 times in Sonic Adventure 2's `1ST_READ.BIN` and 5 times in Sonic Adventure's, and the two that were read are ordinary compiled functions that open with a call.
- **Rests on** what wasn't read:
  - 29 of the 36 functions of R10's SHCPEP that delete a node. They were classed by the ops they compare, integer moves, constants, `ADD`, compares, shifts and extensions, labels and jumps, and not read one by one. A deleter that takes a node without testing its op would have to take a template's load and leave its `FIPR`: none was found.
  - From R26 on, every peephole beside the four passes. In R26, 35 functions delete a node and seven compare an op with `FMOV`'s, as seven of R10's 36 do; they were matched to R10's and not read.
  - Any release, for a pass that takes a dead post-increment off a load. The last load through each pointer increments a register nothing reads again, and a load through `@Rn` is one the four passes can take. Every template load in the compiles keeps its increment.
  - SHCASM, for whether it deletes an instruction it was handed.
  - From R26 on, the interpreter: that it runs each row of the template once rests on the rows' zero condition bytes, on R10's interpreter, and on the 248,495 `FIPR`s those releases compiled in the probes with their loads beside them.
  - The scheduler's windows and a label at each function, which keep the give-up inside one function: both rest on what [pr-save-below-branch](#pr-save-below-branch) rests on. The scheduler wasn't read for this rule: a scan found its compare with 200.
  - `optsh.exe`, whose behaviour rests on [eleven probes](#the-inter-module-optimizer).
- **Refuted by** any release compiling C, in either mode, outside inline text, to a function that holds `FIPR FV4,FV0` and no load of one of FR0 to FR7. That takes a second writer of the op, a run of the template that skips a load, a pass that deletes a load through `@Rn+` or makes it a register move, a pass that takes the increment off such a load, which the four passes can then take, or a function whose text SHC parts between the loads and the `FIPR`. A release whose template fills a vector otherwise, by a register move or by `FLDI0`, refutes it too. The two ways found from C to the witness lie outside the claim: lines that hold [a part of a function](#fiprs-loads), and a module [the inter-module optimizer](#the-inter-module-optimizer) rewrote.

#### fipr-shared

- **Read.** What [fipr-unloaded](#fipr-unloaded) rests on, and in R10's SHCPEP the common-code pass and each walk that chooses a node for a delay slot. Of R26 and 5.1 R13, at the places named below: the routine that puts a node in a conditional branch's slot, and the pass over conditional branches that calls it. By script, in the same two: which functions call the node copier, and which of them test for a `FIPR`. For the other releases it is inference from the compiles.
- **Merging takes tails.** In R10, `d_comcd` (`0x413b20`) merges code that two blocks share. Its compare, `dc_cmp` (`0x4141a0`), takes each block's last node (`0x414eb0`), needs the first block to end in a jump (`0x4141f1` to `0x4141fa`) and the second to end in one too or to fall through (`0x414236` to `0x414267`), and walks both blocks backward. So a merged stretch is a common tail. A template's `FIPR` stands below its loads, so a tail that takes a load takes the `FIPR` too, and the function keeps at least as many loads of each register as `FIPR`s.
- **A conditional branch's slot takes a node by a move.** In R10 the walks that choose a node for a slot give none at a `FIPR`: the slot pass's (`0x416c0c`, `0x416c9a`, `0x41a04e`) and those of the pass over conditional branches (`0x417cba`, `0x417dbd`, `0x4180c3`). From R26 on a `FIPR` does go into a slot. The routine that puts a node in a conditional branch's slot copies the branch to a new slot, copies the chosen node over the branch's old slot and deletes the node where it stood, in R10 (`0x417f1b`, `0x417f25`, `0x417f2e`), in R26 (`0x41756b`, `0x417575`, `0x41757e`) and in 5.1 R13 (`0x41124f`, `0x411256`, `0x41125c`): a move, which leaves the count as it was. The pass over conditional branches reaches a slot only through it, and is its one caller in each of the three (R10 `0x417e59`, R26 `0x4174b3`, 5.1 R13 `0x41119b`). R26's pass (`0x417080`) keeps five of R10's tests of a node's op, for 0x11, 0x23, 0x95, 0x93 and 0x99 (`0x417401` to `0x417421`), and no longer has the tests that follow them in R10's, of an eight-byte node and of ten ops, `FIPR`'s among them (R10 `0x417c9b` to `0x417cf4`). It still tests the label record's count against 1 as R10's does (`0x41725b`, `0x4172de`, `0x41735a`), and still ends in that routine. 5.1 R13's compares the same five ops as R26's (`0x4110f3` to `0x411113`). What fills any other branch's slot from R26 on wasn't read.
- **The node copier.** In each of R10, R26 and 5.1 R13, SHCPEP's node copier (`0x414a20` in R10) is called 27 times from nine functions: the routine above, three that their strings name, and five that no string names. In R10 three of them test for a `FIPR` among ten ops, and in R26 and R13 those three don't.
- **Where a copy would stand.** A second `FIPR` on one path would change the result, since `FIPR` reads FR3 and writes it. So a copy would have to stand on another path, below a branch that parts a template's loads from its `FIPR`. The pass over conditional branches moves a join's first node up into a slot, and merging takes nodes away: neither puts a branch there. That is inference, and no compile shows one.
- **Compiles.** In [the FIPR compiles](#the-fipr-compiles):
  - A vector that several products share is loaded for each. Three products with one vector of weights load the weights three times, and the same call twice loads sixteen registers ([shared loads](#shared-loads)).
  - Arms share a tail and never a head. Two arms that open with the same call keep two templates whole, in every release.
  - In the second round's texts of R10, R26, R32 and 5.1 R13, 32,447 labels stand right before a `FIPR`. A `BRA` names 32,437 of them, no branch names the other 10, and no conditional branch names one.
  - In the second round each release from R26 on puts a `FIPR` in the slot of `BT/S`, `BF/S`, `BRA`, `JSR` and `BSRF`, and R26, R27 and R28 in the slot of `RTS`, `JMP` and `BRAF` too: 36,511 times after `BT/S`, 29,291 after `BF/S`, 59,106 after `BRA`, 495 after `JSR`, 201 after `RTS`, 120 after `JMP`, 22 after `BSRF` and 6 after `BRAF` in all, and never after `BSR`. R10 puts none in a slot. In 33,863 of that round's 68,123 functions of `-code=asmcode` text, some register has exactly as many loads as the function has `FIPR`s, where one copy would show.
  - In the probes' `-code=asmcode` texts, no delayed branch has in its slot a copy of its target's first instruction.
  - Under `-lang=cpp`, 5.1 R13 takes the builtin declared `extern "C"`, in an `inline` function too, and its two functions with four `FIPR`s show nothing.
- **Sweeps.** In the texts [fipr-unloaded](#fipr-unloaded) was read in, none of the 171,433 compiled functions with a `FIPR FV4,FV0` holds fewer loads of a register than such `FIPR`s. The witness shows in 41 functions of the retained compiles, at 86 `FIPR`s, each of them inline text, and 31 of those functions show `fipr-unloaded` too.
- **Kept as assembly.** A fifteenth function that DreamComp keeps as assembly shows this rule and not [fipr-unloaded](#fipr-unloaded): `cubic_float` of the Ninja library's 1.20J `njMotLinkSrc`, read in R10's own text. It holds three `FIPR FV4,FV0`, from a `#pragma inline_asm` body, and one load each of FR5, FR6 and FR7, from its C. The ten Ninja functions that show `fipr-unloaded` show this rule too.
- **The inter-module optimizer** makes this witness from C as it makes `fipr-unloaded`'s: a routine with two `FIPR`s and one load of each register ([evidence](#the-inter-module-optimizer)).
- **Rests on** what [fipr-unloaded](#fipr-unloaded) rests on, and on more that wasn't read:
  - From R26 on, the functions that call the node copier, beside the one that fills a conditional branch's slot: what they copy, where three of them no longer test for a `FIPR`.
  - From R26 on, what fills the slot of a branch that isn't conditional. 59,950 of the second round's `FIPR`s stand in the slot of `BRA`, `JSR`, `RTS`, `JMP`, `BSRF` or `BRAF`. R10's slot pass gives none at a `FIPR` and doesn't call the routine above, and from R26 on SHCPEP compares an op with `FIPR`'s at two places only (R26 `0x4055c5`, `0x40fe54`; 5.1 R13 `0x40e178`, `0x418617`). Whether those releases move a node into such a slot or copy it wasn't read: that no copy stands in one rests on the compiles.
  - R26's and R13's pass over conditional branches and their slot routine, line by line, and every other release's from R26 on. R26, R27 and R28 put a `FIPR` in the slot of an `RTS` where no other release does, so those three treat the node more freely than R10's reading shows.
  - R10's common-code pass beyond its compare, and every other release's.
- **Refuted by** any release compiling C, in either mode, outside inline text, to a function that holds more `FIPR FV4,FV0` than loads of one of FR0 to FR7. Beside what refutes [fipr-unloaded](#fipr-unloaded), that takes a pass that copies a `FIPR` without its loads, as into a conditional branch's slot, or one that merges the loads of two templates and leaves both `FIPR`s, as a merge of common heads would.

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

#### fschg-slot

- **Read.** R10: each place in SHCPEP and SHCASM that marks a record for a slot, SHCASM's writer of slots, and each instruction of its stages that holds `FSCHG`'s op. Of each release from R26 to 5.1 R13, and of 6.0 AD, SHCASM's form rows alone: none of the 182 is `FSCHG`'s, as R10's row at `0x426fc4` is, and FSCA's is the one row whose fields could form its word, with an odd register number only ([fschg-pairs](#fschg-pairs)). No stage of theirs holds the text FSCHG either, so a text of theirs holds an `FSCHG` only as inline text.
- **Slots.** In R10 an instruction goes in a slot only where a stage marked its record for one, by bit 0x40 of the record's second byte. SHCASM's dispatcher sets a marked record aside for the next delayed branch (`0x414bad` to `0x414bde`), and gives a branch with nothing set aside a `NOP` (`0x414c44` to `0x414c4d`). The loop that writes the records (`0x414140`) writes the one set aside after each delayed branch, and has no other source for a slot. The scheduler collects its windows through the same dispatcher, so it never sees a marked record.
- **SHCPEP.** It sets the bit at two places, and by no other instruction that names it.
  - `0x41743c` marks the candidate of the slot pass, a node one test read first (`0x41a030`). The test refuses a node whose op is `FSCHG`'s, 0xef, as it refuses `FRCHG`'s, `FIPR`'s, `FTRV`'s and six more. The candidates come from a walk up from the branch (`0x416b60`), which returns none at the same ops. So each instruction between a marked node and its branch was a candidate too, and none is an `FSCHG`: a node SHCPEP marks doesn't cross one.
  - `0x417f33`, in a pass over conditional branches, marks a copy of the node another walk returned (`0x417f60`), and that walk returns none at op 0xef (`0x4180bf`).
- **SHCASM.** It sets the bit at eight places, on records it builds or expands, and it builds no `FSCHG`: its one instruction with 0xef as an immediate is a compare in the scheduler (`0x40907a`).
  - The exit builder marks a record of the exit (`0x40c197`).
  - Four builders of expansions (`0x40ce60`, `0x40e250`, `0x40e850`, `0x40ebe0`) mark the last record they build only where the record they were given was marked. The dispatcher calls them for ops 0x27, 0x28, 0x2a and 0x2c, SHCPEP's `MOV_LOC`, `MOVA_LC` and `MOVI` among them, and SHCASM's frame builders call one of them for records of their own. So they pass a mark on to the load a record expands into, and originate none.
  - Under bit 0x40 of `-extra=a`, the expansion of a float add or subtract (`0x40eeb0`) marks the pop of one of FR0 to FR3 and the `FADD` or `FSUB` itself (`0x40f2f4`, `0x40f42e`), and the expansion of a float multiply or divide (`0x40f800`) marks one record, compiled `MOV.L @R15+,R0` (`0x40ffd8`).
- **SHCGEN.** No instruction of it sets the bit by an immediate. It pushes `FSCHG`'s op at two places of its template interpreter (`0x411bf8`, `0x411c25`), and SHCPEP holds the op only in compares and masks. In the 116 `-code=asmcode` texts of the matrices at `-optimize=0`, where SHCPEP doesn't run, every slot is a `NOP` but under the values of `-extra=a=` that set bit 0x40: in the other 92 texts, 4,830 after `RTS`, 1,198 after `BRA`, 322 after `JSR`, 92 after `RTE` and 101 after `BT/S` and `BF/S`.
- **The drift.** At `-optimize=0`, with bit 0x40 of `-extra=a` set and bit 1 clear, a marked record can land in a later branch's slot than its own, across an `FSCHG` too. For `void b_if9(M a, M b, M c, int k) { if (k) _builtin_mtrx4muladd(a, b, c); else _builtin_mtrx4mulsub(a, b, c); }`, with `M` a `float[4][4]`, R10 under `-optimize=0 -extra=a=40` writes a run that holds the `BRA` over a literal pool, with the `FADD` of the expansion above the run in its slot:

  ```
            ADD         #32,R2
            FSCHG
            FMOV        DR2,@-R2
            FMOV        DR0,@-R2
            FMOV        @R1+,DR0
            BRA         L573
            FADD        FR7,FR3
  L573:
            FMOV        @R1+,DR2
  ```

  FR3 is stored before it is summed, so the text is a miscompile, in both modes. What drifts is a marked record, which at `-optimize=0` is one of the expansions': an `FADD`, an `FSUB`, or a pop of R0 or of FR0 to FR3. The dispatcher tests bit 1 where one delayed branch follows another with only marked records between (`0x414c54` to `0x414c74`); that this path makes the drift is inference from the compiles. A probe of ten functions has 19 of its 35 expanded adds and subtracts in the slot of a `BF/S` under `-extra=a=` 40, 42, 44, 48, 140, 240 and 440 at `-optimize=0`, and each in the slot of a `BRA`, as the expansion writes it, under `-extra=a=41` there and under 40, 41 and c1 at `-optimize=1`.
- **Probes.** 29 functions under nine sets of options put an `FSCHG` right above a call and a tail call through a pointer, a near `BSR`, the `BRA` of an endless loop and a `goto`, and at the top of loops that begin with one. Each slot takes another instruction or a `NOP`. `void a10(M m, int n) { do { _builtin_ld_ext(m); m = *gpm; } while (--n); }` compiles to a loop whose top is the opening `FSCHG`, and its `BF/S L400` takes the load below the branch, `MOV.L @R6,R4`, into its slot. `void a1(M m, void (*f)(void)) { _builtin_st_ext(m); f(); }` ends `FSCHG`, `MOV.L @R15,R3`, `JMP @R3`, `ADD #4,R15`.
- **Near misses.** In 40 of the random texts, of 1,080 functions and 78,906 `FSCHG`s, an `FSCHG` stands right above a `BRA` 1,225 times and right above a `JSR` 18 times, and 2,686 branches go to a label whose first instruction is an `FSCHG`. Of 10,464 slots of `BRA`, 3,403 hold an instruction other than `NOP`, and of 3,465 slots of `BT/S` and `BF/S`, all but 7. None holds an `FSCHG`.
- **Inline text.** R10 fills the slot of each branch it writes, so an `FSCHG` it compiled could stand in a slot only below inline text that ends in a branch, and only where the code below began with one. For `void w_slot(M m, int n) { if (n) back(); _builtin_ld_ext(m); }`, with `back` a `#pragma inline_asm` function of `RTS`, R10 writes `.ALIGN 4` below the inline text and reloads `m` above the run, so the rule places no slot, and in the text drop-inline-align makes of it the slot takes the load. For `void pc_slot(M m)`, whose body is a `#pragma asm` block of `BRA PA_OUT`, `_builtin_ld_ext(m);` and a block of `PA_OUT:`, it reloads nothing: R10 under `-cpu=sh4 -optimize=1 -speed` writes `BRA PA_OUT`, `.ALIGN 4` and the run's first `FSCHG`. In the text drop-inline-align makes of it that `FSCHG` is a witness, which stands on the inline `BRA`.
- **Rests on** what wasn't read: the path from the dispatcher's test of bit 1 to the drift; and SHCGEN's default path for a template row, by which a row that names `FSCHG`'s op becomes its record.
- **Refuted by** any release writing an `FSCHG` in a delay slot for C, in either mode, outside inline text. In R10 that takes a place that marks an `FSCHG`'s record for a slot.

#### fschg-pairs

- **Read.** R10, the one release known to write `FSCHG`. No stage of any release from R26 to 5.1 R13 holds the text FSCHG, and in the family compiles none writes it. R32 has no form for it either: no row of its 182 is FSCHG's, as R10's row 187 is, and `_builtin_fschg()` compiles to a call to an external function of that name. Take each register field as the register's number, 0 to 15, at its shift, and each displacement or immediate field as any twelve bits at its own. Then only FSCA's row, `F0FD` with DRn's number at bit 8, could form FSCHG's word, `F3FD`, and only with an odd number there, which a DRn never has.
- **Writers.** R10 writes `FSCHG` only in the templates SHCGEN expands for its vector builtins, such as `_builtin_ld_ext`, `_builtin_st_ext`, `_builtin_ftrv` and `_builtin_fipr`. SHCASM forms FSCHG's word from one form row, for one internal op, and only template rows write that op. Each template a builtin reaches writes an even number of them, so its runs open at an odd count and close at an even one. `_builtin_fschg()` compiles to a call, in both modes, under every profile and option tried and inside a `#pragma inline` function. SHCGEN's one template with a single `FSCHG` is on no builtin's list.
- **Pairs.** Inside the runs R10 moves only the templates' pairs, DR0 to DR6 and XD0 to XD14, and for four builtins integer registers that set a pointer up ([fschg-dt](#fschg-dt)). SHCASM forms a paired `FMOV` only for an 8-byte record. SHCGEN gives an `FMOV` record that size only for a DR or XD operand, which it takes only from its table of fixed registers, as template rows and constants name them. Nothing names DR8 to DR14, and the table holds no FR8 to FR15. SHCASM saves and restores FR12 to FR15 one at a time, and FR8 and FR10 singly too, only in an interrupt function's frame. SHCPEP builds no pair, and SHCASM's scheduler keeps each instruction on its side of an `FSCHG`. A record marked for a slot can cross one all the same, at `-optimize=0` under `-extra=a=40`, and it is no move of these pairs ([fschg-slot](#fschg-slot)).
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
- **Rests on** SHCPEP renaming no pair's register. Each pass read tests for a pair and an `FSCHG` before it changes a node, but not every pass was read, nor the passes that share code between branches. How shcfrt and shcmdl lower `_builtin_fschg` wasn't read; the compiles show a call. The option under which R10's templates part their stores into single moves is `-extra=a=80`, and under it the singles stand at even counts ([fschg-stores](#fschg-stores)).
- **Refuted by** any release writing an `FMOV` between memory and FR8, FR10, FR12, FR14 or DR8 to DR14 after an odd number of `FSCHG`s on every path from the entry, for any C source, outside inline text.

#### fschg-dt

- **Read.** R10: where its `FSCHG`s come from, what SHCGEN's templates write after an odd number of them, SHCPEP's one writer of `DT`, and what marks an instruction for a slot ([fschg-slot](#fschg-slot)). No release from R26 to 5.1 R13, nor 6.0 AD, has a form for `FSCHG`, so in a text of theirs the witness holds inline text.
- **Runs.** R10's `FSCHG`s come from eleven templates alone, those of `_builtin_fipr`, `_builtin_ftrv`, `_builtin_ld_ext`, `_builtin_st_ext`, `_builtin_ftrvadd`, `_builtin_ftrvsub`, `_builtin_add4`, `_builtin_sub4`, `_builtin_mtrx4mul`, `_builtin_mtrx4muladd` and `_builtin_mtrx4mulsub`. Of SHCGEN's 538 named templates, twelve hold a row that writes one. Each of the eleven ends at an even count, with the option bit that parts their stores clear or set, so a run opens and closes inside one builtin. The twelfth is one bare `FSCHG`, and no C is known to reach it: the table of template names alone names its descriptor, no four bytes of any section of `shcgen.exe` hold an address inside that table, and SHCGEN gives a call a builtin's code by its name (`0x41a270` to `0x41a321`), from 47 names with no `fschg` among them. So `_builtin_fschg()` is a call, as each release from R10 to 5.1 R13 compiles it.
- **Rows.** After an odd number of `FSCHG`s the eleven templates hold: a load of a fixed pair, DR0 to DR6 or XD0 to XD14, from an operand, 70 rows; a store of DR0 or DR2, 34, or of an XD pair, 8; `FIPR FV4,FV0`, 1, and `FTRV XMTRX,FV0`, 7; an `ADD` of 16 or 32 to an operand's register, 5; and the set-up of an operand, 4. With the option bit set the stores stand outside the runs. No row of any template is a `DT`, and none of these is a compare, a jump or a label.
- **The set-up.** The templates of `_builtin_ftrvadd`, `_builtin_ftrvsub`, `_builtin_add4` and `_builtin_sub4` move their second pointer into a register inside a run. The row runs one of 40 descriptors through the interpreter again (`0x4122f7` to `0x4125ef`), whose rows are loads, moves, `ADD`s and float moves, and no compare, jump, label, call or `FSCHG`. Compiled, the set-up is `MOV.L @(d,R15),Rn`, `MOV.L @(R0,Rm),Rn`, `MOV.L @Rm,Rn`, `MOV Rm,Rn`, `MOV #imm,Rn`, a `MOV.L` or `MOV.W` of a pool word, or `ADD`. An operand that needs a sum, a call, a conditional or a step of a counter is computed above the run's first `FSCHG`: for `_builtin_ftrvadd(a, b[--n], c)` R10 writes `ADD #-1,R14`, the index and its load, and then the run.
- **`DT`'s writer.** SHCPEP stores `DT`'s op in a node at one place (`0x404273`), and no instruction of SHCGEN or SHCASM stores it in one. It makes the `DT` of a compare, `CMP/EQ #0,Rn`, `CMP/PL Rn` or `TST Rn,Rn`, in place, and only where the node right above the compare is `ADD #-1` of its register (`0x4040f0` to `0x404265`). Where that node is an `FSCHG` it returns (`0x40412d`). So a `DT` stands where a compare stood, and a compare is C's own, written outside a builtin's runs.
- **Passes.** SHCPEP compares a node's op with `FSCHG`'s at 41 places. At 39 of them the op stands in one list of ten that a pass leaves alone, and a match goes where a node the pass may not touch goes: 19 return from their function at once, and 20 jump past the change. Six passes hold no such compare, `br_brchg`, `d_comcd`, `dc_cmp`, `dc_cpycd`, `sw_delete` and `d_condbr`, its branch and common-code passes, and they weren't read. The compiles show what one of them does: it joins the tails of two arms that end in runs, so that a `BRA`, its `NOP` and a label stand in a run ([what has no rule](#what-has-no-rule)). A joined tail is the same instructions on both arms, and an `FSCHG` in it opens a run on both or closes one on both, so a line below the join keeps the count it had on each arm. That is inference.
- **Without optimization** SHCPEP doesn't run, and R10 writes no `DT`: the 232 `-optimize=0` texts of the matrices, in both modes, hold none. A marked record [drifts](#fschg-slot) only there, so no `DT` drifts into a run.
- **Probes.** Beside [the FSCHG compiles](#the-fschg-compiles):
  - 15 functions under nine sets of options give two arms the same first statements inside a run, to see whether a pass lifts a common `FSCHG` above the compare and its branch. R10 joins tails and leaves heads: each arm keeps its own `FSCHG`.
  - 12 functions under seven sets nest one builtin in another's operand and put one in a loop's test. The inner builtin's runs stand above the outer one's: `_builtin_add4(a, (_builtin_ld_ext(m), b), c)` compiles to the load's run, closed, and then the add's.
  - `void tog3(float *d, float *s, int n) { _builtin_fschg(); do { *d++ = *s++; } while (--n); _builtin_fschg(); }` compiles to an ordinary loop, `FMOV.S @R5+,FR3`, `DT R6`, `FMOV.S FR3,@R4` and `BF/S`, between a `JSR` and a `JMP`: it holds no `FSCHG`.
- **Near misses.** In the 40 random texts [fschg-slot](#fschg-slot) counts, a `DT` stands right below an `FSCHG` 756 times, and a compare 1,285 times.
- **Inline text.** For `void w_dt(float *d, float *s) { sz_on(); do { *d++ = *s++; } while (--gk); sz_off(); }`, with `sz_on` and `sz_off` `#pragma inline_asm` functions of one `FSCHG` and `gk` a global in R14, R10 under `-optimize=1 -speed` writes its own `DT R14` between the two inline `FSCHG`s. In its text `.ALIGN 4` stands below each of them and stops the count. In the text drop-inline-align makes of it the `DT` is a witness, which stands on the inline `FSCHG`s. So is `fschg-pairs`' there for `*--d = gf12`, with `gf12` a global in FR12: R10's own `FMOV.S FR12,@(R0,R2)` between two inline `FSCHG`s.
- **Rests on** what wasn't read: the six passes; which descriptor the set-up takes for which class of operand, and one command of the descriptors' rows; SHCGEN's default path for a template row, by which a row that names `FSCHG`'s op becomes its record; what shcmdl does with the builtins' names, whose one function over them answers whether a name is one of them and whose callers weren't read; and an address of the bare `FSCHG`'s descriptor computed from another base, which no scan shows.
- **Refuted by** any release writing a `DT` after an odd number of `FSCHG`s on every path from the entry, for C, in either mode, outside inline text. In R10 that takes a compare inside a template's run, or a pass that carries a `DT` or its compare across an `FSCHG`. C that reaches the bare `FSCHG`'s template refutes this rule and [fschg-pairs](#fschg-pairs) with it.

#### fschg-stores

- **Read.** R10's runs and the rows of their templates, as [fschg-dt](#fschg-dt) gives them, and what marks an instruction for a slot ([fschg-slot](#fschg-slot)). No release from R26 to 5.1 R13, nor 6.0 AD, has a form for `FSCHG`, so in a text of theirs the witness holds inline text.
- **Stores.** After an odd number of `FSCHG`s, R10's templates store through two kinds of row: of DR0 or DR2, 34 rows (`0x411ae7`, writer `0x427850`), and of an XD pair, 8 rows (`0x411b5e`, writer `0x427940`). They name DR4 and DR6 only as the destinations of loads, 13 rows each: the second vector of `_builtin_fipr`, `_builtin_ftrvadd`, `_builtin_add4` and the matrix products. `_builtin_st_ext` stores XD4 and XD6, whose fields are 5 and 7.
- **`-extra=a=80`.** Bit 0x80 of the option parts each of those stores into two single moves (`0x427896` to `0x427930`) and moves the templates' `FSCHG`s. Compiled under it, `_builtin_st_ext` is `FRCHG`, sixteen `FMOV.S` of FR15 down to FR0 and `FRCHG`, with no `FSCHG`, and `_builtin_ftrv` closes its run and then stores FR3 down to FR0 singly. The singles stand at even counts.
- **The set-up.** Of the 40 descriptors the set-up of a pointer can run ([fschg-dt](#fschg-dt)), those that move a float hold a float store. They would serve an operand whose address is taken while it lives in a register, and C hands the set-up none:
  - The builtins' operands are pointers, by the prototypes the front end declares. A cast of `_builtin_add4` and a pointer to it are error 2020, a new declaration of it is 2118, a float argument is 2201, and `&y` of a `register float y` is 2101.
  - R10 keeps a variable whose address is taken in memory from the entry. For `void s1(float *a, float x, float *c) { _builtin_add4(a, &x, c); }` it writes `FMOV.S FR4,@R3` above the first `FSCHG`, and the run holds the templates' loads alone.
  - 19 functions at `-optimize=0` and 1 pass `_builtin_add4`, `_builtin_sub4`, `_builtin_ftrvadd` and `_builtin_ftrvsub` the address of a float parameter in FR4, FR6 and FR8, of a local, of a struct's member, of a struct a call returns and of a `double`. None stores a float in a run.
  - 25 functions under six sets of options give the pointer as a `gbr_base`, `abs16`, `volatile` or `global_register` variable, as a member of a member, in a 160,000-byte frame, as a constant, a string and a function's address, and in interrupt functions. Their runs hold `MOV`, `MOV.L`, `MOV.W`, `MOV #imm` and `ADD` beside the templates' rows.
- **Carried.** A marked record that [drifts](#fschg-slot) into a run at `-optimize=0` is an `FADD`, an `FSUB`, or a pop of R0 or of FR0 to FR3, and no store. With optimization on, a node SHCPEP marks doesn't cross an `FSCHG`.
- **C's stores.** `*--p = x` beside a builtin stands above or below its run: `FMOV.S FR4,@-R5`, `FSCHG`, the pairs, `FSCHG`. A store in the second pointer's own expression, `_builtin_ftrvadd(a, (*d = x, b), c)`, stands above the first `FSCHG`, and the run holds the pointer's load, `MOV.L @(4,R15),R2`. In the 40 random texts [fschg-slot](#fschg-slot) counts, a store of FR4 or FR6 stands right beside an `FSCHG` 10 times, each outside the run.
- **Inline text.** In a text drop-inline-align has patched, a store R10 compiled from C shows the witness where the `FSCHG` above it is inline text, as [fschg-dt](#fschg-dt)'s `DT` does. For `void pb_store(float *d, float a, float b, float c) { *d = a; d[1] = c; }`, with a `#pragma asm` block of one `FSCHG` above the two stores and one below them, R10 under `-cpu=sh4 -optimize=1 -speed` writes its own `FMOV.S FR4,@R4` and `FMOV.S FR6,@(R0,R4)` between the two inline `FSCHG`s. In its text `.ALIGN 4` stands below each of them and stops the count. In the text drop-inline-align makes of it both stores are witnesses, which stand on the inline `FSCHG`s. Between two `#pragma inline_asm` functions of one `FSCHG` no compile shows one: for `float w_store(float *d, float a, float b, float c, float e)`, which stores a product of its arguments there, R10 keeps the arguments in memory and in FR13 and FR15 across the inline text, and stores the product from FR2.
- **Rests on** what wasn't read: which descriptor the set-up takes for which class of operand; and whether SHCPEP renames a template's DR0 or DR2. The templates load DR0 and DR2 from memory and compute in them in place, so no copy from FR4 to FR7 feeds a store in a run: that is an argument from the rows, not a reading of the pass.
- **Refuted by** any release writing an `FMOV` from FR4, FR6, DR4 or DR6 to memory after an odd number of `FSCHG`s on every path from the entry, for C, in either mode, outside inline text. In R10 that takes a store row that names DR4 or DR6, a float store in a pointer's set-up, SHCPEP renaming a template's DR0 or DR2, or a marked store carried into a run as the `FADD` is. C that reaches the bare `FSCHG`'s template refutes it too ([fschg-dt](#fschg-dt)).

#### fschg-register-moves

- **Read.** R10's runs and the rows of their templates, as [fschg-dt](#fschg-dt) gives them, and what marks an instruction for a slot ([fschg-slot](#fschg-slot)). No release from R26 to 5.1 R13, nor 6.0 AD, has a form for `FSCHG`, so in a text of theirs the witness holds inline text.
- **Rows.** No row after an odd number of `FSCHG`s has a register on both sides: R10's templates move a pair there between a register and an operand in memory. Of the 86 rows of `FMOV`'s op in SHCGEN's 538 templates, 70 load a fixed pair from an operand, 14 move between operands, and 2 store a fixed single, FR0 or FR1, in FSCA's template. None has a fixed register on both sides.
- **The set-up.** Of the 40 descriptors the set-up of a pointer can run, those that move a float hold a float move. C hands the set-up no float ([fschg-stores](#fschg-stores)), and in every compile it moves integer registers alone.
- **C's copies.** They stand outside the runs. `float r_fl(M m, float x) { _builtin_ld_ext(m); return x; }` ends `FSCHG`, `RTS`, `FMOV.S FR4,FR0`: the copy is in the slot, at an even count. `void m_5(M m) { float x = r12, y = r13; _builtin_st_ext(m); r12 = r14; r13 = r15f; r14 = x; r15f = y; _builtin_ld_ext(m); }`, with `r12` to `r15f` globals in FR12 to FR15, compiles to four copies between the `FSCHG` that closes one run and the `FSCHG` that opens the next. In the 40 random texts [fschg-slot](#fschg-slot) counts, a move between registers stands right below an `FSCHG` 699 times and right above one 19 times, each outside the run.
- **Carried.** A marked record that [drifts](#fschg-slot) into a run at `-optimize=0` is an `FADD`, an `FSUB`, or a pop of R0 or of FR0 to FR3. The expansion's own copies, such as `FMOV.S FR4,FR1`, are unmarked and stay in place, at even counts. With optimization on, a node SHCPEP marks doesn't cross an `FSCHG`.
- **Inline text.** For `void w_move(void) { sz_on(); gf12 = gf14; sz_off(); }`, with `sz_on` and `sz_off` `#pragma inline_asm` functions of one `FSCHG` and `gf12` and `gf14` globals in FR12 and FR14, R10 under `-optimize=1 -speed` writes its own `FMOV.S FR14,FR12` between the two inline `FSCHG`s. In its text `.ALIGN 4` stands below each of them and stops the count. In the text drop-inline-align makes of it the copy is a witness, which stands on the inline `FSCHG`s.
- **Rests on** what [fschg-stores](#fschg-stores) rests on: which descriptor the set-up takes for which class of operand, and whether SHCPEP renames a template's registers, weren't read.
- **Refuted by** any release writing an `FMOV` between two FPU registers after an odd number of `FSCHG`s on every path from the entry, for C, in either mode, outside inline text. In R10 that takes a template row with a register on both sides, a float move in a pointer's set-up, or a marked move carried into a run as the `FADD` is. C that reaches the bare `FSCHG`'s template refutes it too ([fschg-dt](#fschg-dt)).

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
| a 16-bit constant under `-extra=a=1000`, in 5.1 R01 to R13 | `MOV #imm,R0`, then `EXTU.B R0,R0`, or `SHLL2 R0`, or `SHLL8 R0` and after it nothing, one or two `ADD #imm,R0`, or `NOT R0,R0` |
| a constant from −128 to 127 | `MOV #imm,R0` |
| a pointer variable | its address, then `MOV.L @R0,R0` |
| a `#pragma gbr_base` pointer | `MOV.L @(d,GBR),R0` |
| a `gbr_base` variable's address | `STC GBR,R0`, then `ADD #d,R0` |

- **`-extra=a=1000`.** Under this option the 5.1 releases build a 16-bit constant from `MOV #imm` where 5.0 R10, R26 and R32 keep `MOV.W`. 5.1 R13 compiles `#pragma interrupt(h(sp=0x100))` over `void h(void) { g = a; }` under `-cpu=sh4 -extra=a=1000` to a head of `MOV.L R0,@-R15`, `MOV #1,R0`, `SHLL8 R0`, `MOV.L R15,@-R0` and `MOV R0,R15`. For each x from −32768 to 32767 outside −128 to 127, under the same options, R13 writes one of six loads after `MOV #imm,R0`: `SHLL8` and an `ADD` 64,706 times, `SHLL8` alone 255, `EXTU.B` 128, `SHLL8` and two `ADD`s 127, `SHLL2` 63, and `SHLL8` and `NOT` once, for H'7FFF. 5.1 R01, R03, R04, R08 and R11 write the same loads for the 7,520 values of x tried, a wider x by `MOV.L` among them. Each such head refutes an exemption that knows the other loads alone.
- **The scheduler.** SHC's scheduler moves the switch as it moves a pop ([return-above-entry](#return-above-entry)). From R26 to 5.1 R13, a function with a stack switch and fourteen or more float divisions has other instructions above the head: `FDIV`s, a store, or a lifted pop, `LDS.L @R15+,FPSCR` among them. From thirty-four divisions on, `MOV.L @R15+,R15` stands among the exit's pops, after `MOV.L @R15+,R5` and `MOV.L @R15+,R0` and before `LDS.L @R15+,FPSCR`, so the pops past it read the interrupted stack. In 7,035 such functions, of every release from R10 to 5.1 R13 and from 4 to 256 divisions, the head stands whole and in order above the first branch, and one `MOV.L @R15+,R15` stands below it. 5.1 R01 stops with no message on an `sp=` that names a variable, so its functions hold a constant stack alone. The heads `-extra=a=1000` builds stay whole too: in functions of R26, R32, 5.1 R01, R03, R04, R08, R11 and R13 with 1 to 100 divisions and eight values of x, with `-extra=a=1000` and without, the rule finds no witness.
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

#### pr-save-below-branch

- **Read.** R10, and 5.1 R11 in part. In each other release from R26 to 5.1 R13 a scan found the same places by their shape, and nothing was read line by line. Of SHCPEP, what builds an `STS` or a PR operand was read, its pass that deletes PR's push and pop, and R10's slot walk: its other passes weren't read for this rule, in any release.
- **Entry.** In R10, one function of SHCGEN opens a function's code: it writes the `FLABEL` record, then the `ENTER` record, and nothing between them (`0x409c84` to `0x409cf6`). Its record writer gets `ENTER`'s op at that site alone, and none of its 2,757 template rows holds it. SHCASM expands `ENTER` where it reads it, in the one call of its entry builder (`0x4148c1`, `0x40ae70`). The builder builds the push of PR at one place, under one bit of the frame record (`0x40b474`), and it builds no branch: its records are `MOV`, `STS`, the FR pushes, `ADD`, `MOVI` and `STC`. No other instruction of SHCASM builds a PR operand but the exit builder's pop (`0x40bd9b`). SHCGEN's 23 `STS` template rows take one of three fixed operands, by their templates MACL, FPUL and FPSCR. SHCPEP builds no `STS`, and builds a PR operand only in the pop of an exit it writes itself (`0x40b36f`). It compares with PR's number at five more places. Two stand in one pass (`0x40b670`): in a function whose records hold none of `JSR`, `BSR`, `BSRF`, `CALL` and `TRAPA`, it sets the frame record's bit that SHCASM tests for the push, and deletes each record that is `LDS.L @R15+,PR` or `STS.L PR,@-R15` (`0x40b733` to `0x40b7a1`). What would write a record of the push for it to delete wasn't found. The other three, in two routines that return a flag (`0x408af4`, `0x408d23`, `0x40912c`), weren't read. SHCPEP's slot walk passes over `ENTER` as over a label (`0x416ba0`), so `ENTER` goes into no slot.
- **Windows.** R10's SHCASM orders instructions only inside a window. Its collector (`0x408aa0`) ends a window at each label, at each `BRA`, `BT`, `BF`, `BT/S`, `BF/S`, `BSR`, `BSRF`, `BRAF`, `JMP`, `JSR`, `RTS`, `RTE`, `TRAPA` and case-table jump, at one more kind of record, and at 32 records, and a branch that ends one is its last entry. One function marks that entry as writing every resource (`0x409440`). The dependence builder makes an entry wait for the nearest entry above it that writes a resource it names, and for each entry above it that reads a resource it writes (`0x4094e0`), so the branch waits, itself or through others, for every entry of its window that names a register. The push names PR and R15. So the push stands above the function's first branch where one window holds both, and in a window above the branch's otherwise.
- **5.1 R11.** The same places hold: SHCGEN's one site (`0x4224a4` to `0x422516`), the one call of the entry builder and its one push of PR (`0x410100`, `0x4107fa`), SHCPEP's one PR operand, in a pop (`0x41c1dc`), its pass that deletes PR's push and pop (`0x41c4c0`), the collector's ends and its bound of 32 (`0x41b130`), and the mark on a window's last branch (`0x41b990`). R11 has a second window driver (`0x41c5a0`), which fills the windows the scheduler's give-up reorders ([return-above-entry](#return-above-entry)). It takes a branch for a window's end, leaves it out of the window and hands it on after, so the give-up puts nothing of a window below its branch. R11's dependence builder wasn't read, nor the routine that hands a window out, which of the two drivers runs when, or the three record kinds R11's dispatcher expands and R10's lacks.
- **Scan.** Each release from R26 to 5.1 R13 has one site in SHCGEN that writes `FLABEL` and then `ENTER`, one place in SHCASM that builds a push of PR, a collector that compares a count with 32 beside the chain of branch ops, and a SHCPEP that builds no `STS`, builds a PR operand at one place, among pops, and at one place compares a record's op with `STS`'s and `LDS`'s beside PR's number. 5.1 R13's entry builder is R11's byte for byte.
- **Labels.** SHC starts every function at a label, and joins C functions with no return between them ([what has no rule](#what-has-no-rule)). So a function's lines, bounded by names, can hold the static function below it, whose entry pushes PR below the upper function's branches, and only where no label stands between a branch and a push does one function hold both. A scan of the retained compiles found 300 function ends with no transfer above the next function's label, none of them R10's, R26's, R27's or R28's, and 89 pushes of PR below a conditional branch and its slot with nothing but that label to end the run: 82 of R32's, 5 of 5.1 R11's and one each of 5.1 R01's and R13's.
- **Probes.** Eight files of C written to reach the witness, 148 functions: returns before a call, a call on one path and a tail call on the other, calls through pointers, in loops and behind `&&` and `||`, a rounding function that calls `floorf` on both paths, interrupt functions with `sp=` and `tn=`, frames of 256 bytes and 16 KB, structs and varargs, `#pragma noregsave`, `regsave`, `noregalloc`, `inline` and `global_register`, 8 to 40 divisions before the first branch, and `_builtin_trapa` or `_builtin_sleep` first. Each release from R10 to 5.1 R13 compiled them under the options of [the family compiles'](#the-family-compiles) probes. 4,727 compiles wrote 94,760 functions, and 84,645 push PR. In each of the 84,390 that hold no inline text, the push stands above the function's first branch, call, jump and return, and in no slot: R11 writes `if (a == 0) return 0; return g(a) + 1;` as `STS.L PR,@-R15`, `TST R4,R4`, `BF L246`, and R10 the entry of `if (a == 0) return g(1) + 2; return g(a);` as `TST R4,R4`, `STS.L PR,@-R15`, `BF L246`. The other 255 expand a `#pragma inline_asm` body that holds the witness, the control.
- **Refutation.** 2,437 more compiles tried to put one function's push below its own branch, and none did. With functions cut at SHC's comments they wrote 113,724 functions, 81,683 of which push PR and hold a conditional branch. Among them: 560 functions of R32 and 5.1 R11 with the push, 6 to 27 divisions and a conditional branch in one window, where from 17 divisions on the give-up shows and the branch stays last; each single bit of `-extra=a=` in R10, R26, R32 and 5.1 R01 and R13, but `a=8`, SHCASM's dump bit, under which R26 wrote no text and SHCASM in R32 and 5.1 R01 and R13 didn't finish the two files of chained divisions; 420 random functions in all twelve releases under eight sets of options; and `-case=table`, `-case=ifthen`, `-goptimize`, `-structreg=4` and `-aggressive=2` in 5.1 R11 and R13.
- **Compiles.** inspect read each code section whole, as one function, in 24,036 texts of every release from R10 to 5.1 R13: 8,030 retained compiles, the family compiles' and later ones, every text of DreamComp's cache as it stood on 2026-11-19 among them, as many of 9,588 as are still on disk; the 6,113 distinct texts of the family compiles' probes; 4,062 of this rule's probes and refutation; and 5,831 under three other rules' research: their probes and refutations, their targets' texts, and 82 `#pragma inline_asm` bodies cut from DreamComp's sources, which no release compiled. They hold 178,307 pushes of PR. The witness shows 23 times, each in inline text: 19 in hand-written `#pragma inline_asm` bodies, 7 of them among the 82, and 4 in the control. check refuses 48 more of the other rules' texts, compiled under `-debug`.
- **Libraries.** In the bytes of the Katana SDKs' libraries, none of the 27,146 pushes of PR in the 10,268 distinct `C_SH` members, which hold no inline text, stands in one straight run below a conditional branch; 3 more such members couldn't be read. A run there ends upward at the slot of an `RTS`, `RTE`, `JMP`, `BRA` or `BRAF`, at a pool word a load names, at a relocated word, at bytes no record wrote and at a name the member defines, and a second pass that ends no run at a name finds none in one either. The 142 pushes that do stand in one are in `A_SH` members of eight names. 25 of the `C_SH` pushes, and 27 in the second pass, stand in a run that a conditional branch above enters, as in R11b `sofdec.lib`'s `sfd_see`, where `_SFSEE_ExecServer` ends in `BF`, `RTS` and `NOP` and the `BF` goes to `_sfsee_ExecHeadAnaly` right below.
- **The inter-module optimizer** makes this witness from C: in place of a block that stood right below a conditional branch or its slot it writes a push of PR, in a function compiled from C ([evidence](#the-inter-module-optimizer)).
- **Rests on** six things this rule's reading doesn't settle:
  - **SHCPEP.** That no pass puts a record between `FLABEL` and `ENTER`, or moves either. For R10 that is the frame argument's "`ENTER` first" ([R10's frame](#abi-rules)), which this rule didn't read again, and no other release's SHCPEP was read for it. No compile shows a branch or an entry moved.
  - **R10's stores.** Nine sites of SHCGEN's emitter pass an op from a register and weren't traced. That the scheduler's masks give the push PR and R15 was taken from its operands, and that no stage turns another record into an `STS` of PR rests on scans of immediates. What writes the record of the push that SHCPEP's pass deletes wasn't found.
  - **The order outside R10.** That a window's branch is issued after the push was read for R10. For 5.1 R11 it rests on the mark on the branch and on the second driver, with the dependence builder, the routine that hands a window out and which driver runs when unread. For the other releases nothing of the scheduler was read for this rule, and from R26 on it can put what it hasn't issued at the top of its window ([return-above-entry](#return-above-entry)). In the refutation's 560 functions of R32 and 5.1 R11 with the push and a conditional branch in one window, the branch stays last.
  - **The join.** Where SHC drops the `BRA` to the function below, and under what test, wasn't found. Every compile that drops it writes the lower function's label there.
  - **A label at each function.** That SHC starts every function at a label rests on SHCGEN's one `FLABEL` site, read in R10 and 5.1 R11 and found by its shape in the others, and on the compiles, where each function SHC's comment marks stands at a label. How SHCASM writes an `FLABEL` into the text wasn't read.
  - **The text's labels.** `f3` and `s3` as 5.1 R11 writes them ([what has no rule](#what-has-no-rule)), written without `L239`, show the witness, and nothing in their bytes holds that label. In a text someone wrote from bytes, the witness shows that the branch and the push aren't one C function's. That no function starts between them has to be shown apart from inspect.
- **Refuted by** any release writing, for C, in either mode, outside inline text, `STS.L PR,@-R15` below a `BT`, `BF`, `BT/S` or `BF/S` and its slot with no label, none of `BRA`, `BRAF`, `JMP`, `RTS`, `RTE` and `TRAPA`, and no data or directive but `.EXPORT` and `.IMPORT` between them. The rule took this form from the compiles that refute a rule blind to labels, so they confirm the form rather than test it.

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

- **FSCHG.** R10 writes it in the templates of its vector builtins, 2,628 times in the sweeps. Its place, or a pattern after it, can still be a witness ([fschg-slot](#fschg-slot), [fschg-pairs](#fschg-pairs), [fschg-dt](#fschg-dt), [fschg-stores](#fschg-stores), [fschg-register-moves](#fschg-register-moves)).
- **`BRA`, a label and `ADD` after an odd number of `FSCHG`s.** R10 lets two arms that end in runs share the closing `FSCHG`. For `void b_if(M m, M n, int c) { if (c) _builtin_st_ext(m); else _builtin_st_ext(n); }`, with `M` a `float[4][4]`, it writes under `-cpu=sh4 -optimize=1 -speed`:

  ```
            FMOV        XD0,@-R4
            BRA         L593
            NOP
  L555:
            ADD         #64,R5
            FSCHG
            ...
            FMOV        XD0,@-R5
  L593:
            FSCHG
            RTS
            NOP
  ```

  The first arm ends inside its run in `BRA` and a `NOP`, and `L593` stands in the second arm's. Where the join is far R10 writes `JMP`, or under `-pic=1` `BRAF`, and a literal pool it forces into straight-line code puts a `BRA` and a label in a run too. Its templates write `ADD #16,Rn` and `ADD #32,Rn` there, and for an operand's address integer loads, `MOV` and `ADD` ([fschg-dt](#fschg-dt)).
- **A `FIPR` that reads a register no load filled before it.** From R26 to 5.1 R13 the scheduler's [give-up](#return-above-entry) can leave a `FIPR FV4,FV0` above its template's loads. `_late` [above](#fiprs-loads) reads eight registers as the entry left them, and with twenty `FSQRT`s before the builtin, R26 writes `FSQRT FR4` three times and `FMOV.S FR3,FR0` right above the `FIPR`. In the probes of [the FIPR compiles](#the-fipr-compiles), 2,558 compiled `FIPR`s read a register that a load didn't write last: 626 read every register as the entry left it, and 1,932 one that something else wrote. R10 writes none. So `fipr-unloaded` and `fipr-shared` count over the function and follow no path.
- **A 16-bit word by `MOV.L` for `LDS`.** With no `-fpu`, every release from R31 to 5.1 R13 loads a float 0.0 as H'00000000 by `MOV.L`, `LDS` and `FSTS`. Under `-denormalize=on`, with `-fpu=single` or no `-fpu`, every release from R26 to 5.1 R13 loads a denormal word the same way: `void s_idx(float *p, int i) { p[i] = 1.78e-43f; }` compiles to `MOV.L L236+2,R1`, whose pool word is `H'0000007F`. So `integer-constant` reads the word's use, and `LDS` shows nothing.
- **R15 above the entry before a return.** Every release from R26 to 5.1 R13 can [put a pop above the entry's pushes](#return-above-entry).
- **A pop of a word the function's first pushes wrote, with a call below.** SHC computes an argument that is an expression into its register, pushes it across a call in an earlier argument, and pops it back. With a `register` parameter under a unary plus, the expression is the parameter as it arrived, and its push is the body's first instruction, right below the entry's pushes. For `int w1(register int a, register int b, register int c) { return g3(s1(), +b, +c) + 1; }`, with `s1` static, R10 under `-cpu=sh4 -optimize=1 -size` writes:

  ```
  _w1:                             ; function: w1
                                   ; frame size=20
            MOV.L       R14,@-R15
            MOV.L       R13,@-R15
            STS.L       PR,@-R15
            MOV.L       R6,@-R15
            BSR         L236
            MOV.L       R5,@-R15
            MOV.L       @R15+,R5
            MOV         R0,R4
            MOV.L       L292+2,R2  ; _g3
            JSR         @R2
            MOV.L       @R15+,R6
  ```

  Every release from R10 to 5.1 R13 pushes R6 there and pops it in the slot of the call to `g3`, for size and for speed. The text doesn't show where the entry's pushes end and the body's begin, so a pop of a word the entry seems to push shows nothing.
- **A pushed word that two pops take.** Each copy of an exit pops the saved registers again. From R26 to 5.1 R13 two pops into R0 to R7 can take one word too, with calls below them: a pop can stand above its own push, as [a pop can stand above the entry's pushes](#return-above-entry), and it then takes the word of an earlier push. `tw3` pushes `b` for `g2` above its branches. On each of two arms it pushes the address of the float it adds to and pops it for the store, with fifteen divisions between:

  ```c
  extern int h1(void), h2(void), h3(int), g2(int, int);
  extern float gf, gg, gh;
  struct pad { unsigned long on, press; short x1, y1; unsigned char r, l; };
  struct cam { float x, y, z; int ang_x, ang_y, ang_z; float distance, d2; };
  extern struct cam *gc; extern struct pad gpad[2];
  #define D15(e) ((e) / dv / dv / dv / dv / dv / dv / dv / dv / dv / dv / dv / dv / dv / dv / dv)
  #define D3(e) ((e) / dv / dv / dv)
  int tw3(register int a, register int b)
  {
      float dv = gg;
      unsigned short r0, r1, r2;
      r0 = ((short)gpad[0].r << 8) + 0x8000;
      r1 = ((short)gpad[0].l << 8) + 0x8000;
      r2 = ((short)gpad[1].r << 8) + 0x8000;
      a = g2(h1() ? (int)(gc->distance += (r0 + (int)(D15(gf))) * 0.0001f)
           : h3(1) ? (int)(gc->d2 += (r1 + (int)(D15(gh))) * 0.0001f)
                   : (int)(gc->z += (r2 + (int)(D3(gf))) * 0.0001f), +b);
      return a + h2();
  }
  ```

  5.1 R13 under `-cpu=sh4 -fpu=single -optimize=1 -speed` writes each arm's pop above the arm's push:

  ```
            JSR         @R1
            MOV.L       R14,@-R15
            TST         R0,R0
            BT          L249
            ...
            FLOAT       FPUL,FR3
            MOV.L       @R15+,R3
            FMOV.S      @R0,FR0
            ...
            ADD         R0,R2
            MOV.L       R2,@-R15
            FMOV.S      @R1,FR3
            ...
            BRA         L251
            STS         FPUL,R4
  L249:
            MOV.L       L255+28,R3 ; _h3
            JSR         @R3
            MOV         #1,R4
            TST         R0,R0
            BT          L250
            ...
            FLOAT       FPUL,FR3
            MOV.L       @R15+,R2
            FMOV.S      @R0,FR0
            ...
            ADD         R0,R3
            MOV.L       R3,@-R15
            FMOV.S      @R1,FR3
  ```

  `MOV.L R14,@-R15` pushes `b`. Each of the two pops takes its word, and the calls to `g2` and `h2` stand below both. Every release from R26 to 5.1 R13 writes `tw3` so, for size and for speed, and R10 doesn't.
- **PR's push below a branch of the function above.** SHC joins C functions with no return between them, and starts a static one at an `Lnn` label that only its comment marks, and that a direct object gives no name. For `void out1(int a, int b, int c) { if (pause_word == 0) in1(a, b, c); }` above `static void in1(int a, int b, int c) { vi(a); vi(b); vi(c); }`, R10 under `-cpu=sh4 -endian=little -optimize=1 -size` branches to `in1`'s entry:

  ```
  _out1:                           ; function: out1
                                   ; frame size=0
            MOV.L       L401,R2    ; _pause_word
            MOV.L       @R2,R3
            TST         R3,R3
            BT          L237
            RTS
            NOP
  L237:                            ; function: in1
                                   ; frame size=16
            MOV.L       R14,@-R15
            STS.L       PR,@-R15
  ```

  R26, R32 and 5.1 R01, R04, R11 and R13 write it so too, under `-optimize=1 -size` and under `-optimize=1 -speed -inline=0`. R31, R32 and every 5.1 release also let a `void` function fall into the static function it calls last: under `-optimize=1 -size` each writes 27 of a file's 63 callers so, the `void` ones. R10 and R26 to R28 aren't known to: they keep a transfer in all 63, and no function end of their retained compiles falls. For `void f3(int a) { { int i; for (i = 4; i; i -= 2) ; } s3(a); }` above `static void s3(int a) { v(); v(); }`, 5.1 R11 under `-cpu=sh4 -endian=little -optimize=1 -size -fpu=single` writes no return, and no branch to `L239`:

  ```
  _f3:                             ; function: f3
                                   ; frame size=0
            BRA         L757
            MOV         #4,R5
  L758:
            ADD         #-2,R5
  L757:
            TST         R5,R5
            BF          L758
  L239:                            ; function: s3
                                   ; frame size=4
            STS.L       PR,@-R15
  ```

  So a push of PR below a conditional branch shows nothing past a label ([pr-save-below-branch](#pr-save-below-branch)).
- **An `FMAC` that names FR0 twice.** `FMAC FR0,FRm,FRn` adds the product of FR0 and FRm to FRn. From R26 on SHC writes it for a `float` sum of a product, at `-optimize=0` too, and every release from R26 to 5.1 R11 can write it with FR0 as the sum, `FMAC FR0,FRm,FR0`, and as both factors, `FMAC FR0,FR0,FRn`. For

  ```c
  extern float gf(void);
  extern float gb;
  float v_2(float *p) { return gf() + (p[0] - p[1]) * (p[2] + p[3]); }
  float v_5(float *p) { return gf() * gb + (p[0] - p[1]) * (p[2] + p[3]); }
  ```

  5.1 R11 under `-cpu=sh4 -fpu=single -optimize=1 -speed` writes `v_2`, and the end of `v_5`:

  ```
  _v_2:                            ; function: v_2
                                   ; frame size=8
            MOV.L       R14,@-R15
            STS.L       PR,@-R15
            MOV.L       L242+2,R3  ; _gf
            JSR         @R3
            MOV         R4,R14
            MOV         #12,R0
            FMOV.S      @R14,FR1
            FMOV.S      @(R0,R14),FR3
            MOV         #8,R0
            FMOV.S      @(R0,R14),FR2
            MOV         #4,R0
            LDS.L       @R15+,PR
            FADD        FR3,FR2
            FMOV.S      @(R0,R14),FR3
            FSUB        FR3,FR1
            FMOV.S      FR1,FR0
            FMAC        FR0,FR2,FR0
            RTS
            MOV.L       @R15+,R14
            ...
            MOV.L       L242+6,R3  ; _gb
            FADD        FR3,FR2
            FMOV.S      @(R0,R14),FR3
            FMOV.S      @R3,FR0
            FSUB        FR3,FR1
            LDS.L       @R15+,PR
            FMUL        FR1,FR2
            FMAC        FR0,FR0,FR2
            FMOV.S      FR2,FR0
            RTS
            MOV.L       @R15+,R14
  ```

  Both are wrong code. `gf`'s result is in FR0 below each call, as the addend of `v_2` and as a factor of `v_5`, and nothing reads it before `FMOV.S FR1,FR0` and `FMOV.S @R3,FR0` write FR0. So `v_2` returns the product plus its first factor, with no `gf()` in it, and `v_5` the product plus the square of `gb`. Each release from R26 to 5.1 R11 writes these two `FMAC`s, for speed and for size, and R26, R32 and 5.1 R04 and R11 write both forms at `-optimize=0` and under `-code=machinecode` too. 5.1 R13 writes neither under any of those options: for speed and for size it copies `gf`'s result to FR12 first, and writes `FMAC FR0,FR2,FR12` and `FMAC FR0,FR12,FR2`. R10 writes no `FMAC`, for this C or in any compile read: its SHCGEN holds the code that builds one behind a test of one bit of a tree node's record (`0x406004`), and none of its byte stores sets that bit. Its word stores and block copies weren't traced.

  The cause was read in SHCGEN, in the code that gives an `FMAC`'s three operands their registers: whole in 5.1 R13, and in R11 at the step that differs. One factor takes FR0. Before it does, a step moves an operand out of a scratch register that an operand after it uses, and either other operand out of FR0: it asks the float selector for another register, with a mask of those registers excluded. Up to 5.1 R11 the mask starts at 0, and in R13 at FR0: at `0x41a3cd`, R11's `shcgen.exe` holds `xor ebp,ebp` and R13's `mov ebp,0x10000`. With FR0 not excluded the selector can hand FR0 back, and in the compiles it does where the other operands take up FR1 to FR3. The operand then stays in FR0, and the FR0 operand is written over it. The selector was read in R13 alone, without the classifier that gives it each register's class. R13's `shcgen.exe` with that one immediate set to 0 compiles `v_2` and `v_5` to R11's text. A script finds the step in each release from R10 to 5.1 R13 and the immediate in R13's alone, and Hitachi's history of 5.1 lists under R13 a case where code that uses `FMAC` destroyed FR0.

  So no rule reads an `FMAC`'s operands. What holds is narrower than a witness: 5.1 R13 isn't known to write either form, and R10 isn't known to write an `FMAC`. inspect can't say that, because a text comes with no release to ask about: a witness stands only while no release of the family is known to write it ([what a witness claims](#what-a-witness-claims)), and ten write these.

**The entry registers.** C passes nothing in R0 to R3 or FR0 to FR3: arguments go in R4 to R7 and FR4 to FR11, and a struct return's address on the stack. No rule reads them at the entry all the same, because R10 and R32 read them for a variable C reads before it sets it:

- With optimization on, a value C never sets and only reads compiles to a read of R15 or FR15, which SHCGEN names in its records, or of DR14 under R32's double precision, and the software doubles read a stack slot.
- A variable C rewrites in place before it sets it, as in `x += a`, takes the register the allocator gives the result: an argument register the function leaves free, then R0 to R3 or FR0 to FR3, a callee-saved register after its push, or a stack slot. `int f(int a, int b, int c, int d) { int x; x += a; x += b; return x + c + d; }` reads R1 by `ADD R4,R1` before any write of R1, in both releases and modes: as its first instruction in R10, and as its second, after `MOV R6,R0`, in R32.
- In an interrupt function such a variable takes R4 to R7 and FR4 to FR11 first, then R0, FR0 or FR1, which hold the interrupted code's values.
- R32 reads FR0 to FR3 before it writes them in fully defined C too: under double precision it moves `FADD DR2,DR4` above the writes of FR2 and FR3 in `double f(double a, double b) { double x = a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; x = x / b + a; return x; }`.
- At `-optimize=0` every local lives in a stack slot.

These have no rule because their reading isn't whole:

- **An `FTRV` whose vector no load fills.** SHC writes `FTRV` from templates too, and the passes that leave the loads of `FIPR`'s template alone would carry a rule like `fipr-unloaded` for it. But the fifteen template rows that write an `FTRV`, and the stores of its result, weren't read for one.
- **CLRT and NEGC.** No probe or sweep writes either. CLRT's op, 0x8b, is no immediate, template row or byte constant in any stage of R10 or R32, but the writes that could make it haven't all been read, nor NEGC's.
- **A compare or a conditional branch after an odd number of `FSCHG`s.** No function of [the FSCHG compiles](#the-fschg-compiles) holds `BT`, `BF`, `BT/S` or `BF/S` there, and none of the matrices or of the random C a compare. But R10 does write a `BRA` there, and SHCPEP's branch and common-code passes, which hold no test for an `FSCHG`, weren't read ([fschg-dt](#fschg-dt)). So of a loop inside a run inspect reads the `DT`, and not the compare or the branch.
- **A pair's move through `@Rn` after an odd number of `FSCHG`s.** In every compiled text read, a pair's load there is `@Rn+` and a pair's store `@-Rn`: 135,108 loads and 120,876 stores in the matrices under `-extra=a=` alone ([the FSCHG compiles](#the-fschg-compiles)). But how the template rows fix the mode wasn't read, nor SHCPEP's `prdecea`.
- **R10's FPUL saves.** In R10, FPUL's push and pop come from the same byte of the frame record as FPSCR's, and in R10's texts in the sweeps appear only in functions that return by `RTE` or end in `TRAPA`, but R10's other writers of FPUL weren't read.
- **PR's push below `BRA`, a call, a jump or a return.** R10's scheduler ends a window at each of them as at a conditional branch ([pr-save-below-branch](#pr-save-below-branch)). But SHCASM writes a `BRA` of its own over a literal pool it forces into straight-line code, and where it may force one wasn't read. In 216 functions of R10, R32 and 5.1 R11 and R13 that run 151 to 4,677 instructions before their first conditional branch, the push stands above the function's first branch of any kind, which in 176 of them is such a `BRA`. A call, a jump and a return got no probes of their own.
- **A pushed word that two pops into R0 to R7 take, with a call below each and no push below either.** In `tw3` above, each pop that takes `b`'s word has a push through R15 below it, above the next label, branch, call and return. Of the 185 words that two such pops take in the 49 texts of `tw3` and of functions like it, with 13 to 20 divisions on an arm, none has two pops with no such push below. A rule that read those pushes would rest on the scheduler moving a pop only inside a window that ends at each label and branch, which nobody read for a pop above its push, and on SHC fetching a pushed operand at one place, which nobody read in any release.
- **A pop into R0 to R3 of an argument pushed as it arrived.** No compile found writes one with a call below it: C written to reach it keeps the argument in a saved register or the frame across the call, or pops a word pushed from R4 to R7, before any line wrote that register, back into R4 to R7. But nothing read says why, in any release, and `tw3`'s pops show that a pop into R0 to R3 can take another push's word. From R31 on, with optimization on, SHC also pushes an argument as it arrived for a one-word frame, and it makes a frame word's last read a pop: 5.1 R13 under `-cpu=sh4 -fpu=single -optimize=1 -size` ends `void u2(void) { int x = h1(); h2(); h3(x); }` in `MOV.L @R15+,R4`, `JMP @R3` and `LDS.L @R15+,PR`. In 5.1 R13's SHCPEP, the one read for it, that pop is only of a word an `ADD` made room for.
