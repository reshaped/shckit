# Patches

A patch rewrites SHC's text so that asmsh assembles it to one target's bytes. Each is for SHC 5.0 Release 10 (R10) text of members of the Katana SDK 1.20J `ninja.lib`, names the members it is for, and says what it must not be applied to. Applying one doesn't show that the code still runs the same, or guarantee an object that matches a target.

| patch | what it does | for |
|---|---|---|
| [drop-inline-align](#drop-inline-align) | drops SHCASM's `.ALIGN 4` after inline text | `njModel_`, `njModelS_`, `njModelE_`, `njMotSrc_`, `njMotionE_`, `njMotLinkSrc_` |
| [align-pool](#align-pool) | writes `.ALIGN 4` before each literal pool, and `.RES.W 1` where SHCASM counted the label at a half word | `njControl_`, `njModel_`, `njModelS_`, `njModelE_` |
| [use-assembler-pools](#use-assembler-pools) | turns SHC's literal pools into `#` loads and `.POOL` lines, so asmsh lays the literals out | the fifteen motion members |
| [drop-r0-move-nop](#drop-r0-move-nop) | drops SHCASM's `NOP` after `MOV Rm,R0` outside a delay slot | the fifteen motion members |
| [fill-call-slot](#fill-call-slot) | drops SHCASM's `NOP` in the slot of `JSR @Rm` when `ADD #imm,Rn` to R8 to R14 follows it, so the add fills the slot | `njMotLinkSrc_` |

Each patch has the same parts: what it is for, what it does, what it refuses, then its evidence and what would refute it. If you find a member or a compile that refutes a patch, please send a pull request.

## Applying them

`shckit apply -p PATCH ... -o OUT IN` reads `IN` as asmsh does, to its first `.END`, and writes `OUT`.

- **Order.** The patches run in the table's order, whatever order they are named in. align-pool and use-assembler-pools never run together: they lay pools out two ways.
- **One run.** Name all of a text's patches in one `apply`. A patch can't tell an earlier run's output from SHC's text. On align-pool's output, drop-inline-align drops the `.ALIGN 4` lines align-pool wrote.
- **Untouched lines.** A line no patch drops, rewrites or writes comes out byte for byte, line end included.
- **Command-line errors.** An unknown or repeated patch, the two pool patches together, and an `OUT` that names `IN`, by the same path or another, are found before `IN` is read.
- **Refused before any patch.** A line up to `.END` that shckit can't read, which could hide what a patch reads: an alignment, a pool, or the start of a section. An `.ALIGN` whose operand isn't a decimal number is one, and so is a continuation below SHCASM's `.ALIGN 4`, which joins nothing, since SHCASM prints its line whole. A line after `.END` that isn't blank or a comment is refused too.
- **On a refusal.** apply writes nothing, and names the original line, and the patch if one refused:

```
shckit: x.src:31: drop-inline-align: an .ALIGN 4 in a code section that isn't SHCASM's line
```

The examples below are schematic where they say so. `make test` writes exact samples to `build/test/`, and each patch shows the command that patches one.

### Terms

- **SHCASM's lines.** The patches recognize the lines of SHCASM, the SHC phase that writes its assembly, by their exact bytes: a mnemonic or directive at column 10, operands at 22 and a comment at 33, counting from 0, and a pool label padded with spaces to 34 bytes. Lines in other columns are left alone or refused, never reinterpreted.
- **A literal pool** is a label `Lnn` alone on a line of a code section, and the `.DATA` and `.RES` lines right below it. The labels of inline text, such as `?MASK:`, aren't `Lnn`, and data in a data section isn't a pool. Both stay as they are. align-pool and use-assembler-pools take only a pool whose label line is exactly as SHCASM's pool writer prints it.
- **A code label** is a label asmsh defines in a code section, except a pool's. An `.EQU`'s symbol whose value names a code label is one too, and so is a `?label`, wherever it is defined: asmsh takes the definition in the use's block, which use-assembler-pools, drop-r0-move-nop and fill-call-slot don't follow.
- **An operand that offsets a place in code** adds a code label to anything other than another label, such as `_f+6`, `_f+N` for an `.EQU`'s `N`, or `_f+_x` for an imported `_x`; or it holds `$`, where its line stands; or it is `@(disp,PC)`. A label alone, or the difference of two, moves with its code. A symbol `.ASSIGN` defines can hold another value at each use, which those patches don't follow either, so a patch that refuses such an operand also refuses an `.ASSIGN` whose value names a code label, and the symbol otherwise counts as a number.
- **No address.** No patch computes an address. So use-assembler-pools, and drop-r0-move-nop and fill-call-slot once they drop a `NOP`, refuse an operand that offsets a place in code: none of them can tell where such an operand points afterwards.

## drop-inline-align

drop-inline-align removes the `.ALIGN 4` line that SHCASM prints after every piece of inline text.

- **For:** SHC 5.0 R10 text of six members of the Katana SDK 1.20J `ninja.lib`, whose bytes have no NOP after inline text that ends on a half word: the model members `njModel_`, `njModelS_` and `njModelE_`, and the motion members `njMotSrc_`, `njMotionE_` and `njMotLinkSrc_`.
- **With:** after this patch, [align-pool](#align-pool) on the model members, and [use-assembler-pools](#use-assembler-pools) and [drop-r0-move-nop](#drop-r0-move-nop) on the motion members, with [fill-call-slot](#fill-call-slot) last on `njMotLinkSrc_`.
- **Not for:** the SDK's other inline-text members, whose bytes keep R10's NOP.

### What it does

In a code section, the patch drops each line that is exactly SHCASM's `.ALIGN 4`: ten spaces, `.ALIGN`, six spaces and `4`, with nothing after it, ending in LF or CR LF. It writes nothing in the line's place and changes no other line. Other alignments stay, and data sections are not touched.

<!-- example drop-inline-align: input x.src -->
```
_f:
    CLRMAC
          .ALIGN      4
          RTS
          NOP
```

<!-- example drop-inline-align: run -->
```sh
build/shckit apply -p drop-inline-align -o out.src x.src
```

<!-- example drop-inline-align: result out.src -->
```
_f:
    CLRMAC
          RTS
          NOP
```

`CLRMAC` is inline text, in its programmer's columns. The `.ALIGN 4` below it is SHCASM's.

The line's exact bytes are the only sign in the text of where SHCASM ended a piece of inline text. Inline text keeps the programmer's layout, so an `.ALIGN 4` written inside it differs from SHCASM's line in its columns. The patch assumes that every line in SHCASM's form is the one SHCASM prints after inline text. That would be wrong if a programmer wrote that line inside inline text byte for byte, or if SHCASM printed it for another reason. SHCASM's only other `.ALIGN` writer prints 16 or 32.

A text of nothing but such lines, above any `.SECTION`, comes out empty. asmsh assembles it to the same object as the original text, with a warning that it has no `.END`, but shckit's reader refuses an empty text.

**With the other patches.** The patch runs first. SHC laid its literal pools out for the alignment it wrote, so dropping that alignment can leave a pool's longs off their boundary. [align-pool](#align-pool), which runs after it, aligns them again. On the motion members, [use-assembler-pools](#use-assembler-pools) has asmsh lay the literals out instead.

To run the patch on an exact SHC text, use the sample `make test` writes:

```sh
make test
build/test/shckit apply -p drop-inline-align -p align-pool -o x.src build/test/sample.src
cmp x.src build/test/sample.want
```

### What it refuses

The patch refuses, naming the line:

- `an .ALIGN 4 in a code section that isn't SHCASM's line`: any other code-section `.ALIGN` whose value is 4, in other columns, with tabs, a label, a comment, a trailing blank, or as `04`. Nothing in the text shows whether the producer kept such a line.

### Evidence

#### Why

Three members of the Katana SDK 1.20J `ninja.lib`, `njModel_`, `njModelS_` and `njModelE_`, hold code that R10 writes, except that their inline text runs straight into the next instruction. So does the inline text of three motion members, `njMotSrc_`, `njMotionE_` and `njMotLinkSrc_`, whose literals sit in asmsh's pools ([use-assembler-pools](#use-assembler-pools)). Every SHC release and assembler [tried](#evidence-base) puts a NOP there when the text ends on a half word.

#### Inline text and the alignment after it

`#pragma inline_asm(f)` declares a function whose body is assembly text. SHC copies the text into each call, and into a definition of its own, with `RTS` and `NOP` after it, when the function is external or its address is taken. `#pragma asm` and `#pragma endasm` put a block of text in place. Both are inline text. SHC writes them only with `-code=asmcode`, assembly text for asmsh. With `-code=machinecode`, a direct object, every release refuses them (errors 2808 and 2708).

SHC doesn't know how long the text is. After every piece of it, SHCASM writes `.ALIGN 4` and treats its own count of the code position as a multiple of 4 again. The count matters: SHC uses it to decide whether a pool's longs need a pad, and R10's scheduler keeps memory accesses off a count of 2 mod 4 where it can, with or without `-extra=a=400` (SHCASM at 0x409930). asmsh fills a code section's `.ALIGN 4` with a NOP when the code ends on a half word.

An excerpt of R10's text for a one-instruction body, its definition and a call (`shc p.c -code=asmcode -cpu=sh4 ... -extra=a=400`):

```
_body1:                          ; function: body1
    CLRMAC
          .ALIGN      4
          RTS
          NOP
...
          MOV.L       R5,@R3
    CLRMAC
          .ALIGN      4
          MOV.L       @R15,R1
```

`asmsh p.src -cpu=sh4` assembles it as `0028 0009 000b 0009 ...`: a NOP at P+2.

#### What the target holds

In the model members, inline text runs into what follows with no fill:

- In `njModel_`'s code section, PSG, 0xb4c to 0xb5c is a nine-instruction body, a standalone definition. Its `RTS` is at 0xb5e, a half word, its `NOP` at 0xb60, and the next function starts at 0xb62. R10 writes a NOP at 0xb5e and the `RTS` at 0xb60.
- The same body copied into `_njCalcModel` takes 0xbd4 to 0xbe4. The next instruction, a `MOV.L`, is at 0xbe6. R10 puts a NOP there.
- `njModelE_`'s one body ends with its own `RTS` at 0xba and its slot at 0xbc. The `RTS` SHC adds follows at 0xbe.

Every such line in R10's text of the members, placed by the build that matches the target:

| Member | `.ALIGN 4` after inline text | at 2 mod 4: R10 writes a NOP the target lacks | at 0 mod 4: no byte |
|---|---:|---:|---:|
| `njModel_` | 973 | 458 | 515 |
| `njModelS_` | 165 | 64 | 101 |
| `njModelE_` | 1 | 1 | 0 |
| `njMotSrc_` | 6 | 5 | 1 |
| `njMotionE_` | 1 | 1 | 0 |
| `njMotLinkSrc_` | 11 | 7 | 4 |
| `njControl_` | 0 | | |

`njModel_`'s count includes 38 lines after the comment line of a `#pragma asm` marker and one after the empty hook's own body. `njModelS_`'s includes 20 and one. `njControl_`, the fourth member built with [align-pool](#align-pool), has no inline text.

#### What the compilers and assemblers write

**Compilers.** One inline body, its definition and a call were compiled by each release with 26 option sets (`-speed`, `-optimize=0`, `-align16`, other CPUs, `-pic=1`, the `-extra=a=` values and more) and with every single bit of `-extra=a=`, and by R10, R32, 5.1 R01 and R13 with each of the nine hidden `-extra` suboptions at each bit: 2,042 runs, of which 1,804 compile. Every one writes `.ALIGN 4` after the body and assembles to a NOP at P+2. A three-instruction body in six forms (unsized with two calls, `size=6`, `size=2`, `static`, `#pragma asm` in a function and at file scope) makes 1,180 compiled runs, with an `.ALIGN` right after every piece of inline text. `-goptimize` leaves the text as it is.

**Assemblers.** Each of the five asmsh builds, under a dozen option sets (`-cpu=`, `-literal=`, `-auto_literal`, `-debug`, `-expand` and others), puts a NOP in the gap whenever it writes an object. None of their options is about alignment. In a data section the gap is left unwritten, but it is still a gap. In an `ALIGN=2` section every build refuses the line (error 510). The target's sections are `ALIGN=4`.

**The inter-module optimizer.** 5.1 R08, R11 and R13 ship `optlnksh.exe`, which can delete alignment gaps. On the one-body text it leaves the NOP. It needs `-goptimize` data, which assembly text never carries, and writes a linked module, not an object.

None of these tools makes `.ALIGN 4` on a half word take no bytes. The target needs the line to be absent.

#### Where R10's line comes from

In R10's `shcasm.exe`:

- 0x416830 handles inline text. Its only caller is the record dispatcher, at 0x4142e1. 0x4169fa to 0x416a94 copies the text's lines.
- 0x416a9a to 0x416ac9 is straight-line code: it ends the last line, prints `.ALIGN` (the string at 0x4283a8) in field 1, prints the character `4` in field 2, and ends the line. No branch lands inside it. The handler's one option test raises the `-code=machinecode` refusal.
- 0x416ad1 rounds the count: an unsized body adds 0x1000 and truncates to a multiple of 4, a sized one adds its size and rounds up.
- SHCASM's other `.ALIGN` writer, the gap filler at 0x416500, is called once, with 16, for `-align16`.

The fields are columns. For the text it writes, SHCASM sets field 0 at column 0, field 1 at 10, field 2 at 22 and field 3, the comment, at 33 (0x407b05 to 0x407b1f), and fills the line with spaces. So the line is exactly ten spaces, `.ALIGN`, six spaces and `4`, with nothing after it, and ends with CR LF as every line of the text does. The other eleven `shcasm.exe` builds have the same print: a push of their `.ALIGN` string and of `'4'`, with no jump past them. That is a static read, not a trace.

A copy of R10 whose SHCASM has only the three calls of that print replaced by NOPs, with its count still rounded, is the patch expressed as a compiler. Of the 71 [texts built here](#evidence-base), each one it compiles comes out byte for byte as shckit writes it from R10's text, at all 68 that shckit accepts. shckit refuses the other 3 (below).

With the pools aligned as [align-pool](#align-pool) aligns them, that text assembles to the objects of the three members' builds, which place all 87, 80 and 22 of their functions as the target does. A copy that prints the line but doesn't round the count fails: on `njModel_` SHC stops with internal error 4925, and `njModelS_` gets 32 `.ALIGN 16` lines that asmsh refuses. R10 places its labels by a count that includes the rounding.

The target agrees that the producer counted the alignment it didn't write. After a half-word site, R10's count says 0 mod 4 where the code is at 2, until the next site or section. Over SHC's own instructions in `njModel_`, memory accesses make 52.4% of the words where the code is at 2 mod 4 and R10's count says 0, and 25.0% where both say 2: they follow the count, not the address. `njModelE_` agrees, and `njModelS_` doesn't separate the two. The line was absent when the text was assembled, after SHC had scheduled it. The figure is an aggregate, and much of it restates that the members match, so it can't rule out a different choice at a few sites.

#### Where it applies, and where it must not

On `njModel_`, `njModelS_` and `njModelE_` it drops 973, 165 and 1 lines, and with align-pool the result assembles to the objects their builds keep ([align-pool](#where-it-applies-and-where-it-must-not-1) gives the builds). At the 523 half-word sites that removes a NOP the target lacks. At the other 616 it changes no byte. Without align-pool none of the three assembles: asmsh reports 1,036, 558 and 189 errors, the first of them 452 (illegal data area address) at the first pool SHC padded.

On `njMotSrc_`, `njMotionE_` and `njMotLinkSrc_` it drops 6, 1 and 11 lines. With use-assembler-pools and drop-r0-move-nop, and fill-call-slot on `njMotLinkSrc_`, R10's text of a C source for each, under the [1.20J ninja flags](#evidence-base), assembles to the member's object, every byte but asmsh's timestamps. At the 13 half-word sites that removes a NOP the target lacks. At the other 5 it changes no byte.

The SDK's other inline-text members are R10's, and the patch must not be applied to them. All 62 match their targets as R10 writes them:

- 17 have one of SHCASM's lines at 2 mod 4, 28 lines in all: `njAddVector_`, `njCos_`, `njGetMatrix_`, `njInnerProduct_`, `njLight_`, `njLtSrcs_`, `njRotate_`, `njRotateXYZ_`, `njRotateY_`, `njRotateZ_`, `njScale_`, `njScalor2_`, `njSubVector_`, `njTan_`, `njTranslate_`, `njUnitMatrix_` and `syCache_`. Their targets hold R10's NOP at all 28. With the print removed, 15 come out 2 or 4 bytes short and `njLtSrcs_` and `syCache_` no longer assemble.
- The other 45 have SHCASM's lines at 0 mod 4 only, where they write nothing. Their bytes can't show what the producer did.

Three of the 62 hold programmer-written `.ALIGN 4` lines, and the patch refuses them: `njInvertMatrix_`, `syCache_` and `syIntCtrl_`. The seven lines are `    .ALIGN  4` in `syCache_` and `njInvertMatrix_` and `    .ALIGN      4` in `syIntCtrl_`. None of the 71 texts holds SHCASM's line inside inline text, or one SHCASM printed for another reason. `syIntCtrl_` matches its target with or without SHCASM's lines, all of which are at 0 mod 4, and breaks if its own two are dropped too (asmsh reports 29 errors).

Over the other 67 `-code=asmcode` texts built here, 62 SDK members with inline text, 4 without and the unit from outside the SDK, the patch changes 60, leaves the 4 without inline text as they are, and refuses those 3.

Across the library, copies of known bodies that end on a half word are followed by a NOP in nine vector and matrix members and in `syCache_`, and by the next instruction in `njModel_`, `njModelS_` and `njModelF_`. No member holds both forms:

- In R10's form, `njSubVector_`'s body ends with `RTS` at 0x16 and its slot at 0x18, 0x1a holds the NOP, and the definition's `RTS` and `NOP` follow at 0x1c.
- `njModelF_`, which none of the builds here compiles, shows the patch's form. Copies of `njModel_`'s bodies, 9 to 24 words each, occur 467 times in its code. 252 end on a half word, and none of the 252 is followed by a NOP. The Katana R4.01 `njModelF_` (May 1998) has 250 and none.

#### What it rests on

- **The producer.** The bytes can't tell whether the line was never printed or was removed. A 5.0 release before R10 may have built the members. Sega's release note for Shinobi 0.40 (27 May 1998) lists, among the changes from Pre1 to Pre2, that Shinobi was compiled with SHC v5r10, but the part corresponding to Ninja's lower-level modules with v5r7, a release that isn't publicly available. The SHCASM of the release that built them, or the original `.src` files, would show which.
- **`njModelF_`** holds the form at 252 sites, but none of the builds here compiles it. Its C, built with R10, would test the patch apart from the six members.
- **`njPtclPolygon_`** holds no copy of a known body, so its inline text can't be checked against the form.
- **`njModelS_`'s phase.** Its memory accesses don't separate R10's count from the true address. `njModelF_`'s C would be the next, larger test.

### What would refute it

- An SHC release, option, source form, assembler or optimizer that writes no alignment after inline text. None of the runs above does.
- A half-word site in one of the six members whose target holds a NOP. There is none among 536.
- A stretch after a half-word site where the target's memory accesses follow the true address rather than R10's count.
- A member with the pool form [align-pool](#align-pool) writes that holds R10's NOP after inline text. `njModelF_` holds none at 252 sites.

## align-pool

align-pool writes alignment lines before each of SHC's literal pools.

- **For:** SHC 5.0 R10 text of the Katana SDK 1.20J `ninja.lib` members `njControl_`, `njModel_`, `njModelS_` and `njModelE_`, whose pools start differently from R10's.
- **With:** [drop-inline-align](#drop-inline-align), before this patch, on the three model members. Never with [use-assembler-pools](#use-assembler-pools).
- **Not for:** members R10 builds as they are: it breaks every one with a pool after code that ends on a half word.

### What it does

Before each [literal pool](#terms)'s label in a code section, the patch writes `.ALIGN 4`, and then `.RES.W 1` when the pool's items ahead of its first `.DATA.L` take 2 bytes mod 4. Both lines are SHCASM's own, in its columns, and end as the label's line does: CR LF in SHC's text. The label, the pool's lines and every reference to them stay as SHC wrote them. The pool's own items record where SHCASM counted the label (below), so no address is needed.

A schematic example (SHCASM pads the `L257:` line with spaces to 34 bytes, which isn't shown):

```
          RTS
          FMOV.S      FR3,@R3
L257:
          .RES.W      1
          .DATA.L     __nj_vertex_buf_
```

becomes

```
          RTS
          FMOV.S      FR3,@R3
          .ALIGN      4
          .RES.W      1
L257:
          .RES.W      1
          .DATA.L     __nj_vertex_buf_
```

R10's own `.RES.W 1` ahead of the long says it counted the label at a half word, so the patch writes a `.RES.W 1` of its own too.

**With the other patches.** [drop-inline-align](#drop-inline-align) runs first, so the patch's own `.ALIGN 4` lines stay.

For an exact SHC text, use the sample `make test` writes:

```sh
make test
build/test/shckit apply -p drop-inline-align -p align-pool -o x.src build/test/sample.src
cmp x.src build/test/sample.want
```

### What it refuses

The patch refuses, naming the line:

- `a pool item Lnn-Lnn, as a switch table's entries are`: a switch table, as the table writer prints its entries.
- `a pool's label line isn't the one SHCASM's pool writer prints`: any other label `Lnn` alone on a code line over `.DATA` or `.RES` lines, since nothing says how the producer treated it.
- `a pool item SHCASM's pool writer doesn't print`: an item that isn't `.DATA.W x`, `.RES.W 1` or `.DATA.L x` in SHCASM's columns, with one operand and no comment.
- `a pool's items are not .DATA.W lines, at most one .RES.W 1, then .DATA.L lines`: items out of the writer's order.
- `a pool's label that doesn't follow an instruction`: a label whose nearest line above, past any blank or comment lines, isn't an instruction. A label there, alone or on a line that takes no bytes such as `.POOL`, names the same address until the patch's lines come between them. The patch reads that line's kind, not where control goes: an instruction that falls into the pool passes, and check's data-in-flow refuses it.
- `a pool without a .DATA.L`: such a pool holds no record of the count SHC laid it out by.
- `a pool's label is defined twice`.

### Evidence

#### Why

Four members of the Katana SDK 1.20J `ninja.lib`, `njControl_`, `njModel_`, `njModelS_` and `njModelE_`, hold code that R10 writes, but their literal pools start differently. None of the SHC releases and assemblers [tried](#evidence-base) writes those heads.

#### Literal pools, and how R10 writes them

An SH-4 instruction is 16 bits, so a 32-bit constant comes from memory. `MOV.L @(disp,PC),Rn` reads a long at `(PC & ~3) + 4 + disp*4`, and `MOV.W @(disp,PC),Rn` a word at `PC + 4 + disp*2`. A compiler collects the constants into literal pools after unconditional branches, and a long must sit on a 4-byte boundary.

With `-code=asmcode`, SHC writes assembly text for asmsh, and its SHCASM phase writes each pool like this one from `njControl_` (`shc njControl.c -code=asmcode -cpu=sh4 ... -section=p=PSG,... -extra=a=400`), annotated and schematic, as the one above (SHCASM pads the `L257:` line with spaces to 34 bytes, which isn't shown):

```
          RTS
          FMOV.S      FR3,@R3          ; the delay slot ends at 0x76
L257:
          .RES.W      1                ; 2 bytes, since 0x76 is 2 mod 4
          .DATA.L     __nj_vertex_buf_ ; 0x78
```

SHCASM counts the code position itself. It writes the label, then the pool's words, then one `.RES.W 1` if its count is 2 mod 4 with longs to come, then the longs. asmsh places the label where the code ends. In a SYSROF object a `.RES` writes no data, so its bytes stay unwritten, while asmsh fills a code section's `.ALIGN 4` with a written NOP.

#### What the target holds

The same place in the target's `njControl_`:

| offset | R10 and asmsh | target |
|---|---|---|
| 0x74 | `FMOV.S`, the delay slot | the same |
| 0x76 | unwritten | NOP |
| 0x78 | the first long | unwritten |
| 0x7a | | unwritten |
| 0x7c | | the first long |

`njControl_` has five C functions and no inline text. R10 writes every instruction of it. Only this head differs, and the member is 160 bytes under R10 against the target's 164.

A pool that starts with a word, `njModelE_`'s `L1152`: the code ends at 0x466. The target holds a NOP at 0x466, two unwritten bytes at 0x468, R10's word `H'7FFF` at 0x46a and the longs from 0x46c. R10 puts the word at 0x466 and the first long at 0x468.

The four members hold 370 pools (`njControl_` 1, `njModelE_` 20, `njModelS_` 135, `njModel_` 214). Two things decide each head, without exception: where the code before it ends, and the position mod 4 that R10 counted for the label, which its own pads record (below).

| first item | R10's count | code ends | target head | pools |
|---|---|---|---|---:|
| long | 0 | 0 mod 4 | nothing | 111 |
| long | 0 | 2 mod 4 | NOP | 36 |
| long | 2 | 0 mod 4 | 4 unwritten | 40 |
| long | 2 | 2 mod 4 | NOP, 4 unwritten | 69 |
| word | 0 | 0 mod 4 | nothing | 42 |
| word | 0 | 2 mod 4 | NOP | 27 |
| word | 2 | 0 mod 4 | 2 unwritten | 11 |
| word | 2 | 2 mod 4 | NOP, 2 unwritten | 34 |

Read across: a NOP when the code ended on a half word, two unwritten bytes when R10 counted the label at 2, then R10's pool as R10 wrote it, its own `.RES.W 1` included. Before a first long that makes 4 unwritten bytes, before a first word 2. No head holds more than one NOP, so the alignment is to 4. The labels are local, so the object can't show where each stood.

The table counts the pools of the four members the patch reproduces. R10's own members and `njModelF_`, below, test it apart from them.

#### What the compilers and assemblers write

**Compilers, read.** In every release tried, only `shcasm.exe` holds the string `.ALIGN`, and each of its twelve builds prints it in two places: after inline text, with the constant `4`, and in a gap filler, with 16 or 32. So no release can write `.ALIGN 4` before a pool label, unless inline text ends right before one. In R10's text of the four members every pool label follows an instruction, and in 250 other R10 members no `.ALIGN` stands before a pool label.

**Compilers, run.** No run writes the target's head. The runs:

- 3,293 compiles of `njControl_` by every release, as text and as a direct object, with the SDK's options, `-speed`, `-optimize=0`, `-align16`, `-pic=1`, `-debug`, without `-extra`, with `-extra=a=800` and its neighbors, and with every single bit of the hidden `-extra` suboptions;
- 535 of `njControl_` and `njModelE_` by R10 with `a=400` and each other bit, `-loop`, `-rtnext`, `-inline`, `-macsave=1`, `-string=data`, `-abs16=all`, `-goptimize`, `-case=` and others;
- 324 of `njModelE_` over the twelve releases, which write 7,500 code pools.

Of the 7,500 pools, none reserves bytes ahead of its words, reserves twice, or reserves 4 bytes ahead of its first long: each is words, at most one `.RES.W 1`, then longs. On R26 and later, `-extra=a=800` writes `.ALIGN 32` before and after each pool and lays the pool out again from there, with no pad: a different alignment, and the target's heads hold one NOP at most. R10 ignores the option.

**Assemblers.** The five asmsh builds, run with no option, `-auto_literal`, `-literal=`, `-private`, `-emulation`, `-debug` and `-cpu=`:

- R10's head assembles to two unwritten bytes in the four that write SYSROF objects. 5.0A writes ELF, which has no unwritten bytes.
- None moves a long. A misaligned long is written where it falls. A load from it is refused with error 452, illegal data area address.
- Each fills a code-section `.ALIGN 4` with a written NOP, right after a `.RES` too, so `.RES.W 1` then `.ALIGN 4` can't spell four unwritten bytes.
- Each pads its own pools for `MOV.L #x` with a written `0000`, never a NOP and never unwritten bytes.

#### Where R10's form comes from

R10's `shcasm.exe` writes literal pools in one function, at 0x417780. It prints the label, then the word list. Then, if longs follow and its count at 0x42a400 is not a multiple of 4, it adds exactly 2 and prints `.RES.W 1`. Then it prints the long list. Nothing else is printed between the label and the longs. A second block, at 0x40a8b0, pads a table that follows a pool the same way. Neither prints `.ALIGN` or reserves more than 2 bytes. R32's and 5.1 R13's writers, read in full, keep the same order and add only the `.ALIGN 32` of `-extra=a=800` before the label. The other releases have the same structure.

So the pool's own lines record R10's count. With the count at the label `e` (mod 4) and `w` words, R10 pads when `e + 2w` is 2 mod 4. The bytes ahead of the first long, `2w` plus 2 for a pad, are then `e` mod 4 whether it padded or not. The items ahead of the first `.DATA.L` take 2 bytes mod 4 exactly when R10 counted the label at a half word.

The pool writer prints the label in SHCASM's field 0, at column 0, and then a blank in field 3, the comment field, at column 33 (0x4177d4 to 0x4177e4), before ending the line. A literal pool's label line is therefore `L`, a decimal number and a colon, then spaces through column 33: 34 bytes. The items follow in SHCASM's columns, the directive at 10 and the operand at 22. All 435 such labels in the 71 SHC texts built here have that form. In every switch table of those texts, the table writer's label has no blank after the colon.

#### How the two lines make the target's heads

With both lines, `njControl_`'s pool becomes the result shown under What it does. asmsh resolves the alignment where the code really ends, a NOP after a half word and nothing after a long boundary: here a NOP at 0x76. The reservation puts the label at R10's count, two bytes past the boundary when R10 counted 2, here 0x7a, and R10's own pad then puts the longs on a boundary, at 0x7c. No reference moves.

#### Where it applies, and where it must not

The patch stands alone on `njControl_`. On the three model members it goes with drop-inline-align. Alone it leaves SHC's NOPs after inline text, and the objects differ from the members'.

On the four members it writes `.ALIGN 4` before all 370 labels, 166 of which become a NOP, and `.RES.W 1` at 154 pools: `njControl_` 1, `njModelE_` 9, `njModelS_` 60, `njModel_` 84. With drop-inline-align on the model members, the results assemble to the objects the builds keep, equal in every byte but asmsh's timestamps, under asmsh 4.0 of both dates, 4.1B and 3.7, run as `asmsh x.src -object=M.obj -cpu=sh4 -endian=little -sjis -private` (`tools/objects.py`).

So does the R10 text of `njControl.c` that a second, independent build compiles, against the object that build keeps. 5.0A, which writes ELF, places every line where 4.0 does. Writing the pad after the label instead gives the same objects, but every reference into a padded pool then has to move by 2: 1,793 of them in the four members.

Each part is needed, and each follows its own position: the alignment where the code ends, the reservation where R10 counted. After inline text the two differ, at 114 of the 370 pools. Without the patch the four members lose all 194 functions. The alignment without the pad leaves R10's own `.RES.W 1` putting longs off their boundary (error 452), or, with that pad dropped too, starts every long-first pool R10 counted at 2 four bytes early. The pad without the alignment fails the same way. `.RES.W 1` then `.ALIGN 4` writes a NOP where the target has nothing.

**R10's own members.** 250 of the 1.20J members that R10 builds from C come out equal to the target from R10's text unchanged, with 678 pools in their code sections. 339 of the pool labels follow code that ends on a half word, in 128 members. The patch must not be applied to them. It leaves the bytes of the 122 members without such a pool as they are and breaks all 128 that have one: in 127 the bytes differ, and in `njLoadTexturePvmFile` asmsh refuses a displacement (error 402).

Among the inline-text members, `njLtSrcs_`, `njTexLoad_`, `syCache_` and `syIntCtrl_` hold 16 pools that start with words and that R10 counted at a half word. At the 9 of them measured, the targets hold R10's form, the first word right where the code ends.

Over the other 67 `-code=asmcode` texts built here, the inline-text members, four members without inline text and one unit from outside the SDK, the patch changes 8, leaves 56 as they are, and refuses `njLtSrcs_`, `njTexLoad_` and `syCache_`, each for a pool of words with no long.

**The rest of the libraries.** In every member of the 1.20J `ninja`, `shinobi`, `sofdec`, `sg_mw`, `sg_mwwav` and `sh4nlfzz` libraries, unwritten runs that start on a long boundary inside code, the pad's form, occur only in `njModel_`, `njModelS_`, `njModelE_`, `njModelF_` and `njControl_`, and in two places that aren't pool heads: a reserve between a table and code in `syIntIsr_`, and buffers in `mpvadec`.

**`njModelF_`** is compiled by none of the builds here, so it tests the patch apart from the four. Read from its bytes alone, none of its pool heads is in R10's form, and all are in the forms the patch writes. 20 of its 56 word-first pools carry the two unwritten bytes, and 58 of its long-first heads hold four.

#### What it rests on

- **The producer.** One reading fits every head: a writer that kept R10's pool layout and count, and wrote `.ALIGN 4` and, at a half-word count, `.RES.W 1` before each label without counting them, as if R10's pad block ran once more ahead of the pool. A 5.0 release before R10 could be that writer ([why](#what-it-rests-on)).
- **`njModelF_`**'s heads have the patch's forms, but its pads can be checked against R10's count only once C for it builds with R10.
- **`njPtclPolygon_`**'s one pool after plain compiled code, after `JMP @R1` and `LDS.L @R15+,PR`, holds R10's form: the code ends at 0x2e, two unwritten bytes, the first long at 0x30. The patch would write a NOP and four bytes there. Its other pools are forms R10 writes too, so by its pools it is R10's. Its C, built with R10, would settle it.
- **Pools without a long.** The patch refuses them: their lines carry no record of R10's count. None occurs in the four members, `njModelF_` or `njPtclPolygon_`.

### What would refute it

- A pool in `njModel_`, `njModelS_`, `njModelE_`, `njModelF_` or `njControl_` with R10's head: two unwritten bytes right after code that ends on a half word, or a first word right where such code ends.
- Once C for `njModelF_` builds with R10, a pool whose reservation disagrees with R10's pads there.
- A release, option or assembler that writes `.ALIGN 4`, a NOP or four unwritten bytes before a compiler's pool, or reserves ahead of a pool's words.
- A member whose target holds the reservation and that one of the toolchains above builds unchanged.
- A written head, a NOP or `0000`, where the reservation stands.

## use-assembler-pools

use-assembler-pools hands SHC's literal pools to asmsh, which then writes the literals itself.

- **For:** SHC 5.0 R10 text of the [fifteen motion members](#what-the-target-holds-2) of the Katana SDK 1.20J `ninja.lib`, whose literals sit as asmsh writes them.
- **With:** [drop-r0-move-nop](#drop-r0-move-nop), after this patch. On `njMotSrc_`, `njMotionE_` and `njMotLinkSrc_`, [drop-inline-align](#drop-inline-align), first. On `njMotLinkSrc_`, [fill-call-slot](#fill-call-slot), last. Never with [align-pool](#align-pool).
- **Not for:** a member whose target holds SHC's own pools: the patch adds a `BRA` and a `NOP` to each pool, and asmsh lays every literal out anew.

### What it does

The patch takes a [literal pool](#terms) whose items are `.DATA.W x`, `.RES.W 1` and `.DATA.L x`, in SHCASM's columns, with one operand and no comment, in any order. A `.RES.W 1` is a pad, and the others are values, of 2 and 4 bytes. A load or `MOVA` of `label+n` reads or addresses the item that starts n bytes past the label, pads counted.

- **Loads.** Each load from a pool, a `MOV.W` or `MOV.L` of the pool's label or `label+n` to a register, becomes the same load of `#` and the value of the item it reads. It keeps its size, its register and every byte but its first operand's, and its immediate is the item's own expression, symbol and addend included, so it reads the same value.
- **MOVA.** A `MOVA` of a pool's long to R0, whose address the code reads only as that long, becomes `MOVA` of `#` and the long's value: asmsh writes the value into its pool and takes that literal's address.
- **Pools.** The pool's label and items give way to `.ALIGN 4` and `.POOL`.
- **.NOPOOL** goes right after the delay slot of each `RTS`, `RTE`, `BRA`, `BRAF` and `JMP` that stands between such a load or `MOVA` and the first `.POOL` below it, past the slot's continuations.
- **Lines written.** A rewritten load's comment keeps its column, or follows the operands directly where they reach it, as SHCASM writes one. The lines the patch writes are in SHCASM's columns and end as the slot's or the label's line does, CR LF in SHC's text.

A schematic example (SHCASM pads the `L10:` line with spaces to 34 bytes, which isn't shown):

```
          MOV.L       L10+4,R1   ; _buffer+12
          MOV.W       L10,R2     ; H'0080
          RTS
          NOP
L10:
          .DATA.W     H'0080
          .RES.W      1
          .DATA.L     _buffer+12
```

becomes

```
          MOV.L       #_buffer+12,R1; _buffer+12
          MOV.W       #H'0080,R2 ; H'0080
          RTS
          NOP
          .NOPOOL
          .ALIGN      4
          .POOL
```

R10 loads a float constant through its address, which it takes with `MOVA`:

```
          MOVA        L10,R0
          FMOV.S      @R0,FR3
          ...
L10:
          .DATA.L     H'3727C5AC
```

becomes

```
          MOVA        #H'3727C5AC,R0
          FMOV.S      @R0,FR3
          ...
          .NOPOOL
          .ALIGN      4
          .POOL
```

A `MOVA` takes an item's address, which asmsh's pool moves. So the patch rewrites one only where each read of that address is a read of the long's four bytes: each read then gets the same value at another address, and asmsh's pool decides what stands beside it.

**How it checks a MOVA.** The patch follows control from each SHCASM function's label line, which carries `; function:` and the name, through branches to the text's labels and each delay slot. It keeps FPSCR.SZ, 0 at the entry under SHC's calling interface, and whether R0 may hold such an address. Until R0 is written, each path may read the address only as the long's four bytes: `FMOV` of `@R0` to an FR register where SZ is 0, or `MOV.L` of `@R0`. A call, a return or a jump leaves the address unread: no C function takes an argument or returns a value that is a pool's address, and a call's result replaces it.

**Why .NOPOOL goes there.** asmsh keeps a `#` load's literal until the first place below the load where it writes a pool: right after the slot of `RTS`, `RTE`, `BRA`, `BRAF` or `JMP`, past blanks and comments, unless the next statement there is `.NOPOOL`, or at `.POOL`. It writes none after the slot of any other branch, or after data in a slot, and a label after a slot ends the place: it writes the pool before the label. A `.NOPOOL` where no literal waits keeps nothing for later. asmsh keeps a `MOVA` of `#x`'s literal as it keeps a `MOV.L`'s, and the `MOVA` takes the literal's address.

**Which .POOL.** A literal goes to the first `.POOL` below its load, which can stand above the pool SHC put it in, and asmsh refuses one out of its load's reach. When the loads of two pools all stand above the first, its `.POOL` takes every literal, nothing waits at the second pool's slot, and the patch writes no `.NOPOOL` there.

The patch changes the bytes the object holds: asmsh lays each pool out, not SHC. A text without pools comes out as it went in, whatever else it holds.

**With the other patches.** [drop-inline-align](#drop-inline-align) runs first, so SHCASM's `.ALIGN 4` between a slot and a pool is gone when this patch reads the pool, and the `.ALIGN 4` this patch writes stays. drop-r0-move-nop runs after it.

For an exact SHC text, use the samples `make test` writes:

```sh
make test
build/test/shckit apply -p use-assembler-pools -o x.src build/test/sample.pools
cmp x.src build/test/sample.pooled
```

### What it refuses

The patch refuses, naming the line, a text with a pool and any of these.

Lines asmsh pools itself:

- `a .POOL, a .NOPOOL or a load asmsh writes a literal for`: a `.POOL`, a `.NOPOOL`, or a `MOV.W`, `MOV.L` or `MOVA` of `#x`. asmsh pools those already, and the patch's `.NOPOOL` lines would carry an inline body's literals to a pool not its own. `MOV #x`, an 8-bit immediate, is no load from a pool.

Branches and slots, by which the patch places `.NOPOOL` and asmsh writes a pool:

- `an instruction the opcode table doesn't hold`: one the [opcode table](opcodes.md) doesn't hold could be a delayed branch.
- `a delayed branch whose slot shckit can't place`: the slot has to be the next statement of the branch's section, past labels, blanks, comments and `.EXPORT`, `.IMPORT`, `.EQU` and `.ASSIGN` lines, and an instruction legal in a slot, not data, a `.SECTION` or nothing. An `.ALIGN` can't stand there either: whether it writes a `NOP` into the slot depends on the address.
- `a label on a slot or above it, below its branch, where a .NOPOOL is due`: a label on the slot of a branch that never falls through, or between the two, where the patch writes `.NOPOOL` after the slot. asmsh refuses the `.NOPOOL` there with error 521. The label can stand alone, on the slot, or on an `.EQU` or `.ASSIGN` line, and a label on the branch is fine.

The pool:

- `a pool item Lnn-Lnn, as a switch table's entries are`: a switch table, as the table writer prints its entries.
- `a pool's label line isn't the one SHCASM's pool writer prints`.
- `a pool item SHCASM's pool writer doesn't print`: such an item, or a value asmsh can't write as an immediate: one that names a register or holds `@` or `#`, one that holds `$`, which asmsh reads as where the item stands, and one that names a symbol `.ASSIGN` defines, which can hold another value at the load.
- `a pool's label is defined twice`.
- `a ?label in the block a pool's label opens`: a `?label` defined or named there, up to the next `.SECTION` or name asmsh defines on a line by itself, an instruction or data. Dropping the label ends the block, and the block before could give the `?label` another definition, whether a pool's item or a later branch names it.
- `a pool's label that isn't right below the slot of a branch that never falls through`: the slot of `RTS`, `RTE`, `BRA`, `BRAF` or `JMP`, past blanks and comments. Otherwise control could fall into the pool, and asmsh's branch past the pool would run, or a label could name its address.
- `a pool that the BRA above it branches over`: to a label right below the pool. That is SHC's own guard, which asmsh's would double.

Uses of the pool:

- `a pool's label used other than by a load or MOVA above it`: any use but as the first operand of a `MOV.W` or `MOV.L` to a register, or of a `MOVA` to R0, written `label` or `label+n` (n in decimal, no leading zero), above the pool and below the same `.SECTION` line. That rules out branches, data, exports, `.EQU` and `.ASSIGN`, a load written any other way, and loads below the pool or past a `.SECTION`.
- `a load or MOVA of no whole item of its size`: a load of a pad, of part of an item, past the pool, or of an item of the other size, and a `MOVA` of no whole long.
- `a pool item's address used other than to read the item`:
  - a `MOVA` of a long whose address a path may use other than as the long's four bytes: a copy, an offset, a store, a read past it or beside it, an `FMOV` where SZ may be 1, a jump through R0, a write to SR, which can switch R0's bank, `RTE` or `TRAPA`;
  - a `MOVA` that no SHCASM function's entry reaches, as nothing gives its SZ;
  - in a text with a `MOVA` of a long, whatever R0 holds, a path the patch can't follow, as R0 and SZ would be lost with it: control running into data, a `.SECTION` or `.END`, or into a delay slot other than as its branch's, past the lines that may stand between a slot and its branch; a branch to a name `.EQU` or `.ASSIGN` defines, or to an expression that names a label of the text; and a `BSR` to a label of the text but an SHCASM function's.
- `a pool item no load or MOVA uses`: a value, or a pad in a pool no load or `MOVA` uses. Nothing shows such an item's role.

Offsets:

- `an operand that offsets a code label, $ or PC`: an [operand that offsets a place in code](#terms). asmsh's pool takes other bytes than SHC's, so the patch can't tell whether such an operand points past a pool. A pool's label is not a code label here.

### Evidence

#### Why

Fifteen members of the Katana SDK 1.20J `ninja.lib`, its motion members, hold their literals as asmsh writes a `.POOL`, and no SHC release [tried](#evidence-base) writes that form from C.

#### What the target holds

Every literal the fifteen members' code loads sits in a pool behind a `BRA` past it, with a `NOP` in its slot, right after a return or a jump, its slot and at most one `NOP`, where control doesn't fall.

| member | literals | pools |
|---|---:|---:|
| `njCameraMotion_` | 34 | 3 |
| `njGetMotion_` | 31 | 2 |
| `njLightMotion_` | 39 | 6 |
| `njMotInt_` | 46 | 4 |
| `njMotIntOld_` | 20 | 2 |
| `njMotLink_` | 99 | 5 |
| `njMotLinkE_` | 116 | 8 |
| `njMotLinkF_` | 73 | 4 |
| `njMotLinkS_` | 72 | 4 |
| `njMotLinkSrc_` | 89 | 10 |
| `njMotSrc_` | 41 | 7 |
| `njMotion_` | 82 | 4 |
| `njMotionE_` | 72 | 6 |
| `njMotionF_` | 82 | 4 |
| `njMotionS_` | 81 | 4 |

No other member of the library holds all its literals so. The fifteen hold no `NOP` after a copy into R0 either, the other half of their form ([drop-r0-move-nop](#drop-r0-move-nop)).

Four of the fifteen, `njMotionE_`, `njCameraMotion_`, `njMotSrc_` and `njMotLinkSrc_`, take a float constant's address in such a pool with `MOVA` and read it through R0 with `FMOV.S`. R10 writes `MOVA` of its own pool's long for such a constant, and the same read.

#### How asmsh writes a .POOL

At `.POOL`, asmsh writes a `BRA` past the pool with a `NOP` in its slot, then the words, a `0000` where the first long would fall on a half word, and the longs, each value once and in the order the loads first use it. The `.ALIGN 4` before it writes a `NOP` where the code ends on a half word. So each pool comes out where SHC put its own, after a return or a jump and behind a branch nothing reaches, in asmsh's layout.

#### What the compilers write

Under the [1.20J ninja flags](#evidence-base), every release tried but 6.0 AD, which compiles nothing, writes the C sources for `njMotLinkS_`, `njMotionE_`, `njCameraMotion_` and `njMotSrc_` (below) with compiler pools, not one `#` load, `.POOL` or `.NOPOOL`.

#### Where it applies, and where it must not

The patch is for R10's text of a member whose target holds asmsh's pools. It must not be applied to one whose target holds SHC's own: it adds a `BRA` and a `NOP` to each pool, and asmsh lays every literal out anew.

**`njMotLinkS_`** has eleven C functions and four pools. A C source for it, compiled by R10 with the 1.20J ninja flags, comes out as R10's pools: R10's text assembles to 1812 bytes of code, against the member's 1824. Through this patch and drop-r0-move-nop it assembles to the member's object, every byte but asmsh's timestamps, relocations and external records included. Through this patch alone it assembles to 1828 bytes of code: the member's and the two `NOP` lines that drop-r0-move-nop drops.

**The MOVA members.** R10's text of C sources for `njMotionE_`, `njCameraMotion_` and `njMotSrc_` holds 1, 4 and 18 `MOVA` lines of a float constant's long. Through this patch and drop-r0-move-nop each text assembles, and each `MOVA` addresses a long that holds its item's value. With [drop-inline-align](#where-it-applies-and-where-it-must-not) before them, the texts for `njMotionE_` and `njMotSrc_` assemble to the members' objects. The source for `njCameraMotion_` differs from the member elsewhere, so its object isn't compared whole.

### What would refute it

- A pool in one of the fifteen members that no such `BRA` precedes, or a literal that sits where asmsh's first-use order doesn't put it.
- A release or an option that writes `#` loads, `.POOL` or `.NOPOOL` for C.
- A member among the fifteen whose code R10 writes, with whose text this patch and drop-r0-move-nop make other bytes than the member's.
- A `MOVA` in one of the fifteen members that addresses anything but one long of asmsh's pool, or whose address the code reads otherwise.

## drop-r0-move-nop

drop-r0-move-nop drops the `NOP` that SHC writes after a copy into R0 outside a delay slot under `-extra=a=400`. The option works around a bug of the SH-4 chip that Hitachi's SHC 5.0 R07 release note (28 March 1998) gives: a `MOV Rm,R0` that sets R0 again, directly followed by an instruction that refers to R0 implicitly, as `MOV @(R0,R6),R3` does. The note says SHC writes the `NOP` "after all the MOV Rm, R0, for safety". The patch deletes an instruction from code SHC chose for the processor, and nothing in the text says the code runs correctly without it: it is for making the bytes of members that hold the code without it.

- **For:** SHC 5.0 R10 text of the [fifteen motion members](#what-the-target-holds-2) of the Katana SDK 1.20J `ninja.lib`.
- **With:** [use-assembler-pools](#use-assembler-pools), before this patch. On `njMotSrc_`, `njMotionE_` and `njMotLinkSrc_`, [drop-inline-align](#drop-inline-align), first. On `njMotLinkSrc_`, [fill-call-slot](#fill-call-slot), after this patch.
- **Not for:** a member whose target holds the `NOP`.

### What it does

In a code section, the patch drops each line that is SHCASM's `NOP`, ten spaces and `NOP`, right below SHCASM's copy of a register into R0, `MOV Rm,R0` in its columns with m from 1 to 15, where the two were adjacent in the original text and the copy is in no delay slot. It changes no other line.

<!-- example drop-r0-move-nop: input x.src -->
```
          MOV         R5,R0
          NOP
          CMP/EQ      #1,R0
```

<!-- example drop-r0-move-nop: run -->
```sh
build/shckit apply -p drop-r0-move-nop -o out.src x.src
```

<!-- example drop-r0-move-nop: result out.src -->
```
          MOV         R5,R0
          CMP/EQ      #1,R0
```

The two lines' form is all the text shows of the `NOP` lines `-extra=a=400` writes. These stay as they are:

- a copy in a delay slot, after which SHC writes no `NOP`;
- a `NOP` with a label or a comment, or with any line between it and the copy: a blank, a comment, a label, an `.ALIGN`;
- a copy or a `NOP` written otherwise than SHCASM writes it: in other columns, as inline text keeps its programmer's, or as `MOV.L Rm,R0`, which asmsh assembles to the same copy;
- any other move: `MOV #imm,R0`, `MOV.L @Rm,R0`, `MOV Rm,Rn`, `MOV R0,R0`.

**With the other patches.** The patch runs after drop-inline-align and align-pool or [use-assembler-pools](#use-assembler-pools), and before [fill-call-slot](#fill-call-slot). It reads each pair by the original line numbers, so a copy and a `NOP` that an earlier patch brings together stay: one with SHCASM's `.ALIGN 4` between them keeps its `NOP` when [drop-inline-align](#drop-inline-align) drops the alignment.

For a whole text in SHC's form, use the samples `make test` writes:

```sh
make test
build/test/shckit apply -p use-assembler-pools -p drop-r0-move-nop -o x.src build/test/sample.pools
cmp x.src build/test/sample.moved
```

### What it refuses

The patch refuses, naming the line:

- `a delayed branch whose slot shckit can't place`, at the branch:
  - a copy into R0 that may be the slot of a delayed branch above it: one after a delayed branch and an `.ALIGN`, a `.POOL` or a `.NOPOOL`, which may put a `NOP` in the slot depending on the address;
  - once it drops a `NOP`, an `.ALIGN`, a `.POOL` or a `.NOPOOL` below it in its section where a delay slot is due, or may be. Whether asmsh writes a `NOP` into the slot there depends on the address, which dropping the `NOP` can change.
- `an instruction the opcode table doesn't hold`, at that instruction: one the [opcode table](opcodes.md) doesn't hold that is in no slot, which may be a delayed branch, with no data and no instruction that can't be a delayed branch between it and
  - a copy into R0 whose `NOP` the patch would drop;
  - an `.ALIGN`, a `.POOL` or a `.NOPOOL` below a `NOP` the patch drops, in its section.
- `an operand that offsets a code label, $ or PC`, once it drops a `NOP`: an [operand that offsets a place in code](#terms), since the patch can't tell whether it points past a dropped `NOP`.

### Evidence

#### Why

The fifteen motion members of the Katana SDK 1.20J `ninja.lib` that hold their literals in asmsh's pools ([use-assembler-pools](#use-assembler-pools)) hold no `NOP` after any of their 63 copies into R0.

Under `-extra=a=400` R10 writes one after each of its own copies outside a slot, but for some copies from R15 ([below](#what-it-rests-on-2)), and none without the option. For `njMotLinkS_`'s C under the [1.20J ninja flags](#evidence-base), `-extra=a=400` among them, every release tried but 6.0 AD writes a `NOP` after every copy into R0 outside a slot, two or three of them. In a slot R10 writes none: in [the sweeps](inspect.md#the-sweeps)' `-code=asmcode` texts under the option, none of R10's 217 copies into R0 in a delay slot has a `NOP` after it, nor do any of the 131 in its `-code=machinecode` listings.

**Not compiling without the option.** Dropping the `NOP` after compiling is not compiling without `-extra=a=400`. Without the option SHC writes no such `NOP`, but it also orders the code otherwise: R10's text of `njMotLinkS_`'s C without the option holds the instructions of its text with the option, the two `NOP` lines dropped, with 26 of them in other places.

R10's text of that C, through use-assembler-pools and this patch, assembles to the member's object ([use-assembler-pools](#use-assembler-pools) gives the build). Through use-assembler-pools alone the code is 4 bytes longer: the two `NOP` lines after `MOV R4,R0`.

**The option's order.** In `njMotSrc_`'s `shape_cubic_keys`, R10's text of the member's C without the option puts `MOV R4,R0` in the slot of a `BT/S`. With the option, R10 takes the copy out of the slot, writes it above the compare with a `NOP` after it, and branches with `BT`. The R07 note gives that change for a copy in a conditional branch's slot: its Japanese text puts the `NOP` after the copy, as R10 does, and its English text after the branch. R10 also writes a `NOP` after the `MOV R14,R0` that follows the branch. The member holds that order without either `NOP`, at PSG+0x544 to 0x550: `MOV R4,R0`, `MOV R5,R3`, `ADD #1,R3`, `CMP/HS R6,R3`, `BT`, `MOV R14,R0`, `ADD #8,R0`. Through drop-inline-align, use-assembler-pools and this patch, R10's text with the option assembles to the member ([drop-inline-align](#where-it-applies-and-where-it-must-not) gives the build).

**Copies the option leaves in a slot.** The option doesn't take every copy out of a conditional branch's slot. SHCPEP's delay pass first offers the slot the record above the branch, or above its compare, and under the option its filler refuses a copy into R0 offered that way (0x4173c5 to 0x41743a). The copy then stays above with its `NOP`, as in `shape_cubic_keys`. When the record above conflicts with what follows it, or there is none, the forward filler can take the first record past the place both paths join, and doesn't test the option: a copy it takes stays in the slot with no `NOP` ([fill-call-slot](#where-r10s-form-comes-from-1) gives both fillers). In R10's `-code=asmcode` texts in the sweeps under the option, all 11 copies left in a conditional branch's slot came that way, and with the test removed from a copy of SHCPEP, the 14 copies the option takes out stay in their slots. The text doesn't show which filler filled a slot, so only R10's text of a member's C tells whether the option takes its copy out.

#### Where it applies, and where it must not

The patch must not be applied to a member whose target holds the `NOP`. Elsewhere in the 1.20J `ninja.lib`, 1331 of 1511 copies into R0 are followed by one.

#### What it rests on

- **Copies from R15.** SHCASM writes the `NOP` after a `MOV R15,R0` that copies R15 itself, and the patch drops it. It writes a stack object's address at a nonzero offset from -128 to 127 as `MOV R15,Rn` and `ADD #d,Rn`, by a path that doesn't read the option (0x40e29b to 0x40e359), so no `NOP` follows. None of the fifteen members holds a copy from R15, so their bytes don't show what their producer wrote there.
- **What follows the dropped `NOP`s.** In R10's text of a C source for each of the eight of the fifteen members that have one here, the patch drops 40 `NOP`s. None stands before an instruction that refers to R0 implicitly: 23 stand before `SHLL2 R0` or `ADD #imm,R0`, which name it, and 17 before code that doesn't use R0. By the R07 note's conditions, then, the bug can't occur at those sites of the texts. Of the sources, the four for `njMotLinkS_`, `njMotionE_`, `njMotLinkSrc_` and `njMotSrc_` build their members; the others aren't compared whole. The other seven members, `njLightMotion_`, `njMotInt_`, `njMotIntOld_`, `njMotLink_`, `njMotLinkE_`, `njMotLinkF_` and `njMotionF_`, have no C source here, so nothing is measured for them.

### What would refute it

- A `NOP` after a copy into R0 in one of the fifteen members.
- A copy into R0 in a `BT/S` or `BF/S` slot of one of the fifteen members, where R10's text of its C under the option takes the copy out of the slot.
- A member among them whose code R10 writes, with whose text this patch and use-assembler-pools make other bytes than the member's.

## fill-call-slot

fill-call-slot drops SHCASM's `NOP` from a call's delay slot, so the add below the call fills the slot. It moves an instruction across a call in code SHC chose, and nothing in the text says the code runs the same: the patch is for making the bytes of the one member whose two calls hold the add in the slot.

- **For:** SHC 5.0 R10 text of the Katana SDK 1.20J `ninja.lib` member `njMotLinkSrc_`, and no other text.
- **With:** [drop-inline-align](#drop-inline-align), [use-assembler-pools](#use-assembler-pools) and [drop-r0-move-nop](#drop-r0-move-nop), before this patch, as the member's text takes them. It runs last.
- **Not for:** any other member. `njMotSrc_`, `njMotLinkE_`, `njMotExpand_`, `njCnkShapeSrc_` and `njCnkShapeLinkSrc_` keep the `NOP` at the same kind of site. Each site is listed [below](#where-it-applies-and-where-it-must-not-4).

### What it does

In a code section, the patch drops each line that is SHCASM's `NOP`, ten spaces and `NOP`, between two other lines in SHCASM's columns, the three adjacent in the original text:

- **The call:** `JSR @Rm`, m from 0 to 15 without a leading zero, in no delay slot.
- **The slot:** the `NOP`, which is the call's delay slot.
- **The add:** `ADD #imm,Rn`, the immediate in decimal from -128 to 127 without a leading zero and not `-0`, n from 8 to 14 and not m, with no continuation below it.

The add is then the call's slot. The patch changes no other line.

<!-- example fill-call-slot: input x.src -->
```
          JSR         @R3
          NOP
          ADD         #8,R13
          MOV.L       R13,@R14
```

<!-- example fill-call-slot: run -->
```sh
build/shckit apply -p fill-call-slot -o out.src x.src
```

<!-- example fill-call-slot: result out.src -->
```
          JSR         @R3
          ADD         #8,R13
          MOV.L       R13,@R14
```

**The class it admits.** `JSR @Rm` and `ADD #imm` to R8 to R14 are the subset the member holds: the two sites it fills are both of that form. They are not every call, or every instruction the move would keep correct. `BSR`, `BSRF` and any other call, and any other instruction below the `NOP`, are not admitted, since no site tests them.

**Why the move keeps what the code computes.** A call's slot runs before the callee, so the add moves from after the call to before it, and what follows the add still runs after the call. `ADD #imm` writes Rn alone, touching neither T nor memory, and Rn isn't Rm, which holds the call's target. Under SHC's calling interface a callee takes no argument in R8 to R14 and returns them as it got them, so it neither reads the add's result nor returns with Rn changed: only its save of Rn, if it makes one, sees the new value. That holds for any callee that keeps the interface, as a C function does.

These stay as they are:

- a slot that holds anything but SHCASM's `NOP`;
- an add of another form or to another register: `ADD Rm,Rn`; `ADD #imm` to R0 to R7, which a callee may read or change, or to R15, which moves the callee's stack arguments; to the call's own register, as in `JSR @R13` and `ADD #8,R13`; or with an immediate in hex, with a `+`, a leading zero or `-0`, or outside -128 to 127;
- the slot of another call or branch: `BSR`, `BSRF`, `JMP`, `BRA`;
- a call, `NOP` or add written otherwise than SHCASM writes it: in other columns, as inline text keeps its programmer's, in lower case, with a label or a comment, or with a continuation below the add;
- any line between the three: a blank, a comment, a label, an `.ALIGN`. A label there is a join other branches reach, as at four calls in `njModelS_`;
- a call in a delay slot, which opens no slot of its own.

**With the other patches.** The patch runs last. It reads the three lines by their original line numbers, so lines that an earlier patch brings together stay: a call and a `NOP` with SHCASM's `.ALIGN 4` between them keep the `NOP` when [drop-inline-align](#drop-inline-align) drops the alignment.

For a whole text in SHC's form, use the samples `make test` writes:

```sh
make test
build/test/shckit apply -p use-assembler-pools -p drop-r0-move-nop -p fill-call-slot -o x.src build/test/sample.shapes
cmp x.src build/test/sample.filled
```

### What it refuses

The patch refuses what [drop-r0-move-nop](#drop-r0-move-nop) refuses, naming the line:

- `a delayed branch whose slot shckit can't place`, at the branch:
  - such a call that may be the slot of a delayed branch above it: one after a delayed branch and an `.ALIGN`, a `.POOL` or a `.NOPOOL`, which may put a `NOP` in the slot depending on the address;
  - once it drops a `NOP`, an `.ALIGN`, a `.POOL` or a `.NOPOOL` below it in its section where a delay slot is due, or may be. Whether asmsh writes a `NOP` into the slot there depends on the address, which dropping the `NOP` can change.
- `an instruction the opcode table doesn't hold`, at that instruction: one the [opcode table](opcodes.md) doesn't hold that is in no slot, which may be a delayed branch, with no data and no instruction that can't be a delayed branch between it and
  - such a call whose `NOP` the patch would drop;
  - an `.ALIGN`, a `.POOL` or a `.NOPOOL` below a `NOP` the patch drops, in its section.
- `an operand that offsets a code label, $ or PC`, once it drops a `NOP`: an [operand that offsets a place in code](#terms), since the patch can't tell whether it points past a dropped `NOP`.

### Evidence

#### Why

`njMotLinkSrc_` holds 67 calls. Its two shape helpers each call the current shape callback, then store the record pointer advanced by one record. Both hold the add in the call's slot:

- `shape_one_channel`: `JSR @R3; ADD #8,R13` at PSG+0x16f6, then `MOV.L R13,@R14`;
- `shape_two_channels`: `JSR @R3; ADD #16,R13` at PSG+0x1734, then the same store.

R10 writes the member's other 65 calls for its C, and doesn't write these two.

#### What R10 writes

C for the member that stores `record + 1` after the call, as its four motion helpers store theirs, compiled by R10 with the [1.20J ninja flags](#evidence-base), writes `JSR @R3`, `NOP`, `ADD #8,R13` and `JSR @R3`, `NOP`, `ADD #16,R13` there. They are the only two of the text's 67 calls with `NOP` in the slot.

- Through drop-inline-align, use-assembler-pools and drop-r0-move-nop, the text assembles to 7,496 bytes of code: the member's 7,492 and the two `NOP` lines.
- Through those and this patch, both shape helpers are the member's words at the member's addresses, and the code differs from the member only in 4 of `njMakeMotionLink`'s 274 words.
- With `njMakeMotionLink`'s C changed to give those 4 words, the text assembles to the member's object: every byte but asmsh's timestamps is the member's.

C that adds before the call, `record++` ahead of it, comes out of R10 with the add in each slot. Through the three patches it assembles to the member's length, and `shape_one_channel` to the member's words, but `shape_two_channels` differs from the member in 9 of its 31 words, which R10 orders otherwise.

For the C that stores after the call, R10 under `-speed`, and R26, R28, R31, R32 and 5.1 R01, each write `NOP` in both slots too.

#### Where R10's form comes from

In R10, SHCPEP chooses what fills a delay slot, and SHCASM writes the text:

- SHCPEP's delay pass, at 0x4161c0, fills a call's slot only from the records before the call. Its filler, at 0x416cd0, walks back from the call and flags the first record that doesn't conflict with it. A record after the call is never a candidate.
- Its forward filler, at 0x4178f0, takes a record from after a branch, but returns at once unless the branch is `BT` or `BF` (0x417948 to 0x41794e).
- SHCASM writes `NOP` in a slot that no flagged record fills (0x414c4d).
- Both count a record flagged for a slot as 0 bytes: SHCPEP at 0x41bb6f and SHCASM at 0x41ac2f. So with the add in the slot, R10 counts each helper 2 bytes shorter, and lays out and schedules the code after it by that count.

A GDB trace of SHCPEP on the C that stores after the call finds no candidate at either call: each of the 21 records before it conflicts. In R10's text, then, the add in a call's slot always comes with the shorter count, and the member's code after each helper is what R10 writes for the count with the `NOP`.

#### Where it applies, and where it must not

The 1.20J `ninja.lib`, read from its bytes, holds four calls with an add of an immediate to R8 to R14 in the slot, and fifteen with `NOP` in the slot and such an add right below it:

| member | calls | slot |
|---|---|---|
| `njMotLinkSrc_` | `JSR @R3` at PSG+0x16f6 and 0x1734, adding 8 and 16 to R13 | the add |
| `njModelS_` | `JSR @R3` at PSG+0x85b4 and 0xc58a, adding 4 to R11 | the add, which R10 writes for the member's C, from above the call |
| `njModelS_` | `JSR @R3` at PSG+0x7798, 0x96e6, 0xe8a6 and 0xf424, adding 1 to R14 | `NOP`, with a label between it and the add in R10's text |
| `njMotSrc_` | `JSR @R2` at PSG+0xeb8 and 0xee2, adding 8 and 16 to R13 | `NOP` |
| `njMotLinkE_` | `BSR` at PSG+0xcaa and 0xcde, adding 8 and 16 to R13 | `NOP` |
| `njMotExpand_` | `JSR @R2` at PSG+0x1be, adding 8 to R14 | `NOP` |
| `njCnkShapeSrc_` | `JSR @R2` at PSG+0x4d6, 0x500 and 0x52a, adding 8, 16 and 24 to R14 | `NOP` |
| `njCnkShapeLinkSrc_` | `JSR @R2` at PSG+0x312, 0x376 and 0x3da, adding 8, 16 and 24 to R13 | `NOP` |

The patch must not be applied to any of these but `njMotLinkSrc_`:

- **`njMotSrc_`** is a motion member like `njMotLinkSrc_`, assembled two seconds before it in both the 1.0B2 and the 1.20J library. Its shape helpers have the same C, and the same code but for the call's register and how it is loaded. R10's text of its C holds both calls as `JSR @R2`, `NOP`, `ADD`, and through drop-inline-align, use-assembler-pools and drop-r0-move-nop it assembles to its member. This patch drops both `NOP` lines and makes other bytes than the member's.
- **`njMotLinkE_`** calls by `BSR`, which the patch doesn't admit. Its `NOP` slots are the target's.
- **`njMotExpand_`, `njCnkShapeSrc_` and `njCnkShapeLinkSrc_`** hold the `NOP` in their targets, which the patch would drop.
- **`njModelS_`**'s four `NOP` slots stand above a label in R10's text, so the patch leaves them, and R10 writes its two filled slots itself.

`njMotLinkSrc_`'s code is the same in the Katana R1 1.0B2, 1.00J, 1.20J and 1.30J libraries. Its build in the Katana R4.01 library, from May 1998, is older, and its builds in the Katana R8, R9, R10 and R11b libraries and in 1.55J are newer. Each of those keeps `NOP` in both shape helpers' call slots and adds to R13 in the second or third instruction after it.

#### What it rests on

- **Who filled the slots.** The bytes don't show whether a tool or a hand filled them. Of every call slot in the twelve SH builds of the library read here, from Katana R4.01 to 2.00J, only these two, in four of the builds, hold an add whose register the instruction after the slot stores. The only other slots that add to R8 to R14 are `njModelS_`'s two, in eleven of the builds, and one in `njCnkCompile_` in the Katana R8 and R9 libraries, two of the builds: `ADD #32,R14` in the slot of `JSR @R1` at PSG+0x5de, whose register the instruction after the slot copies. `njMotSrc_` and `njMotLinkE_`, assembled in the same batches, keep the `NOP`. So the patch reproduces the effect of a late call-slot fill, keeping the compiler's earlier scheduling around it. How the original producer made the fill isn't established.
- **Its scope.** It is declared for one member, so no other text tests it. A rule for a family of members would break `njMotSrc_`.

### What would refute it

- A source for `njMotLinkSrc_` that R10 compiles, under the 1.20J ninja flags, to the member's two slots and the member's addresses for `shape_one_channel`, `shape_two_channels` and everything after them, without this patch.
- An SHC release or option that fills a call's slot with an instruction from after the call.
- A text of the member's C with which this patch and the three before it make other bytes than the member's, outside the functions whose C still differs.

## Evidence base

The patches' evidence draws on these:

- **Members and targets.** A member is an object in the SDK's library archive. Its *target* is the member's own bytes, which a build of its source has to reproduce.
- **Comparison.** Objects are compared with `tools/objects.py`, which normalizes asmsh's two timestamp fields.
- **Compilers.** SHC 5.0 R10, R26, R27, R28, R31 and R32, 5.1 R01, R03, R04, R08, R11 and R13, and 6.0 AD. 6.0 AD compiles nothing, and its SHCASM is 5.1 R13's.
- **R10's SHCASM and SHCPEP.** `shcasm.exe`, SHA-256 `2b101819b877c078216e81fd29abe16ddf6af5a6cd3aeb575357d28206ad8bcf`, and `shcpep.exe`, SHA-256 `025c946338bb01c81b5c18fc97000171586cf10dc8e1b699072a6aaa1ba4b779`. An address cited in either is a virtual address under their image base, 0x400000.
- **Assemblers.** Five asmsh builds: 4.0 ('98.07.06), 4.0 ('98.04.10), 4.1B, 5.0A and 3.7. 5.0A writes ELF, and the others SYSROF objects.
- **The 1.20J ninja flags.** `-code=asmcode -cpu=sh4 -division=cpu -endian=little -double=float -pic=0 -string=const -comment=nonest -sjis -section=p=PSG,c=C,d=DSG,b=BSG -optimize=1 -size -nodebug -macsave=0 -extra=a=400`.
- **The texts built here.** 71 `-code=asmcode` units: 70 SDK members and one unit from outside the SDK.
- **Not built.** No build here compiles `njModelF_`.
