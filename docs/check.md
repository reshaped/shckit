# check

`shckit check FILE...` reports each line that breaks a rule below, naming the file, the line number and the rule:

```
shckit: x.src:101: data-in-flow: data in a code section that control falls into
```

check doesn't modify files. Its rules fall into three groups:

- **unknown-line** refuses a line shckit can't read, which asmsh may still assemble.
- **unknown-instruction** and **after-end** refuse a line asmsh won't assemble as the text has it.
- **data-in-flow**, **delay-slot** and **no-slot** refuse a line the SH-4 may run otherwise than the text reads.

If any line up to the first `.END` breaks unknown-line or unknown-instruction, the lines that do are check's only findings ([Reading](#reading)).

check is not an assembler. A text it passes can still fail in asmsh, or run otherwise than it reads. The [opcode table](opcodes.md) matches an operand's form and not its value, so a displacement out of range or a symbol never defined is left to asmsh. And check follows control from each line to the next, never to a branch's target ([Flow](#flow)).

## Reading

**unknown-line**: a line the reader can't type, or a continuation joined to no statement. The line kinds and directives in [shckit.h](../shckit.h) say what the reader types. asmsh assembles some lines the reader can't type, such as `X .ASSIGN 1`, whose label has no colon, and check refuses them all the same. It refuses an `.EQU` or `.ASSIGN` without a label or with other than one value, as asmsh does.

**unknown-instruction**: an instruction whose mnemonic, size or operands the [opcode table](opcodes.md) doesn't hold.

Either could be anything to asmsh: a section, the end of the text, a branch. So check refuses each such line up to the first `.END`, and checks nothing else until none is left.

**after-end**: a line after `.END` that isn't blank or a comment. asmsh reads nothing past `.END`, so the line is in the text and not in the object. check reads each line there by after-end alone.

asmsh joins a continuation to the instruction above it, past that one's continuations, when the instruction has operands, and to the directive above it when the field above ends with a comma. shckit reads an operand within one line, so it joins a continuation to an instruction only where a comma ends the field above or starts the continuation's own.

## Flow

check follows control through each section to the first `.END`, as asmsh lays the text out, from each line to the next and nowhere else. Lines above any `.SECTION` are in a code section P, and a section named again resumes where it stood, as asmsh assembles it. Each line does one of three things to control:

- **It falls into the next line** after an instruction that falls through, `BT` and `BF` among them, and after the slot of a call or of `BT/S` or `BF/S`. A slot itself is in the flow.
- **It is out of the flow** at a section's start, past the slot of `BRA`, `BRAF`, `JMP`, `RTS` or `RTE`, and past `TRAPA`, until the next instruction. A pad after a return's slot, as in `RTE`, `NOP`, `.DATA.W 0`, is out of the flow. `TRAPA`'s handler may not come back: SHC ends an interrupt function with `TRAPA` under `#pragma interrupt(f(tn=n))`, and writes the function's pool right after it.
- **It stays where it is** at a label, a comment, and any directive that writes no data. A label leaves it there because the text doesn't say which branches reach it, so data under a label that a branch reaches passes when no line above falls into it. An `.ALIGN` leaves it too, though whether it writes a `NOP` into a slot depends on the address, which the text doesn't show. A `.POOL` leaves it too: asmsh branches around a pool that control falls into.

**data-in-flow**: a `.DATA`, `.DATAB`, `.FDATA.S`, `.SDATA` or `.RES` line in a code section that control falls into, or that is a slot.

The SH-4 runs such data as instructions. A word meant as one, such as `.DATA.W H'F77D` for `FSRRA FR7`, hides an instruction from the opcode table and from every reader, its flow and its slot with it. asmsh assembles FSRRA and FSCA with `-private`. A word not meant as one is a fall into data.

**delay-slot**: an instruction illegal in the delay slot it stands in: the statement after a delayed branch. The SH-4 raises its slot illegal instruction exception there, and asmsh refuses the line too where it can place the slot. check names it by the text alone, `.ALIGN` or not. The [opcode table](opcodes.md#flow) says what is illegal in a slot.

**no-slot**: a delayed branch with no instruction or data after it in its section, up to the first `.END`. A section named again can still give the branch its slot. An `.ALIGN` after the branch may write a `NOP` into the slot or not, depending on the address. asmsh assembles the branch without a warning, and its slot is whatever the linker puts after the section.
