# shckit build and checks
#
#   make         build the command, build/shckit
#   make test    run the tests under asan and ubsan (make test SAN= without)
#   make golden  compare the command with the golden texts in CORPUS
#   make lint    check the c against .clang-format and .clang-tidy
#   make format  format the c
#   make fuzz    fuzz for FUZZ_TIME seconds (needs clang with libfuzzer)

CFLAGS    ?= -O2
STD       := -std=c99
WARN      := -Wall -Wextra -Wpedantic
SAN       ?= -fsanitize=address,undefined -fno-sanitize-recover=all
FUZZ_TIME ?= 60
C_FILES   := $(wildcard *.[ch] tests/*.[ch])
LLVM      := tools/llvm/bin
SAMPLE    := build/test/sample
# the samples make fuzz starts from: a text for drop-inline-align and
# align-pool, one for use-assembler-pools, one whose second pool takes no
# literals, one whose float constant a MOVA addresses, one whose slot is in a
# section's next block, one for fill-call-slot, one a patch refuses, one check
# refuses, one lint refuses, three for inspect, and one for translate
SEEDS     := src pools two-pools mova resumed shapes other flawed ragged \
             returns calls pops joins translate

# sanitizers exit 1 on a finding, which is also shckit's exit code for a text
# it refuses
export ASAN_OPTIONS  := exitcode=99
export UBSAN_OPTIONS := exitcode=99

all: build/shckit

build/shckit: shckit.c shckit.h main.c
	@mkdir -p $(@D)
	$(CC) $(STD) $(WARN) $(CFLAGS) -o $@ shckit.c main.c

# the tests get a sanitized build of their own, the command included, where
# a warning is an error. the stamp remembers CC and SAN, so changing either
# rebuilds them
build/test/test: shckit.c shckit.h tests/test.c build/test/stamp
	$(CC) $(STD) $(WARN) -Werror -g -O1 $(SAN) -o $@ $(filter %.c,$^)

build/test/shckit: shckit.c shckit.h main.c build/test/stamp
	$(CC) $(STD) $(WARN) -Werror -g -O1 $(SAN) -o $@ shckit.c main.c

build/test/stamp: FORCE
	@mkdir -p $(@D)
	@[ "$$(cat $@ 2>/dev/null)" = '$(CC) $(SAN)' ] || echo '$(CC) $(SAN)' > $@

# the unit tests write the sample texts and what each command below must
# write or print
test: build/test/test build/test/shckit
	build/test/test $(SAMPLE)
	build/test/shckit 2> /dev/null; [ $$? -eq 2 ]
# the order of -p options doesn't matter
	build/test/shckit apply -p align-pool -p drop-inline-align \
		-o $(SAMPLE).out $(SAMPLE).src
	cmp $(SAMPLE).want $(SAMPLE).out
	build/test/shckit apply -p use-assembler-pools -o $(SAMPLE).out \
		$(SAMPLE).pools
	cmp $(SAMPLE).pooled $(SAMPLE).out
	build/test/shckit apply -p drop-r0-move-nop -p use-assembler-pools \
		-o $(SAMPLE).out $(SAMPLE).pools
	cmp $(SAMPLE).moved $(SAMPLE).out
	build/test/shckit apply -p fill-call-slot -p drop-r0-move-nop \
		-p use-assembler-pools -o $(SAMPLE).out $(SAMPLE).shapes
	cmp $(SAMPLE).filled $(SAMPLE).out
# an OUT that names IN, by its own path or a link, leaves IN unchanged
	cp $(SAMPLE).src $(SAMPLE).kept
	ln -sf $(notdir $(SAMPLE)).src $(SAMPLE).link
	build/test/shckit apply -p align-pool -o $(SAMPLE).src $(SAMPLE).src \
		2> /dev/null; [ $$? -eq 2 ]
	build/test/shckit apply -p align-pool -o $(SAMPLE).link $(SAMPLE).src \
		2> /dev/null; [ $$? -eq 2 ]
	cmp $(SAMPLE).kept $(SAMPLE).src
	build/test/shckit apply -p drop-inline-align -o $(SAMPLE).x \
		$(SAMPLE).other 2> $(SAMPLE).refused; [ $$? -eq 1 ]
	cmp $(SAMPLE).refusal $(SAMPLE).refused
	build/test/shckit apply -p align-pool -o $(SAMPLE).x $(SAMPLE).bad \
		2> /dev/null; [ $$? -eq 1 ]
	build/test/shckit apply -p drop-inline-align -o $(SAMPLE).x \
		$(SAMPLE).unended 2> /dev/null; [ $$? -eq 1 ]
# an unknown or repeated patch, or align-pool with use-assembler-pools,
# fails before IN is read or OUT is opened
	rm -f $(SAMPLE).none
	build/test/shckit apply -p no-such-patch -o $(SAMPLE).none \
		$(SAMPLE).src 2> /dev/null; [ $$? -eq 2 ]
	build/test/shckit apply -p align-pool -p align-pool \
		-o $(SAMPLE).none $(SAMPLE).src 2> /dev/null; [ $$? -eq 2 ]
	[ ! -e $(SAMPLE).none ]
	for suffix in pools empty unended none; do \
		build/test/shckit apply -p use-assembler-pools -p align-pool \
			-o $(SAMPLE).kept $(SAMPLE).$$suffix 2> $(SAMPLE).conflicting; \
		[ $$? -eq 2 ] && cmp $(SAMPLE).conflict $(SAMPLE).conflicting \
			|| exit 1; \
	done
	cmp $(SAMPLE).src $(SAMPLE).kept
	build/test/shckit apply -o $(SAMPLE).x $(SAMPLE).src 2> /dev/null; \
		[ $$? -eq 2 ]
	build/test/shckit apply -p drop-inline-align -o $(SAMPLE).x \
		build/test/none.src 2> /dev/null; [ $$? -eq 2 ]
# a missing file outweighs a refused one
	build/test/shckit check $(SAMPLE).src
	build/test/shckit check $(SAMPLE).src $(SAMPLE).flawed \
		2> $(SAMPLE).checking; [ $$? -eq 1 ]
	cmp $(SAMPLE).checked $(SAMPLE).checking
	build/test/shckit check $(SAMPLE).bad 2> /dev/null; [ $$? -eq 1 ]
	build/test/shckit check $(SAMPLE).flawed build/test/none.src \
		2> /dev/null; [ $$? -eq 2 ]
	build/test/shckit check 2> /dev/null; [ $$? -eq 2 ]
	build/test/shckit lint $(SAMPLE).flawed
	build/test/shckit lint $(SAMPLE).flawed $(SAMPLE).ragged \
		2> $(SAMPLE).linting; [ $$? -eq 1 ]
	cmp $(SAMPLE).linted $(SAMPLE).linting
	build/test/shckit lint $(SAMPLE).ragged build/test/none.src \
		2> /dev/null; [ $$? -eq 2 ]
	build/test/shckit lint 2> /dev/null; [ $$? -eq 2 ]
# inspect prints a witness, or with --all every witness, or none, and any
# limit, exiting 3 for a limit. a function without --end runs to the end of
# its .SECTION block, and a text check refuses gets check's findings
	build/test/shckit inspect --release 5.0R10 --mode asmcode \
		--entry _entry --end _end $(SAMPLE).returns > $(SAMPLE).inspecting
	cmp $(SAMPLE).returned $(SAMPLE).inspecting
	build/test/shckit inspect --end _end --entry _entry --mode both \
		--release 5.0R10 $(SAMPLE).calls > $(SAMPLE).inspecting; \
		[ $$? -eq 3 ]
	cmp $(SAMPLE).called $(SAMPLE).inspecting
	build/test/shckit inspect --release 5.0R10 --mode machinecode \
		--entry _entry $(SAMPLE).loads > $(SAMPLE).inspecting
	cmp $(SAMPLE).constant $(SAMPLE).inspecting
	build/test/shckit inspect --release 5.0R32 --mode asmcode \
		--entry _entry $(SAMPLE).loads > $(SAMPLE).inspecting; \
		[ $$? -eq 3 ]
	cmp $(SAMPLE).scoped $(SAMPLE).inspecting
	build/test/shckit inspect --release 5.0R10 --mode asmcode \
		--entry _entry $(SAMPLE).blocks > $(SAMPLE).inspecting; \
		[ $$? -eq 3 ]
	cmp $(SAMPLE).fell $(SAMPLE).inspecting
	build/test/shckit inspect --release 5.0R10 --mode asmcode \
		--entry _entry $(SAMPLE).last > $(SAMPLE).inspecting
	cmp $(SAMPLE).lasted $(SAMPLE).inspecting
	build/test/shckit inspect --release 5.0R10 --mode asmcode --all \
		--entry _entry $(SAMPLE).witnesses > $(SAMPLE).inspecting; \
		[ $$? -eq 3 ]
	cmp $(SAMPLE).witnessed $(SAMPLE).inspecting
	build/test/shckit inspect --all --all --release 5.0R10 --mode asmcode \
		--entry _entry $(SAMPLE).witnesses 2> /dev/null; [ $$? -eq 2 ]
	build/test/shckit inspect --release 5.0R10 --mode asmcode --entry _f \
		--end _f $(SAMPLE).flawed 2> $(SAMPLE).checking; [ $$? -eq 1 ]
	cmp $(SAMPLE).checked $(SAMPLE).checking
	for label in _none _end; do \
		build/test/shckit inspect --release 5.0R10 --mode asmcode \
			--entry $$label --end _end $(SAMPLE).returns 2> /dev/null; \
		[ $$? -eq 2 ] || exit 1; \
	done
	build/test/shckit inspect --release 5.0R10 --mode asmcode --sz 2 \
		--entry _entry --end _end $(SAMPLE).returns 2> /dev/null; \
		[ $$? -eq 2 ]
	build/test/shckit inspect --release 5.0R10 --mode asmcode \
		--entry _entry --end _end build/test/none.src 2> /dev/null; \
		[ $$? -eq 2 ]
	[ ! -c /dev/full ] || { build/test/shckit inspect --release 5.0R10 \
		--mode asmcode --entry _entry --end _end $(SAMPLE).returns \
		> /dev/full 2> /dev/null; [ $$? -eq 2 ]; }
# translate writes GNU as text, or refuses a text naming its line, and an
# OUT that names IN, a -p and a missing -o fail before IN is read
	build/test/shckit translate -o $(SAMPLE).s $(SAMPLE).translate
	cmp $(SAMPLE).translated $(SAMPLE).s
	build/test/shckit translate -o $(SAMPLE).x $(SAMPLE).waits \
		2> $(SAMPLE).refused; [ $$? -eq 1 ]
	cmp $(SAMPLE).waited $(SAMPLE).refused
	cp $(SAMPLE).translate $(SAMPLE).kept
	build/test/shckit translate -o $(SAMPLE).kept $(SAMPLE).kept \
		2> /dev/null; [ $$? -eq 2 ]
	cmp $(SAMPLE).translate $(SAMPLE).kept
	build/test/shckit translate -p align-pool -o $(SAMPLE).x \
		$(SAMPLE).translate 2> /dev/null; [ $$? -eq 2 ]
	build/test/shckit translate $(SAMPLE).translate 2> /dev/null; \
		[ $$? -eq 2 ]
	build/test/shckit translate -o $(SAMPLE).x build/test/none.src \
		2> /dev/null; [ $$? -eq 2 ]
# the pages' worked examples, and tools/objects.py tested with synthetic
# objects and stand-in commands
	for doc in docs/*.md; do \
		tests/examples.sh $$doc build/test/example build/test/shckit || exit 1; \
	done
	python3 tests/objects.py

# CORPUS holds a directory per compiler text, with the patches to apply and
# the expected output, and a SHA256SUMS of its own (see tests/golden.sh)
golden: build/test/shckit
	@[ -n "$(CORPUS)" ] || { echo 'make golden CORPUS=DIR'; exit 2; }
	tests/golden.sh build/test/shckit "$(CORPUS)"

build/fuzz: shckit.c shckit.h tests/fuzz.c
	@mkdir -p $(@D)
	clang $(STD) $(WARN) -Werror -g -O1 \
		-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
		-o $@ $(filter %.c,$^)

# the corpus starts from the tests' samples in SEEDS. the dictionary holds
# pieces of SHC's lines. whatever the fuzzer finds lands in build/
fuzz: build/fuzz build/test/test
	@mkdir -p build/corpus build/seeds
	build/test/test build/seeds/sample > /dev/null
	cp $(SEEDS:%=build/seeds/sample.%) build/corpus/
	build/fuzz -artifact_prefix=build/ -max_total_time=$(FUZZ_TIME) \
		-max_len=4096 -dict=tests/fuzz.dict build/corpus

lint: | $(LLVM)/clang-format
	$(LLVM)/clang-format --dry-run -Werror $(C_FILES)
	$(LLVM)/clang-tidy --quiet $(filter %.c,$(C_FILES))

format: | $(LLVM)/clang-format
	$(LLVM)/clang-format -i $(C_FILES)

# clang-format's output changes between releases, so both tools are pinned
$(LLVM)/clang-format:
	python3 -m venv tools/llvm
	$(LLVM)/pip install -q clang-format==22.1.8 clang-tidy==22.1.8

clean:
	rm -rf build

FORCE:

.PHONY: all test golden fuzz lint format clean FORCE
.DELETE_ON_ERROR:
