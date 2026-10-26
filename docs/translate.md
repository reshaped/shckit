# translate

`shckit translate -o OUT IN` writes `IN`, an asmsh text, as GNU as text. For a text asmsh 4.0 ('98.07.06) assembles with `-cpu=sh4 -endian=little -sjis -private` and no error or warning, binutils 2.47's `sh-elf-as --isa=sh4 --little --no-pad-sections --fatal-warnings` assembles `OUT` to asmsh's bytes and relocations, or translate refuses the text, naming the line, or `sh-elf-as` fails. The only exceptions are the [limits](#limits), where the bytes can differ without a message. The first is `>>` and `/`, which GNU as computes in 64 bits where asmsh computes them in 32.

translate reads the text as [apply](patches.md#applying-them) does, to its first `.END`, and computes no address: GNU as lays the text out as asmsh does, and checks every reach. Each line comes out as a line, or as a few, in its order. When translate refuses a text, it writes nothing:

```
shckit: x.src:12: a literal still waiting at a .SECTION, at .END or at the text's end
```

For example, `x.src` holds one function:

<!-- example translate: input x.src -->
```
          .IMPORT     _counter
          .SECTION    P,CODE,ALIGN=4
_bump:                           ; add i to the counter, up to H'1000
          MOV.L       #_counter,R1
          MOV.L       @R1,R0
          ADD         R4,R0
          MOV.W       #H'1000,R2
          CMP/GT      R2,R0
          BF          ?keep
          MOV.W       #H'1000,R0
?keep:
          RTS
          MOV         R0,@R1
          .END
```

<!-- example translate: run -->
```sh
build/shckit translate -o x.s x.src
```

<!-- example translate: result x.s -->
```
          .extern     _counter
          .section    P,"ax",@progbits
          .balign     4
_bump:                           ! add i to the counter, up to H'1000
          MOV.L       .Llit0,R1
          MOV.L       @R1,R0
          ADD         R4,R0
          MOV.W       .Llit1,R2
          CMP/GT      R2,R0
          BF          .L_bump.keep
          MOV.W       .Llit1,R0
.L_bump.keep:
          RTS
          MOV.L       R0,@R1
.Llit1:   .short      0x1000
          .balign     4,0
.Llit0:   .long       _counter
          .end
```

asmsh writes the function's literals right after the slot of its `RTS`, the word first, and its two loads of `#H'1000` share one. GNU as has no `#` literal, so translate writes the pool there, and each load loads its literal's label. `?keep` holds in the block `_bump` opens, and its GNU name says so. `MOV R0,@R1` is `MOV.L` to GNU as, and `H'1000` is `0x1000`.

## Assembling and linking

Assemble the text with:

```sh
sh-elf-as --isa=sh4 --little --no-pad-sections --fatal-warnings -o x.o x.s
```

- `--isa=sh4` and `--little` are asmsh's `-cpu=sh4` and `-endian=little`.
- `--no-pad-sections` keeps GNU as from padding each section's end to its alignment, which asmsh doesn't.
- `--fatal-warnings` makes an error of what GNU as only warns about: a `BT` or `BF` past its reach, which asmsh refuses and GNU as turns into a longer sequence, and a value too wide for its field, which asmsh warns of and GNU as truncates.
- `--relax` changes what GNU as writes for a branch, so it stays off.

The object holds the text's sections under their own names. A link places them and binds each import, as Hitachi's linker does for asmsh's object. For each section `X` whose start or size the text takes, it also defines the names translate writes for `STARTOF X`, `SIZEOF X` and `(STARTOF X)+(SIZEOF X)`, as these lines of a GNU ld script do:

```
__startof_X = ADDR(X);
__sizeof_X = SIZEOF(X);
__endof_X = ADDR(X) + SIZEOF(X);
```

Each section name asmsh takes holds only letters, digits, `_` and `$`, so it can follow `__startof_` in a GNU symbol: asmsh refuses a name with a `.` or a `?` inside it, with error 202, and one that starts with `?`, a local label, with error 203.

## What it writes

| asmsh | GNU as |
|---|---|
| an instruction with a [size](opcodes.md#sizes) GNU as refuses on its form, as in `ADD.L R1,R2` and `FMOV.S FR1,FR2` | the same without it: 118 of the opcode table's instructions |
| an instruction without a size where GNU as wants one, as in `MOV @R4,R5` | the same with the size asmsh reads, `MOV.L @R4,R5`: 50 of them |
| any other instruction | the same |
| `MOV.W #x,Rn`, `MOV.L #x,Rn`, `MOVA #x,R0` | the load of a literal's label, in a [pool](#literal-pools) |
| `BRA` or `BSR` to an import, alone or plus or minus a number | `.reloc .,R_SH_IND12W,` and the target less 4, then `.short 0xA000` or `0xB000`: the branch, whose displacement counts from 4 bytes past it |
| `; text` | `! text` |
| `H'1F`, `B'101`, `Q'17`, `D'12`, `012` | `0x1F`, `0b101`, `017`, `12`, `12`: GNU as reads a leading zero as octal |
| `^x` | `~x`: asmsh reads a unary `^` as `~` |
| `?name` | `.L`, the label that opens its block, a dot and the name: `?out` in the block `_f` opens is `.L_f.out`. In a block the text's kth `.SECTION` line opens, it is `.L.k.out`, and above the first `.SECTION` line `.L.0.out` |
| `STARTOF X`, `SIZEOF X`, `(STARTOF X)+(SIZEOF X)` | `__startof_X`, `__sizeof_X`, `__endof_X` |
| `.SECTION X,CODE,ALIGN=n` | `.section X,"ax",@progbits`, then `.balign n` |
| `.SECTION X,DATA,ALIGN=n` | `.section X,"aw",@progbits`, or `@nobits` where no line of `X` writes a byte, then `.balign n` |
| a `.SECTION` line that resumes `X` | `.section X` |
| `.ALIGN n` | `.balignw n,0x0009`, which fills with NOPs, in a code section, and `.balign n` in a data section |
| `.DATA.B`, `.DATA.W`, `.DATA.L` | `.byte`, `.short`, `.long` |
| `.DATAB.B n,v` | `.fill n,1,v` |
| `.RES.B n`, `.RES.W n`, `.RES.L n` | `.space n`, `.space (n)*2`, `.space (n)*4` |
| `.SDATA "s"` | `.ascii "s"` |
| `.FDATA.S F'x` | `.float x`, an `S` before its exponent as `e`, and a lone `0` before it as `0.`: GNU as reads `0e` as a radix's prefix |
| `NAME: .EQU v` | `.equ NAME,v` |
| `.EXPORT`, `.IMPORT`, `.END` | `.global`, `.extern`, `.end` |

The lines above any `.SECTION` are in `P`, as asmsh assembles them, so translate writes `.section P,"ax",@progbits` and `.balign 4` above the first of them that takes a place. Every other byte stays: registers, operators, symbols and the spelling of each expression. A field keeps its column, or follows the field before it past one blank where that reaches the column, and the lines translate adds are in SHCASM's columns. A comment keeps its bytes after the `!`, CP932 included. Every line ends with LF.

GNU as reads mnemonics, registers and operands in either case, and blanks between an operand's tokens. It refuses `@(Rn)`, which asmsh reads as `@Rn`.

## Literal pools

translate writes each pool where asmsh does, which the statements alone decide:

- **Where.** A literal waits for the slot of the next `RTS`, `RTE`, `BRA`, `BRAF` or `JMP` in its section, as [check](check.md#flow) places slots, and goes out right after it. A label on the slot, or between the branch and its slot, keeps the pool from that slot: the literals wait for the next. There is none after the slot of `BSR`, `BSRF`, `JSR`, `BT/S` or `BF/S`.
- **Which loads share.** `MOV.W` reads a word, and `MOV.L` and `MOVA` a long. Two loads of one size share a literal where both are numbers with the same 32 bits, so `#17` and `#H'11` share, and `MOV.W #-1` and `MOV.W #H'FFFF` don't. Two other values share where they are spelled alike and no line between the loads defines or imports a name in them or, for a `?label`, opens a block: asmsh keys a literal by what is defined or imported above its load.
- **The pool.** The words, in the order of their first loads, then `.balign 4,0` where longs follow, which writes asmsh's zero half word before a long on a half word, then the longs. `.Llitk` numbers the literals from 0, in the order of their first loads in the text. A literal's value is its first load's.

## What it refuses

translate refuses what apply refuses before any patch, naming the line:

- `a line shckit can't read`: a line of no kind the reader types, or a continuation that joins nothing;
- `a line after .END that isn't blank or a comment`.

Since it writes pools by the slots, it refuses what use-assembler-pools refuses of them:

- `an instruction the opcode table doesn't hold`;
- `a delayed branch whose slot shckit can't place`: an `.ALIGN`, data, a `.SECTION` or nothing where its slot is due, or an instruction illegal there.

And it refuses what GNU as would read otherwise, and what it doesn't write:

- `a directive, operand or continuation translate doesn't write`: `.POOL`, `.NOPOOL`, `.ASSIGN`, `.DATAB.W` and `.DATAB.L`; `.END` with an entry point; an `.FDATA.S` operand other than `F'` and a constant, such as a bit pattern; `@(disp,PC)`, whose displacement GNU as reads as a place, not a distance; and any continuation.
- `$, _GLOBAL_OFFSET_TABLE_, or a word operator other than STARTOF or SIZEOF`: `$`, asmsh's location, which GNU as reads as a symbol of that name; `_GLOBAL_OFFSET_TABLE_`, an ordinary name to asmsh, which GNU as relocates as the address of a GOT it builds; and `HIGH`, `LOW`, `HWORD`, `LWORD`, `$EVEN`, `$EVEN2`, `$ODD` and `$ODD2`.
- `a name that starts with __startof_, __sizeof_ or __endof_`: a label's, a section's or an operand's name that could be one translate writes for `STARTOF X`, `SIZEOF X` or `(STARTOF X)+(SIZEOF X)`. GNU as makes each section's name a symbol.
- `a ~ or ^ between operands, or operators asmsh and GNU as group apart`. asmsh reads a binary `~` or `^` as exclusive or, and ranks `*` and `/`, then `+` and `-`, then `<<` and `>>`, then `&`, then `|`. GNU as ranks `*`, `/`, `<<` and `>>`, then `|` and `&`, then `+` and `-`. So translate refuses, unparenthesized between them, `*` or `/` with `<<` or `>>`, `+` or `-` with `<<`, `>>`, `&` or `|`, and `&` with `|`.
- `a branch GNU as can't relocate as asmsh does`: a `BT`, `BF`, `BT/S` or `BF/S` to an import, a branch to a label of another section, one to a number or an `.EQU`'s name, and one whose target is anything but a name, alone or plus or minus a number written in the text. GNU as builds a branch to a target such as `_f+STEP` or `_f+(_g-_f)`, where a name in it is defined below the branch, to another place, with no message.
- `a literal still waiting at a .SECTION, at .END or at the text's end`. asmsh keeps a literal past its section's end for the section's next pool, and writes one still waiting at `.END` with warning 876.
- `a string outside .SDATA, or one with "", \ or a byte not printable`: a string in an expression, which asmsh reads as its characters' number and GNU as as 0; an `.SDATA` string that holds `""`, `\`, a tab or a byte past ASCII, which GNU as reads otherwise; and an `.SDATA` operand that isn't one string.

## Limits

translate evaluates no expression:

- **`>>` and `/`.** asmsh computes in 32 bits, and GNU as in 64. For `+`, `-`, `*`, `<<`, `&`, `|` and a unary `~`, the low 32 bits agree, and GNU as refuses a value too wide for its field. A `>>` or `/` can come out another value, with no message, where GNU as's value of an operand lies outside -H'80000000 to H'7FFFFFFF, or a `>>` shifts by 32 or more: `.DATA.L H'80000000>>4` is `H'F8000000` to asmsh, for which H'80000000 is negative, and `0x08000000` to GNU as, and `(H'FFFFFFFF+2)/2` is 0 to asmsh, which wraps the sum, and `0x80000000` to GNU as. Elsewhere it agrees, or GNU as fails. translate keeps such an expression as written.
- **Sharing.** Two loads of one value spelled apart, such as `#X` for an `.EQU` of 17 and `#17`, or `#_x+4` and `#_x + 4`, share a literal in asmsh and take two in translate's text.
- **Floats.** An `.FDATA.S` constant that isn't exactly a single-precision number can round apart in the two assemblers, and asmsh writes 2⁻¹²⁷ as 2⁻¹²⁶ with no warning. It writes the other denormal constants with warning 842.
- **Unwritten bytes.** asmsh leaves the bytes of a `.RES`, and the gap of a data section's `.ALIGN`, unwritten, and GNU as writes zeros there, in a code section too. A data section that writes no byte is the exception: it is `@nobits`, and both leave it unwritten.

## Evidence

asmsh 4.0 ('98.07.06) assembled, and Hitachi's linker linked, each text below, and `sh-elf-as` and `sh-elf-ld` 2.47 did the same with translate's text, each section and import at the same address on both sides:

- **The opcode table.** Each of its mnemonics, in each size and form it takes, but the four `#` loads and the five `@(disp,PC)` forms, comes out as the same word from both: 421 instructions.
- **Probes.** asmsh assembles 341 of 606 probe texts, of numbers, operators, names, sections, data, strings, floats, slots and pools, with no error and no warning. translate refuses 61 of them and `sh-elf-as` 24. Of the other 256, the link holds asmsh's bytes for 242, 7 of them but for bytes asmsh leaves unwritten, and the other 14 meet a limit: 5 `>>` and `/`, 4 floats and 5 sharing.
- **Probes written to break it.** asmsh assembles 1,216 of 1,530 more, many of them random expressions and pools, with no error and no warning. translate refuses 12 of them and `sh-elf-as` 114. Of the other 1,090, the link holds asmsh's bytes for 1,065, 3 of them but for bytes asmsh leaves unwritten, and the other 25 meet a limit: 22 `>>` and `/`, 1 float and 2 sharing.

The unit tests state the text translate writes for each row, form, refusal and limit above, and check its refusal of operators against both assemblers' ranks, pair by pair.

### What would refute it

- A text asmsh 4.0 ('98.07.06) assembles with no error and no warning, which translate translates and `sh-elf-as` 2.47 assembles with these options, whose bytes or relocations differ from asmsh's, but for the limits.
- A form of the opcode table, in a size it takes, whose GNU spelling assembles to another word.
- A pool asmsh writes after another slot than translate's text does, or two loads asmsh reads from one literal where translate's text gives them two, or the reverse, but for the sharing limit.
