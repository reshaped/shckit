# The opcode table

The opcode table in `shckit.c` lists the SH-4 instructions shckit accepts: 110 mnemonics in 57 rows. A row names its mnemonics, the [sizes](#sizes) they take and the size GNU as writes for each, the [forms](#forms) of their operands, and what they do to the [flow](#flow) of control. Mnemonics that take the same sizes and forms and do the same to the flow share a row. Mnemonics, or forms of one, whose sizes GNU as writes apart take rows of their own: `DMULS` leaves `ADDC`'s row, and `LDC.L @Rm+,GBR`, which keeps its size in GNU as, leaves `LDC.L Rm,GBR`, which loses it. The table is shckit's model of what asmsh assembles as written with `-cpu=sh4 -private`. It isn't a claim about every asmsh build.

`shckit_flow` reads an instruction line against the table, and [check](check.md) refuses an instruction it doesn't hold: by the model, one asmsh won't assemble as written, such as a mnemonic of another SuperH, a size asmsh would change, a register where the form wants R0, or an `@` form asmsh can't read. Which instructions are delayed, which never fall through, and which are illegal in a delay slot come from the rows alone: the text doesn't say.

## Sizes

A size is the suffix after the mnemonic's dot, `.B`, `.W`, `.L`, `.S` or `.D`, or none.

- **Held:** the ones asmsh takes: `ADD.L` but not `ADD.W`, `EXTS.B` and `EXTS.W`, `MULS.W` and `MULS.L`, which are one instruction, `FADD.S` on single registers and `FADD.D` on double ones.
- **With none:** asmsh reads `EXTS`, `EXTU`, `MAC` and `SWAP` as `.W`, `MOV` as `.L` but `MOV #imm,Rn`, the 8-bit immediate, as `.B`, and the rest as the form's one size.
- **Changed with a warning:** asmsh assembles some other sizes too, with a warning, as another instruction: `MOV.B @(4,R3),R3` comes out as `MOV.L @(4,R3),R3`. The table holds none of them.
- **In GNU as:** binutils 2.47's `sh-elf-as` refuses some sizes asmsh takes, as in `ADD.L` and `FMOV.S FR1,FR2`, and wants a size where asmsh reads one into an instruction written without it, as in `MOV @R4,R5` and `DMULS R1,R2`, which it takes only as `MOV.L @R4,R5` and `DMULS.L R1,R2`. `MULS.L`, which asmsh assembles as `MULS.W`, is `MULS` to it. So the table gives, for each size a row takes, the size GNU as writes for the same instruction.

## Forms

A form lists its operands in the instruction set's names:

- general registers: `Rm` and `Rn` for any, `R0` where only R0 will do, and `Rn_BANK`;
- memory: `@Rn`, `@Rn+`, `@-Rn`, `@(disp,Rn)`, `@(R0,Rn)`, `@(disp,GBR)`, `@(R0,GBR)` and `@(disp,PC)`;
- `#imm` and `label`;
- floating point: `FRn`, `FR0` where only FR0 will do, `DRn`, `XDn`, `FVn`, `XMTRX`, `FPUL` and `FPSCR`;
- the control and system registers `SR`, `GBR`, `VBR`, `SSR`, `SPC`, `SGR`, `DBR`, `MACH`, `MACL` and `PR`.

In `MOV.W #imm,Rn`, `MOV.L #imm,Rn` and `MOVA #imm,R0`, asmsh writes the immediate into a literal pool, and the instruction loads it, or its address, relative to the PC. `MOV.W label,Rn`, `MOV.L label,Rn` and `MOVA label,R0` load from the label, or take its address, the same way.

## Operands as asmsh reads them

- Mnemonics, sizes and registers in any case, `SP` for R15, and `@(Rn)` for `@Rn`.
- Blanks between an operand's tokens, as in `@( 4 , R3 )` and `@R3 +`, but not inside a name or a number.
- A register's name with any other number is a symbol: `R16`, `DR1`, `FV2` and `R8_BANK`, and the names of other SuperH CPUs' registers.
- A `label`, a displacement or an immediate is an expression: it names no register and holds no `@` or `#`, and a word follows another only as the operand of a word operator, `STARTOF`, `SIZEOF`, `HIGH`, `LOW`, `HWORD`, `LWORD`, `$EVEN`, `$ODD`, `$EVEN2` or `$ODD2`.
- An instruction with operands takes those of the continuation lines below it, where [check](check.md#reading) joins them.

The table matches forms, not values. A displacement or an immediate out of range, a symbol never defined, and a plain number as the label of a load are left to asmsh to refuse, and so is an expression asmsh can't parse that holds no register, `@`, `#` or word right after another, such as `LAB%2`, `LAB(1)` or `STARTOF` with no word after it.

## Flow

Each row says three things of its instructions.

- **Delayed:** `BRA`, `BRAF`, `BSR`, `BSRF`, `JMP`, `JSR`, `RTS`, `RTE`, `BT/S` and `BF/S`. The next instruction, the delay slot, runs before the branch. After `BT/S` and `BF/S` it runs whether or not the branch is taken.
- **Never falls through:** `BRA`, `BRAF`, `JMP`, `RTS` and `RTE`. Control never reaches the line after the slot from it.
- **Illegal in a delay slot:** every branch, `BT` and `BF` included, `TRAPA`, `LDC Rm,SR` and `LDC.L @Rm+,SR`, and every load relative to the PC: `MOV.W` and `MOV.L` from `@(disp,PC)`, a `label` or a pooled `#imm`, and `MOVA` in every form. In a slot each raises the SH-4's slot illegal instruction exception, and asmsh refuses each there.

A privileged instruction in a slot raises the exception only in user mode, which the text doesn't show, so the table doesn't count it.
