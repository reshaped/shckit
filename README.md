# shckit

**shckit** is a C99 library and command-line tool for asmsh assembly, the SuperH dialect Hitachi's SHC compiler writes and its assembler, asmsh, reads.

shckit grew out of decompiling Dreamcast games:

- **Patch** SHC's output minimally, with targeted, deterministic rules, to rebuild specific SDK library members.
- **Inspect** an assembly function for evidence that a specified SHC release didn't compile it from C, inline assembly aside.
- **Check** assembly for instructions the opcode table doesn't hold and for control-flow hazards.
- **Lint** asmsh assembly: the format is old, and decomp projects need a modern tool to keep their source formatted.
- **Translate** asmsh assembly to GNU as text that binutils assembles to asmsh's bytes, within stated limits, so a build can leave Hitachi's assembler out.

## Build

```sh
make
make test
```

The executable is `build/shckit`. GCC and Clang are supported. The library uses only C99's standard library; the command also needs POSIX.

## Usage

```sh
build/shckit apply -p PATCH ... -o OUT IN
build/shckit check FILE...
build/shckit lint FILE...
build/shckit inspect --release RELEASE --mode MODE --entry LABEL [--end LABEL] [--sz 0|1] [--all] FILE
build/shckit translate -o OUT IN
```

`apply` runs the named [patches](docs/patches.md) on `IN` and writes `OUT`. A line no patch touches comes out byte for byte, and when a patch refuses, nothing is written:

```
shckit: x.src:31: drop-inline-align: an .ALIGN 4 in a code section that isn't SHCASM's line
```

A patched text goes to asmsh as the compiler's would:

```sh
build/shckit apply -p drop-inline-align -p align-pool -o patched.src original.src
asmsh patched.src -object=patched.obj -cpu=sh4 -endian=little -sjis -private
```

[`check`](docs/check.md) refuses lines asmsh may not assemble as written, or the SH-4 may not run as they read. A text it passes can still fail in asmsh, or run otherwise than it reads. [`lint`](docs/lint.md) holds the text people write to a consistent layout. Both name every line they refuse, and change nothing.

[`inspect`](docs/inspect.md) looks in one function for an instruction, operands or a use of R15 that is evidence the release named didn't compile it from C. It prints the witness, or `none`, and any limit it met, a place it couldn't see past:

```
x.src:3: _f: abi: stack-above-entry (SP +8)
```

`stack-above-entry` and `return-below-entry` rest on an [unfinished argument](docs/inspect.md#abi-rules) about SHC 5.0 R10's frames.

inspect doesn't find where a function ends. Without `--end` it reads to the end of the entry's `.SECTION` block, so give `--end` when another function follows. `none` means no witness, not that SHC compiled the function from C.

[`translate`](docs/translate.md) writes `IN` as GNU as text. For a text asmsh assembles with no error and no warning, binutils' `sh-elf-as` assembles the output to the bytes and relocations asmsh writes, or translate refuses the text, naming the line, or `sh-elf-as` fails, except where a [silent limit](docs/translate.md#limits) applies, the first of them `>>` and `/`: GNU as computes them in 64 bits, where asmsh computes them in 32. It reads the text as `apply` does and computes no address: GNU as lays the text out, and translate writes asmsh's literal pools where asmsh does.

Every command reads CP932, as SHC writes it, and checks its byte structure, not its characters. It refuses a text with a control character other than a tab or a line end, a byte CP932 doesn't use, a lead byte without its trail, or a last line without a line end.

Exit codes:

- `0` success;
- `1` a text shckit refuses;
- `2` a command-line, I/O or memory error, which outranks a refusal;
- `3` an inspection that met a limit.

## Patches

Each patch is for the members [its page](docs/patches.md) names:

- [`drop-inline-align`](docs/patches.md#drop-inline-align) drops SHCASM's `.ALIGN 4` after inline text in a code section, and refuses any other code-section `.ALIGN 4`.
- [`align-pool`](docs/patches.md#align-pool) writes SHCASM's `.ALIGN 4` before each literal pool's label, then `.RES.W 1` where SHCASM counted the label at a half word.
- [`use-assembler-pools`](docs/patches.md#use-assembler-pools) hands SHC's literal pools to asmsh, turning the loads and `MOVA`s it accepts into `#` literals and each pool into `.ALIGN 4` and `.POOL`.
- [`drop-r0-move-nop`](docs/patches.md#drop-r0-move-nop) drops SHCASM's `NOP` right after its `MOV Rm,R0` outside a delay slot. It deletes an instruction.
- [`fill-call-slot`](docs/patches.md#fill-call-slot) drops SHCASM's `NOP` in the delay slot of its `JSR @Rm` when its `ADD #imm,Rn` to R8 to R14 comes right below, so the add fills the slot. It moves an instruction across a call.

A patch reads only its input text and its own rules, never a target object. Applying one doesn't show that the code still runs the same, or guarantee an object that matches a target. The objects check the result separately, with `tools/objects.py`.

## Library

Compile `shckit.c` with your application and include [`shckit.h`](shckit.h), which gives every function's ownership, indexing and error contracts. Copy `LICENSE` when vendoring. The [pages](docs) the header cites stay here. The library keeps no mutable global state.

## Development

- `make test` runs the unit tests under ASan and UBSan, then runs the command on sample texts. The tests cover the reader, every patch's output and refusals, every rule, and translate's rows and refusals. `make test SAN=` runs them without sanitizers. It also tests `tools/objects.py`, with Python 3.
- `make lint` checks formatting and runs clang-tidy, and `make format` reformats the source. Both install pinned LLVM tools into a Python 3 virtual environment on first use.
- `make fuzz` feeds any bytes to the reader, every command and every selection of patches. It needs Clang with libFuzzer, and runs for `FUZZ_TIME` seconds, 60 by default.
- `make golden CORPUS=DIR` applies each case's patches to its SHC text and compares the result with the case's expected text. `tools/objects.py` assembles the results with asmsh and compares them with the cases' objects.

The corpus and Hitachi's tools stay out of the repository, so CI runs neither. It runs the tests under GCC and Clang, lint, and fuzzing on every push and pull request.

## License

MIT. See [LICENSE](LICENSE).
