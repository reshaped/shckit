// Unit tests for the reader, each patch's output and refusals, apply's
// preflight, the opcode table, check's and lint's rules, inspect's rules, and
// translate's text and refusals. Given a path, also write the samples make
// test runs the command on.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../shckit.h"

#define CHECK(x) check((x), #x, __LINE__)
#define BIT(patch) (1U << (patch))
#define DROP BIT(SHCKIT_DROP_INLINE_ALIGN)
#define POOL BIT(SHCKIT_ALIGN_POOL)
#define USE BIT(SHCKIT_USE_ASSEMBLER_POOLS)
#define MOVE BIT(SHCKIT_DROP_R0_MOVE_NOP)
#define FILL BIT(SHCKIT_FILL_CALL_SLOT)

// A statement in SHCASM's columns, and code and data section lines.
#define S(statement) "          " statement "\n"
#define CODE S(".SECTION    P,CODE,ALIGN=4")
#define DATA S(".SECTION    D,DATA,ALIGN=4")

// SHCASM's .ALIGN 4 and .RES.W 1, and pool labels as its pool writer prints
// them: padded with spaces to 34 bytes.
#define ALIGN S(".ALIGN      4")
#define RESERVE S(".RES.W      1")
#define L10 "L10:                              \n"
#define L11 "L11:                              \n"
#define L12 "L12:                              \n"

static int failures;

static bool check(bool ok, const char *what, int line)
{
    // keep running to report every failed check
    if (!ok) {
        printf("tests/test.c:%d: %s\n", line, what);
        failures++;
    }
    return ok;
}

static struct shckit_text *read_text(const char *in)
{
    struct shckit_text *text = NULL;
    size_t line = 0;
    int error = shckit_read(&text, in, strlen(in), &line);
    if (error != SHCKIT_OK) {
        printf("  read: \"%s\" at line %zu\n", shckit_strerror(error), line);
    }
    return text;
}

static bool part_is(const struct shckit_line *line, struct shckit_span part,
                    const char *want)
{
    return part.len == strlen(want) &&
           memcmp(line->bytes + part.at, want, part.len) == 0;
}

static bool operands_are(const struct shckit_line *line,
                         const char *const want[], size_t count)
{
    struct shckit_span operand;
    size_t at = 0;
    size_t seen = 0;
    while (shckit_operand(line, &at, &operand)) {
        if (seen == count || !part_is(line, operand, want[seen])) {
            return false;
        }
        seen++;
    }
    return seen == count;
}

static bool typed_as(const char *in, int kind, int directive)
{
    struct shckit_text *text = read_text(in);
    const struct shckit_line *line = text != NULL ? shckit_line(text, 0) : NULL;
    bool ok = line != NULL && shckit_count(text) == 1 && line->kind == kind &&
              line->directive == directive;
    if (line != NULL && !ok) {
        printf("  typed as %d, %d\n", line->kind, line->directive);
    }
    shckit_free(text);
    return ok;
}

static bool read_refuses(const char *in, size_t len, int error, size_t at)
{
    struct shckit_text *text = NULL;
    size_t line = SIZE_MAX;
    int got = shckit_read(&text, in, len, &line);
    bool ok = got == error && line == at && text == NULL;
    if (!ok) {
        printf("  got \"%s\" at line %zu\n", shckit_strerror(got), line);
    }
    shckit_free(text);
    return ok;
}

static bool same_bytes(const struct shckit_text *text, const char *want)
{
    // whether the text's bytes, line ends included, are exactly want, and a
    // buffer a byte short gets SHCKIT_FULL, the size, and nothing written
    size_t size = 0;
    size_t short_size = 0;
    unsigned char *bytes = NULL;
    bool ok = shckit_write(text, NULL, 0, &size) == SHCKIT_OK && size != 0;
    bytes = malloc(size != 0 ? size : 1);
    if (bytes == NULL) {
        fputs("test: out of memory\n", stderr);
        exit(1);
    }
    memset(bytes, '#', size);
    ok = ok &&
         shckit_write(text, bytes, size - 1, &short_size) == SHCKIT_FULL &&
         short_size == size;
    for (size_t i = 0; ok && i < size; i++) {
        ok = bytes[i] == '#';
    }
    ok = ok && shckit_write(text, bytes, size, NULL) == SHCKIT_OK &&
         size == strlen(want) && memcmp(bytes, want, size) == 0;
    if (!ok) {
        printf("  got:\n%.*s", (int)size, (const char *)bytes);
    }
    free(bytes);
    return ok;
}

static void test_kinds(void)
{
    static const char instruction[] = S("MOV.L       L257+2,R3  ; v");
    static const char *const two[] = {"L257+2", "R3"};
    static const char *const pair[] = {"@(4,R4)", "R14"};
    static const char *const spaced[] = {"FPUL", "DR0"};
    static const char *const gap[] = {"1", "", "2"};
    static const char *const list[] = {"H'1", "H'2"};
    struct shckit_text *text = read_text(instruction);
    const struct shckit_line *line = text != NULL ? shckit_line(text, 0) : NULL;

    if (CHECK(line != NULL)) {
        CHECK(line->kind == SHCKIT_INSTRUCTION && line->directive == -1);
        CHECK(line->label.len == 0);
        CHECK(part_is(line, line->name, "MOV.L"));
        CHECK(part_is(line, line->operands, "L257+2,R3"));
        CHECK(part_is(line, line->comment, "; v"));
        CHECK(operands_are(line, two, 2));
        CHECK(line->input == 1 && line->patch == -1 && line->pool == 0);
        CHECK(!line->crlf && line->code);
    }
    shckit_free(text);

    CHECK(typed_as("\n", SHCKIT_BLANK, -1));
    CHECK(typed_as("   \t\n", SHCKIT_BLANK, -1));
    CHECK(typed_as("; a comment\n", SHCKIT_COMMENT, -1));
    CHECK(typed_as("                    ; frame size=0\n", SHCKIT_COMMENT, -1));
    CHECK(typed_as("_njInit3D:\n", SHCKIT_LABEL, -1));
    CHECK(typed_as("L257:                    \n", SHCKIT_LABEL, -1));
    CHECK(typed_as("_f:          ; function: f\n", SHCKIT_LABEL, -1));
    CHECK(typed_as("?L1: MOV R0,R1\n", SHCKIT_INSTRUCTION, -1));
    CHECK(typed_as("x:MOV R0,R1\n", SHCKIT_INSTRUCTION, -1));
    CHECK(typed_as(S("CMP/EQ      #0,R0"), SHCKIT_INSTRUCTION, -1));
    CHECK(typed_as(S("BF/S        L12"), SHCKIT_INSTRUCTION, -1));
    CHECK(typed_as(S("RTS"), SHCKIT_INSTRUCTION, -1));
    CHECK(typed_as(S("nop"), SHCKIT_INSTRUCTION, -1));
    CHECK(
        typed_as(S(".DATA.L     H'437F0000"), SHCKIT_DIRECTIVE, SHCKIT_DATA_L));
    CHECK(typed_as(S(".RES.W      1"), SHCKIT_DIRECTIVE, SHCKIT_RES_W));
    CHECK(typed_as(S(".FDATA.S    F'256.0"), SHCKIT_DIRECTIVE, SHCKIT_FDATA_S));
    CHECK(typed_as(S(".DATAB.B    10,0"), SHCKIT_DIRECTIVE, SHCKIT_DATAB_B));
    CHECK(typed_as(S(".DATAB.W    1,0"), SHCKIT_DIRECTIVE, SHCKIT_DATAB_W));
    CHECK(typed_as(S(".DATAB.L    16,0"), SHCKIT_DIRECTIVE, SHCKIT_DATAB_L));
    CHECK(typed_as(S(".POOL"), SHCKIT_DIRECTIVE, SHCKIT_POOL));
    CHECK(typed_as(S(".NOPOOL     ; c"), SHCKIT_DIRECTIVE, SHCKIT_NOPOOL));
    CHECK(typed_as(S(".END"), SHCKIT_DIRECTIVE, SHCKIT_END));
    CHECK(typed_as(S(".ALIGN      4"), SHCKIT_DIRECTIVE, SHCKIT_ALIGN));
    CHECK(typed_as("\t.ALIGN 016 ; c\n", SHCKIT_DIRECTIVE, SHCKIT_ALIGN));
    CHECK(typed_as(S(".SECTION    BSG32,DATA,ALIGN=32"), SHCKIT_DIRECTIVE,
                   SHCKIT_SECTION));
    CHECK(typed_as("x: .DATA.W 1\n", SHCKIT_DIRECTIVE, SHCKIT_DATA_W));
    CHECK(typed_as("+                     H'0,H'0\n", SHCKIT_CONTINUATION, -1));
    CHECK(typed_as("SYSCALL:        .EQU    H'8C0000B0\n", SHCKIT_DIRECTIVE,
                   SHCKIT_EQU));
    CHECK(typed_as("CX:  .EQU 0 ; the center\n", SHCKIT_DIRECTIVE, SHCKIT_EQU));
    CHECK(typed_as("X:.EQU -(4*Y)\n", SHCKIT_DIRECTIVE, SHCKIT_EQU));
    CHECK(typed_as("ENTRY: .ASSIGN H'8C0000B0\n", SHCKIT_DIRECTIVE,
                   SHCKIT_ASSIGN));
    CHECK(
        typed_as("N:      .ASSIGN N+1 ; c\n", SHCKIT_DIRECTIVE, SHCKIT_ASSIGN));

    // unknown lines: a comment that starts with *, a word in
    // the first column without a colon, a label further in, directives
    // other than its own, .SECTION as SHC doesn't write it, .ALIGN with
    // other than a decimal number, .EQU or .ASSIGN without a label or with
    // other than one value, .POOL or .NOPOOL with an operand, a continuation
    // with nothing to add, and operands that don't parse
    CHECK(typed_as("* comment\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("MOV R0,R1\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("1x:\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("    x: MOV R0,R1\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".data.l     1"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".LINE       12"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".ALIGN4"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".ALIGN"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".ALIGN      H'4"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".ALIGN      2+2"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".ALIGN      4,2"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".DATA"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".SECTION    P, CODE, ALIGN=4"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".SECTION    P"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".SECTION    P,CODE"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".SECTION    P,STACK,ALIGN=4"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".SECTION    P,CODE,ALIGN=04"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".SECTION    P,code,ALIGN=4"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".EQU        4"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X       .EQU    4\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X:      .EQU\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X:      .EQU    ; c\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X:      .EQU    1,2\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X:      .EQU    ,1\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("x:      .equ    4\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".ASSIGN     4"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X       .ASSIGN 4\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X:      .ASSIGN\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X:      .ASSIGN 1,2\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("X:      .ASSIGN ,1\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("x:      .assign 4\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".POOL       1"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".NOPOOL     L10"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".nopool"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("+\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("+   ; nothing\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S("1MOV"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S("MOV,R1"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S("MOV.L       @(R0,R1,R2"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S("MOV.L       R0),R1"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as(S(".SDATA      \"abc"), SHCKIT_UNKNOWN, -1));

    // a comma inside parentheses or a string doesn't end an operand, blanks
    // around one are not part of it, and an operand can be empty
    text =
        read_text(S("MOV.L       @(4,R4),R14") S("FSCA        FPUL , DR0")
                      S(".DATA.L     1,,2") "+          H'1,H'2   ; more\n" S(
                          "MOV.L       L1057+10,R8; H'00008000"));
    if (CHECK(text != NULL)) {
        CHECK(operands_are(shckit_line(text, 0), pair, 2));
        CHECK(operands_are(shckit_line(text, 1), spaced, 2));
        CHECK(part_is(shckit_line(text, 1), shckit_line(text, 1)->operands,
                      "FPUL , DR0"));
        CHECK(operands_are(shckit_line(text, 2), gap, 3));
        CHECK(operands_are(shckit_line(text, 3), list, 2));
        CHECK(part_is(shckit_line(text, 3), shckit_line(text, 3)->comment,
                      "; more"));
        CHECK(part_is(shckit_line(text, 4), shckit_line(text, 4)->operands,
                      "L1057+10,R8"));
        CHECK(part_is(shckit_line(text, 4), shckit_line(text, 4)->comment,
                      "; H'00008000"));
    }
    shckit_free(text);

    // an .EQU's label is the symbol it gives the value
    text = read_text("CX:             .EQU    0               ; the center\n");
    line = text != NULL ? shckit_line(text, 0) : NULL;
    if (CHECK(line != NULL)) {
        CHECK(part_is(line, line->label, "CX"));
        CHECK(part_is(line, line->name, ".EQU"));
        CHECK(part_is(line, line->operands, "0"));
        CHECK(part_is(line, line->comment, "; the center"));
    }
    shckit_free(text);
}

static void test_strings(void)
{
    // a string may hold a ;, a comma, a doubled quote, and CP932
    static const char *const one[] = {"\"a;b,c\"\"d\""};
    static const char *const kana[] = {"\"\x83\x5c\x81\x40\"", "0"};
    struct shckit_text *text =
        read_text(S(".SDATA      \"a;b,c\"\"d\"   ; e")
                      S(".SDATA      \"\x83\x5c\x81\x40\",0"));
    if (CHECK(text != NULL)) {
        const struct shckit_line *line = shckit_line(text, 0);
        CHECK(line->kind == SHCKIT_DIRECTIVE &&
              line->directive == SHCKIT_SDATA);
        CHECK(operands_are(line, one, 1));
        CHECK(part_is(line, line->comment, "; e"));
        CHECK(operands_are(shckit_line(text, 1), kana, 2));
    }
    shckit_free(text);
}

static void test_bytes(void)
{
    // the text is CP932, and a trail byte can look like @, \, [ or a letter.
    // A comment or a string may hold it; anywhere else makes the line
    // unknown.
    static const char comment[] =
        "; \x82\xa0\x83\x5c\x81\x40\x83\x41\x81\x5b\xb1\n";
    static const char mixed[] = "a:\r\n\r\n    NOP\n";
    struct shckit_text *text = read_text(comment);

    if (CHECK(text != NULL)) {
        CHECK(shckit_line(text, 0)->kind == SHCKIT_COMMENT);
        CHECK(same_bytes(text, comment));
    }
    shckit_free(text);
    CHECK(typed_as(S("NOP         ; \x83\x5c"), SHCKIT_INSTRUCTION, -1));
    CHECK(typed_as(S("MOV         \x82\xa0,R0"), SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("\x82\xa0:\n", SHCKIT_UNKNOWN, -1));
    CHECK(typed_as("    \x82\xa0\n", SHCKIT_UNKNOWN, -1));

    // a lead byte with no trail byte after it, a trail byte out of range,
    // and the bytes CP932 leaves unused
    CHECK(read_refuses("; \x81\n", 4, SHCKIT_NOT_CP932, 1));
    CHECK(read_refuses("; \x81\r\n", 5, SHCKIT_NOT_CP932, 1));
    CHECK(read_refuses("\n; \x81\x7f\n", 6, SHCKIT_NOT_CP932, 2));
    CHECK(read_refuses("; \x81\x3f\n", 5, SHCKIT_NOT_CP932, 1));
    CHECK(read_refuses("; \x80\n", 4, SHCKIT_NOT_CP932, 1));
    CHECK(read_refuses("; \xa0\n", 4, SHCKIT_NOT_CP932, 1));
    CHECK(read_refuses("; \xfd\n", 4, SHCKIT_NOT_CP932, 1));

    // control characters, a CR that ends no line among them
    CHECK(read_refuses("\0\n", 2, SHCKIT_CONTROL, 1));
    CHECK(read_refuses("a:\rb:\n", 6, SHCKIT_CONTROL, 1));
    CHECK(read_refuses("\n\f\n", 3, SHCKIT_CONTROL, 2));
    CHECK(read_refuses("; \x7f\n", 4, SHCKIT_CONTROL, 1));

    // an empty text, and a last line without a line end
    CHECK(read_refuses("", 0, SHCKIT_UNENDED, 0));
    CHECK(read_refuses("    NOP", 7, SHCKIT_UNENDED, 1));
    CHECK(read_refuses("    NOP\n    RTS", 15, SHCKIT_UNENDED, 2));

    // each line keeps its own line end
    text = read_text(mixed);
    if (CHECK(text != NULL)) {
        CHECK(shckit_count(text) == 3);
        CHECK(shckit_line(text, 0)->crlf && shckit_line(text, 1)->crlf &&
              !shckit_line(text, 2)->crlf);
        CHECK(shckit_line(text, 1)->kind == SHCKIT_BLANK);
        CHECK(same_bytes(text, mixed));
    }
    shckit_free(text);
}

static void test_sections(void)
{
    // a line is in the section of the last .SECTION line, that one
    // included, and above any, in asmsh's code section P
    struct shckit_text *text =
        read_text(S(".IMPORT     _a") CODE S("NOP") DATA S(".DATA.L     1")
                      S(".SECTION    PSG,CODE,ALIGN=4") S("RTS"));
    if (CHECK(text != NULL)) {
        CHECK(shckit_line(text, 0)->code);
        CHECK(shckit_line(text, 1)->code && shckit_line(text, 2)->code);
        CHECK(!shckit_line(text, 3)->code && !shckit_line(text, 4)->code);
        CHECK(shckit_line(text, 5)->code && shckit_line(text, 6)->code);
    }
    shckit_free(text);
}

static void test_pools(void)
{
    // a pool is a label Lnn alone on a code line and the .DATA and .RES lines
    // right below it, whatever their columns
    struct shckit_text *text = read_text(
        CODE L10 RESERVE S(".DATA.L     _a") "L11:  ; c\n"
                                             "    .DATA.W 1\n" S("RTS"));
    if (CHECK(text != NULL)) {
        CHECK(shckit_line(text, 0)->pool == 0);
        CHECK(shckit_line(text, 1)->pool == 1 &&
              shckit_line(text, 1)->kind == SHCKIT_LABEL);
        CHECK(shckit_line(text, 2)->pool == 1);
        CHECK(shckit_line(text, 3)->pool == 1);
        CHECK(shckit_line(text, 4)->pool == 2 &&
              shckit_line(text, 5)->pool == 2);
        CHECK(shckit_line(text, 6)->pool == 0);
    }
    shckit_free(text);

    // not pools: in a data section, with a comment or a label between, a
    // label that isn't Lnn, as inline text's are, an item with a label, a
    // label with a statement on its line, and a label over an instruction
    text = read_text(
        DATA L10 S(".DATA.L     _a") CODE L11
        "; c\n" S(".DATA.L     _a") "?MASK:\n" S(".DATA.L     _a") "L12x:\n" S(
            ".DATA.L     _a") "l13:\n" S(".DATA.L     _a") L12
        "y: .DATA.L _a\n"
        "L15: .DATA.L _a\n"
        "L:\n" S(".DATA.L     _a") L10 S("RTS"));
    if (CHECK(text != NULL)) {
        for (size_t i = 0; i < shckit_count(text); i++) {
            CHECK(shckit_line(text, i)->pool == 0);
        }
    }
    shckit_free(text);
}

// Flow bits, abbreviated for the table below.
#define DELAYED SHCKIT_DELAYED
#define NO_FALL SHCKIT_NO_FALL_THROUGH
#define ILLEGAL SHCKIT_SLOT_ILLEGAL
#define BRANCH (DELAYED | NO_FALL | ILLEGAL)

static int flow_of(const char *in)
{
    // shckit_flow of in's first line, or -2 if in doesn't read
    struct shckit_text *text = read_text(in);
    int bits = text != NULL ? shckit_flow(text, 0) : -2;
    shckit_free(text);
    return bits;
}

static void test_opcodes(void)
{
    // a sample of every kind of row, each way to spell an operand, and what
    // each does to the flow of control
    static const struct {
        const char *line;
        int bits;
    } held[] = {
        {S("ADD         #4,R3"), 0},
        {S("ADD.L       R1,R2"), 0},
        {S("XTRCT       R1,R0"), 0},
        {S("AND.B       #1,@(R0,GBR)"), 0},
        {S("TST         #1,R0"), 0},
        {S("CMP/EQ      #1,R0"), 0},
        {S("SHLL16      R3"), 0},
        {S("DT          R3"), 0},
        {S("EXTS.B      R1,R2"), 0},
        {S("EXTU        R1,R2"), 0},
        {S("MULS.W      R1,R2"), 0},
        {S("MULS.L      R1,R2"), 0},
        {S("MAC.L       @R1+,@R2+"), 0},
        {S("TAS.B       @R3"), 0},
        {S("MOVCA.L     R0,@R3"), 0},
        {S("OCBWB       @R3"), 0},
        {S("CLRMAC"), 0},
        {S("SLEEP"), 0},
        {S("FADD        FR1,FR2"), 0},
        {S("FADD.D      DR2,DR4"), 0},
        {S("FCMP/GT.S   FR0,FR15"), 0},
        {S("FSQRT       DR14"), 0},
        {S("FMAC        FR0,FR1,FR2"), 0},
        {S("FMOV.S      @R3+,FR1"), 0},
        {S("FMOV        XD2,@-R15"), 0},
        {S("FMOV.D      @(R0,R3),XD14"), 0},
        {S("FMOV        DR2,XD4"), 0},
        {S("FIPR        FV4,FV12"), 0},
        {S("FTRV        XMTRX,FV0"), 0},
        {S("FSCA        FPUL,DR2"), 0},
        {S("FSRRA       FR7"), 0},
        {S("FCNVDS      DR2,FPUL"), 0},
        {S("FLOAT       FPUL,DR2"), 0},
        {S("FSTS        FPUL,FR3"), 0},
        {S("LDC         R1,R7_BANK"), 0},
        {S("LDC.L       @R15+,DBR"), 0},
        {S("STC.L       SGR,@-R15"), 0},
        {S("STC         R0_BANK,R1"), 0},
        {S("LDS.L       @R15+,PR"), 0},
        {S("LDS.L       R1,PR"), 0},
        {S("STS         FPSCR,R1"), 0},
        {S("MOV.L       @(4,R3),R1"), 0},
        {S("MOV         @(4,R3),R0"), 0},
        {S("MOV.B       @(4,R3),R0"), 0},
        {S("MOV.W       R0,@(2,GBR)"), 0},
        {S("MOV.L       @(R0,R3),R1"), 0},
        {S("MOV.B       R1,@-R15"), 0},
        {S("MOV         #1,R1"), 0},
        {S("MOV.B       #200,R1"), 0},
        // a load of a literal or an address from the code is PC-relative,
        // and so is illegal in a slot, as are TRAPA and an LDC to SR
        {S("MOV.W       @(4,PC),R1"), ILLEGAL},
        {S("MOV         L10,R1"), ILLEGAL},
        {S("MOV.L       #H'12345678,R1"), ILLEGAL},
        {S("MOV.W       #-1,R1"), ILLEGAL},
        {S("MOVA        @(4,PC),R0"), ILLEGAL},
        {S("MOVA        L10,R0"), ILLEGAL},
        {S("MOVA        #H'3F000000,R0"), ILLEGAL},
        {S("TRAPA       #1"), ILLEGAL},
        {S("LDC         R1,SR"), ILLEGAL},
        {S("LDC.L       @R15+,SR"), ILLEGAL},
        {S("LDC         R1,GBR"), 0},
        // branches
        {S("BT          L10"), ILLEGAL},
        {S("BF          L10"), ILLEGAL},
        {S("BT/S        L10"), DELAYED | ILLEGAL},
        {S("BF/S        L10"), DELAYED | ILLEGAL},
        {S("BSR         _f"), DELAYED | ILLEGAL},
        {S("BSRF        R1"), DELAYED | ILLEGAL},
        {S("JSR         @R1"), DELAYED | ILLEGAL},
        {S("BRA         L10"), BRANCH},
        {S("BRAF        R1"), BRANCH},
        {S("JMP         @R1"), BRANCH},
        {S("RTS"), BRANCH},
        {S("RTE"), BRANCH},
        // operands as asmsh reads them: any case, blanks between tokens, SP
        // for R15, @(Rn) for @Rn, a register's name with other numbers a
        // symbol, and expressions with word operators
        {S("mov.l       @r3+,r4"), 0},
        {S("Nop"), 0},
        {S("LDC         R1,r2_bank"), 0},
        {S("MOV.L       @SP+,R1"), 0},
        {S("MOV.L       R1,@-sp"), 0},
        {S("MOV.L       @( 4 , SP ),R1"), 0},
        {S("MOV.L       @ R3 +,R1"), 0},
        {S("MOV.L       R1,@ - R3"), 0},
        {S("MOV         # 4 , R1"), 0},
        {S("MOV.L       @(R3),R1"), 0},
        {S("JMP         @(R3)"), BRANCH},
        {S("MOV.L       @((CM_FLAGS),R4),R0"), 0},
        {S("MOV.L       @(STARTOF P,R4),R0"), 0},
        {S("MOV.L       #SIZEOF P+4,R1"), ILLEGAL},
        {S("MOV.L       #\"a,b\",R1"), ILLEGAL},
        {S("BRA         LAB + 2"), BRANCH},
        {S("BRA         (LAB)"), BRANCH},
        {S("MOV.L       R16,R1"), ILLEGAL},
        {S("BRA         DR1"), BRANCH},
        {S("BRA         R01"), BRANCH},
        {S("BT          ?L1"), ILLEGAL},
        // a continuation gives an instruction the rest of its operands,
        // after a comma at the end of the field above or at its own start.
        // An instruction without operands takes none.
        {S("MOV         R1,") "+           R2\n", 0},
        {S("FMAC        FR0,") "+   FR1,\n"
                               "+   FR2 ; c\n",
         0},
        {S("ADD         R1") "+           ,R2\n", 0},
        {S("MOV.L       @(4,R3)") "+   ,R1\n", 0},
        {S("ADD         R1") "+   ,\n"
                             "+   R2\n",
         0},
        {S("NOP") "+           ,R2\n", 0},
    };
    static const char *const not_held[] = {
        // a mnemonic or size asmsh doesn't take, or extra dots
        S("FOO         R1"),
        S("MOVLI.L     @R1,R0"),
        S("PADD        X0,Y0,A0"),
        S("BT.S        L10"),
        S("MOV.Q       R1,R2"),
        S("ADD.W       R1,R2"),
        S("DT.L        R3"),
        S("FADD.D      FR1,FR2"),
        S("MOV.L.L     R1,R2"),
        S("MOV.1       R1,R2"),
        S("MOV.       R1,R2"),
        // R0 or FR0 alone where a form wants it, and a form that's another
        // size's
        S("MOV.B       @(4,R3),R3"),
        S("AND         #1,R3"),
        S("MOVA        L10,R1"),
        S("FMAC        FR1,FR2,FR3"),
        S("MOV.L       @(4,GBR),R1"),
        S("MOV.B       R1,R2"),
        S("FMOV.S      @R3,DR2"),
        S("FMOV.D      @R3,FR2"),
        S("FADD        FR1,DR2"),
        S("FMOV        DR1,DR2"),
        S("LDC         R1,R8_BANK"),
        S("STC         SGR,@R1"),
        S("LDS         R1,SR"),
        // too many operands or too few
        S("MOV         R1,R2,R3"),
        S("NOP         R1"),
        S("RTS         x"),
        S("BRA"),
        S("BRA         L10,"),
        S("MOV         R1,,R2"),
        S("MOV         ,R1"),
        S("FMAC        FR0,FR1"),
        // registers where a form takes none, or within an expression
        S("BRA         R3"),
        S("BRA         PC"),
        S("BRA         SP"),
        S("BRA         R3+4"),
        S("MOV.L       #R3,R1"),
        S("MOV.L       @(R0+4,R3),R1"),
        // a blank inside a name, or two words with no operator
        S("MOV         R 1,R2"),
        S("MOV         R1,R2 extra"),
        S("BRA         LAB 2"),
        S("MOV         #H'1 2,R1"),
        // @ forms asmsh doesn't read
        S("MOV.L       @(R3,R0),R1"),
        S("MOV.L       @(4,R3,R5),R1"),
        S("MOV.L       @(4),R1"),
        S("MOV.L       @(),R1"),
        S("MOV.L       @(,R3),R1"),
        S("MOV.L       @LAB,R1"),
        S("MOV.L       @(GBR,R0),R0"),
        S("MOV.L       @(R0,PC),R1"),
        S("MOV.L       @(4,R3)+,R1"),
        S("MOV.L       @(R3)+,R1"),
        S("MOV.L       @-(R3),R1"),
        S("MOV.L       @((R3)),R1"),
        S("MOV.L       @R3++,R1"),
        S("MOV.L       @@R3,R1"),
        S("MOV.L       @-R3+,R1"),
        S("JMP         @(R0,R3)"),
        S("JMP         @R3+"),
        S("MOV         #,R1"),
        // an operand that runs on to a continuation, which shckit doesn't
        // read, an empty one between two lines' commas, more operands than
        // a form holds, and no continuation where the field needs one
        S("MOV         R1") "+           R2\n",
        S("ADD         R1,") "+           ,R2\n",
        S("ADD         R1,R2") "+           ,R3\n",
        S("ADD         R1") "+           ,\n",
        S("MOV         R1,"),
    };

    for (size_t i = 0; i < sizeof held / sizeof held[0]; i++) {
        int bits = flow_of(held[i].line);
        if (!CHECK(bits == held[i].bits)) {
            printf("  %s  is %d\n", held[i].line, bits);
        }
    }
    for (size_t i = 0; i < sizeof not_held / sizeof not_held[0]; i++) {
        int bits = flow_of(not_held[i]);
        if (!CHECK(bits == -1)) {
            printf("  %s  is %d\n", not_held[i], bits);
        }
    }

    // a line that isn't an instruction, and a line past the text
    CHECK(flow_of(S(".DATA.W     H'F77D")) == -1);
    CHECK(flow_of("_f:\n") == -1);
    CHECK(flow_of("+   R1\n") == -1);
    CHECK(flow_of("* x\n") == -1);
    struct shckit_text *text = read_text(S("NOP"));
    if (CHECK(text != NULL)) {
        CHECK(shckit_flow(text, 0) == 0 && shckit_flow(text, 1) == -1);
    }
    shckit_free(text);
}

// shckit_check or shckit_lint.
typedef int ruleset(const struct shckit_text *text,
                    struct shckit_finding **found, size_t *count);

static bool finds(ruleset *rules, const char *in, const char *want)
{
    // the rules refuse exactly want: each line and rule, a comma between two
    struct shckit_text *text = read_text(in);
    struct shckit_finding *found = NULL;
    size_t count = 0;
    char got[2048] = "";
    size_t len = 0;
    bool ok = text != NULL && rules(text, &found, &count) == SHCKIT_OK;
    for (size_t i = 0; ok && i < count; i++) {
        int put = snprintf(got + len, sizeof got - len, "%s%zu %s",
                           i != 0 ? ", " : "",
                           shckit_line(text, found[i].index)->input,
                           shckit_rule_name(found[i].rule));
        ok = put >= 0 && (size_t)put < sizeof got - len;
        len += ok ? (size_t)put : 0;
    }
    ok = ok && strcmp(got, want) == 0;
    if (!ok) {
        printf("  got \"%s\"\n", got);
    }
    free(found);
    shckit_free(text);
    return ok;
}

static bool checks(const char *in, const char *want)
{
    return finds(shckit_check, in, want);
}

static void test_check(void)
{
    // a text as it should be: data after the slot of a branch that never
    // falls through, past a .NOPOOL too, at a section's start and anywhere
    // in a data section, and nothing past .END but blanks and comments
    CHECK(checks(CODE "_f:\n" S("MOV.L       L10,R1") S("RTS") S("NOP")
                     L10 S(".DATA.L     _a") S(".SECTION    Q,CODE,ALIGN=4")
                         S(".DATA.W     1") S("NOP") DATA S(".DATA.L     2")
                             S(".END") "; the end\n"
                                       "   \n",
                 ""));
    static const char *const stops[] = {"BRA         _f", "BRAF        R1",
                                        "JMP         @R1", "RTS", "RTE"};
    for (size_t i = 0; i < sizeof stops / sizeof stops[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE "          %s\n" S("NOP") S(".DATA.W     0"),
                       stops[i]);
        CHECK(checks(in, ""));
    }

    // a trap's handler may not come back: SHC ends an interrupt function
    // with TRAPA under tn= and writes its pool right after
    CHECK(checks(CODE S("MOV.L       @R15+,R0") S("TRAPA       #6") "L1:\n" S(
                     ".DATA.L     _k") S(".DATA.L     H'8C200000"),
                 ""));
    CHECK(checks(CODE S("TRAPA       #6") S("NOP") S(".DATA.W     0"),
                 "4 data-in-flow"));
    CHECK(checks(DATA S(".DATAB.B    10,0") S(".DATAB.L    2,0") CODE S("RTS")
                     S("NOP") S(".DATAB.W    1,0"),
                 ""));
    CHECK(checks(CODE S("RTS") S("NOP") S(".NOPOOL") S(".DATA.W     0"), ""));

    // .ASSIGN writes nothing, before a slot or anywhere else
    CHECK(
        checks("ENTRY: .ASSIGN H'8C0000B0\n" CODE S("MOV.L       #ENTRY,R1")
                   S("JMP         @R1") "ENTRY: .ASSIGN H'8C0000B4\n" S("NOP"),
               ""));
    CHECK(checks(CODE S("RTS") "X:        .ASSIGN     1\n" S(".DATA.W     0"),
                 "4 data-in-flow"));

    // data that control falls into: after an instruction that falls
    // through, BT or BF, a call's slot or BT/S's, as a slot, past a label,
    // an .ALIGN, a .NOPOOL or a continuation, before any .SECTION, which is
    // code, and where a section resumes. Every kind of data line.
    CHECK(checks(CODE S("FIPR        FV4,FV4") S(".DATA.W     H'F77D ; FSRRA")
                     S("OCBI        @R5"),
                 "3 data-in-flow"));
    static const char *const data[] = {
        ".DATA.B     1",      ".DATA.W     1",     ".DATA.L     1",
        ".RES.B      1",      ".RES.W      1",     ".RES.L      1",
        ".SDATA      \"ab\"", ".FDATA.S    F'1.0", ".DATAB.B    4,0",
    };
    for (size_t i = 0; i < sizeof data / sizeof data[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, CODE S("NOP") "          %s\n", data[i]);
        CHECK(checks(in, "3 data-in-flow"));
    }
    CHECK(
        checks(CODE S("BT          _f") S(".DATA.W     0"), "3 data-in-flow"));
    CHECK(checks(CODE S("BT/S        _f") S("NOP") S(".DATA.W     0"),
                 "4 data-in-flow"));
    CHECK(checks(CODE S("BSR         _f") S("NOP") S(".DATA.W     0"),
                 "4 data-in-flow"));
    CHECK(checks(CODE S("JSR         @R1") S("NOP") S(".DATA.W     0"),
                 "4 data-in-flow"));
    CHECK(checks(CODE S("RTS") S(".DATA.W     H'0009") S(".DATA.W     0"),
                 "3 data-in-flow"));
    CHECK(checks(CODE S("RTS") ALIGN "?x:\n" S(".DATA.L     1"),
                 "5 data-in-flow"));
    CHECK(checks(CODE S("NOP") S(".NOPOOL") S(".DATA.W     1"),
                 "4 data-in-flow"));
    CHECK(checks(CODE S("NOP") "L1:\n" S(".DATA.W     1") S(".DATA.W     2"),
                 "4 data-in-flow, 5 data-in-flow"));
    CHECK(checks(CODE S("NOP") S(".DATA.W     1,") "+           2\n",
                 "3 data-in-flow"));
    CHECK(checks(S("NOP") S(".DATA.W     1"), "2 data-in-flow"));
    CHECK(checks(S("NOP") CODE S(".DATA.W     1"), "3 data-in-flow"));
    CHECK(checks(CODE S("NOP") DATA S(".DATA.L     0") CODE S(".DATA.W     1"),
                 "6 data-in-flow"));

    // an instruction illegal in the delay slot it stands in: past labels,
    // blanks, comments and an .ALIGN
    static const char *const slots[][2] = {
        {"BRA         _f", "BRA         _g"},
        {"RTS", "MOV.L       #1,R1"},
        {"JSR         @R1", "MOVA        L10,R0"},
        {"BT/S        _f", "BT          _g"},
        {"RTE", "TRAPA       #1"},
        {"BRAF        R1", "LDC         R1,SR"},
        {"BSR         _f", "MOV.W       @(4,PC),R1"},
        {"BF/S        _f", "LDC.L       @R15+,SR"},
        {"BSRF        R1", "MOV         L10,R1"},
        {"JMP         @R1", "RTS"},
    };
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE "          %s\n          %s\n" S("NOP"),
                       slots[i][0], slots[i][1]);
        CHECK(checks(in, "3 delay-slot"));
    }
    CHECK(checks(CODE S("RTS") "L1:\n\n; c\n" ALIGN S("BRA         _f")
                     S("NOP"),
                 "7 delay-slot"));

    // what a slot may hold, and a branch outside one. A data section's slots
    // are checked too.
    static const char *const legal[] = {
        "NOP",
        "MOV         #1,R1",
        "MOV.B       #1,R1",
        "LDS.L       @R15+,PR",
        "STC         SR,R0",
        "SLEEP",
        "FADD        FR1,FR2",
    };
    for (size_t i = 0; i < sizeof legal / sizeof legal[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE S("JMP         @R1") "          %s\n", legal[i]);
        CHECK(checks(in, ""));
    }
    CHECK(checks(CODE S("RTS") S("NOP") S("BRA         _f") S("NOP"), ""));
    CHECK(checks(CODE S("BT          _f") S("BRA         _g") S("NOP"), ""));
    CHECK(checks(DATA S("RTS") S("BRA         _f"), "3 delay-slot"));

    // a delayed branch with no instruction or data after it in its
    // section: at the text's end, at .END, past an .ALIGN or a label, or
    // before a section that never resumes its own. Its slot is whatever
    // follows the section. A slot waits for the section to resume, and a
    // branch in a slot opens none.
    CHECK(checks(CODE S("RTS"), "2 no-slot"));
    CHECK(checks(S("NOP") S("RTS") S(".END"), "2 no-slot"));
    CHECK(checks(CODE S("BRA         _f") ALIGN "L1:\n", "2 no-slot"));
    CHECK(
        checks(CODE S("JSR         @R1") DATA S(".DATA.L     1"), "2 no-slot"));
    CHECK(checks(CODE S("BT/S        _f") S(".END") S("NOP"),
                 "2 no-slot, 4 after-end"));
    CHECK(
        checks(CODE S("RTS") DATA S("BRA         _f"), "2 no-slot, 4 no-slot"));
    CHECK(checks(CODE S("RTS") DATA S(".DATA.L     1") CODE S("NOP"), ""));
    CHECK(checks(CODE S("RTS") S("BRA         _f"), "3 delay-slot"));

    // past .END, asmsh reads nothing: any line but a blank or a comment
    CHECK(checks(CODE S("RTS") S("NOP") S(".END") "_f:\n" S("NOP")
                     S(".DATA.W     1,") "+           2\n" S(".END"),
                 "5 after-end, 6 after-end, 7 after-end, 8 after-end, "
                 "9 after-end"));

    // past .END a line shckit can't read, or an instruction outside the
    // table, is after-end's alone
    CHECK(checks(CODE S("NOP") S(".DATA.W     1")
                     S(".END") "* x\n" S("FOO") "+   1\n",
                 "3 data-in-flow, 5 after-end, 6 after-end, 7 after-end"));

    // every line shckit can't read and every instruction outside the
    // table, up to .END, and then nothing else
    CHECK(checks(CODE "* x\n" S(".END") "* y\n" S("FOO"), "2 unknown-line"));
    CHECK(checks(CODE "* x\n" S("NOP") S(".DATA.W     1") "    y: NOP\n",
                 "2 unknown-line, 5 unknown-line"));
    CHECK(checks(CODE S("FOO") S("NOP") S(".DATA.W     1")
                     S("MOV.B       @(4,R3),R3") S(".END") S("NOP"),
                 "2 unknown-instruction, 5 unknown-instruction"));
    CHECK(
        checks(CODE S("FOO") "* x\n", "2 unknown-instruction, 3 unknown-line"));

    // a continuation joined to no statement: below a label, a comment, a
    // blank or an instruction without operands, below a directive's field
    // without a comma at its end, and one an operand runs on to. One below
    // a field that ends with a comma, or that starts with one below an
    // instruction, or below another such, is read.
    CHECK(checks(
        CODE "_f:\n+   1\n; c\n+   2\n\n+   3\n" S(".DATA.W     4") "+   5\n",
        "3 unknown-line, 5 unknown-line, 7 unknown-line, "
        "9 unknown-line"));
    CHECK(checks(CODE S("NOP") "+   R1\n", "3 unknown-line"));
    CHECK(checks(CODE S(".DATA.W     1") "+   ,2\n", "3 unknown-line"));
    CHECK(checks(CODE S(".DATA.W     1,") "+   2\n"
                                          "+   ,3\n",
                 "4 unknown-line"));
    CHECK(checks(CODE S("ADD         R1") "+   R2\n",
                 "2 unknown-instruction, 3 unknown-line"));
    CHECK(checks("+   1\n", "1 unknown-line"));
    CHECK(checks(CODE S("RTS") S("NOP") S(".DATA.W     1,") "+   2,\n"
                                                            "+   3\n",
                 ""));
    CHECK(checks(CODE S("ADD         R1") "+   ,R2\n" S("RTS") S("NOP"), ""));
}

static bool lints(const char *in, const char *want)
{
    return finds(shckit_lint, in, want);
}

static void test_lint(void)
{
    // a text that reads well: one column for each field of a statement
    // alone, and one for each after a label, which a long label pushes past,
    // one line end, and a comment line right above each exported label
    CHECK(lints(
        S(".EXPORT     _f,X") "; a value\n"
                              "X:      .EQU    4       ; four\n"
                              "LONGER_X:   .EQU 8\n" CODE "; f\n"
                              "_f:\n" S("MOV.L       @(X,R4),R0  ; a load")
                                  S("MOV.L       @(LONGER_X,R4),R1 ; pushed") S(
                                      "RTS")
                                      S("NOP") "        ; any comment line\n" S(
                                          ".DATA.W     1,") "+   2\n",
        ""));

    // a field off its column: a mnemonic, operands or a comment, alone or
    // after a label, and a field the one before pushed but with no blank
    CHECK(lints(CODE S("NOP") "    NOP\n" S("NOP"), "3 columns"));
    CHECK(lints(CODE S("MOV         R1,R2") S("MOV          R1,R2")
                    S("MOV         R1,R2"),
                "3 columns"));
    CHECK(lints(CODE S("NOP         ; a") S("NOP           ; b")
                    S("NOP         ; c"),
                "3 columns"));
    CHECK(lints("X:      .EQU    4\n"
                "Y:      .EQU    5\n"
                "Z:     .EQU    6\n",
                "3 columns"));
    CHECK(lints("X:      .EQU    4\n"
                "Y:      .EQU    5\n"
                "Z:      .EQU      6\n",
                "3 columns"));
    CHECK(lints(CODE S("NOP         ; a") S("MOV.L       L1057+10,R8; b")
                    S("NOP         ; c"),
                "3 columns"));
    CHECK(lints("X:      .EQU    4\n"
                "Y:      .EQU    5\n"
                "LONG_NAME:.EQU  6\n",
                "3 columns"));

    // on a tie, the column the file uses first
    CHECK(lints(CODE S("NOP") S("NOP") "        NOP\n"
                                       "        NOP\n",
                "4 columns, 5 columns"));

    // a tab, a blank at a line's end, and a line end unlike most, or on a
    // tie unlike the first's
    CHECK(lints(CODE "; a\tb\n" S("NOP"), "2 tab"));
    CHECK(lints(CODE "; a \n   \n" S("NOP"),
                "2 trailing-blank, 3 trailing-blank"));
    CHECK(lints(CODE S("NOP") "          NOP\r\n" S("NOP"), "3 line-ends"));
    CHECK(lints("          .SECTION    P,CODE,ALIGN=4\r\n"
                "          NOP\r\n"
                "          NOP\n",
                "3 line-ends"));
    CHECK(lints("          .SECTION    P,CODE,ALIGN=4\r\n"
                "          NOP\n",
                "2 line-ends"));

    // an exported label without a comment line right above it: after a
    // blank, a statement or nothing, the first of two definitions, and a
    // symbol .EQU gives a value. One exported twice is refused once, and one
    // never defined isn't lint's to refuse.
    CHECK(lints(S(".EXPORT     _f,_g,_h,_x") S(".EXPORT     _f") CODE
                "; f\n\n"
                "_f:\n" S("NOP") "_g:\n" S(
                    "NOP") "; h\n"
                           "_h:\n" S("NOP") "_h:\n"
                                            "_y:     .EQU    1\n",
                "6 export-comment, 8 export-comment"));
    CHECK(lints("_f:\n" S(".EXPORT     _f"), "1 export-comment"));
    CHECK(lints(S(".EXPORT     _x") "_x:     .EQU    1\n", "2 export-comment"));

    // a label exported on a continuation, below a field that ends with a
    // comma, and none on one asmsh joins to nothing
    CHECK(lints(S(".EXPORT     _a,") "+           _b\n" CODE "; a\n"
                                     "_a:\n" S("NOP") "_b:\n" S("RTS") S("NOP"),
                "7 export-comment"));
    CHECK(lints(S(".EXPORT     _a") "+           _b\n"
                                    "; a\n"
                                    "_a:\n"
                                    "_b:\n" S("NOP"),
                "2 unknown-line"));

    // a line shckit can't read, by check's rule: a label is read in the first
    // column only, and a continuation only where it joins a statement
    CHECK(lints(CODE "    x: NOP\n" S("NOP"), "2 unknown-line"));
    CHECK(lints(CODE S("NOP") "+           1\n", "3 unknown-line"));
}

static void test_rules(void)
{
    // every rule has a distinct name and a definition, and nothing past the
    // enum has either
    for (int i = 0; i < SHCKIT_RULES; i++) {
        CHECK(shckit_rule_name(i) != NULL && *shckit_rule_name(i) != '\0');
        CHECK(shckit_rule_definition(i) != NULL &&
              *shckit_rule_definition(i) != '\0');
        for (int j = 0; j < i; j++) {
            CHECK(strcmp(shckit_rule_name(i), shckit_rule_name(j)) != 0);
        }
    }
    CHECK(shckit_rule_name(-1) == NULL &&
          shckit_rule_name(SHCKIT_RULES) == NULL);
    CHECK(shckit_rule_definition(-1) == NULL &&
          shckit_rule_definition(SHCKIT_RULES) == NULL);
    CHECK(strcmp(shckit_rule_name(SHCKIT_DATA_IN_FLOW), "data-in-flow") == 0);
}

static bool same_span(struct shckit_span one, struct shckit_span two)
{
    return one.at == two.at && one.len == two.len;
}

static bool typed_alike(const struct shckit_text *text, const char *bytes)
{
    // whether each line is typed as the reader types bytes, pools included
    struct shckit_text *read = read_text(bytes);
    bool ok = read != NULL && shckit_count(read) == shckit_count(text);
    for (size_t i = 0; ok && i < shckit_count(text); i++) {
        const struct shckit_line *one = shckit_line(text, i);
        const struct shckit_line *two = shckit_line(read, i);
        ok = one->len == two->len &&
             memcmp(one->bytes, two->bytes, one->len) == 0 &&
             one->crlf == two->crlf && one->code == two->code &&
             one->kind == two->kind && one->directive == two->directive &&
             same_span(one->label, two->label) &&
             same_span(one->name, two->name) &&
             same_span(one->operands, two->operands) &&
             same_span(one->comment, two->comment) && one->pool == two->pool;
    }
    shckit_free(read);
    return ok;
}

static bool rewrites(unsigned patches, const char *in, const char *want)
{
    // the patches turn in into exactly want, typed as the reader types it
    struct shckit_text *text = read_text(in);
    size_t line = 0;
    int patch = -1;
    int error = text != NULL ? shckit_apply(text, patches, &line, &patch)
                             : SHCKIT_NOMEM;
    bool ok =
        error == SHCKIT_OK && same_bytes(text, want) && typed_alike(text, want);
    if (error != SHCKIT_OK) {
        printf("  refused: \"%s\" at line %zu by %s\n", shckit_strerror(error),
               line, patch >= 0 ? shckit_patch_name(patch) : "nothing");
    }
    shckit_free(text);
    return ok;
}

static bool keeps(unsigned patches, const char *in)
{
    return rewrites(patches, in, in);
}

static bool refuses(unsigned patches, const char *in, int error, size_t at,
                    int patch)
{
    struct shckit_text *text = read_text(in);
    size_t line = SIZE_MAX;
    int by = -2;
    int got =
        text != NULL ? shckit_apply(text, patches, &line, &by) : SHCKIT_NOMEM;
    bool ok = got == error && line == at && by == patch;
    if (!ok) {
        printf("  got \"%s\" at line %zu by %s\n", shckit_strerror(got), line,
               by >= 0 ? shckit_patch_name(by) : "nothing");
    }
    shckit_free(text);
    return ok;
}

static bool leaves(unsigned patches, const char *in)
{
    // the patches refuse in and leave it as it was read, each line typed as
    // the reader types it at its own number, and free for another apply,
    // which refuses it alike
    struct shckit_text *text = read_text(in);
    size_t line = 0;
    size_t again = 0;
    int by = -2;
    int again_by = -2;
    int error =
        text != NULL ? shckit_apply(text, patches, &line, &by) : SHCKIT_NOMEM;
    bool ok = text != NULL && error != SHCKIT_OK && same_bytes(text, in) &&
              typed_alike(text, in);
    for (size_t i = 0; ok && i < shckit_count(text); i++) {
        ok = shckit_line(text, i)->input == i + 1 &&
             shckit_line(text, i)->patch == -1;
    }
    ok = ok && shckit_apply(text, patches, &again, &again_by) == error &&
         again == line && again_by == by;
    shckit_free(text);
    return ok;
}

static void test_drop_inline_align(void)
{
    // SHCASM's .ALIGN 4 goes from a code section, whatever its line end,
    // and from above any .SECTION, which is P. Other alignments, and
    // .ALIGN 4 in a data section, stay.
    CHECK(rewrites(
        DROP,
        ALIGN CODE
        "_f:\n"
        "    CLRMAC\n" ALIGN S("RTS") "    CLRMAC\r\n"
                                      "          .ALIGN      4\r\n" S("NOP")
                                          S(".ALIGN      2") S(".ALIGN      16")
                                              DATA ALIGN "    .ALIGN  4\n",
        CODE "_f:\n"
             "    CLRMAC\n" S("RTS") "    CLRMAC\r\n" S("NOP")
                 S(".ALIGN      2") S(".ALIGN      16") DATA ALIGN
        "    .ALIGN  4\n"));
    CHECK(rewrites(DROP, CODE ALIGN ALIGN, CODE));
    CHECK(keeps(DROP, CODE S("NOP")));

    // every other .ALIGN 4 of a code section: as programmers write them in
    // inline text, in other columns, with a label, a comment, a blank after
    // it, or leading zeros
    static const char *const others[] = {
        "    .ALIGN  4\n",
        "    .ALIGN      4\n",
        "\t.ALIGN\t4\n",
        "           .ALIGN      4\n",
        "          .ALIGN     4\n",
        "          .ALIGN       4\n",
        "x:        .ALIGN      4\n",
        "          .ALIGN      4 ; c\n",
        "          .ALIGN      4;\n",
        "          .ALIGN      4 \n",
        "          .ALIGN      04\n",
    };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, CODE "    CLRMAC\n%s" ALIGN, others[i]);
        CHECK(
            refuses(DROP, in, SHCKIT_OTHER_ALIGN, 3, SHCKIT_DROP_INLINE_ALIGN));
    }

    // and above any .SECTION, in P
    CHECK(refuses(DROP, "    .ALIGN  4\n" CODE ALIGN, SHCKIT_OTHER_ALIGN, 1,
                  SHCKIT_DROP_INLINE_ALIGN));

    // SHCASM prints its line whole, and a continuation below it joins
    // nothing, since its field doesn't end with a comma. apply refuses one
    // before the patch could hand it to the statement above.
    CHECK(refuses(DROP,
                  CODE S(".DATA.W     1,") ALIGN "+                     2\n",
                  SHCKIT_UNREADABLE, 4, -1));

    // pools, switch tables included, are for the pool patches to refuse
    CHECK(keeps(DROP, CODE "L20:\n" S(".DATA.W     L21-L20")));

    // it drops nothing from a text it refuses, though it reached SHCASM's
    // line first
    CHECK(leaves(DROP, CODE ALIGN "    .ALIGN  4\n"));
}

// A function that loads from a pool, its code ending at RTS and NOP.
#define LOADS(load) CODE "_f:\n" S(load) S("RTS") S("NOP")

// A label on a line that takes no bytes, and a label over such a line, right
// above a pool's label.
static const char label_pool[] = "          .SECTION    P,CODE,ALIGN=4\r\n"
                                 "          NOP\r\n"
                                 "_x:       .POOL\r\n"
                                 "L10:                              \r\n"
                                 "          .DATA.L     _x\r\n"
                                 "          .DATA.L     L10\r\n"
                                 "          .END\r\n";
static const char label_pool2[] = "          .SECTION    P,CODE,ALIGN=4\r\n"
                                  "          NOP\r\n"
                                  "_x:\r\n"
                                  "          .POOL\r\n"
                                  "L10:                              \r\n"
                                  "          .DATA.L     _x\r\n"
                                  "          .DATA.L     L10\r\n"
                                  "          .END\r\n";

static void test_align_pool(void)
{
    // .ALIGN 4 before each literal pool's label, then .RES.W 1 when its
    // items ahead of the first .DATA.L take 2 bytes mod 4. The pool and
    // every reference to it stay as they are.
    static const char *const pools[][2] = {
        {S(".DATA.L     _a"), ""},
        {RESERVE S(".DATA.L     _a"), RESERVE},
        {S(".DATA.W     H'0080") S(".DATA.L     _a"), RESERVE},
        {S(".DATA.W     1") S(".DATA.W     2") S(".DATA.L     _a"), ""},
        {S(".DATA.W     1") RESERVE S(".DATA.L     _a"), ""},
        {S(".DATA.W     1") S(".DATA.W     2") RESERVE S(".DATA.L     _a"),
         RESERVE},
        {S(".DATA.W     1") S(".DATA.W     2") S(".DATA.W     3")
             S(".DATA.L     _a") S(".DATA.L     _b"),
         RESERVE},
    };
    for (size_t i = 0; i < sizeof pools / sizeof pools[0]; i++) {
        char in[512];
        char want[512];
        (void)snprintf(in, sizeof in, LOADS("MOV.L       L10+2,R1") L10 "%s",
                       pools[i][0]);
        (void)snprintf(want, sizeof want,
                       LOADS("MOV.L       L10+2,R1") ALIGN "%s" L10 "%s",
                       pools[i][1], pools[i][0]);
        CHECK(rewrites(POOL, in, want));
    }
    CHECK(rewrites(POOL,
                   CODE S("NOP") L10 RESERVE S(".DATA.L     1") S("NOP")
                       L11 S(".DATA.L     2") DATA "L12:\n" S(".DATA.L     3"),
                   CODE S("NOP") ALIGN RESERVE L10 RESERVE S(".DATA.L     1")
                       S("NOP") ALIGN L11 S(".DATA.L     2") DATA
                   "L12:\n" S(".DATA.L     3")));

    // the lines it writes end as the label does
    CHECK(rewrites(POOL,
                   CODE S("NOP") "L10:                              \r\n" S(
                       ".DATA.W     1") S(".DATA.L     _a"),
                   CODE S("NOP") "          .ALIGN      4\r\n"
                                 "          .RES.W      1\r\n"
                                 "L10:                              \r\n" S(
                                     ".DATA.W     1") S(".DATA.L     _a")));

    // a pool above any .SECTION is in P, a code section
    CHECK(rewrites(POOL, S("NOP") L10 S(".DATA.L     _a"),
                   S("NOP") ALIGN L10 S(".DATA.L     _a")));

    // not literal pools: labels of inline text, a label over an
    // instruction, and data in a data section
    CHECK(keeps(
        POOL,
        CODE "?MASK:\n" S(
            ".DATA.L     H'EFFFFF0F") "?BLOCK:\n"
                                      "    .DATA.L     H'10000000\n" L10 S(
                                          "RTS") DATA L11 S(".DATA.W     1")));

    // after drop-inline-align, which goes first, its own .ALIGN 4 stays
    CHECK(rewrites(
        DROP | POOL,
        CODE "    CLRMAC\n" ALIGN S("RTS") S("NOP") L10 S(".DATA.L     _a"),
        CODE "    CLRMAC\n" S("RTS") S("NOP") ALIGN L10 S(".DATA.L     _a")));

    // and it reads the pools as the drop left them: a label over SHCASM's
    // line heads no pool until the line goes
    CHECK(rewrites(DROP | POOL, CODE S("NOP") L10 ALIGN S(".DATA.L     _a"),
                   CODE S("NOP") ALIGN L10 S(".DATA.L     _a")));
    CHECK(rewrites(DROP, CODE S("NOP") L10 ALIGN S(".DATA.L     _a"),
                   CODE S("NOP") L10 S(".DATA.L     _a")));
    CHECK(keeps(POOL, CODE S("NOP") L10 ALIGN S(".DATA.L     _a")));

    // when it refuses a pool, the drop is undone too, and it writes
    // nothing, not even before the pools above
    CHECK(leaves(DROP | POOL,
                 CODE "    CLRMAC\n" ALIGN S("RTS") S("NOP")
                     L10 S(".DATA.L     _a") S("NOP") L11 S(".DATA.W     1")));

    // a switch table, which SHCASM's table writer prints under a label of
    // its own form, and after a pad of its own
    CHECK(refuses(POOL,
                  LOADS("MOVA        L20,R0") "L20:\n" S(".DATA.W     L21-L20")
                      S(".DATA.W     L22-L20"),
                  SHCKIT_SWITCH, 7, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL,
                  CODE "L756:                             \n" RESERVE
                       "L757:\n" S(".DATA.W     L607-L756"),
                  SHCKIT_SWITCH, 5, SHCKIT_ALIGN_POOL));

    // a literal that is a difference of two symbols is a literal
    CHECK(rewrites(POOL, CODE S("NOP") L10 S(".DATA.L     _b-_a"),
                   CODE S("NOP") ALIGN L10 S(".DATA.L     _b-_a")));

    // a label line SHCASM's pool writer doesn't print: unpadded, a blank
    // short or over, a tab, a comment, a leading zero
    static const char *const labels[] = {
        "L10:\n",
        "L10:                             \n",
        "L10:                               \n",
        "L10:\t                             \n",
        "L10:                             ;\n",
        "L010:                             \n",
    };
    for (size_t i = 0; i < sizeof labels / sizeof labels[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, CODE S("NOP") "%s" S(".DATA.L     _a"),
                       labels[i]);
        CHECK(refuses(POOL, in, SHCKIT_POOL_LABEL, 3, SHCKIT_ALIGN_POOL));
    }

    // items SHCASM's pool writer doesn't print: in other columns, with a
    // comment, more than one operand, a string, other directives and other
    // reservations, and more operands on the line below
    static const char *const items[] = {
        "    .DATA.L     _a\n",           "           .DATA.L    _a\n",
        "\t  .DATA.L     _a\n",           "          .DATA.L      _a\n",
        "          .DATA.L     _a ; c\n", "          .DATA.L     _a \n",
        "          .DATA.L     _a,_b\n",  "          .DATA.L     \"abcd\"\n",
        "          .DATA.B     1\n",      "          .RES.B      2\n",
        "          .RES.W      2\n",      "          .RES.W      01\n",
        "          .RES.L      1\n",
    };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE S("NOP") L10 "%s" S(".DATA.L     _a"), items[i]);
        CHECK(refuses(POOL, in, SHCKIT_ITEM, 4, SHCKIT_ALIGN_POOL));
    }
    CHECK(refuses(POOL,
                  CODE S("NOP") L10 S(".DATA.L     _a,") "+          _b\n",
                  SHCKIT_ITEM, 4, SHCKIT_ALIGN_POOL));

    // items out of SHCASM's order: words, at most one .RES.W 1, then longs
    CHECK(refuses(
        POOL, CODE S("NOP") L10 RESERVE S(".DATA.W     1") S(".DATA.L     _a"),
        SHCKIT_SHAPE, 5, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL,
                  CODE S("NOP") L10 S(".DATA.W     1")
                      RESERVE RESERVE S(".DATA.L     _a"),
                  SHCKIT_SHAPE, 6, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL,
                  CODE S("NOP") L10 S(".DATA.L     _a") S(".DATA.W     1"),
                  SHCKIT_SHAPE, 5, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL, CODE S("NOP") L10 S(".DATA.L     _a") RESERVE,
                  SHCKIT_SHAPE, 5, SHCKIT_ALIGN_POOL));

    // a pool without a .DATA.L holds no record of where SHCASM counted it
    CHECK(refuses(POOL, CODE S("NOP") L10 S(".DATA.W     1") S(".DATA.W     2"),
                  SHCKIT_NO_LONG, 3, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL, CODE S("NOP") L10 S(".DATA.W     1") RESERVE,
                  SHCKIT_NO_LONG, 3, SHCKIT_ALIGN_POOL));

    // the nearest line above a pool's label, past blanks and comments, is
    // an instruction. A label there names the pool's address too: alone,
    // over blanks and comments, on a line that takes no bytes, or above
    // one. asmsh puts _x and L10 both at 2 in label_pool and label_pool2,
    // and the lines the patch would write separate them.
    CHECK(refuses(POOL,
                  LOADS("MOV.L       L11,R0") "L9:\n" L11 S(".DATA.L     _a"),
                  SHCKIT_ABOVE, 7, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL,
                  CODE "_f:          ; f\n"
                       "\n"
                       "; c\n" L11 S(".DATA.L     _a"),
                  SHCKIT_ABOVE, 5, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL, label_pool, SHCKIT_ABOVE, 4, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL, label_pool2, SHCKIT_ABOVE, 5, SHCKIT_ALIGN_POOL));
    CHECK(refuses(
        POOL,
        CODE S("NOP") "_x:       .SDATA      \"\"\n" L10 S(".DATA.L     _x"),
        SHCKIT_ABOVE, 4, SHCKIT_ALIGN_POOL));

    // and any other line: a directive, the section's own line, or the last
    // item of a pool right above
    CHECK(refuses(POOL,
                  CODE S("NOP") S(".ALIGN      16") L10 S(".DATA.L     _a"),
                  SHCKIT_ABOVE, 4, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL, CODE L10 S(".DATA.L     _a"), SHCKIT_ABOVE, 2,
                  SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL,
                  CODE S("NOP") L10 S(".DATA.L     _a") L11 S(".DATA.L     _b"),
                  SHCKIT_ABOVE, 5, SHCKIT_ALIGN_POOL));

    // a label on an instruction's line names the instruction, a label above
    // the instruction stands where it did, and blanks and comments between
    // the instruction and the pool stay above the written lines
    CHECK(rewrites(POOL, CODE "L9: NOP\n" L11 S(".DATA.L     _a"),
                   CODE "L9: NOP\n" ALIGN L11 S(".DATA.L     _a")));
    CHECK(rewrites(POOL,
                   CODE "_x:       .POOL\n" S("NOP") L10 S(".DATA.L     _x")
                       S(".DATA.L     L10"),
                   CODE "_x:       .POOL\n" S("NOP")
                       ALIGN L10 S(".DATA.L     _x") S(".DATA.L     L10")));
    CHECK(rewrites(POOL, CODE S("NOP") "\n; c\n" L11 S(".DATA.L     _a"),
                   CODE S("NOP") "\n; c\n" ALIGN L11 S(".DATA.L     _a")));

    // a pool's label defined again, alone or on a statement's line, in any
    // section
    CHECK(refuses(POOL, CODE S("NOP") L10 S(".DATA.L     _a") DATA "L10:\n",
                  SHCKIT_DUPLICATE, 6, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL, CODE "L10: NOP\n" S("RTS") L10 S(".DATA.L     _a"),
                  SHCKIT_DUPLICATE, 2, SHCKIT_ALIGN_POOL));

    // an .EQU is a directive like any other: it may stand anywhere but right
    // above a pool's label, and its symbol is a label
    CHECK(rewrites(
        DROP | POOL, "X:      .EQU    4\n" CODE S("NOP") L10 S(".DATA.L     X"),
        "X:      .EQU    4\n" CODE S("NOP") ALIGN L10 S(".DATA.L     X")));
    CHECK(refuses(POOL,
                  CODE S("NOP") "X:      .EQU    4\n" L10 S(".DATA.L     X"),
                  SHCKIT_ABOVE, 4, SHCKIT_ALIGN_POOL));
    CHECK(refuses(POOL,
                  "L10:    .EQU    4\n" CODE S("NOP") L10 S(".DATA.L     1"),
                  SHCKIT_DUPLICATE, 1, SHCKIT_ALIGN_POOL));
}

// The lines use-assembler-pools writes: .NOPOOL after a slot, and .ALIGN 4
// and .POOL in place of a pool.
#define NOPOOL S(".NOPOOL")
#define POOLS ALIGN S(".POOL")

// SHCASM's label lines for two functions, a comment at column 33 naming
// each, and the long a MOVA addresses.
#define FUNCTION "_f:                              ; function: f\n"
#define FUNCTION_G "_g:                              ; function: g\n"
#define MOVA S("MOVA        L10,R0")
#define FLOAT L10 S(".DATA.L     H'3727C5AC")

// Loads with comments, a label, and a blank after the operands.
static const char commented[] = "          .SECTION    P,CODE,ALIGN=4\n"
                                "          MOV.L       L10+4,R1   ; _a\n"
                                "          MOV.L       L10,R2     ; _abcdefgh\n"
                                "x:        MOV.L       L10+4,R3 \r\n"
                                "          RTS\n"
                                "          NOP\n"
                                "L10:                              \n"
                                "          .DATA.L     _abcdefgh\n"
                                "          .DATA.L     _a\n";
static const char commented_want[] =
    "          .SECTION    P,CODE,ALIGN=4\n"
    "          MOV.L       #_a,R1     ; _a\n"
    "          MOV.L       #_abcdefgh,R2; _abcdefgh\n"
    "x:        MOV.L       #_a,R3 \r\n"
    "          RTS\n"
    "          NOP\n"
    "          .NOPOOL\n"
    "          .ALIGN      4\n"
    "          .POOL\n";

// Loads from two pools above both. The first .POOL takes both literals, so
// none waits at the second pool's slot and it gets no .NOPOOL.
static const char two_pools[] = CODE S("MOV.L       L10,R1")
    S("MOV.L       L11,R2") S("RTS") S("NOP") L10 S(".DATA.L     1") S("RTS")
        S("NOP") L11 S(".DATA.L     2") S(".END");
static const char two_pools_want[] =
    CODE S("MOV.L       #1,R1") S("MOV.L       #2,R2") S("RTS") S("NOP")
        NOPOOL POOLS S("RTS") S("NOP") POOLS S(".END");

static void test_use_assembler_pools(void)
{
    // each load from a pool takes the item's value as # and keeps its
    // register, .NOPOOL goes right after each slot of a branch that never
    // falls through while a literal waits, and .ALIGN 4 and .POOL go where
    // the pool's label and items stood
    CHECK(rewrites(USE, LOADS("MOV.L       L10,R1") L10 S(".DATA.L     _a"),
                   CODE "_f:\n" S("MOV.L       #_a,R1") S("RTS") S("NOP")
                       NOPOOL POOLS));
    CHECK(rewrites(USE,
                   CODE S("MOV.W       L10+2,R1") S("MOV.W       L10,R2")
                       S("MOV.W       L10+2,R3") S("RTS") S("NOP")
                           L10 S(".DATA.W     1") S(".DATA.W     -1"),
                   CODE S("MOV.W       #-1,R1") S("MOV.W       #1,R2")
                       S("MOV.W       #-1,R3") S("RTS") S("NOP") NOPOOL POOLS));
    CHECK(rewrites(
        USE,
        CODE S("MOV.L       L10+8,R1") S("MOV.L       L10+4,R2")
            S("MOV.L       L10,R3") S("RTS") S("NOP") L10 S(".DATA.L     _b-_a")
                S(".DATA.L     H'0000000C+_x") S(".DATA.L     STARTOF P"),
        CODE S("MOV.L       #STARTOF P,R1") S("MOV.L       #H'0000000C+_x,R2")
            S("MOV.L       #_b-_a,R3") S("RTS") S("NOP") NOPOOL POOLS));

    // a pool two functions share: each slot above it gets .NOPOOL, and one
    // above every load or below the pool gets none
    CHECK(rewrites(USE,
                   CODE S("RTS") S("NOP") "_f:\n" S("MOV.L       L10,R1")
                       S("RTS") S("NOP") "_g:\n" S("MOV.L       L10+4,R2")
                           S("RTS") S("NOP") L10 S(".DATA.L     _a")
                               S(".DATA.L     _b") S("RTS") S("NOP"),
                   CODE S("RTS") S("NOP") "_f:\n" S("MOV.L       #_a,R1")
                       S("RTS") S("NOP") NOPOOL "_g:\n" S("MOV.L       #_b,R2")
                           S("RTS") S("NOP") NOPOOL POOLS S("RTS") S("NOP")));
    CHECK(rewrites(USE, two_pools, two_pools_want));

    // a MOVA of a long, whose address each path reads only as the long's
    // four bytes until R0 is written, takes # and the long's value: past
    // other instructions and a branch, to a return, after an FSCHG pair, by
    // MOV.L, beside a load of the same long or a word of a mixed pool, and
    // up to a call, whose result R0 then holds
    CHECK(rewrites(
        USE,
        CODE FUNCTION MOVA S("FMOV.S      @R0,FR3") S("RTS") S("NOP") FLOAT,
        CODE FUNCTION S("MOVA        #H'3727C5AC,R0") S("FMOV.S      @R0,FR3")
            S("RTS") S("NOP") NOPOOL POOLS));
    CHECK(rewrites(
        USE,
        CODE FUNCTION S("FSCHG") S("FMOV        @R2+,DR4") S("FSCHG")
            MOVA S("FMOV.S      FR3,FR4") S("FMOV.S      @R0,FR3")
                S("FCMP/GT     FR3,FR4") S("BF          L1")
                    S("FSRRA       FR4") "L1:\n" S("RTS") S("NOP") FLOAT,
        CODE FUNCTION S("FSCHG") S("FMOV        @R2+,DR4") S("FSCHG")
            S("MOVA        #H'3727C5AC,R0") S("FMOV.S      FR3,FR4")
                S("FMOV.S      @R0,FR3") S("FCMP/GT     FR3,FR4")
                    S("BF          L1") S("FSRRA       FR4") "L1:\n" S("RTS")
                        S("NOP") NOPOOL POOLS));
    CHECK(rewrites(
        USE,
        CODE FUNCTION MOVA S("MOV.L       @R0,R1") S("EXTU.W      R4,R0")
            S("ADD         #4,R0") S("RTS") S("NOP") FLOAT,
        CODE FUNCTION S("MOVA        #H'3727C5AC,R0") S("MOV.L       @R0,R1")
            S("EXTU.W      R4,R0") S("ADD         #4,R0") S("RTS") S("NOP")
                NOPOOL POOLS));
    CHECK(rewrites(
        USE,
        CODE FUNCTION S("MOV.W       L10,R1") S("MOVA        L10+4,R0")
            S("MOV.L       L10+4,R2") S("FMOV.S      @R0,FR3") S("RTS") S("NOP")
                L10 S(".DATA.W     1") RESERVE S(".DATA.L     H'3F000000"),
        CODE FUNCTION S("MOV.W       #1,R1") S("MOVA        #H'3F000000,R0")
            S("MOV.L       #H'3F000000,R2") S("FMOV.S      @R0,FR3") S("RTS")
                S("NOP") NOPOOL POOLS));
    CHECK(rewrites(
        USE,
        CODE FUNCTION MOVA S("FMOV.S      @R0,FR3") S("JSR         @R4")
            S("NOP") S("MOV         R0,R1") S("RTS") S("NOP") FLOAT,
        CODE FUNCTION S("MOVA        #H'3727C5AC,R0") S("FMOV.S      @R0,FR3")
            S("JSR         @R4") S("NOP") S("MOV         R0,R1") S("RTS")
                S("NOP") NOPOOL POOLS));

    // .NOPOOL after the slot of RTS, RTE, BRA, BRAF and JMP, past the
    // slot's continuations, and after no other branch's
    static const char *const stops[] = {"RTS", "RTE", "BRA         _f",
                                        "BRAF        R2", "JMP         @R2"};
    for (size_t i = 0; i < sizeof stops / sizeof stops[0]; i++) {
        char in[512];
        char want[512];
        (void)snprintf(in, sizeof in,
                       CODE "_f:\n" S("MOV.L       L10,R1") "          %s\n" S(
                           "MOV         R1,") "+                     R2\n" L10
                           S(".DATA.L     _a"),
                       stops[i]);
        (void)snprintf(
            want, sizeof want,
            CODE "_f:\n" S("MOV.L       #_a,R1") "          %s\n" S(
                "MOV         R1,") "+                     R2\n" NOPOOL POOLS,
            stops[i]);
        CHECK(rewrites(USE, in, want));
    }
    static const char *const falls[] = {"BSR         _f", "BSRF        R2",
                                        "JSR         @R2", "BT/S        _f",
                                        "BF/S        _f"};
    for (size_t i = 0; i < sizeof falls / sizeof falls[0]; i++) {
        char in[512];
        char want[512];
        (void)snprintf(in, sizeof in,
                       CODE
                       "_f:\n" S("MOV.L       L10,R1") "          %s\n" S("NOP")
                           S("RTS") S("NOP") L10 S(".DATA.L     _a"),
                       falls[i]);
        (void)snprintf(want, sizeof want,
                       CODE
                       "_f:\n" S("MOV.L       #_a,R1") "          %s\n" S("NOP")
                           S("RTS") S("NOP") NOPOOL POOLS,
                       falls[i]);
        CHECK(rewrites(USE, in, want));
    }

    // a slot past blanks, comments and lines that write nothing, and a pool
    // past blanks and comments, which stay
    CHECK(rewrites(
        USE,
        CODE S("MOV.L       L10,R1") S("RTS") "\n; c\n" S(".EXPORT     _a")
            S("NOP") "\n; d\n" L10 S(".DATA.L     _a"),
        CODE S("MOV.L       #_a,R1") S("RTS") "\n; c\n" S(".EXPORT     _a")
            S("NOP") NOPOOL "\n; d\n" POOLS));

    // a label on a slot or above it, below its branch, where the patch writes
    // .NOPOOL after the slot, which asmsh refuses there with error 521. A
    // label on the branch is fine.
    static const struct {
        const char *between;
        const char *slot;
    } labelled[] = {
        {"L5:\n", S("NOP")},
        {"", "L5:       NOP\n"},
        {"X:        .EQU        1\n", S("NOP")},
    };
    for (size_t i = 0; i < sizeof labelled / sizeof labelled[0]; i++) {
        char in[512];
        (void)snprintf(in, sizeof in,
                       CODE S("MOV.L       L10,R1")
                           S("RTS") "%s%s" L10 S(".DATA.L     _a"),
                       labelled[i].between, labelled[i].slot);
        CHECK(
            refuses(USE, in, SHCKIT_SLOT_LABEL, 4, SHCKIT_USE_ASSEMBLER_POOLS));
    }
    CHECK(rewrites(USE,
                   CODE S("MOV.L       L10,R1") "L5:       RTS\n" S("NOP")
                       L10 S(".DATA.L     _a"),
                   CODE S("MOV.L       #_a,R1") "L5:       RTS\n" S("NOP")
                       NOPOOL POOLS));

    // a comment keeps its column while the operands end before it, and
    // follows them directly past it. The other bytes stay: a blank after the
    // operands, a label, and the line end.
    CHECK(rewrites(USE, commented, commented_want));

    // the lines it writes end as the slot's and the label's lines do
    CHECK(rewrites(
        USE,
        CODE S("MOV.L       L10,R1") "          RTS\r\n"
                                     "          NOP\r\n"
                                     "L10:                              \n" S(
                                         ".DATA.L     _a"),
        CODE S("MOV.L       #_a,R1") "          RTS\r\n"
                                     "          NOP\r\n"
                                     "          .NOPOOL\r\n" POOLS));
    CHECK(rewrites(USE,
                   CODE S("MOV.L       L10,R1") S("RTS")
                       S("NOP") "L10:                              \r\n" S(
                           ".DATA.L     _a"),
                   CODE S("MOV.L       #_a,R1") S("RTS") S("NOP") NOPOOL
                   "          .ALIGN      4\r\n"
                   "          .POOL\r\n"));

    // a text without pools comes out as it went in, whatever it holds: a
    // load from elsewhere, immediates asmsh pools itself or doesn't, .POOL,
    // an instruction outside the opcode table, a branch with no slot, and
    // label differences in a data section
    CHECK(keeps(USE, CODE "_f:\n" S("MOV.L       @(4,R4),R1") S(
                         "MOV.L       #_a,R2") S("FOO         R1") S(".POOL")
                         S("MOV         #1,R0") S("MOV.B       #1,R1") S("RTS")
                             DATA "L10:\n" S(".DATA.W     L21-L20")));

    // after drop-inline-align, which goes first, its own .ALIGN 4 stays,
    // and a pool SHCASM's line parted from its slot is right below it
    CHECK(rewrites(DROP | USE,
                   CODE "    CLRMAC\n" ALIGN S("MOV.L       L10,R1") S("RTS")
                       S("NOP") ALIGN L10 S(".DATA.L     _a"),
                   CODE "    CLRMAC\n" S("MOV.L       #_a,R1") S("RTS") S("NOP")
                       NOPOOL POOLS));
    CHECK(refuses(USE,
                  CODE S("MOV.L       L10,R1") S("RTS") S("NOP")
                      ALIGN L10 S(".DATA.L     _a"),
                  SHCKIT_REACHED, 6, SHCKIT_USE_ASSEMBLER_POOLS));

    // a line that hands asmsh a pool of its own: .POOL, .NOPOOL, and a
    // MOV.W, MOV.L or MOVA of #x. A MOV of #x, an 8-bit immediate, isn't
    // one.
    static const char *const pooled[] = {
        "MOV.L       #1,R1",
        "MOV.W       #-1,R1",
        "mov.l       #_a,r1",
        "MOVA        #1,R0",
        ".POOL",
        ".NOPOOL",
    };
    for (size_t i = 0; i < sizeof pooled / sizeof pooled[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, LOADS("%s") L10 S(".DATA.L     _a"),
                       pooled[i]);
        CHECK(refuses(USE, in, SHCKIT_POOLED, 3, SHCKIT_USE_ASSEMBLER_POOLS));
    }

    // an instruction outside the opcode table could be a branch
    CHECK(refuses(USE, LOADS("FOO         R1") L10 S(".DATA.L     _a"),
                  SHCKIT_NOT_HELD, 3, SHCKIT_USE_ASSEMBLER_POOLS));

    // a delayed branch whose slot shckit can't place, below a pool: past an
    // .ALIGN, data, an instruction illegal there, in a section that
    // resumes, or none
    static const char *const slots[] = {
        CODE S("RTS") ALIGN S("NOP"),
        CODE S("RTS") S(".DATA.W     H'0009"),
        CODE S("BRA         _f") S("BRA         _g") S("NOP"),
        CODE S("JSR         @R1") DATA S(".DATA.L     1") CODE S("NOP"),
        CODE S("RTS"),
        CODE S("BT/S        _f") S(".END"),
    };
    for (size_t i = 0; i < sizeof slots / sizeof slots[0]; i++) {
        char in[512];
        (void)snprintf(in, sizeof in,
                       LOADS("MOV.L       L10,R1") L10 S(".DATA.L     _a") "%s",
                       slots[i]);
        CHECK(refuses(USE, in, SHCKIT_SLOT, 9, SHCKIT_USE_ASSEMBLER_POOLS));
    }
    CHECK(rewrites(DROP | USE,
                   LOADS("MOV.L       L10,R1") L10 S(".DATA.L     _a")
                       CODE S("RTS") ALIGN S("NOP"),
                   LOADS("MOV.L       #_a,R1") NOPOOL POOLS CODE S("RTS")
                       S("NOP")));

    // a switch table, and an entry Lnn-Lnn+k, which isn't the table
    // writer's: it names the pool's label as no load does
    CHECK(refuses(USE,
                  LOADS("MOVA        L20,R0") "L20:\n" S(".DATA.W     L21-L20"),
                  SHCKIT_SWITCH, 7, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  LOADS("MOV.W       L10,R1") L10 S(".DATA.W     L11-L10+2"),
                  SHCKIT_USE, 7, SHCKIT_USE_ASSEMBLER_POOLS));

    // a label line SHCASM's pool writer doesn't print
    CHECK(refuses(USE, LOADS("MOV.L       L10,R1") "L10:\n" S(".DATA.L     _a"),
                  SHCKIT_POOL_LABEL, 6, SHCKIT_USE_ASSEMBLER_POOLS));

    // items SHCASM's pool writer doesn't print, and values asmsh can't write
    // as a literal: a register, @, or $, where the item stands
    static const char *const items[] = {
        "    .DATA.L     _a\n",          "          .DATA.L     _a ; c\n",
        "          .DATA.L     _a,_b\n", "          .DATA.L     \"abcd\"\n",
        "          .DATA.B     1\n",     "          .RES.W      2\n",
        "          .RES.L      1\n",     "          .DATA.L     R1\n",
        "          .DATA.L     $\n",     "          .DATA.L     $-_f\n",
        "          .DATA.L     @_a\n",
    };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, LOADS("MOV.L       L10,R1") L10 "%s",
                       items[i]);
        CHECK(refuses(USE, in, SHCKIT_ITEM, 7, SHCKIT_USE_ASSEMBLER_POOLS));
    }

    // and a value that names a symbol .ASSIGN defines: at the load, asmsh
    // can read it as another value than the item's line gives it
    CHECK(refuses(
        USE,
        "X:        .ASSIGN     H'1000\n" CODE
        "_f:\n" S("MOV.L       L10+2,R1") "X:        .ASSIGN     5\n" S("RTS")
            S("NOP") L10 RESERVE S(".DATA.L     X"),
        SHCKIT_ITEM, 10, SHCKIT_USE_ASSEMBLER_POOLS));

    // a pool's label defined twice
    CHECK(refuses(
        USE, LOADS("MOV.L       L10,R1") L10 S(".DATA.L     _a") DATA "L10:\n",
        SHCKIT_DUPLICATE, 9, SHCKIT_USE_ASSEMBLER_POOLS));

    // a pool control can reach: below an instruction that falls through, a
    // call's slot, a label or a section's line
    CHECK(refuses(USE,
                  CODE S("MOV.L       L10,R1") S("NOP") L10 S(".DATA.L     _a"),
                  SHCKIT_REACHED, 4, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE S("MOV.L       L10,R1") S("JSR         @R1") S("NOP")
                      L10 S(".DATA.L     _a"),
                  SHCKIT_REACHED, 5, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  LOADS("MOV.L       L10,R1") "L9:\n" L10 S(".DATA.L     _a"),
                  SHCKIT_REACHED, 7, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE, CODE L10 S(".DATA.L     _a"), SHCKIT_REACHED, 2,
                  SHCKIT_USE_ASSEMBLER_POOLS));

    // a pool the BRA above it branches over, to a label right below it,
    // past blanks and comments or on a statement's line, and one to a
    // label elsewhere
    static const char *const below[] = {"L5:\n", "\n; c\nL5:\n",
                                        "L5:       NOP\n"};
    for (size_t i = 0; i < sizeof below / sizeof below[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE S("MOV.L       L10,R1") S("BRA         L5") S("NOP")
                           L10 S(".DATA.L     _a") "%s" S("RTS") S("NOP"),
                       below[i]);
        CHECK(refuses(USE, in, SHCKIT_GUARDED, 5, SHCKIT_USE_ASSEMBLER_POOLS));
    }
    CHECK(rewrites(USE,
                   CODE "L5:\n" S("MOV.L       L10,R1") S("BRA         L5")
                       S("NOP") L10 S(".DATA.L     _a") "L6:\n",
                   CODE "L5:\n" S("MOV.L       #_a,R1") S("BRA         L5")
                       S("NOP") NOPOOL POOLS "L6:\n"));

    // a pool's label used other than by a MOV.W or MOV.L of label or
    // label+n, n as SHC writes it, to a register, or a MOVA of it to R0,
    // above the pool in the lines of its .SECTION: by a branch, data, an
    // export, an .EQU, a load below the pool or past a .SECTION, or a load
    // written otherwise
    static const char *const uses[] = {
        "          BT          L10\n",
        "          MOV.L       R1,@(L10,R2)\n",
        "          MOV         L10,R1\n",
        "          MOV.L       L10 + 4,R1\n",
        "          MOV.L       L10+04,R1\n",
        "          MOV.L       L10+H'4,R1\n",
        "          MOV.L       4+L10,R1\n",
        "          MOV.L       L10-4,R1\n",
        "          MOV.L       (L10),R1\n",
        "          .EXPORT     L10\n",
        "X:        .EQU        L10\n",
    };
    for (size_t i = 0; i < sizeof uses / sizeof uses[0]; i++) {
        char in[512];
        (void)snprintf(in, sizeof in,
                       CODE "%s" S("MOV.L       L10,R1")
                           S("MOV.L       L10+4,R2") S("RTS") S("NOP")
                               L10 S(".DATA.L     _a") S(".DATA.L     _b"),
                       uses[i]);
        CHECK(refuses(USE, in, SHCKIT_USE, 2, SHCKIT_USE_ASSEMBLER_POOLS));
    }
    CHECK(refuses(USE,
                  LOADS("MOV.L       L10,R1") L10 S(".DATA.L     _a")
                      DATA S(".DATA.L     L10"),
                  SHCKIT_USE, 9, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  LOADS("MOV.L       L10,R1") L10 S(".DATA.L     _a")
                      S("MOV.L       L10,R2") S("RTS") S("NOP"),
                  SHCKIT_USE, 8, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE S("MOV.L       L10,R1") S("RTS") S("NOP")
                      S(".SECTION    Q,CODE,ALIGN=4") S("RTS") S("NOP")
                          L10 S(".DATA.L     _a"),
                  SHCKIT_USE, 2, SHCKIT_USE_ASSEMBLER_POOLS));

    // a MOVA whose address a path may use other than as the long's four
    // bytes: copied, offset, stored to, read past or beside, read as eight
    // bytes where SZ is 1 or unknown, jumped to, or held while SR switches
    // banks or control runs into another section. A MOVA in no SHCASM
    // function, whose SZ nothing sets, is refused too, and one in a slot is
    // illegal there.
    static const struct {
        const char *body;
        size_t at;
    } addresses[] = {
        {MOVA S("MOV         R0,R1"), 4},
        {MOVA S("ADD         #4,R0") S("FMOV.S      @R0,FR3"), 4},
        {MOVA S("FMOV.S      FR0,@R0"), 4},
        {MOVA S("FMOV.S      @R0+,FR3"), 4},
        {MOVA S("MOV.L       @(4,R0),R1"), 4},
        {MOVA S("FMOV.S      @(R0,R4),FR1"), 4},
        {MOVA S("FMOV        @R0,DR2"), 4},
        {S("FSCHG") MOVA S("FMOV.S      @R0,FR3"), 5},
        {S("LDS         R1,FPSCR") MOVA S("FMOV.S      @R0,FR3"), 5},
        {S("LDS         R1,") "+FPSCR\n" MOVA S("FMOV.S      @R0,FR3"), 6},
        {S("BT          L1") S("FSCHG") "L1:\n" MOVA S("FMOV.S      @R0,FR3"),
         7},
        {MOVA S("JMP         @R0") S("NOP"), 4},
        {MOVA S("LDC         R1,SR"), 4},
    };
    for (size_t i = 0; i < sizeof addresses / sizeof addresses[0]; i++) {
        char in[512];
        (void)snprintf(in, sizeof in,
                       CODE FUNCTION "%s" S("RTS") S("NOP") FLOAT,
                       addresses[i].body);
        CHECK(refuses(USE, in, SHCKIT_ADDRESS, addresses[i].at,
                      SHCKIT_USE_ASSEMBLER_POOLS));
    }
    CHECK(refuses(USE,
                  CODE FUNCTION MOVA S("BT          L2") S("RTS") S("NOP") FLOAT
                  "L2:\n" S("NOP") S(".SECTION    Q,CODE,ALIGN=4"),
                  SHCKIT_ADDRESS, 11, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE, LOADS("MOVA        L10,R0") FLOAT, SHCKIT_ADDRESS, 3,
                  SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE FUNCTION S("BRA         L1") MOVA
                  "L1:\n" S("FMOV.S      @R0,FR3") S("RTS") S("NOP") FLOAT,
                  SHCKIT_SLOT, 3, SHCKIT_USE_ASSEMBLER_POOLS));

    // a branch or a function's entry that runs another branch's slot: past
    // a label, comments, blanks and an .EXPORT, or on the slot's own line.
    // With a NOP for the RTS, the line is no slot, and the ADD or the FMOV
    // is refused. A branch to its own slot is refused, and one past it
    // stays.
    static const char *const entered[] = {
        "L1:\n" S("NOP"),
        "L1:       NOP\n",
        "L1:\n; c\n\n" S(".EXPORT     L1") S("NOP"),
    };
    static const char *const enter[] = {"BRA         L1", "BT          L1",
                                        "BSR         L1"};
    for (size_t i = 0; i < sizeof entered / sizeof entered[0]; i++) {
        for (size_t j = 0; j < sizeof enter / sizeof enter[0]; j++) {
            char in[512];
            (void)snprintf(in, sizeof in,
                           CODE FUNCTION MOVA "          %s\n" S("NOP")
                               S("RTS") "%s" S("ADD         #4,R0") S("RTS")
                                   S("NOP") FLOAT,
                           enter[j], entered[i]);
            CHECK(refuses(USE, in, SHCKIT_ADDRESS, 4,
                          SHCKIT_USE_ASSEMBLER_POOLS));
        }
    }
    CHECK(refuses(USE,
                  CODE FUNCTION MOVA S("BRA         L1") S("NOP")
                      S("NOP") "L1:\n" S("NOP") S("ADD         #4,R0") S("RTS")
                          S("NOP") FLOAT,
                  SHCKIT_ADDRESS, 9, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE FUNCTION S("BRA         L1") S("NOP") S("RTS")
                      FUNCTION_G S("NOP") S("FSCHG") "L1:\n" MOVA S(
                          "FMOV.S      @R0,FR3") S("RTS") S("NOP") FLOAT,
                  SHCKIT_ADDRESS, 6, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE FUNCTION S("BRA         L1") S("NOP") S("NOP")
                      FUNCTION_G S("NOP") S("FSCHG") "L1:\n" MOVA S(
                          "FMOV.S      @R0,FR3") S("RTS") S("NOP") FLOAT,
                  SHCKIT_ADDRESS, 11, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE FUNCTION MOVA S("BRA         L1") "L1:\n" S(
                      "MOV.L       @R0,R1") S("RTS") S("NOP") FLOAT,
                  SHCKIT_ADDRESS, 4, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(rewrites(
        USE,
        CODE FUNCTION MOVA S("BRA         L1")
            S("NOP") "L1:\n" S("MOV.L       @R0,R1") S("RTS") S("NOP") FLOAT,
        CODE FUNCTION S("MOVA        #H'3727C5AC,R0") S("BRA         L1")
            S("NOP") NOPOOL "L1:\n" S("MOV.L       @R0,R1") S("RTS") S("NOP")
                NOPOOL POOLS));

    // a BSR to a label of the text but a function's, and control running
    // into data where R0 holds no address; a BSR to a function stays
    CHECK(refuses(USE,
                  CODE FUNCTION MOVA S("BSR         L1") S("NOP") S("RTS")
                      S("NOP") "L1:\n" S("ADD         #4,R0") S("RTS") S("NOP")
                          FLOAT,
                  SHCKIT_ADDRESS, 4, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE FUNCTION S("FSCHG") S(".DATA.W     H'0009")
                      FUNCTION_G MOVA S("FMOV.S      @R0,FR3") S("RTS") S("NOP")
                          FLOAT,
                  SHCKIT_ADDRESS, 4, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(rewrites(
        USE,
        CODE FUNCTION MOVA S("FMOV.S      @R0,FR3") S("BSR         _g") S("NOP")
            S("RTS") S("NOP") FUNCTION_G S("RTS") S("NOP") FLOAT,
        CODE FUNCTION S("MOVA        #H'3727C5AC,R0") S("FMOV.S      @R0,FR3")
            S("BSR         _g") S("NOP") S("RTS") S("NOP")
                NOPOOL FUNCTION_G S("RTS") S("NOP") NOPOOL POOLS));

    // a branch to a name an .EQU or .ASSIGN defines, or to an expression
    // that names a label of the text; to the label itself, the ADD is
    // refused
    static const struct {
        const char *above;
        const char *target;
        size_t at;
    } targets[] = {
        {"X:        .EQU        L2\n", "X", 12},
        {"X:        .ASSIGN     H'0C010000\n", "X", 12},
        {"", "(L2)", 11},
        {"", "L2+L1-L1", 11},
        {"", "L2", 6},
    };
    for (size_t i = 0; i < sizeof targets / sizeof targets[0]; i++) {
        char in[512];
        (void)snprintf(in, sizeof in,
                       CODE FUNCTION S("BRA         L1")
                           S("NOP") "L2:\n" S("ADD         #4,R0") S("RTS")
                               S("NOP") "%sL1:\n" MOVA
                                        "          BRA         %s\n" S("NOP")
                                            FLOAT,
                       targets[i].above, targets[i].target);
        CHECK(refuses(USE, in, SHCKIT_ADDRESS, targets[i].at,
                      SHCKIT_USE_ASSEMBLER_POOLS));
    }

    // a MOVA of no whole long: of a word, of a pad, or inside a long
    CHECK(refuses(USE,
                  CODE FUNCTION MOVA S("RTS") S("NOP") L10 S(".DATA.W     1")
                      S(".DATA.W     2"),
                  SHCKIT_NO_ITEM, 3, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE FUNCTION S("MOVA        L10+2,R0")
                      S("MOV.W       L10,R1") S("MOV.L       L10+4,R2") S("RTS")
                          S("NOP") L10 S(".DATA.W     1")
                              RESERVE S(".DATA.L     _a"),
                  SHCKIT_NO_ITEM, 3, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE,
                  CODE FUNCTION S("MOVA        L10+2,R0")
                      S("MOV.L       L10,R1") S("RTS") S("NOP")
                          L10 S(".DATA.L     _a"),
                  SHCKIT_NO_ITEM, 3, SHCKIT_USE_ASSEMBLER_POOLS));

    // a load of no whole item of its size: of the pad, inside an item, past
    // the pool, of the other size, or further than a size_t reaches
    static const char *const offsets[] = {
        "MOV.L       L10+2,R1",
        "MOV.L       L10+6,R1",
        "MOV.W       L10+1,R1",
        "MOV.L       L10+8,R1",
        "MOV.W       L10+4,R1",
        "MOV.L       L10,R1",
        "MOV.L       L10+99999999999999999999999999,R1",
    };
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        char in[512];
        (void)snprintf(in, sizeof in,
                       CODE "          %s\n" S("MOV.W       L10,R2")
                           S("MOV.L       L10+4,R3") S("RTS") S("NOP")
                               L10 S(".DATA.W     1")
                                   RESERVE S(".DATA.L     _a"),
                       offsets[i]);
        CHECK(refuses(USE, in, SHCKIT_NO_ITEM, 2, SHCKIT_USE_ASSEMBLER_POOLS));
    }

    // an operand that offsets a code label, $ or PC: asmsh's pool takes
    // other bytes than SHC's, so the place it names may move past it. A
    // pool's label isn't a code label.
    static const char *const places[] = {
        "BT          _f+6",
        "BT          $+8",
        "MOV.L       @(4,PC),R2",
        "BT          Y+2",
    };
    for (size_t i = 0; i < sizeof places / sizeof places[0]; i++) {
        char in[512];
        (void)snprintf(in, sizeof in,
                       "Y:        .EQU        _f\n" CODE
                       "_f:\n" S("MOV.L       L10,R1") "          %s\n" S("RTS")
                           S("NOP") L10 S(".DATA.L     _a"),
                       places[i]);
        CHECK(refuses(USE, in, SHCKIT_OFFSET, 5, SHCKIT_USE_ASSEMBLER_POOLS));
    }
    CHECK(refuses(USE,
                  CODE "_f:\n" S("MOV.L       L10+2,R1") S("BRA         _f+12")
                      S("NOP") L10 RESERVE S(".DATA.L     _a") S("RTS")
                          S("NOP"),
                  SHCKIT_OFFSET, 4, SHCKIT_USE_ASSEMBLER_POOLS));

    // a ?label defined or named in the block a pool's label opens. Dropping
    // the label ends the block, and a ?label there could take a definition
    // from the block before: one the pool's item names, and one a later
    // branch names, whatever the item holds
    CHECK(refuses(USE,
                  CODE "_f:\n?t:\n" S("MOV.L       L10,R1") S("NOP")
                      S("JMP         @R1") S("NOP")
                          L10 S(".DATA.L     ?t") "?t:\n" S("RTS") S("NOP"),
                  SHCKIT_LOCAL_LABEL, 9, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(
        refuses(USE,
                CODE "_f:\n?l:\n" S("MOV.L       L10,R1") S("RTS") S("NOP")
                    L10 S(".DATA.L     H'12345678") "?l:\n" S("DT          R4")
                        S("BF          ?l") S("RTS") S("NOP"),
                SHCKIT_LOCAL_LABEL, 9, SHCKIT_USE_ASSEMBLER_POOLS));
    // and none above the pool, or past the next name that opens a block
    CHECK(rewrites(
        USE,
        CODE "_f:\n?l:\n" S("MOV.L       L10,R1") S("RTS") S("NOP")
            L10 S(".DATA.L     _a") "_g:\n?l:\n" S("BRA         ?l") S("NOP"),
        CODE "_f:\n?l:\n" S("MOV.L       #_a,R1") S("RTS") S("NOP") S(".NOPOOL")
            ALIGN S(".POOL") "_g:\n?l:\n" S("BRA         ?l") S("NOP")));

    // an item no load reads: a value, or a pad in a pool no load reads from
    CHECK(refuses(USE,
                  LOADS("MOV.L       L10+4,R1") L10 S(".DATA.L     _a")
                      S(".DATA.L     _b") S(".DATA.L     _c"),
                  SHCKIT_UNUSED, 7, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(refuses(USE, CODE "_f:\n" S("RTS") S("NOP") L10 RESERVE,
                  SHCKIT_UNUSED, 6, SHCKIT_USE_ASSEMBLER_POOLS));

    // align-pool and use-assembler-pools together fail before anything
    // changes, at no line and by no patch, and leave the text unpatched. A
    // load or MOVA the patch rewrites keeps its original line number, and the
    // lines it writes have none.
    static const size_t inputs[] = {1, 2, 3, 4, 5, 0, 0, 0};
    struct shckit_text *text =
        read_text(LOADS("MOV.L       L10,R1") L10 S(".DATA.L     _a"));
    size_t at = SIZE_MAX;
    int by = -2;
    if (CHECK(text != NULL)) {
        CHECK(shckit_apply(text, POOL | USE, &at, &by) == SHCKIT_CONFLICT &&
              at == 0 && by == -1);
        CHECK(shckit_apply(text, DROP | POOL | USE, NULL, NULL) ==
              SHCKIT_CONFLICT);
        CHECK(shckit_count(text) == 7);
    }
    if (CHECK(text != NULL) &&
        CHECK(shckit_apply(text, USE, NULL, NULL) == SHCKIT_OK) &&
        CHECK(shckit_count(text) == 8)) {
        for (size_t i = 0; i < 8; i++) {
            const struct shckit_line *line = shckit_line(text, i);
            CHECK(line->input == inputs[i]);
            CHECK(line->patch ==
                  (i == 2 || inputs[i] == 0 ? SHCKIT_USE_ASSEMBLER_POOLS : -1));
        }
    }
    shckit_free(text);
    text = read_text(CODE FUNCTION MOVA S("FMOV.S      @R0,FR3") S("RTS")
                         S("NOP") FLOAT);
    if (CHECK(text != NULL) &&
        CHECK(shckit_apply(text, USE, NULL, NULL) == SHCKIT_OK)) {
        CHECK(shckit_line(text, 2)->input == 3 &&
              shckit_line(text, 2)->patch == SHCKIT_USE_ASSEMBLER_POOLS);
        CHECK(shckit_line(text, 3)->input == 4 &&
              shckit_line(text, 3)->patch == -1);
    }
    shckit_free(text);

    // a refusal names the original line number, though an earlier patch
    // dropped lines above it, and undoes what that patch made
    CHECK(refuses(DROP | USE,
                  CODE "    CLRMAC\n" ALIGN ALIGN LOADS("MOV         L10,R1")
                      L10 S(".DATA.L     _a"),
                  SHCKIT_USE, 7, SHCKIT_USE_ASSEMBLER_POOLS));
    CHECK(leaves(DROP | USE,
                 CODE "    CLRMAC\n" ALIGN ALIGN LOADS("MOV         L10,R1")
                     L10 S(".DATA.L     _a")));
}

// A section named again resumes where it stood, so FOO is the slot of the
// RTS in P's first block, opens no slot, and leaves the copy below it in
// none.
static const char resumed[] = CODE S("RTS") DATA S(".DATA.L     1")
    CODE S("FOO         R1") S("MOV         R1,R0") S("NOP") S("RTS") S("NOP");
static const char resumed_want[] = CODE S("RTS") DATA S(".DATA.L     1")
    CODE S("FOO         R1") S("MOV         R1,R0") S("RTS") S("NOP");

static void test_drop_r0_move_nop(void)
{
    // SHCASM's NOP right below SHCASM's copy of R1 to R15 into R0 goes, in a
    // code section, whatever its line end, and nothing else changes
    for (int i = 1; i <= 15; i++) {
        char in[256];
        char want[256];
        (void)snprintf(in, sizeof in,
                       CODE "          MOV         R%d,R0\n" S("NOP")
                           S("CMP/EQ      #1,R0"),
                       i);
        (void)snprintf(
            want, sizeof want,
            CODE "          MOV         R%d,R0\n" S("CMP/EQ      #1,R0"), i);
        CHECK(rewrites(MOVE, in, want));
    }
    CHECK(rewrites(MOVE,
                   CODE "_f:\n"
                        "          MOV         R4,R0\r\n"
                        "          NOP\r\n" S("RTS") S("NOP"),
                   CODE "_f:\n"
                        "          MOV         R4,R0\r\n" S("RTS") S("NOP")));

    // a copy in a delay slot, after which SHC writes no NOP, and a NOP that
    // isn't right below SHCASM's copy into R0 in the original text stay
    static const char *const kept[] = {
        S("RTS") S("MOV         R4,R0") S("NOP"),
        S("JSR         @R1") S("MOV         R4,R0") S("NOP"),
        S("BT/S        _f") S("MOV         R4,R0") S("NOP"),
        S("MOV         R0,R0") S("NOP"),
        S("MOV         R16,R0") S("NOP"),
        S("MOV         #1,R0") S("NOP"),
        S("MOV.L       @R1,R0") S("NOP"),
        S("MOV.L       R1,R0") S("NOP"),
        S("MOV         R1,R2") S("NOP"),
        S("mov         r1,r0") S("NOP"),
        S("MOV         R1,R0 ; c") S("NOP"),
        "    MOV     R1,R0\n    NOP\n",
        S("MOV         R1,R0") "    NOP\n",
        S("MOV         R1,R0") "x:        NOP\n",
        S("MOV         R1,R0") "L5:\n" S("NOP"),
        S("MOV         R1,R0") "; c\n" S("NOP"),
        S("MOV         R1,R0") "\n" S("NOP"),
        S("MOV         R1,R0") S("NOP         ; c"),
        S("MOV         R1,R0") S(".ALIGN      2") S("NOP"),
        DATA S("MOV         R1,R0") S("NOP"),
    };
    for (size_t i = 0; i < sizeof kept / sizeof kept[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, CODE "_f:\n%s" S("RTS") S("NOP"),
                       kept[i]);
        CHECK(keeps(MOVE, in));
    }

    // a NOP that drop-inline-align, which goes first, brings right below a
    // copy into R0 stays, and so does one whose copy the drop puts in a slot
    CHECK(rewrites(DROP | MOVE, CODE S("MOV         R1,R0") ALIGN S("NOP"),
                   CODE S("MOV         R1,R0") S("NOP")));
    CHECK(rewrites(DROP | MOVE,
                   CODE S("RTS") ALIGN S("MOV         R1,R0") S("NOP"),
                   CODE S("RTS") S("MOV         R1,R0") S("NOP")));

    // a copy shckit can't say is in no slot: past a delayed branch and an
    // .ALIGN, a .POOL or a .NOPOOL, which may fill the slot, or right below
    // an instruction outside the opcode table and in no slot. One below
    // such an instruction in a slot, which opens none, past one and an
    // instruction that isn't, or past a branch whose slot is filled, is in
    // no slot.
    static const char *const fills[] = {
        S(".ALIGN      2"),
        S(".POOL"),
        S(".NOPOOL"),
    };
    for (size_t i = 0; i < sizeof fills / sizeof fills[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE S("BRA         _f") "%s" S("MOV         R1,R0")
                           S("NOP"),
                       fills[i]);
        CHECK(refuses(MOVE, in, SHCKIT_SLOT, 2, SHCKIT_DROP_R0_MOVE_NOP));
    }
    CHECK(refuses(MOVE,
                  CODE S("FOO         R1") S("MOV         R1,R0") S("NOP"),
                  SHCKIT_NOT_HELD, 2, SHCKIT_DROP_R0_MOVE_NOP));
    CHECK(rewrites(
        MOVE, CODE S("RTS") S("FOO         R1") S("MOV         R1,R0") S("NOP"),
        CODE S("RTS") S("FOO         R1") S("MOV         R1,R0")));
    CHECK(rewrites(MOVE,
                   CODE S("FOO         R1") S("ADD         #1,R1")
                       S("MOV         R1,R0") S("NOP"),
                   CODE S("FOO         R1") S("ADD         #1,R1")
                       S("MOV         R1,R0")));
    CHECK(rewrites(
        MOVE,
        CODE S("BRA         _f") S("NOP") ALIGN S("MOV         R1,R0") S("NOP"),
        CODE S("BRA         _f") S("NOP") ALIGN S("MOV         R1,R0")));

    // and across a section's blocks: a copy below an instruction in a slot
    // there is in no slot, and a copy that is a slot there keeps its NOP
    CHECK(rewrites(MOVE, resumed, resumed_want));
    CHECK(keeps(MOVE, CODE S("RTS") DATA S(".DATA.L     1")
                          CODE S("MOV         R1,R0") S("NOP")));

    // below a dropped NOP in its section, an .ALIGN, a .POOL or a .NOPOOL
    // where a slot is due, or may be: whether it writes a NOP into the slot
    // depends on the address, which the drop can change. One where no
    // slot is due, one above the NOP, and one in another section stay.
    CHECK(refuses(MOVE,
                  CODE "_f:\n" S("MOV         R1,R0") S("NOP")
                      S("BRA         _f") ALIGN S("ADD         #1,R2") S("RTS")
                          S("NOP"),
                  SHCKIT_SLOT, 5, SHCKIT_DROP_R0_MOVE_NOP));
    for (size_t i = 0; i < sizeof fills / sizeof fills[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE "_f:\n" S("MOV         R1,R0") S("NOP")
                           S("BRA         _f") "%s" S("ADD         #1,R2")
                               S("RTS") S("NOP"),
                       fills[i]);
        CHECK(refuses(MOVE, in, SHCKIT_SLOT, 5, SHCKIT_DROP_R0_MOVE_NOP));
    }
    CHECK(refuses(MOVE,
                  CODE S("MOV         R1,R0") S("NOP") S("FOO         R1")
                      ALIGN S("ADD         #1,R2"),
                  SHCKIT_NOT_HELD, 4, SHCKIT_DROP_R0_MOVE_NOP));
    CHECK(refuses(MOVE,
                  CODE S("MOV         R1,R0") S("NOP") S("BRA         _f")
                      ALIGN ALIGN S("ADD         #1,R2"),
                  SHCKIT_SLOT, 4, SHCKIT_DROP_R0_MOVE_NOP));
    CHECK(rewrites(MOVE,
                   CODE S("MOV         R1,R0") S("NOP") S("RTS") S("NOP")
                       ALIGN S("ADD         #1,R2"),
                   CODE S("MOV         R1,R0") S("RTS") S("NOP")
                       ALIGN S("ADD         #1,R2")));
    CHECK(rewrites(MOVE,
                   CODE S("BRA         _f") ALIGN S("ADD         #1,R2")
                       S("MOV         R1,R0") S("NOP"),
                   CODE S("BRA         _f") ALIGN S("ADD         #1,R2")
                       S("MOV         R1,R0")));
    CHECK(rewrites(MOVE,
                   CODE S("MOV         R1,R0") S("NOP")
                       S(".SECTION    Q,CODE,ALIGN=4") S("BRA         _f")
                           ALIGN S("ADD         #1,R2"),
                   CODE S("MOV         R1,R0") S(".SECTION    Q,CODE,ALIGN=4")
                       S("BRA         _f") ALIGN S("ADD         #1,R2")));
    CHECK(refuses(MOVE,
                  CODE S("MOV         R1,R0") S("NOP")
                      S(".SECTION    Q,CODE,ALIGN=4") CODE S("BRA         _f")
                          ALIGN S("ADD         #1,R2"),
                  SHCKIT_SLOT, 6, SHCKIT_DROP_R0_MOVE_NOP));

    // an operand that names a code label and a word that is no label, a
    // number, a symbol of an .EQU or one from elsewhere: past a dropped NOP
    // what it names moves or doesn't by the offset
    static const struct {
        const char *body;
        size_t at;
    } offsets[] = {
        {S("BRA         _f+4"), 6},
        {S("BT          4+_f"), 6},
        {S("MOV.L       #_f-2,R1"), 6},
        {S("BRA         _f+X"), 6},
        {S("BRA         _f+_x"), 6},
        {DATA S(".DATA.L     STARTOF P,_f+2") CODE, 7},
    };
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       "X:        .EQU        2\n" CODE
                       "_f:\n" S("MOV         R1,R0") S("NOP") "%s" S("RTS")
                           S("NOP"),
                       offsets[i].body);
        CHECK(refuses(MOVE, in, SHCKIT_OFFSET, offsets[i].at,
                      SHCKIT_DROP_R0_MOVE_NOP));
    }

    // a ?label counts as a code label wherever it is defined: asmsh takes
    // the definition in the use's block, which the patch doesn't follow, and
    // a ?label alone moves with its code
    CHECK(refuses(MOVE,
                  DATA "?l:\n" S(".DATA.L     0") CODE
                  "_f:\n?l:\n" S("MOV         R1,R0") S("NOP")
                      S("ADD         #1,R0") S("BRA         ?l+4") S("NOP"),
                  SHCKIT_OFFSET, 10, SHCKIT_DROP_R0_MOVE_NOP));
    CHECK(rewrites(MOVE,
                   CODE "_f:\n?l:\n" S("MOV         R1,R0") S("NOP")
                       S("BRA         ?l") S("NOP"),
                   CODE "_f:\n?l:\n" S("MOV         R1,R0") S("BRA         ?l")
                       S("NOP")));

    // and one that offsets another place in code: $, where the line
    // stands, the PC of @(disp,PC), or an .EQU's symbol whose value names a
    // code label, directly or through another .EQU
    static const char *const places[] = {
        S("BRA         $+8"),        S(".DATA.L     $+4"),
        S("BRA         $"),          S("MOV.L       @(8,PC),R1"),
        S("MOVA        @(4,pc),R0"), S("BRA         Y+6"),
        S("BRA         Z+2"),        S("MOV.L       #Y+4,R1"),
    };
    for (size_t i = 0; i < sizeof places / sizeof places[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       "Y:        .EQU        _f\n"
                       "Z:        .EQU        Y\n" CODE
                       "_f:\n" S("MOV         R1,R0") S("NOP") "%s" S("RTS")
                           S("NOP"),
                       places[i]);
        CHECK(refuses(MOVE, in, SHCKIT_OFFSET, 7, SHCKIT_DROP_R0_MOVE_NOP));
    }
    CHECK(rewrites(MOVE,
                   "Y:        .EQU        _f\n" CODE
                   "_f:\n" S("MOV         R1,R0") S("NOP") S("BRA         Y")
                       S("NOP"),
                   "Y:        .EQU        _f\n" CODE
                   "_f:\n" S("MOV         R1,R0") S("BRA         Y") S("NOP")));

    // a symbol .ASSIGN defines can hold another value at each use, which
    // the patch doesn't follow: an .ASSIGN whose value names a code label is
    // refused at its line, and the symbol otherwise counts as a number, which
    // alone offsets nothing
    CHECK(refuses(MOVE,
                  "X:        .ASSIGN     2\n" CODE
                  "_f:\n" S("MOV         R1,R0") S("NOP") S("BRA         _f+X")
                      S("NOP"),
                  SHCKIT_OFFSET, 6, SHCKIT_DROP_R0_MOVE_NOP));
    CHECK(refuses(
        MOVE,
        "Y:        .ASSIGN     1\n" CODE "_f:\n" S("MOV         R1,R0")
            S("NOP") "Y:        .ASSIGN     _f\n" S("BRA         Y") S("NOP"),
        SHCKIT_OFFSET, 6, SHCKIT_DROP_R0_MOVE_NOP));
    CHECK(rewrites(
        MOVE,
        "X:        .ASSIGN     2\n" CODE "_f:\n" S("MOV         R1,R0")
            S("NOP") "X:        .ASSIGN     4\n" S("MOV         #X+2,R2")
                S("BRA         X") S("NOP"),
        "X:        .ASSIGN     2\n" CODE
        "_f:\n" S("MOV         R1,R0") "X:        .ASSIGN     4\n" S(
            "MOV         #X+2,R2") S("BRA         X") S("NOP")));

    // a label on a line asmsh doesn't take as a definition, such as an
    // .ALIGN's, defines nothing: X is the 6 below it
    static const char *const sixes[] = {
        "X:        .ASSIGN     6\n",
        "X:        .EQU        6\n",
    };
    for (size_t i = 0; i < sizeof sixes / sizeof sixes[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE "X:        .ALIGN      4\n"
                            "_f:\n" S("MOV         R1,R0")
                                S("NOP") "%s" S("BRA         _f+X") S("NOP"),
                       sixes[i]);
        CHECK(refuses(MOVE, in, SHCKIT_OFFSET, 7, SHCKIT_DROP_R0_MOVE_NOP));
    }

    // and none for a code label alone, the difference of two labels, a
    // pool's label or a data label with an offset, or with no NOP to drop
    static const char *const others[] = {
        S("BRA         _f"),
        S(".DATA.W     _f-L1"),
        S("MOV.L       L10+4,R1"),
        S("MOV.L       #_d+4,R1"),
    };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        char in[512];
        char want[512];
        (void)snprintf(in, sizeof in,
                       CODE "_f:\n" S("MOV         R1,R0")
                           S("NOP") "L1:\n%s" S("RTS") S("NOP")
                               L10 S(".DATA.L     1") S(".DATA.L     2") DATA
                       "_d:\n" S(".DATA.L     1"),
                       others[i]);
        (void)snprintf(want, sizeof want,
                       CODE "_f:\n" S("MOV         R1,R0") "L1:\n%s" S("RTS")
                           S("NOP") L10 S(".DATA.L     1") S(".DATA.L     2")
                               DATA "_d:\n" S(".DATA.L     1"),
                       others[i]);
        CHECK(rewrites(MOVE, in, want));
    }
    CHECK(keeps(MOVE, CODE "_f:\n" S("MOV         R1,R2") S("NOP")
                          S("BRA         _f+4") S("NOP")));

    // after the pool patches, which go first, the NOPs below copies into R0
    // in SHC's own text go too
    CHECK(rewrites(USE | MOVE,
                   CODE "_f:\n" S("MOV.L       L10,R1") S("MOV         R4,R0")
                       S("NOP") S("RTS") S("NOP") L10 S(".DATA.L     _a"),
                   CODE "_f:\n" S("MOV.L       #_a,R1") S("MOV         R4,R0")
                       S("RTS") S("NOP") NOPOOL POOLS));
    CHECK(rewrites(DROP | POOL | MOVE,
                   CODE "    CLRMAC\n" ALIGN S("MOV         R4,R0") S("NOP")
                       S("RTS") S("NOP") L10 S(".DATA.L     _a"),
                   CODE "    CLRMAC\n" S("MOV         R4,R0") S("RTS") S("NOP")
                       ALIGN L10 S(".DATA.L     _a")));

    // each kept line keeps its original line number
    static const size_t inputs[] = {1, 2, 3, 5};
    struct shckit_text *text = read_text(CODE "_f:\n" S("MOV         R1,R0")
                                             S("NOP") S("CMP/EQ      #1,R0"));
    if (CHECK(text != NULL) &&
        CHECK(shckit_apply(text, MOVE, NULL, NULL) == SHCKIT_OK) &&
        CHECK(shckit_count(text) == 4)) {
        for (size_t i = 0; i < 4; i++) {
            CHECK(shckit_line(text, i)->input == inputs[i] &&
                  shckit_line(text, i)->patch == -1);
        }
    }
    shckit_free(text);

    // a refusal names the original line number, and undoes what the
    // patches before it made
    CHECK(refuses(DROP | MOVE,
                  CODE ALIGN ALIGN "_f:\n" S("MOV         R1,R0") S("NOP")
                      S("BRA         _f+2"),
                  SHCKIT_OFFSET, 7, SHCKIT_DROP_R0_MOVE_NOP));
    CHECK(leaves(DROP | MOVE, CODE ALIGN "_f:\n" S("MOV         R1,R0") S("NOP")
                                  S("BRA         _f+2") S("NOP")));
}

// SHCASM's call through R3 with NOP in its slot, and the same call with the
// slot dropped.
#define CALL S("JSR         @R3") S("NOP")
#define FILLED S("JSR         @R3")

static void test_fill_call_slot(void)
{
    // SHCASM's NOP in the slot of SHCASM's JSR @Rm goes when SHCASM's
    // ADD #imm,Rn is right below it, n from 8 to 14, in a code section,
    // whatever its line end and immediate, and nothing else changes
    static const char *const adds[] = {"#8",  "#16",  "#-8",  "#0",
                                       "#-1", "#127", "#-128"};
    for (int reg = 8; reg <= 14; reg++) {
        for (size_t i = 0; i < sizeof adds / sizeof adds[0]; i++) {
            char in[256];
            char want[256];
            (void)snprintf(in, sizeof in,
                           CODE "_f:\n" CALL
                                "          ADD         %s,R%d\n" S("RTS")
                                    S("NOP"),
                           adds[i], reg);
            (void)snprintf(want, sizeof want,
                           CODE "_f:\n" FILLED
                                "          ADD         %s,R%d\n" S("RTS")
                                    S("NOP"),
                           adds[i], reg);
            CHECK(rewrites(FILL, in, want));
        }
    }
    CHECK(rewrites(FILL,
                   CODE "          JSR         @R13\r\n"
                        "          NOP\r\n"
                        "          ADD         #8,R14\r\n" S("RTS") S("NOP"),
                   CODE "          JSR         @R13\r\n"
                        "          ADD         #8,R14\r\n" S("RTS") S("NOP")));
    CHECK(rewrites(FILL,
                   CODE CALL S("ADD         #8,R13")
                       CALL S("ADD         #16,R13") S("ADD         #4,R14"),
                   CODE FILLED S("ADD         #8,R13") FILLED S(
                       "ADD         #16,R13") S("ADD         #4,R14")));

    // a slot that holds anything but SHCASM's NOP, an add of another form,
    // to another register or to the call's own, another call or branch, and
    // a call, NOP or add written otherwise than SHCASM writes it, continued,
    // or with any line between the three, stay
    static const char *const kept[] = {
        S("JSR         @R3") S("ADD         #8,R13") S("NOP"),
        S("JSR         @R3") S("MOV         R13,R4") S("ADD         #8,R13"),
        CALL S("ADD         #8,R7"),
        CALL S("ADD         #8,R15"),
        CALL S("ADD         #8,R0"),
        CALL S("ADD         #8,R08"),
        CALL S("ADD         #8,R16"),
        S("JSR         @R13") S("NOP") S("ADD         #8,R13"),
        CALL S("ADD         R1,R13"),
        CALL S("ADD         #128,R13"),
        CALL S("ADD         #-129,R13"),
        CALL S("ADD         #08,R13"),
        CALL S("ADD         #-0,R13"),
        CALL S("ADD         #H'08,R13"),
        CALL S("ADD         #+8,R13"),
        CALL S("ADD         #8,R13,R1"),
        CALL S("ADDC        R1,R13"),
        CALL S("MOV         #8,R13"),
        CALL S("MOV.L       @R13+,R1"),
        S("BSR         _g") S("NOP") S("ADD         #8,R13"),
        S("BSRF        R3") S("NOP") S("ADD         #8,R13"),
        S("JMP         @R3") S("NOP") S("ADD         #8,R13"),
        S("BRA         _g") S("NOP") S("ADD         #8,R13"),
        S("JSR         @R03") S("NOP") S("ADD         #8,R13"),
        S("JSR         @(R3)") S("NOP") S("ADD         #8,R13"),
        S("jsr         @r3") S("NOP") S("ADD         #8,R13"),
        CALL S("add         #8,r13"),
        S("JSR         @R3 ; c") S("NOP") S("ADD         #8,R13"),
        CALL S("ADD         #8,R13 ; c"),
        CALL S("ADD         #8,R13") "+           ,R1\n",
        CALL S("ADD         #8,") "+           R13\n",
        S("JSR         @R3") S("NOP         ; c") S("ADD         #8,R13"),
        "    JSR     @R3\n    NOP\n    ADD     #8,R13\n",
        CALL "    ADD     #8,R13\n",
        S("JSR         @R3") "    NOP\n" S("ADD         #8,R13"),
        CALL "x:        ADD         #8,R13\n",
        S("JSR         @R3") "x:        NOP\n" S("ADD         #8,R13"),
        CALL "L5:\n" S("ADD         #8,R13"),
        CALL "; c\n" S("ADD         #8,R13"),
        CALL "\n" S("ADD         #8,R13"),
        CALL S(".ALIGN      2") S("ADD         #8,R13"),
        S("JSR         @R3") "\n" S("NOP") S("ADD         #8,R13"),
        S("BRA         _g") CALL S("ADD         #8,R13"),
        DATA CALL S("ADD         #8,R13"),
    };
    for (size_t i = 0; i < sizeof kept / sizeof kept[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, CODE "_f:\n%s" S("RTS") S("NOP"),
                       kept[i]);
        CHECK(keeps(FILL, in));
    }

    // a NOP and an add that drop-inline-align, which goes first, brings
    // together stay, and so does one whose call the drop puts in a slot
    CHECK(rewrites(DROP | FILL, CODE CALL ALIGN S("ADD         #8,R13"),
                   CODE CALL S("ADD         #8,R13")));
    CHECK(rewrites(DROP | FILL,
                   CODE S("JSR         @R3") ALIGN S("NOP")
                       S("ADD         #8,R13"),
                   CODE CALL S("ADD         #8,R13")));
    CHECK(rewrites(DROP | FILL,
                   CODE S("RTS") ALIGN CALL S("ADD         #8,R13"),
                   CODE S("RTS") CALL S("ADD         #8,R13")));

    // a call shckit can't say is in no slot: past a delayed branch and an
    // .ALIGN, a .POOL or a .NOPOOL, which may fill the slot, or right below
    // an instruction outside the opcode table and in no slot. One below
    // such an instruction in a slot, or past one and an instruction that
    // isn't, is in no slot.
    static const char *const fills[] = {
        S(".ALIGN      2"),
        S(".POOL"),
        S(".NOPOOL"),
    };
    for (size_t i = 0; i < sizeof fills / sizeof fills[0]; i++) {
        char in[256];
        (void)snprintf(
            in, sizeof in,
            CODE S("BRA         _f") "%s" CALL S("ADD         #8,R13"),
            fills[i]);
        CHECK(refuses(FILL, in, SHCKIT_SLOT, 2, SHCKIT_FILL_CALL_SLOT));
    }
    CHECK(refuses(FILL, CODE S("FOO         R1") CALL S("ADD         #8,R13"),
                  SHCKIT_NOT_HELD, 2, SHCKIT_FILL_CALL_SLOT));
    CHECK(rewrites(
        FILL, CODE S("RTS") S("FOO         R1") CALL S("ADD         #8,R13"),
        CODE S("RTS") S("FOO         R1") FILLED S("ADD         #8,R13")));
    CHECK(rewrites(FILL,
                   CODE S("FOO         R1") S("ADD         #1,R1")
                       CALL S("ADD         #8,R13"),
                   CODE S("FOO         R1") S("ADD         #1,R1")
                       FILLED S("ADD         #8,R13")));

    // and across a section's blocks: a call that is a slot there keeps its
    // NOP
    CHECK(keeps(FILL, CODE S("RTS") DATA S(".DATA.L     1")
                          CODE CALL S("ADD         #8,R13")));

    // below a dropped NOP in its section, an .ALIGN, a .POOL or a .NOPOOL
    // where a slot is due, or may be, and an operand that offsets a place in
    // code, as for drop-r0-move-nop. One where no slot is due, and one in
    // another section, stay.
    for (size_t i = 0; i < sizeof fills / sizeof fills[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE "_f:\n" CALL S("ADD         #8,R13")
                           S("BRA         _f") "%s" S("ADD         #1,R2")
                               S("RTS") S("NOP"),
                       fills[i]);
        CHECK(refuses(FILL, in, SHCKIT_SLOT, 6, SHCKIT_FILL_CALL_SLOT));
    }
    CHECK(refuses(FILL,
                  CODE CALL S("ADD         #8,R13") S("FOO         R1")
                      ALIGN S("ADD         #1,R2"),
                  SHCKIT_NOT_HELD, 5, SHCKIT_FILL_CALL_SLOT));
    CHECK(rewrites(FILL,
                   CODE CALL S("ADD         #8,R13") S("RTS") S("NOP")
                       ALIGN S("ADD         #1,R2"),
                   CODE FILLED S("ADD         #8,R13") S("RTS") S("NOP")
                       ALIGN S("ADD         #1,R2")));
    CHECK(rewrites(
        FILL,
        CODE CALL S("ADD         #8,R13") S(".SECTION    Q,CODE,ALIGN=4")
            S("BRA         _f") ALIGN S("ADD         #1,R2"),
        CODE FILLED S("ADD         #8,R13") S(".SECTION    Q,CODE,ALIGN=4")
            S("BRA         _f") ALIGN S("ADD         #1,R2")));
    static const char *const offsets[] = {
        S("BRA         _f+4"),
        S("BRA         $+8"),
        S("MOV.L       @(8,PC),R1"),
        S("MOVA        @(4,PC),R0"),
    };
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE "_f:\n" CALL S("ADD         #8,R13") "%s" S("RTS")
                           S("NOP"),
                       offsets[i]);
        CHECK(refuses(FILL, in, SHCKIT_OFFSET, 6, SHCKIT_FILL_CALL_SLOT));
    }
    CHECK(keeps(FILL, CODE "_f:\n" S("JSR         @R3") S("NOP") S(
                          "ADD         #8,R7") S("BRA         _f+4") S("NOP")));

    // after the pool patches and drop-r0-move-nop, which go first, the
    // slot's NOP in SHC's own text goes too
    CHECK(rewrites(USE | MOVE | FILL,
                   CODE "_f:\n" S("MOV.L       L10,R3") S("MOV         R4,R0")
                       S("NOP") CALL S("ADD         #8,R13") S("RTS") S("NOP")
                           L10 S(".DATA.L     _a"),
                   CODE "_f:\n" S("MOV.L       #_a,R3") S("MOV         R4,R0")
                       FILLED S("ADD         #8,R13") S("RTS") S("NOP")
                           NOPOOL POOLS));

    // each kept line keeps its original line number
    static const size_t inputs[] = {1, 2, 3, 5, 6};
    struct shckit_text *text =
        read_text(CODE "_f:\n" CALL S("ADD         #8,R13") S("RTS"));
    if (CHECK(text != NULL) &&
        CHECK(shckit_apply(text, FILL, NULL, NULL) == SHCKIT_OK) &&
        CHECK(shckit_count(text) == 5)) {
        for (size_t i = 0; i < 5; i++) {
            CHECK(shckit_line(text, i)->input == inputs[i] &&
                  shckit_line(text, i)->patch == -1);
        }
    }
    shckit_free(text);

    // a refusal names the original line number, and undoes what the
    // patches before it made
    CHECK(refuses(DROP | FILL,
                  CODE ALIGN ALIGN "_f:\n" CALL S("ADD         #8,R13")
                      S("BRA         _f+2"),
                  SHCKIT_OFFSET, 8, SHCKIT_FILL_CALL_SLOT));
    CHECK(leaves(DROP | FILL, CODE ALIGN "_f:\n" CALL S("ADD         #8,R13")
                                  S("BRA         _f+2") S("NOP")));
}

static void test_apply(void)
{
    // before any patch, apply reads the text as asmsh does, to its first
    // .END, and fails at no patch on a line there shckit can't read: one the
    // reader can't type, which could start a section, align the code or
    // hold a pool, or a continuation joined to no statement. Past .END a
    // patch would change lines asmsh doesn't read, so it fails on any line
    // there but a blank or a comment, whatever it holds.
    struct shckit_text *text = NULL;
    CHECK(refuses(DROP, CODE "* x\n" ALIGN, SHCKIT_UNREADABLE, 2, -1));
    CHECK(refuses(DROP, CODE ALIGN S(".ALIGN      H'4"), SHCKIT_UNREADABLE, 3,
                  -1));
    CHECK(refuses(POOL, "* x\n" CODE L10 S(".DATA.L     _a"), SHCKIT_UNREADABLE,
                  1, -1));
    CHECK(refuses(DROP | POOL, CODE S("NOP") S(".END") ALIGN, SHCKIT_PAST_END,
                  4, -1));
    CHECK(refuses(POOL,
                  CODE S("NOP") S(".END") S("NOP") L10 S(".DATA.L     _a"),
                  SHCKIT_PAST_END, 4, -1));
    CHECK(refuses(DROP, CODE S(".END") "\n; c\n* x\n", SHCKIT_PAST_END, 5, -1));
    CHECK(refuses(DROP, CODE S(".END") "_f:\n", SHCKIT_PAST_END, 3, -1));
    CHECK(refuses(DROP, CODE S(".END") S(".END"), SHCKIT_PAST_END, 3, -1));
    CHECK(refuses(DROP, CODE S(".END") "+   1\n", SHCKIT_PAST_END, 3, -1));
    CHECK(keeps(DROP | POOL, CODE S("NOP") S(".END") "\n; the end\n   \n"));

    // a continuation joins nothing on the first line, below a label, a blank,
    // a comment or an instruction without operands, or below a directive
    // whose field doesn't end with a comma, as the .SECTION line's doesn't.
    // One below a field that ends with a comma, or that starts with one
    // below an instruction's, joins it.
    CHECK(refuses(DROP, CODE "+ x\n" ALIGN, SHCKIT_UNREADABLE, 2, -1));
    CHECK(refuses(DROP, "+   1\n" CODE, SHCKIT_UNREADABLE, 1, -1));
    CHECK(refuses(DROP, CODE "_f:\n+   1\n", SHCKIT_UNREADABLE, 3, -1));
    CHECK(refuses(POOL, CODE "\n+   1\n", SHCKIT_UNREADABLE, 3, -1));
    CHECK(refuses(POOL, CODE "; c\n+   1\n", SHCKIT_UNREADABLE, 3, -1));
    CHECK(refuses(DROP, CODE S("NOP") "+   R1\n", SHCKIT_UNREADABLE, 3, -1));
    CHECK(refuses(DROP, CODE S(".DATA.W     1") "+   ,2\n", SHCKIT_UNREADABLE,
                  3, -1));
    CHECK(keeps(DROP | POOL,
                CODE S("RTS") S("NOP") S(".DATA.W     1,") "+   2,\n"
                                                           "+   3\n"));
    CHECK(keeps(DROP | POOL, CODE S("ADD         R1") "+   ,R2\n"));

    // each changes nothing, and leaves the text's one apply
    CHECK(leaves(DROP | POOL, CODE ALIGN S(".END") S("NOP")));
    CHECK(leaves(DROP, CODE "+ x\n" ALIGN));
    text = read_text(CODE S(".END") S("NOP"));
    if (CHECK(text != NULL)) {
        CHECK(shckit_apply(text, DROP, NULL, NULL) == SHCKIT_PAST_END);
        CHECK(shckit_apply(text, DROP, NULL, NULL) == SHCKIT_PAST_END);
    }
    shckit_free(text);
}

static void test_patch_names(void)
{
    // each patch's name finds it, and nothing outside the table has one
    for (int i = 0; i < SHCKIT_PATCHES; i++) {
        CHECK(shckit_patch(shckit_patch_name(i)) == i);
    }
    CHECK(shckit_patch_name(-1) == NULL &&
          shckit_patch_name(SHCKIT_PATCHES) == NULL);
    CHECK(shckit_patch("align") == -1 && shckit_patch("") == -1);
    CHECK(strcmp(shckit_patch_name(SHCKIT_DROP_INLINE_ALIGN),
                 "drop-inline-align") == 0);
    CHECK(strcmp(shckit_patch_name(SHCKIT_ALIGN_POOL), "align-pool") == 0);
    CHECK(strcmp(shckit_patch_name(SHCKIT_FILL_CALL_SLOT), "fill-call-slot") ==
          0);
}

// A function _f, and inspecting one as 5.0 R10's -code=asmcode with SZ
// unknown. R32 is the other release some rules hold for.
#define R10 "5.0R10"
#define R32 "5.0R32"
#define ASM SHCKIT_ASMCODE
#define OBJ SHCKIT_MACHINECODE
#define UNKNOWN SHCKIT_SZ_UNKNOWN
#define F(body) "_f:\n" body
#define INSPECTS(body, want) inspects(F(body), R10, ASM, UNKNOWN, want)

static bool same_witness(const struct shckit_witness *one,
                         const struct shckit_witness *two)
{
    return one->kind == two->kind && one->rule == two->rule &&
           one->index == two->index && one->value == two->value;
}

static bool same_inspection(const struct shckit_inspection *one,
                            const struct shckit_inspection *two)
{
    return same_witness(&one->witness, &two->witness) &&
           one->incomplete == two->incomplete &&
           one->incomplete_index == two->incomplete_index;
}

static void describe(const struct shckit_text *text,
                     const struct shckit_witness *witness, char *got,
                     size_t size)
{
    // append the witness's kind, and its rule, line and the value it names
    static const char *const kinds[] = {"none", "opcode", "operand", "abi"};
    size_t len = strlen(got);
    (void)snprintf(got + len, size - len, "%s", kinds[witness->kind]);
    len = strlen(got);
    if (witness->kind != SHCKIT_NO_WITNESS) {
        (void)snprintf(got + len, size - len, " %s %zu", witness->rule,
                       shckit_line(text, witness->index)->input);
        len = strlen(got);
    }
    if (witness->kind == SHCKIT_ABI && witness->value != 0) {
        (void)snprintf(got + len, size - len, " %+ld", (long)witness->value);
    } else if (witness->kind == SHCKIT_OPERAND &&
               strcmp(witness->rule, "small-constant") == 0) {
        (void)snprintf(got + len, size - len, " #%ld", (long)witness->value);
    }
}

static void describe_limit(const struct shckit_text *text,
                           const struct shckit_inspection *result, char *got,
                           size_t size)
{
    // append a semicolon and the limit and its line if there is one
    size_t len = strlen(got);
    if (result->incomplete != NULL) {
        (void)snprintf(got + len, size - len, "; %s", result->incomplete);
        len = strlen(got);
    }
    if (result->incomplete_index != SIZE_MAX) {
        (void)snprintf(got + len, size - len, " %zu",
                       shckit_line(text, result->incomplete_index)->input);
    }
}

static bool all_agree(const struct shckit_text *text,
                      const struct shckit_producer *producer,
                      const struct shckit_function *function,
                      const struct shckit_inspection *result)
{
    // shckit_inspect_all sets the result shckit_inspect does, and its first
    // witness by kind, then index, then order is the result's
    struct shckit_inspection all;
    struct shckit_witness *found = NULL;
    size_t count = 0;
    size_t first = 0;
    bool ok = shckit_inspect_all(text, producer, function, &all, &found,
                                 &count) == SHCKIT_OK &&
              same_inspection(&all, result) &&
              (count == 0) == (result->witness.kind == SHCKIT_NO_WITNESS);
    for (size_t i = 1; ok && i < count; i++) {
        if (found[i].kind < found[first].kind ||
            (found[i].kind == found[first].kind &&
             found[i].index < found[first].index)) {
            first = i;
        }
    }
    ok = ok && (count == 0 || same_witness(&found[first], &result->witness));
    free(found);
    return ok;
}

static bool inspects_lines(const char *in, size_t first, size_t end,
                           const char *release, unsigned modes, int sz,
                           const char *want)
{
    // inspecting in's lines from index first up to end, or to the text's end
    // for 0, finds exactly want: the witness's kind, and its rule, line and
    // the value it names, then a semicolon and the limit and its line if
    // there is one. Twice alike, the text unchanged, and shckit_inspect_all
    // agrees.
    struct shckit_text *text = read_text(in);
    struct shckit_producer producer = {release, modes};
    struct shckit_function function = {first, end, sz};
    struct shckit_inspection result;
    struct shckit_inspection again;
    char got[256] = "";
    bool ok = text != NULL;
    if (ok) {
        function.end = end != 0 ? end : shckit_count(text);
        ok = shckit_inspect(text, &producer, &function, &result) == SHCKIT_OK &&
             shckit_inspect(text, &producer, &function, &again) == SHCKIT_OK &&
             same_inspection(&result, &again) &&
             all_agree(text, &producer, &function, &result) &&
             same_bytes(text, in);
    }
    if (ok) {
        describe(text, &result.witness, got, sizeof got);
        describe_limit(text, &result, got, sizeof got);
    }
    ok = ok && strcmp(got, want) == 0;
    if (!ok) {
        printf("  got \"%s\"\n", got);
    }
    shckit_free(text);
    return ok;
}

static bool inspects_all(const char *in, const char *release, unsigned modes,
                         int sz, const char *want)
{
    // inspecting the whole of in finds exactly want: every witness, each as
    // inspects_lines() gives one and after the first a comma, or none, then
    // the limit as inspects_lines() gives it
    struct shckit_text *text = read_text(in);
    struct shckit_producer producer = {release, modes};
    struct shckit_function function = {0, 0, sz};
    struct shckit_inspection result;
    struct shckit_witness *found = NULL;
    size_t count = 0;
    char got[1024] = "";
    bool ok = text != NULL;
    if (ok) {
        function.end = shckit_count(text);
        ok = shckit_inspect_all(text, &producer, &function, &result, &found,
                                &count) == SHCKIT_OK;
    }
    for (size_t i = 0; ok && i < count; i++) {
        if (i != 0) {
            (void)snprintf(got + strlen(got), sizeof got - strlen(got), ", ");
        }
        describe(text, &found[i], got, sizeof got);
    }
    if (ok && count == 0) {
        (void)snprintf(got, sizeof got, "none");
    }
    if (ok) {
        describe_limit(text, &result, got, sizeof got);
    }
    ok = ok && strcmp(got, want) == 0;
    if (!ok) {
        printf("  got \"%s\"\n", got);
    }
    free(found);
    shckit_free(text);
    return ok;
}

static bool inspects(const char *in, const char *release, unsigned modes,
                     int sz, const char *want)
{
    return inspects_lines(in, 0, 0, release, modes, sz, want);
}

static void test_inspect_rules(void)
{
    // each instruction rule under the releases and modes it holds for, and
    // a limit for an instruction it names under others
    static const char *const fives[] = {
        "5.0R10", "5.0R26", "5.0R27", "5.0R28", "5.0R31", "5.0R32",
        "5.1R01", "5.1R03", "5.1R04", "5.1R08", "5.1R11", "5.1R13",
    };
    static const struct {
        const char *body;
        const char *rule;
        const char *want;
    } words[] = {
        {S("ROTCL       R4"), "rotcl", "opcode rotcl 2"},
        {S("ROTCR       R15"), "rotcr", "opcode rotcr 2; stack-unknown 2"},
        {S("SWAP.B      R4,R5"), "swap", "opcode swap 2"},
        {S("SWAP.W      R5,R6"), "swap", "opcode swap 2"},
    };
    char in[256];
    for (size_t i = 0; i < sizeof fives / sizeof fives[0]; i++) {
        bool stack = i == 0;
        for (unsigned modes = OBJ; modes <= (OBJ | ASM); modes++) {
            CHECK(inspects(F(S("DMULS.L     R4,R5") S("RTS") S("NOP")),
                           fives[i], modes, UNKNOWN,
                           stack ? "opcode dmuls 2"
                                 : "opcode dmuls 2; abi-scope"));
            CHECK(inspects(F(S("DMULU       R4,R5") S("RTS") S("NOP")),
                           fives[i], modes, UNKNOWN,
                           stack ? "opcode dmulu 2"
                                 : "opcode dmulu 2; abi-scope"));
        }
    }
    // ROTCL, ROTCR and SWAP: under machinecode in each 5.x release, and
    // under asmcode in R10 and R32 alone
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        char want[64];
        (void)snprintf(in, sizeof in, F("%s" S("RTS") S("NOP")), words[i].body);
        (void)snprintf(want, sizeof want, "opcode %s 2; abi-scope",
                       words[i].rule);
        for (unsigned modes = OBJ; modes <= (OBJ | ASM); modes++) {
            CHECK(inspects(in, R10, modes, UNKNOWN, words[i].want));
            CHECK(inspects(in, R32, modes, UNKNOWN, want));
        }
        CHECK(inspects(in, "5.0R26", OBJ, UNKNOWN,
                       i == 1 ? "opcode rotcr 2; rule-scope 2" : want));
        CHECK(inspects(in, "5.0R26", ASM, UNKNOWN, "none; rule-scope 2"));
        CHECK(inspects(in, "5.1R13", OBJ | ASM, UNKNOWN, "none; rule-scope 2"));
    }

    // R10's and R32's cache-line opcodes and MOVCA.L, in both modes, where
    // the stack rule holds for R10 alone
    for (size_t i = 0; i < 2; i++) {
        const char *release = i == 0 ? R10 : R32;
        char want[64];
        (void)snprintf(want, sizeof want, "opcode cache-line 2%s",
                       i == 0 ? "" : "; abi-scope");
        CHECK(inspects(F(S("OCBI        @R4") S("RTS") S("NOP")), release,
                       OBJ | ASM, UNKNOWN, want));
        CHECK(inspects(F(S("OCBP        @R4") S("RTS") S("NOP")), release, OBJ,
                       UNKNOWN, want));
        CHECK(inspects(F(S("OCBWB       @R4") S("RTS") S("NOP")), release, ASM,
                       UNKNOWN, want));
        (void)snprintf(want, sizeof want, "opcode movca 3%s",
                       i == 0 ? "" : "; abi-scope");
        CHECK(inspects(
            F(S("MOV         #0,R0") S("MOVCA.L     R0,@R4") S("RTS") S("NOP")),
            release, OBJ | ASM, UNKNOWN, want));
    }

    // SETT, which neither R10 nor R32 writes, in both modes and any case,
    // before an ADDC, in a slot and where control never goes, but not as
    // its word in data or in a comment, and rule-scope under a release the
    // reading doesn't cover
    static const struct {
        const char *body;
        const char *want;
    } sets[] = {
        {S("SETT") S("EXTU.B      R2,R2") S("EXTU.B      R3,R3")
             S("ADDC        R3,R2") S("SHAR        R2") S("RTS")
                 S("MOV.B       R2,@R4"),
         "opcode sett 2"},
        {S("sett") S("RTS") S("NOP"), "opcode sett 2"},
        {S("RTS") S("SETT"), "opcode sett 3"},
        {S("RTS") S("NOP") S("SETT"), "opcode sett 4"},
        {S("SETT") S("RTS") S("ADD         #8,R15"), "opcode sett 2"},
    };
    for (size_t i = 0; i < sizeof sets / sizeof sets[0]; i++) {
        char want[64];
        (void)snprintf(in, sizeof in, F("%s"), sets[i].body);
        (void)snprintf(want, sizeof want, "%s; abi-scope", sets[i].want);
        CHECK(inspects(in, R10, OBJ, UNKNOWN, sets[i].want));
        CHECK(inspects(in, R10, ASM, UNKNOWN, sets[i].want));
        CHECK(inspects(in, R10, OBJ | ASM, UNKNOWN, sets[i].want));
        CHECK(inspects(in, R32, OBJ | ASM, UNKNOWN, want));
    }
    CHECK(INSPECTS(S("RTS") S("NOP") "L1:\n" S(".DATA.W     H'0018") "; SETT\n",
                   "none"));
    CHECK(inspects(F(S("SETT") S("RTS") S("NOP")), "5.0R31", OBJ | ASM, UNKNOWN,
                   "none; rule-scope 2"));
    CHECK(inspects(F(S("SETT") S("RTS") S("NOP")), "6.0C.001", OBJ, UNKNOWN,
                   "none; rule-scope 2"));

    // FSCHG, which R32 has neither a builtin nor an encoder form for, and
    // R10 writes for its builtins
    CHECK(inspects(F(S("FSCHG") S("RTS") S("NOP")), R32, OBJ | ASM, UNKNOWN,
                   "opcode fschg 2; abi-scope"));
    CHECK(inspects(F(S("fschg") S("RTS") S("NOP")), R32, ASM, UNKNOWN,
                   "opcode fschg 2; abi-scope"));
    CHECK(inspects(F(S("FSCHG") S("RTS") S("NOP")), R10, OBJ | ASM, UNKNOWN,
                   "none"));
    CHECK(inspects(F(S("FSCHG") S("RTS") S("NOP")), "5.0R31", OBJ, UNKNOWN,
                   "none; rule-scope 2"));
    CHECK(inspects(F(S("FRCHG") S("RTS") S("NOP")), R32, OBJ | ASM, UNKNOWN,
                   "none; abi-scope"));
    CHECK(inspects(F(S("OCBI        @R4") S("RTS") S("NOP")), "5.0R31", OBJ,
                   UNKNOWN, "none; rule-scope 2"));
    CHECK(inspects(F(S("MOVCA.L     R0,@R4") S("RTS") S("NOP")), "5.1R13", OBJ,
                   UNKNOWN, "none; rule-scope 2"));
    CHECK(inspects(F(S("ROTCL       R4") S("RTS") S("NOP")), "6.0C.001", OBJ,
                   UNKNOWN, "opcode rotcl 2; abi-scope"));
    CHECK(inspects(F(S("DMULS.L     R4,R5") S("RTS") S("NOP")), "6.0C.001", OBJ,
                   UNKNOWN, "none; rule-scope 2"));
    CHECK(inspects(F(S("ROTCL       R4") S("RTS") S("NOP")), "6.0AD", OBJ,
                   UNKNOWN, "none; producer-scope"));
    CHECK(inspects(F(S("OCBI        @R4") S("RTS") S("NOP")), "gcc", OBJ,
                   UNKNOWN, "none; producer-scope"));

    // R10's fixed operands, and R32's FIPR's and FTRV's, in any case. The
    // operands R32 writes itself are no witness.
    CHECK(INSPECTS(S("fipr.s      fv4,fv4") S("RTS") S("NOP"),
                   "operand fipr-registers 2"));
    CHECK(inspects(F(S("FIPR        FV8,FV4") S("RTS") S("NOP")), R10, OBJ,
                   UNKNOWN, "operand fipr-registers 2"));
    CHECK(INSPECTS(S("FTRV        XMTRX,FV8") S("RTS") S("NOP"),
                   "operand ftrv-registers 2"));
    CHECK(inspects(F(S("FSCA        FPUL,DR4") S("RTS") S("NOP")), R10,
                   OBJ | ASM, UNKNOWN, "operand fsca-registers 2"));
    CHECK(inspects(F(S("FIPR        FV4,FV4") S("RTS") S("NOP")), R32,
                   OBJ | ASM, UNKNOWN, "operand fipr-registers 2; abi-scope"));
    CHECK(inspects(F(S("FTRV        XMTRX,FV4") S("RTS") S("NOP")), R32,
                   OBJ | ASM, UNKNOWN, "operand ftrv-registers 2; abi-scope"));
    CHECK(inspects(F(S("FIPR        FV4,FV0") S("RTS") S("NOP")), R32,
                   OBJ | ASM, UNKNOWN, "none; abi-scope"));
    CHECK(inspects(F(S("FTRV        XMTRX,FV0") S("RTS") S("NOP")), R32,
                   OBJ | ASM, UNKNOWN, "none; abi-scope"));
    CHECK(inspects(F(S("FSCA        FPUL,DR4") S("RTS") S("NOP")), R32, ASM,
                   UNKNOWN, "none; rule-scope 2"));

    // what SHC writes, for a builtin or otherwise, and what no reading
    // excludes
    CHECK(INSPECTS(
        S("FLDI0       FR0") S("FLDI1       FR1") S("FLDI0       FR2")
            S("FLDI0       FR3") S("FIPR        FV4,FV0")
                S("FTRV        XMTRX,FV0") S("FSCA        FPUL,DR0")
                    S("FSRRA       FR0") S("PREF        @R4")
                        S("LDC         R4,SR") S("STC         SR,R0") S("FRCHG")
                            S("FRCHG") S("FSCHG") S("FSCHG") S("RTS") S("NOP"),
        "none"));
    CHECK(inspects(F(S("CLRMAC") S("SUBC        R5,R4") S("DT          R6")
                         S("NEGC        R4,R0") S("CLRT") S("RTS") S("NOP")),
                   R10, OBJ | ASM, UNKNOWN, "none"));

    // an opcode outranks operands and the ABI, whatever comes first, and a
    // limit on the stack leaves an instruction's witness standing
    CHECK(INSPECTS(S("FTRV        XMTRX,FV8") S("OCBI        @R4") S("RTS")
                       S("ADD         #8,R15"),
                   "opcode cache-line 3"));
    CHECK(INSPECTS(S("MOV.L       R1,@(0,R4)") S("OCBI        @R4") S("RTS")
                       S("NOP"),
                   "opcode cache-line 3"));
    CHECK(INSPECTS(S("OCBI        @R4") S("BSR         _g") S("NOP") S("RTS")
                       S("ADD         #8,R15"),
                   "opcode cache-line 2; call 3"));

    // data and comments hold no instruction, and an instruction rule reads
    // no instruction in a data section
    CHECK(INSPECTS(S("RTS") S("NOP") "L1:\n" S(
                       ".DATA.W     H'4024") "; ROTCL R0 and FIPR FV8,FV8\n",
                   "none"));
    CHECK(INSPECTS(S("RTS") S("NOP") DATA S("OCBI        @R4"), "none"));

    // of two limits at one line, continued before rule-scope
    CHECK(inspects(F(S("SWAP.B      R4,") "+           R5\n" S("RTS") S("NOP")),
                   "5.0R26", ASM, UNKNOWN, "none; continued 2"));
}

static void test_inspect_operands(void)
{
    // a MOV.L of a constant from -128 to 127, under R10 in both modes: an
    // expansion in any of asmsh's radixes, or a pool item through any label,
    // plain or by .EQU, each word taken mod 2^32
    static const struct {
        const char *operand;
        long value;
    } small[] = {
        {"#0", 0},           {"#-128", -128},       {"#127", 127},
        {"#H'7F", 127},      {"#H'FFFFFF80", -128}, {"#-H'80", -128},
        {"#-H'FFFFFFFF", 1}, {"#-H'FFFFFF81", 127}, {"#ZERO", 0},
        {"L1", 0},           {"L1+4", -1},          {"L1+8", 0},
        {"L1+20", -1},       {"L2", 127},           {"#b'1111111", 127},
        {"#Q'177", 127},     {"#-D'128", -128},     {"#012", 12},
    };
    static const char *const large[] = {
        "#128",    "#-129",   "#H'80", "#H'FFFFFF7F", "#-H'FFFFFF7F", "#BIG",
        "#ZERO+0", "#_x",     "L1+12", "L3",          "#B'10000000",  "#Q'200",
        "#D'128",  "#-D'129", "#h'80", "#1+0",
    };
    static const char pools[] = S("RTS") S("NOP")
        S(".ALIGN      4") "L1:       .DATA.L     0,H'FFFFFFFF\n" S(
            ".DATA.L     ZERO")
            S(".DATA.L     BIG") "; a pad, then a word\n" S(".DATA.W     1") S(
                ".RES.W      1")
                S(".DATA.L     -1") "L2:       .DATA.L     127\n"
                                    "L3:       .DATA.L     _x\n"
                                    "L4:       .DATA.W     0\n"
                                    "L7:       .SDATA      \"abcd\"\n"
                                    "L5:\n" S(
                                        "NOP") "ZERO:     .EQU        0\n"
                                               "BIG:      .EQU        H'100\n";
    char in[1024];
    for (size_t i = 0; i < sizeof small / sizeof small[0]; i++) {
        char want[64];
        (void)snprintf(in, sizeof in, F(S("MOV.L       %s,R4") "%s"),
                       small[i].operand, pools);
        (void)snprintf(want, sizeof want, "operand small-constant 2 #%ld",
                       small[i].value);
        CHECK(inspects(in, R10, OBJ | ASM, UNKNOWN, want));
        CHECK(inspects(in, R32, OBJ, UNKNOWN, "none; rule-scope 2"));
    }
    for (size_t i = 0; i < sizeof large / sizeof large[0]; i++) {
        (void)snprintf(in, sizeof in, F(S("MOV.L       %s,R4") "%s"), large[i],
                       pools);
        CHECK(inspects(in, R10, OBJ, UNKNOWN, "none"));
    }

    // a word the walk can't read: inside a long, a word, a pad, a string or
    // code, through a label with no definition or an expression, or at a
    // displacement from PC
    static const char *const unread[] = {
        "L1+2", "L1+16", "L1+18", "L4", "L7", "L5", "L6", "L1-4", "@(4,PC)",
    };
    for (size_t i = 0; i < sizeof unread / sizeof unread[0]; i++) {
        (void)snprintf(in, sizeof in, F(S("MOV.L       %s,R4") "%s"), unread[i],
                       pools);
        CHECK(inspects(in, R10, OBJ, UNKNOWN, "none; pool-word 2"));
        CHECK(inspects(in, R32, OBJ, UNKNOWN, "none; rule-scope 2"));
    }

    // a MOV with no size loads a pool's long, as MOV.L does, and #x as the
    // 8-bit immediate
    (void)snprintf(in, sizeof in, F(S("MOV         L2,R4") "%s"), pools);
    CHECK(inspects(in, R10, OBJ, UNKNOWN, "operand small-constant 2 #127"));
    (void)snprintf(in, sizeof in, F(S("MOV         #0,R4") "%s"), pools);
    CHECK(inspects(in, R10, OBJ, UNKNOWN, "none"));

    // what SHC writes for a small constant, and an .EQU defined twice, which
    // asmsh reads as its first
    CHECK(inspects(F(S("MOV         #0,R4") S("MOV.W       L1,R5") S("RTS")
                         S("NOP") "L1:       .DATA.W     0\n"),
                   R10, OBJ, UNKNOWN, "none"));
    CHECK(inspects(
        F(S("MOV.L       #ZERO,R4") S("RTS") S("NOP")) "ZERO: .EQU 0\n"
                                                       "ZERO: .EQU 200\n",
        R10, OBJ, UNKNOWN, "operand small-constant 2 #0"));
    CHECK(inspects(
        F(S("MOV.L       #ZERO,R4") S("RTS") S("NOP")) "ZERO: .EQU 200\n"
                                                       "ZERO: .EQU 0\n",
        R10, OBJ, UNKNOWN, "none"));

    // a MOV through @(0,Rn), under R10 and R32 in both modes
    static const char *const zero[] = {
        "R1,@(0,R4)",   "@(0,R4),R1",      "R1,@(ZERO,R4)",
        "@(H'0,SP),R1", "@( -0 , R4 ),R1", "@(-H'0,R4),R1",
        "@(B'0,R4),R1", "@(Q'00,R4),R1",   "@(d'0,R4),R1",
    };
    static const char *const other[] = {
        "R5,@(4,R4)",
        "R5,@R4",
        "@(ZERO+0,R4),R1",
        "@(BIG,R4),R1",
    };
    for (size_t i = 0; i < sizeof zero / sizeof zero[0]; i++) {
        (void)snprintf(
            in, sizeof in,
            F(S("MOV.L       %s") S("RTS") S("NOP")) "ZERO: .EQU 0\n", zero[i]);
        CHECK(inspects(in, R10, OBJ | ASM, UNKNOWN,
                       "operand zero-displacement 2"));
        CHECK(inspects(in, R32, OBJ | ASM, UNKNOWN,
                       "operand zero-displacement 2; abi-scope"));
        CHECK(inspects(in, "5.1R13", OBJ, UNKNOWN, "none; rule-scope 2"));
    }
    for (size_t i = 0; i < sizeof other / sizeof other[0]; i++) {
        (void)snprintf(
            in, sizeof in,
            F(S("MOV.L       %s") S("RTS") S("NOP")) "BIG: .EQU 4\n"
                                                     "ZERO: .EQU 0\n",
            other[i]);
        CHECK(inspects(in, R10, OBJ | ASM, UNKNOWN, "none"));
    }
    CHECK(INSPECTS(S("MOV.W       @(0,R4),R0") S("MOV.B       R0,@(0,R5)")
                       S("RTS") S("NOP"),
                   "operand zero-displacement 2"));
    CHECK(INSPECTS(S("MOV         @(0,R4),R1") S("RTS") S("NOP"),
                   "operand zero-displacement 2"));
    CHECK(INSPECTS(S("MOV.L       @(Z,R4),R1") S("RTS") S("NOP") "Z: .EQU 0\n"
                                                                 "Z: .EQU 4\n",
                   "operand zero-displacement 2"));
    CHECK(INSPECTS(S("MOV.L       @(Z,R4),R1") S("RTS") S("NOP") "Z: .EQU 4\n"
                                                                 "Z: .EQU 0\n",
                   "none"));
    CHECK(INSPECTS(S("MOV.B       @(0,GBR),R0") S("RTS") S("NOP"), "none"));

    // a name .ASSIGN defines takes, at each use, the value of the last
    // .ASSIGN above it, in any section, and 0 above the first. asmsh
    // ignores a later definition of another kind, and an .ASSIGN of a name
    // another kind defines first. An expression is no constant.
    static const struct {
        const char *body;
        const char *want;
    } assigned[] = {
        {"Z:        .ASSIGN     4\n"
         "Z:        .ASSIGN     0\n" S("MOV.L       @(Z,R4),R1"),
         "operand zero-displacement 4"},
        {"Z:        .ASSIGN     0\n" S(
             "MOV.L       @(Z,R4),R1") "Z:        .ASSIGN     4\n",
         "operand zero-displacement 3"},
        {S("MOV.L       @(Z,R4),R1") "Z:        .ASSIGN     4\n",
         "operand zero-displacement 2"},
        {"Z:        .ASSIGN     4\n" DATA
         "Z:        .ASSIGN     0\n" CODE S("MOV.L       @(Z,R4),R1"),
         "operand zero-displacement 6; section-change 3"},
        {"Z:        .ASSIGN     4\n"
         "Z:        .ASSIGN     0\n"
         "Z:\n"
         "Z:        .EQU        4\n" S("MOV.L       @(Z,R4),R1"),
         "operand zero-displacement 6"},
        {"Z:        .ASSIGN     0\n"
         "Z:        .ASSIGN     4\n" S("MOV.L       @(Z,R4),R1"),
         "none"},
        {"Z:        .EQU        4\n"
         "Z:        .ASSIGN     0\n" S("MOV.L       @(Z,R4),R1"),
         "none"},
        {"Z:        .ASSIGN     1-1\n" S("MOV.L       @(Z,R4),R1"), "none"},
        {"Z:        .ASSIGN     0\n" S("MOV.L       L1,R4") S("RTS")
             S("NOP") "L1:       .DATA.L     Z\n" S(
                 "NOP") "Z:        .ASSIGN     200\n",
         "operand small-constant 3 #0"},
        {S("MOV.L       L1,R4") S("RTS") S("NOP") "L1:       .DATA.L     Z\n" S(
             "NOP") "Z:        .ASSIGN     200\n",
         "operand small-constant 2 #0"},
        {"Z:        .ASSIGN     0\n"
         "Z:        .ASSIGN     200\n" S("MOV.L       L1,R4") S("RTS")
             S("NOP") "L1:       .DATA.L     Z\n",
         "none"},

        // asmsh takes #x's value at the load where an .ASSIGN above it
        // defines x, whatever an .ASSIGN below gives it before the pool.
        // Above the first, it takes the value x has where it writes the
        // pool, which inspect doesn't place, so x is no constant there:
        // asmsh writes 7 here.
        {"Z:        .ASSIGN     0\n" S("MOV.L       #Z,R4"),
         "operand small-constant 3 #0"},
        {"Z:        .ASSIGN     0\n" S(
             "MOV.L       #Z,R4") "Z:        .ASSIGN     200\n",
         "operand small-constant 3 #0"},
        {"Z: .ASSIGN 200\n" S("MOV.L       #Z,R4") "Z: .ASSIGN 5\n" S(
             "MOV.L       #Z,R5"),
         "operand small-constant 5 #5"},
        {S("MOV.L       #Z,R4") "Z:        .ASSIGN     7\n", "none"},
    };
    for (size_t i = 0; i < sizeof assigned / sizeof assigned[0]; i++) {
        (void)snprintf(in, sizeof in, F("%s" S("RTS") S("NOP")),
                       assigned[i].body);
        CHECK(inspects(in, R10, OBJ | ASM, UNKNOWN, assigned[i].want));
    }
}

static void test_inspect_labels(void)
{
    // a ?label read in its block, as asmsh reads it: from a .SECTION line,
    // or from the first definition of a name other than a ?label, alone or
    // on an instruction or data, up to the next such line. A name defined
    // more than once there is its first definition.
    static const char twice[] =
        "_f:\n" S("NOP") S("MOV.L       ?k,R4") "?top:\n" S("DT          R4")
            S("BF          ?top") S("RTS")
                S("NOP") "?k:\n" S(".DATA.L     0") "_g:\n" S(
                    "MOV.L       ?k,R4") "?top:\n" S("DT          R4")
                    S("BF          ?top") S("RTS")
                        S("NOP") "?k:\n" S(".DATA.L     H'1000");
    CHECK(inspects_lines(twice, 0, 11, R10, OBJ, UNKNOWN,
                         "operand small-constant 3 #0"));
    CHECK(inspects_lines(twice, 11, 0, R10, OBJ, UNKNOWN, "none"));
    CHECK(inspects_lines(twice, 11, 0, R10, ASM, UNKNOWN, "none"));
    CHECK(inspects(F(S("MOV.L       ?k,R4") S("RTS") S("NOP") "?k:\n" S(
                       ".DATA.L     0") "X:        .EQU        1\n"
                                        "?k:\n" S(".DATA.L     H'1000")),
                   R10, OBJ, UNKNOWN, "operand small-constant 2 #0"));
    CHECK(inspects(
        F(S("MOV.L       ?k,R4") S("RTS") S("NOP") "?k:\n" S(".DATA.L     0")
              S(".SECTION    Q,CODE,ALIGN=4") "?k:\n" S(".DATA.L     0")),
        R10, OBJ, UNKNOWN, "operand small-constant 2 #0"));
    CHECK(inspects(F(S("BRA         ?top") S("NOP") "?top:\n" S("RTS")
                         S("ADD         #8,R15") "_g:\n"
                                                 "?top:\n" S("NOP")),
                   R10, ASM, SHCKIT_SZ_0, "abi stack-above-entry 6 +8"));
    CHECK(
        inspects(F(S("BRA         ?top") S("NOP") "_g:\n"
                                                  "?top:\n" S("RTS") S("NOP")),
                 R10, ASM, SHCKIT_SZ_0, "none; branch-target 2"));
    CHECK(INSPECTS(S("BRA         ?a") S("NOP") "?a:\n" S("RTS")
                       S("ADD         #8,R15") "?a:\n" S("RTS") S("NOP"),
                   "abi stack-above-entry 6 +8"));
    CHECK(INSPECTS(S("BRA         ?a") S("NOP") "?a:\n" S("RTS")
                       S("NOP") "?a:\n" S("RTS") S("ADD         #8,R15"),
                   "none"));

    // a line between two ?a definitions, above a branch to ?a: one that
    // opens a block, and one that doesn't, a label on another directive, a
    // ?label and a name defined again among them
    static const char *const opens[] = {
        "_x:\n",
        "_x:       NOP\n",
        "_x:       .DATA.L     1\n",
        "_x:       .RES.W      2\n",
        "_x:       .SDATA      \"ab\"\n",
        "          .SECTION    P,CODE,ALIGN=4\n",
        "_x:       .SECTION    P,CODE,ALIGN=4\n",
    };
    static const char *const stays[] = {
        "_x:       .POOL\n",
        "_x:       .ALIGN      4\n",
        "_x:       .EXPORT     _f\n",
        "_x:       .IMPORT     _g\n",
        "_x:       .NOPOOL\n",
        "_x:       .EQU        1\n",
        "_x:       .ASSIGN     1\n",
        "?x:\n",
        "_f:\n",
    };
    static const char between[] =
        "_f:\n?a:\n" S("RTS") S("NOP") "%s" S("BRA         ?a")
            S("NOP") "?a:\n" S("RTS") S("ADD         #8,R15");
    char in[512];
    for (size_t i = 0; i < sizeof opens / sizeof opens[0]; i++) {
        (void)snprintf(in, sizeof in, between, opens[i]);
        CHECK(inspects_lines(in, 5, 0, R10, ASM, UNKNOWN,
                             "abi stack-above-entry 10 +8"));
    }
    // without one, the branch's ?a is the one above, out of the function
    for (size_t i = 0; i < sizeof stays / sizeof stays[0]; i++) {
        (void)snprintf(in, sizeof in, between, stays[i]);
        CHECK(inspects_lines(in, 5, 0, R10, ASM, UNKNOWN, "none"));
    }
    // a label on .ALIGN defines nothing, so the _x below it is the first
    (void)snprintf(in, sizeof in, between, "_x:       .ALIGN      4\n_x:\n");
    CHECK(inspects_lines(in, 6, 0, R10, ASM, UNKNOWN,
                         "abi stack-above-entry 11 +8"));

    // no definition: a label on a directive but data, .POOL, .EQU and
    // .ASSIGN, a ?label on .EQU or .ASSIGN, and a name asmsh refuses. It
    // takes $ in a name.
    CHECK(INSPECTS(S("BRA         _t") S("NOP") "_t:       .EXPORT     _f\n" S(
                       "RTS") S("ADD         #8,R15"),
                   "none; branch-target 2"));
    CHECK(INSPECTS(S("MOV.L       #?z,R4") S("RTS")
                       S("NOP") "?z:       .EQU        0\n",
                   "none"));
    CHECK(INSPECTS("?z:       .ASSIGN     0\n" S("MOV.L       #?z,R4") S("RTS")
                       S("NOP"),
                   "none"));

    // a branch to a name .ASSIGN defines goes to its value, not its line,
    // and control passes the line
    CHECK(INSPECTS(S("BRA         _t") S("NOP") "_t:       .ASSIGN     4\n" S(
                       "RTS") S("ADD         #8,R15"),
                   "none; branch-target 2"));
    CHECK(INSPECTS("_t:       .ASSIGN     4\n" S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 4 +8"));
    CHECK(INSPECTS(S("BRA         ?$a") S("NOP") "?$a:\n" S("RTS")
                       S("ADD         #8,R15"),
                   "abi stack-above-entry 6 +8"));
    static const char *const refused[] = {
        "?abcdefghijklmnop", "?", "?a?b", "_a?b", "_a.b",
    };
    static const char *const taken[] = {
        "?abcdefghijklmno",
        "?$a",
        "_a$b",
        "$x",
    };
    static const char word[] = F(S("MOV.L       %s,R4") S("RTS") S("NOP") S(
        ".ALIGN      4") "%s:\n" S(".DATA.L     0"));
    for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++) {
        (void)snprintf(in, sizeof in, word, refused[i], refused[i]);
        CHECK(inspects(in, R10, OBJ, UNKNOWN, "none; pool-word 2"));
    }
    for (size_t i = 0; i < sizeof taken / sizeof taken[0]; i++) {
        (void)snprintf(in, sizeof in, word, taken[i], taken[i]);
        CHECK(inspects(in, R10, OBJ, UNKNOWN, "operand small-constant 2 #0"));
    }

    // a small pool word whose run of data the text names may be a variable:
    // a label of the run, up to its last data line, that .EXPORT, MOVA, a
    // .DATA item or #label names. A run holds any line but an instruction,
    // .SECTION and .END, and a repeated name names its first definition.
    static const struct {
        const char *before;
        const char *pool;
        const char *after;
        const char *want;
    } words[] = {
        {"", "?k:       .DATA.L     0\n", "", "operand small-constant 2 #0"},
        {S(".EXPORT     _f"), "?k:       .DATA.L     0\n", "",
         "operand small-constant 3 #0"},
        {S(".EXPORT     _v"),
         "?k:       .DATA.L     0\n_v:       .DATA.L     1\n", "",
         "none; pool-word 3"},
        {"", "?k:       .DATA.L     0\n", S("MOVA        ?k,R0"),
         "none; pool-word 2"},
        {"", "?k:       .DATA.L     0\n", S("MOV.L       #?k,R5"),
         "none; pool-word 2"},
        {"", "?k:       .DATA.L     0\n", S(".DATA.L     ?k"),
         "none; pool-word 2"},
        {"", "?j:       .DATA.L     H'1000\n?k:       .DATA.L     0\n",
         S("MOVA        ?j,R0"), "none; pool-word 2"},
        {"", "?k:       .DATA.L     0\n_h:\n", S("MOVA        _h,R0"),
         "operand small-constant 2 #0"},
        {"", "?k:       .DATA.L     0\n",
         S("MOV.W       ?k,R5") S("MOV.L       ?k,R6"),
         "operand small-constant 2 #0"},
        {"", "?k:       .DATA.L     0\n",
         "_g:\n" S("MOVA        ?k,R0") S("RTS")
             S("NOP") "?k:       .DATA.L     1\n",
         "operand small-constant 2 #0"},
        {"",
         "?k:       .DATA.L     0\n" S(
             ".EXPORT     _v") "_v:       .DATA.L     1\n",
         "", "none; pool-word 2"},
        {S(".EXPORT     _v"),
         "?k:       .DATA.L     0\n_n:       .EQU        4\n"
         "_v:       .DATA.L     1\n",
         "", "none; pool-word 3"},
        {S(".EXPORT     _v"),
         "?k:       .DATA.L     0\n" S(
             ".DATA.L     1,") "+         2\n"
                               "_v:       .DATA.L     3\n",
         "", "none; pool-word 3"},
        {S(".EXPORT     _v"),
         "?k:       .DATA.L     0\n" S(".POOL") "_v:       .DATA.L     1\n", "",
         "none; pool-word 3"},
        {S(".EXPORT     _v"),
         "?k:       .DATA.L     0\n" S("RTS")
             S("NOP") "_v:       .DATA.L     1\n",
         "", "operand small-constant 3 #0"},
        {"", "?k:\n?v:       .DATA.L     0\n",
         S(".DATA.L     ?v") S("RTS") S("NOP") "?v:       .DATA.L     2\n",
         "none; pool-word 2"},
    };
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        (void)snprintf(in, sizeof in,
                       "%s_f:\n" S("MOV.L       ?k,R4") S("RTS") S("NOP")
                           S(".ALIGN      4") "%s%s",
                       words[i].before, words[i].pool, words[i].after);
        CHECK(inspects_lines(in, words[i].before[0] != '\0' ? 1 : 0, 0, R10,
                             OBJ, UNKNOWN, words[i].want));
    }
    CHECK(INSPECTS(
        S("MOV.L       ?k,R4") S("RTS") S("NOP") S(
            ".ALIGN      4") "?v:       .DATA.L     1\n" S("RTS") S("NOP")
            S(".ALIGN      4") "?k:\n"
                               "?v:       .DATA.L     0\n" S(".DATA.L     ?v"),
        "operand small-constant 2 #0"));

    // a variable's word loaded into R15 hides no stack-written witness
    CHECK(inspects(F(S("MOV.L       _v,R15") S("RTS") S("NOP") "_v:\n" S(
                       ".DATA.L     0") S(".EXPORT     _v")),
                   R10, OBJ, UNKNOWN, "abi stack-written 2; pool-word 2"));

    // thousands of small words in one run, which a scan of the text for
    // each word's labels would take hours over
    enum { WORDS = 2000 };
    size_t size = (WORDS * 64) + 64;
    char *many = malloc(size);
    size_t len = 0;
    CHECK(many != NULL);
    if (many != NULL) {
        len += (size_t)snprintf(many + len, size - len, "_f:\n");
        for (size_t i = 0; i < WORDS; i++) {
            len += (size_t)snprintf(many + len, size - len,
                                    S("MOV.L       ?k%zu,R4"), i);
        }
        len += (size_t)snprintf(many + len, size - len,
                                S("RTS") S("NOP") S(".ALIGN      4"));
        for (size_t i = 0; i < WORDS; i++) {
            len += (size_t)snprintf(many + len, size - len,
                                    "?k%zu:\n" S(".DATA.L     0"), i);
        }
        CHECK(inspects(many, R10, OBJ, UNKNOWN, "operand small-constant 2 #0"));
        free(many);
    }
}

static void test_inspect_stack_written(void)
{
    // a write to R15 other than a push's or pop's address, an ADD or SUB
    // and MOV R14,R15, reached or not
    static const char *const written[] = {
        "MOV         R4,R15",       "MOV.L       @R4,R15",
        "MOV.L       @R15,R15",     "MOV.L       @R15+,R15",
        "MOV.L       @(4,R15),R15", "MOV.L       L1,R15",
        "MOV.L       #_stack,R15",  "MOV         #0,R15",
        "STS         MACL,R15",     "STC         GBR,SP",
        "MOVT        R15",          "DT          R15",
        "SHLL2       R15",          "NEG         R4,R15",
        "ADDC        R4,R15",       "XTRCT       R4,R15",
    };
    static const char *const kept[] = {
        "ADD         #-8,R15",   "ADD         R4,R15",
        "SUB         R4,R15",    "MOV         R14,R15",
        "MOV.L       R8,@-R15",  "MOV.L       @R15+,R8",
        "STS.L       PR,@-R15",  "LDC.L       @R15+,GBR",
        "FMOV.S      FR4,@-R15", "MOV         R15,R14",
        "MOV.L       R15,@-R4",  "CMP/EQ      R4,R15",
    };
    char in[512];
    for (size_t i = 0; i < sizeof written / sizeof written[0]; i++) {
        (void)snprintf(in, sizeof in,
                       F(S("RTS") S("NOP") "_dead:\n" S("%s") S("RTS")
                             S("NOP") "L1:\n" S(".DATA.L     _stack")),
                       written[i]);
        CHECK(inspects(in, R10, OBJ | ASM, UNKNOWN, "abi stack-written 5"));
        CHECK(inspects(in, R32, ASM, SHCKIT_SZ_0,
                       "abi stack-written 5; abi-scope"));
        CHECK(inspects(in, "5.0R26", OBJ, UNKNOWN, "none; rule-scope 5"));
    }
    for (size_t i = 0; i < sizeof kept / sizeof kept[0]; i++) {
        (void)snprintf(
            in, sizeof in,
            F(S("RTS") S("NOP") "_dead:\n" S("%s") S("RTS") S("NOP")), kept[i]);
        CHECK(inspects(in, R10, OBJ, SHCKIT_SZ_0, "none"));
    }

    // SHC's stack switch for #pragma interrupt(f(sp=x)): each load of R0
    // that SHC writes for x, a switch back that RTE or TRAPA follows, and
    // lines control passes between them, with MOV's size or without it
    static const char *const loads[] = {
        S("MOV.L       L1,R0"),
        S("MOV.L       L1+4,R0") S("MOV.L       @R0,R0"),
        S("MOV.W       L1,R0"),
        S("MOV.W       L1,R0") S("MOV.L       @R0,R0"),
        S("MOV         #64,R0"),
        S("MOV.L       #H'8C100000,R0"),
        S("MOV.W       @(4,PC),R0"),
        S("STC         GBR,R0") S("ADD         #_v-(STARTOF $G0),R0"),
        S("MOV.L       @(_p-(STARTOF $G0),GBR),R0"),
    };
    static const char tail[] =
        S("MOV.L       @R15+,R15") S("MOV.L       @R15+,R0");
    static const char stack[] =
        "L1:       .DATA.L     H'8C100000\n" S(".DATA.L     _stack");
    for (size_t i = 0; i < sizeof loads / sizeof loads[0]; i++) {
        char want[64];
        size_t lines = 0;
        for (const char *at = loads[i]; *at != '\0'; at++) {
            lines += *at == '\n' ? 1 : 0;
        }
        (void)snprintf(
            in, sizeof in,
            F(S("MOV.L       R0,@-R15") "%s" S("MOV.L       R15,@-R0")
                  S("MOV         R0,R15") "%s" S("RTE") S("NOP") "%s"),
            loads[i], tail, stack);
        (void)snprintf(want, sizeof want, "none; stack-unknown %zu", lines + 4);
        CHECK(inspects(in, R10, OBJ | ASM, SHCKIT_SZ_0, want));
        CHECK(inspects(in, R32, OBJ | ASM, SHCKIT_SZ_0, "none; abi-scope"));
    }
    (void)snprintf(
        in, sizeof in,
        F(S("MOV.L       R0,@-R15")
              S("MOV.L       #H'8C100000,R0") "%s" S("MOV.L       R15,@-R0")
                  S("MOV         R0,R15") S("MOV.L       R1,@-R15")
                      S("MOV.L       @R15+,R1") "%s" S("TRAPA       #6")),
        "; the new stack\n", tail);
    CHECK(inspects(in, R10, ASM, UNKNOWN, "none; stack-unknown 6"));
    (void)snprintf(
        in, sizeof in,
        F(S("MOV.L       R0,@-R15")
              S("MOV.L       #H'8C100000,R0") "%s" S("MOV.L       R15,@-R0")
                  S("MOV         R0,R15") S("MOV.L       R1,@-R15")
                      S("MOV.L       @R15+,R1") "%s" S("TRAPA       #6")),
        "STACK:    .ASSIGN     H'8C100000\n", tail);
    CHECK(inspects(in, R10, ASM, UNKNOWN, "none; stack-unknown 6"));
    CHECK(inspects(
        F(S(".EXPORT     _f") "_g:\n" S("MOV         R0,@-R15")
              S("MOV         L1,R0") S("MOV         R15,@-R0")
                  S("MOV         R0,R15") S("MOV         @R15+,R15")
                      S(".EXPORT     _g") S("MOV         @R15+,R0") S("RTE")
                          S("NOP") "L1:\n" S(".DATA.L     H'8C100000")),
        R10, OBJ, UNKNOWN, "none; stack-unknown 7"));

    // a switch that isn't SHC's: not at the entry, loading R0 from a general
    // register, or switching back where RTE doesn't follow
    static const struct {
        const char *head;
        const char *tail;
        const char *want;
    } others[] = {
        {S("NOP") S("MOV.L       R0,@-R15") S("MOV.L       L1,R0"), tail,
         "abi stack-written 6; stack-unknown 6"},
        {S("MOV.L       R0,@-R15") S("MOV.L       @R0,R0"), tail,
         "abi stack-written 5; stack-unknown 5"},
        {S("MOV.L       R0,@-R15") S("ADD         #4,R0"), tail,
         "abi stack-written 5; stack-unknown 5"},
        {S("MOV.L       R0,@-R15") S("MOV         R4,R0"), tail,
         "abi stack-written 5; stack-unknown 5"},
        {S("MOV.L       R0,@-R15"), tail,
         "abi stack-written 4; stack-unknown 4"},
        {S("MOV.L       R0,@-R15") S("MOV.L       L1,R0"),
         S("MOV.L       @R15+,R15") S("NOP") S("MOV.L       @R15+,R0"),
         "abi stack-written 6; stack-unknown 5"},
        {S("MOV.L       R0,@-R15") S("MOV.L       L1,R0"),
         S("MOV.L       @R15+,R15") S("MOV.L       @R15+,R0") S("NOP"),
         "abi stack-written 6; stack-unknown 5"},
    };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        (void)snprintf(in, sizeof in,
                       F("%s" S("MOV.L       R15,@-R0") S(
                           "MOV         R0,R15") "%s" S("RTE") S("NOP") "%s"),
                       others[i].head, others[i].tail, stack);
        CHECK(inspects(in, R10, ASM, UNKNOWN, others[i].want));
    }
    (void)snprintf(in, sizeof in,
                   F(S("JSR         @R4") S("NOP")
                         S("MOV         R0,R15") "%s" S("RTE") S("NOP")),
                   tail);
    CHECK(inspects(in, R10, ASM, UNKNOWN, "abi stack-written 4; call 2"));
}

static void test_inspect_fpscr(void)
{
    // FPSCR's push or pop through R15 in a function that holds an RTS, a
    // witness at its line: each alone, in any case, without a size, through
    // SP, in a slot, where control never goes, past a call, and with the RTS
    // where control never goes. Under a release the reading doesn't cover,
    // rule-scope there.
    static const struct {
        const char *in;
        size_t line;
        const char *after;
    } saves[] = {
        {F(S("STS.L       FPSCR,@-R15") S("FMOV.S      @R4,FR2")
               S("FMUL        FR2,FR2") S("FMOV.S      FR2,@R4")
                   S("LDS.L       @R15+,FPSCR") S("RTS") S("NOP")),
         2, ""},
        {F(S("ADD         #-4,R15") S("LDS.L       @R15+,FPSCR") S("RTS")
               S("NOP")),
         3, ""},
        {F(S("STS.L       FPSCR,@-R15") S("RTS") S("ADD         #4,R15")), 2,
         ""},
        {F(S("sts.l       fpscr,@-sp") S("lds.l       @sp+,fpscr") S("rts")
               S("nop")),
         2, ""},
        {F(S("STS         FPSCR,@-R15") S("LDS         @R15+,FPSCR") S("RTS")
               S("NOP")),
         2, ""},
        {F(S("ADD         #-4,R15") S("RTS") S("LDS.L       @R15+,FPSCR")), 4,
         ""},
        {F(S("RTS") S("NOP") S("STS.L       FPSCR,@-R15")), 4, ""},
        {F(S("JSR         @R4") S("NOP") S("STS.L       FPSCR,@-R15")
               S("LDS.L       @R15+,FPSCR") S("RTS") S("NOP")),
         4, "; call 2"},
        {F(S("STS.L       FPSCR,@-R15") S("LDS.L       @R15+,FPSCR") S("RTE")
               S("NOP") S("RTS") S("NOP")),
         2, ""},
    };
    static const int szs[] = {UNKNOWN, SHCKIT_SZ_0, SHCKIT_SZ_1};
    char want[64];
    char scoped[64];
    for (size_t i = 0; i < sizeof saves / sizeof saves[0]; i++) {
        (void)snprintf(want, sizeof want, "abi fpscr-save %zu%s", saves[i].line,
                       saves[i].after);
        (void)snprintf(scoped, sizeof scoped, "none; rule-scope %zu",
                       saves[i].line);
        for (size_t j = 0; j < sizeof szs / sizeof szs[0]; j++) {
            CHECK(inspects(saves[i].in, R10, OBJ, szs[j], want));
            CHECK(inspects(saves[i].in, R10, ASM, szs[j], want));
            CHECK(inspects(saves[i].in, R10, OBJ | ASM, szs[j], want));
        }
        CHECK(inspects(saves[i].in, R32, OBJ | ASM, UNKNOWN, scoped));
        CHECK(inspects(saves[i].in, "5.1R13", ASM, UNKNOWN, scoped));
        CHECK(inspects(saves[i].in, "6.0C.001", OBJ, UNKNOWN, scoped));
    }

    // a pop above the entry, which stack-above-entry witnesses at the same
    // line, listed after it; an opcode outranks it
    CHECK(INSPECTS(S("FMOV.S      @R4,FR2") S("LDS.L       @R15+,FPSCR")
                       S("RTS") S("NOP"),
                   "abi fpscr-save 3"));
    CHECK(INSPECTS(S("STS.L       FPSCR,@-R15") S("LDS.L       @R15+,FPSCR")
                       S("RTS") S("SETT"),
                   "opcode sett 5"));

    // no witness: FPSCR read and written through a general register, as the
    // builtins write it, its copy stored to a frame slot, pushed as an
    // argument and moved through another pointer; FPSCR through another
    // register; FPUL, MACL and FR12 through R15; and the push and pop in a
    // function that returns by RTE, or ends in TRAPA
    static const struct {
        const char *in;
        const char *want;
    } others[] = {
        {F(S("STS         FPSCR,R3") S("MOV.L       L1,R2")
               S("AND         R3,R2") S("RTS") S(
                   "LDS         R2,FPSCR") "L1:\n" S(".DATA.L     H'FFEFFFFF")),
         "none"},
        {F(S("STS         FPSCR,R3") S("ADD         #-4,R15")
               S("MOV.L       R3,@R15") S("MOV.L       @R15,R1")
                   S("ADD         #4,R15") S("RTS") S("LDS         R1,FPSCR")),
         "none"},
        {F(S("STS         FPSCR,R3") S("MOV.L       R3,@-R15")
               S("MOV.L       @R15+,R3") S("RTS") S("LDS         R3,FPSCR")),
         "none"},
        {F(S("STS         FPSCR,R3") S("MOV.L       R3,@-R4")
               S("MOV.L       @R4+,R3") S("RTS") S("LDS         R3,FPSCR")),
         "none"},
        {F(S("STS.L       FPSCR,@-R4") S("LDS.L       @R4+,FPSCR") S("RTS")
               S("NOP")),
         "none"},
        {F(S("STS.L       FPUL,@-R15") S("STS.L       MACL,@-R15")
               S("FMOV.S      FR12,@-R15") S("FMOV.S      @R15+,FR12")
                   S("LDS.L       @R15+,MACL") S("LDS.L       @R15+,FPUL")
                       S("RTS") S("NOP")),
         "none"},
        {F(S("STS.L       FPSCR,@-R15") S("LDS.L       @R15+,FPSCR") S("RTE")
               S("NOP")),
         "none"},
        {F(S("STS.L       FPSCR,@-R15") S("LDS.L       @R15+,FPSCR")
               S("TRAPA       #5")),
         "none; opaque-transfer 4"},
    };
    for (size_t i = 0; i < sizeof others / sizeof others[0]; i++) {
        CHECK(inspects(others[i].in, R10, OBJ | ASM, SHCKIT_SZ_0,
                       others[i].want));
        CHECK(inspects(others[i].in, R10, OBJ | ASM, SHCKIT_SZ_1,
                       others[i].want));
    }
}

static void test_inspect_fpscr_compiled(void)
{
    // what R10 writes for C, cut down, under an SDK build's flags but the
    // fifth's, R10's for the SH-4 with -speed, which no rule may witness:
    // FPSCR kept across a call through the builtins, passed as a fifth
    // argument and stored and loaded through a pointer, in functions that
    // return by RTS; and the push and pop in a #pragma interrupt function,
    // which returns by RTE. The pop writes FPSCR, so the FR pops past it
    // are stack-unknown.
    static const struct {
        const char *in;
        const char *want;
    } compiled[] = {
        // void b_around(void)
        // { int s = _builtin_get_fpscr(); g(); _builtin_set_fpscr(s); }
        {F(S("STS.L       PR,@-R15") S("STS         FPSCR,R3")
               S("MOV.L       L1+2,R2") S("ADD         #-4,R15")
                   S("JSR         @R2") S("MOV.L       R3,@R15")
                       S("MOV.L       @R15,R1") S("ADD         #4,R15") S(
                           "LDS.L       @R15+,PR") S("RTS")
                           S("LDS         R1,FPSCR") "L1:\n" S(".RES.W      1")
                               S(".DATA.L     _g")),
         "none; call 6"},
        // int c_arg5(int a) { return k(a, a, a, a, _builtin_get_fpscr()); }
        {F(S("STS         FPSCR,R3") S("MOV.L       L1+2,R2")
               S("MOV.L       R14,@-R15") S("STS.L       PR,@-R15")
                   S("MOV         R4,R14") S("MOV.L       R3,@-R15")
                       S("MOV         R14,R7") S("MOV         R14,R6")
                           S("MOV         R14,R5") S("JSR         @R2") S(
                               "MOV         R14,R4") S("ADD         #4,R15")
                               S("LDS.L       @R15+,PR") S("RTS")
                                   S("MOV.L       @R15+,R14") "L1:\n" S(
                                       ".RES.W      1") S(".DATA.L     _k")),
         "none; call 11"},
        // int *pdec2(int *p) { *--p = _builtin_get_fpscr(); return p; }
        {F(S("STS         FPSCR,R3") S("MOV.L       R3,@-R4") S("RTS")
               S("MOV         R4,R0")),
         "none"},
        // void pinc2(int *p)
        // { _builtin_set_fpscr(*p++); _builtin_set_fpscr(*p++); }
        {F(S("MOV.L       @R4+,R3") S("LDS         R3,FPSCR")
               S("MOV.L       @R4,R2") S("RTS") S("LDS         R2,FPSCR")),
         "none"},
        // #pragma interrupt(i_flt) void i_flt(void) { vf = vf * 2.0f; }
        {F(S("MOV.L       R3,@-R15") S("MOV.L       R2,@-R15") S(
             "MOV.L       R0,@-R15") S("FMOV.S      FR3,@-R15")
               S("FMOV.S      FR2,@-R15") S("MOVA        L1,R0") S(
                   "STS.L       FPSCR,@-R15") S("FMOV.S      @R0,FR3")
                   S("MOV.L       L1+4,R3") S("FMOV.S      @R3,FR2") S(
                       "FMUL        FR3,FR2") S("MOV         R3,R2")
                       S("FMOV.S      FR2,@R2") S("LDS.L       @R15+,FPSCR") S(
                           "FMOV.S      @R15+,FR2") S("FMOV.S      @R15+,FR3")
                           S("MOV.L       @R15+,R0") S("MOV.L       @R15+,R2")
                               S("MOV.L       @R15+,R3") S("RTE")
                                   S("NOP") "L1:\n" S(".DATA.L     H'40000000")
                                       S(".DATA.L     _vf")),
         "none; stack-unknown 16"},
    };
    for (size_t i = 0; i < sizeof compiled / sizeof compiled[0]; i++) {
        CHECK(inspects(compiled[i].in, R10, OBJ | ASM, SHCKIT_SZ_0,
                       compiled[i].want));
        CHECK(inspects(compiled[i].in, R10, OBJ | ASM, SHCKIT_SZ_1,
                       compiled[i].want));
    }

    // #pragma interrupt(i_tn(tn=5)) void i_tn(void) { g(); }, cut down, and
    // an ordinary function below it: within the interrupt function's bounds,
    // no witness, but bounds that take in the RTS below find one
    static const char tn[] = CODE "_i_tn:\n" S("MOV.L       R0,@-R15")
        S("STS.L       PR,@-R15") S("STS.L       FPUL,@-R15")
            S("STS.L       FPSCR,@-R15") S("MOV.L       L1,R3")
                S("JSR         @R3") S("NOP") S("LDS.L       @R15+,FPSCR")
                    S("LDS.L       @R15+,FPUL") S("LDS.L       @R15+,PR")
                        S("MOV.L       @R15+,R0") S("TRAPA       #5") "L1:\n" S(
                            ".DATA.L     _g") "_f:\n" S("RTS") S("NOP");
    CHECK(inspects_lines(tn, 1, 16, R10, OBJ | ASM, UNKNOWN, "none; call 8"));
    CHECK(inspects_lines(tn, 1, 0, R10, OBJ | ASM, UNKNOWN,
                         "abi fpscr-save 6; call 8"));

    // FPSCR kept across a call as the text of a function written in
    // assembly keeps it, cut down: through R0, a push and a pop of the copy,
    // and LDS
    CHECK(INSPECTS(S("STS         FPSCR,R0") S("MOV.L       R0,@-R15")
                       S("LDS         R0,FPSCR") S("STS.L       PR,@-R15")
                           S("MOV         R4,R3") S("JSR         @R3") S(
                               "MOV         R5,R4") S("LDS.L       @R15+,PR")
                               S("MOV.L       @R15+,R0")
                                   S("LDS         R0,FPSCR") S("RTS") S("NOP"),
                   "none; call 7"));
}

static void test_inspect_stack(void)
{
    // R15 above its entry value after any step the stack rule reaches, and
    // below it once an RTS's slot has run: at the entry, above it and below
    // it
    CHECK(INSPECTS(S("RTS") S("NOP"), "none"));
    CHECK(INSPECTS(S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 3 +8"));
    CHECK(INSPECTS(S("RTS") S("ADD         #-8,R15"),
                   "abi return-below-entry 3 -8"));
    CHECK(INSPECTS(S("ADD         #4,R15") S("ADD         #-4,R15") S("RTS")
                       S("NOP"),
                   "abi stack-above-entry 2 +4"));
    CHECK(inspects(F(S("RTS") S("ADD         #8,R15")), R32, OBJ | ASM, UNKNOWN,
                   "none; abi-scope"));

    // pushes and pops, by size, through R15 or its alias SP, with asmsh's
    // size where none is written
    CHECK(INSPECTS(S("MOV.L       R8,@-R15") S("RTS") S("MOV.L       @R15+,R8"),
                   "none"));
    CHECK(INSPECTS(S("MOV         R8,@-R15") S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 4 +4"));
    CHECK(INSPECTS(S("RTS") S("MAC         @R15+,@R15+"),
                   "abi stack-above-entry 3 +4"));
    CHECK(INSPECTS(S("MOV.L       R8,@-SP") S("RTS") S("ADD         #8,SP"),
                   "abi stack-above-entry 4 +4"));
    CHECK(INSPECTS(S("RTS") S("MOV.W       @SP+,R4"),
                   "abi stack-above-entry 3 +2"));
    CHECK(INSPECTS(S("RTS") S("MOV.B       @SP+,R4"),
                   "abi stack-above-entry 3 +1"));
    CHECK(INSPECTS(S("RTS") S("MAC.L       @R15+,@R15+"),
                   "abi stack-above-entry 3 +8"));
    CHECK(INSPECTS(S("RTS") S("MAC.W       @R15+,@R15+"),
                   "abi stack-above-entry 3 +4"));
    CHECK(INSPECTS(S("RTS") S("STC.L       SR,@-R15"),
                   "abi return-below-entry 3 -4"));
    CHECK(INSPECTS(S("RTS") S("LDS.L       @R15+,FPUL"),
                   "abi stack-above-entry 3 +4"));
    CHECK(INSPECTS(S("LDS.L       @R15+,PR") S("RTS") S("NOP"),
                   "abi stack-above-entry 2 +4"));

    // an FMOV through R15 moves 4 or 8 bytes by SZ, which FSCHG flips and a
    // write to FPSCR loses. With SZ unknown, a witness under both names the
    // offset nearer the entry, and a witness under one SZ alone is
    // stack-unknown.
    CHECK(inspects(F(S("RTS") S("FMOV.S      @R15+,FR4")), R10, ASM,
                   SHCKIT_SZ_0, "abi stack-above-entry 3 +4"));
    CHECK(inspects(F(S("RTS") S("FMOV.S      @R15+,FR4")), R10, ASM,
                   SHCKIT_SZ_1, "abi stack-above-entry 3 +8"));
    CHECK(INSPECTS(S("RTS") S("FMOV.S      @R15+,FR4"),
                   "abi stack-above-entry 3 +4"));
    CHECK(inspects(
        F(S("FMOV.S      FR4,@-R15") S("ADD         #4,R15") S("RTS") S("NOP")),
        R10, ASM, SHCKIT_SZ_1, "abi return-below-entry 5 -4"));
    CHECK(INSPECTS(S("FMOV.S      FR4,@-R15") S("ADD         #4,R15") S("RTS")
                       S("NOP"),
                   "none; stack-unknown 5"));
    CHECK(INSPECTS(S("FMOV.S      FR4,@-R15") S("FMOV.S      FR5,@-R15")
                       S("ADD         #12,R15") S("RTS") S("NOP"),
                   "none; stack-unknown 4"));
    CHECK(inspects(F(S("FSCHG") S("RTS") S("FMOV.S      @R15+,FR4")), R10, ASM,
                   SHCKIT_SZ_0, "abi stack-above-entry 4 +8"));
    CHECK(inspects(F(S("FSCHG") S("FSCHG") S("RTS") S("FMOV.S      @R15+,FR4")),
                   R10, ASM, SHCKIT_SZ_0, "abi stack-above-entry 5 +4"));
    CHECK(inspects(
        F(S("LDS         R4,FPSCR") S("RTS") S("FMOV.S      @R15+,FR4")), R10,
        ASM, SHCKIT_SZ_0, "none; stack-unknown 4"));

    // R14 keeps a copy of R15's offset until written, and gives it back
    CHECK(INSPECTS(S("MOV         R15,R14") S("ADD         R4,R15") S(
                       "MOV         R14,R15") S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 6 +8; stack-unknown 3"));
    CHECK(INSPECTS(S("MOV         R15,R14") S("ADD         R4,R15")
                       S("MOV         R4,R14") S("MOV         R14,R15") S("RTS")
                           S("NOP"),
                   "none; stack-unknown 3"));
    CHECK(INSPECTS(S("MOV         R15,R14") S("MOV.L       @R14+,R4")
                       S("MOV         R14,R15") S("RTS") S("NOP"),
                   "abi stack-above-entry 4 +4"));
    CHECK(inspects(F(S("MOV         R15,R14") S("FMOV.S      FR4,@-R15")
                         S("MOV         R14,R15") S("RTS") S("NOP")),
                   R10, ASM, SHCKIT_SZ_0, "none"));

    // what reads R15 without writing it, and what writes another register
    CHECK(INSPECTS(S("CMP/PL      R15") S("CMP/EQ      R14,R15") S(
                       "TST         R15,R15") S("MUL.L       R14,R15")
                       S("STS         PR,R4") S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 8 +8"));
    CHECK(INSPECTS(S("DIV1        R4,R5") S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 4 +8"));

    // an ADD of a signed byte, in any of asmsh's radixes
    CHECK(INSPECTS(S("RTS") S("ADD         #H'08,R15"),
                   "abi stack-above-entry 3 +8"));
    CHECK(INSPECTS(S("RTS") S("ADD         #B'1000,R15"),
                   "abi stack-above-entry 3 +8"));
    CHECK(
        INSPECTS(S("RTS") S("ADD         #H'FF,R15"), "none; stack-unknown 3"));
    CHECK(INSPECTS(S("ADD         #_x,R15") S("RTS") S("NOP"),
                   "none; stack-unknown 2"));
    CHECK(INSPECTS(S("ADD         #256,R15") S("RTS") S("NOP"),
                   "none; stack-unknown 2"));

    // a write to PR, an expansion, and RTE, which returns
    CHECK(INSPECTS(S("LDS         R4,PR") S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 4 +8"));
    CHECK(INSPECTS(S("MOV.L       #1234,R4") S("MOVA        #1234,R0")
                       S("MOV.W       #1234,R14") S("RTS")
                           S("ADD         #8,R15"),
                   "abi stack-above-entry 6 +8"));
    CHECK(INSPECTS(S("RTE") S("ADD         #8,R15"),
                   "abi stack-above-entry 3 +8"));

    // a call, a jump, or an exception
    CHECK(INSPECTS(S("STS.L       PR,@-R15") S("BSR         _g") S("NOP")
                       S("ADD         #8,R15") S("LDS.L       @R15+,PR")
                           S("RTS") S("NOP"),
                   "none; call 3"));
    CHECK(INSPECTS(S("JSR         @R4") S("NOP") S("RTS")
                       S("ADD         #8,R15"),
                   "none; call 2"));
    CHECK(INSPECTS(S("BSRF        R4") S("NOP") S("RTS")
                       S("ADD         #8,R15"),
                   "none; call 2"));
    CHECK(INSPECTS(S("JMP         @R4") S("NOP"), "none; opaque-transfer 2"));
    CHECK(INSPECTS(S("BRAF        R4") S("NOP"), "none; opaque-transfer 2"));
    CHECK(INSPECTS(S("TRAPA       #0") S("RTS") S("NOP"),
                   "none; opaque-transfer 2"));
    CHECK(INSPECTS(S("SLEEP") S("RTS") S("NOP"), "none; opaque-transfer 2"));
    CHECK(INSPECTS(S("ADD         #4,R15") S("JMP         @R4") S("NOP"),
                   "abi stack-above-entry 2 +4; opaque-transfer 3"));

    // what the assembler lays out, and a slot it may move
    CHECK(INSPECTS(S(".ALIGN      4") S("RTS") S("ADD         #8,R15"),
                   "none; assembler-layout 2"));
    CHECK(INSPECTS(S("RTS") S(".ALIGN      4") S("ADD         #8,R15"),
                   "none; delay-slot 2"));

    // a continuation of an instruction, and of a line control passes
    CHECK(INSPECTS(S("FIPR        FV8,") "+           FV0\n" S("RTS") S("NOP"),
                   "none; continued 2"));
    CHECK(INSPECTS(S(".EXPORT     _a,") "+           _b\n" S("RTS")
                       S("ADD         #8,R15"),
                   "abi stack-above-entry 5 +8"));

    // control past the function's last line, or into .END
    CHECK(INSPECTS(S("NOP"), "none; fallthrough 2"));
    CHECK(INSPECTS(S("NOP") S(".END"), "none; fallthrough 3"));

    // every run of up to four pushes, pops and adjustments: a witness at
    // the first step that takes R15 above the entry, by the run's sum there,
    // or else at the return's slot if the run ends below the entry
    static const struct {
        const char *line;
        int bytes;
    } steps[] = {
        {S("ADD         #4,R15"), 4},    {S("ADD         #-4,R15"), -4},
        {S("MOV.L       R8,@-R15"), -4}, {S("MOV.L       @R15+,R8"), 4},
        {S("MOV.W       @R15+,R4"), 2},  {S("TST         R4,R4"), 0},
    };
    const size_t kinds = sizeof steps / sizeof steps[0];
    for (size_t runs = kinds, length = 1; length <= 4;
         runs *= kinds, length++) {
        for (size_t run = 0; run < runs; run++) {
            const char *lines[4] = {"", "", "", ""};
            char in[512];
            char want[64] = "none";
            int sum = 0;
            bool above = false;
            for (size_t i = 0, at = run; i < length; i++, at /= kinds) {
                lines[i] = steps[at % kinds].line;
                sum += steps[at % kinds].bytes;
                if (sum > 0 && !above) {
                    (void)snprintf(want, sizeof want,
                                   "abi stack-above-entry %zu %+d", i + 2, sum);
                    above = true;
                }
            }
            if (sum < 0 && !above) {
                (void)snprintf(want, sizeof want,
                               "abi return-below-entry %zu %+d", length + 3,
                               sum);
            }
            (void)snprintf(in, sizeof in, F("%s%s%s%s" S("RTS") S("NOP")),
                           lines[0], lines[1], lines[2], lines[3]);
            CHECK(inspects(in, R10, ASM, UNKNOWN, want));
        }
    }

    // a line no path reaches shows nothing
    CHECK(INSPECTS(S("RTS") S("NOP") "_dead:\n" S("ADD         #8,R15") S("RTS")
                       S("NOP"),
                   "none"));
}

static void test_inspect_branches(void)
{
    // a branch, its slot, and joins whose offsets agree
    CHECK(INSPECTS(S("BRA         _ret") S("ADD         #-8,R15") "_dead:\n" S(
                       "ADD         #8,R15") "_ret:\n" S("RTS")
                       S("ADD         #16,R15"),
                   "abi stack-above-entry 8 +8"));
    CHECK(INSPECTS(
        S("TST         R4,R4") S("BT          _other") S("ADD         #-8,R15")
            S("BRA         _ret")
                S("NOP") "_other:\n" S("ADD         #-8,R15") "_ret:\n" S("RTS")
                    S("ADD         #12,R15"),
        "abi stack-above-entry 11 +4"));
    CHECK(INSPECTS(S("TST         R4,R4") S("BT/S        _ret")
                       S("ADD         #8,R15") "_ret:\n" S("RTS") S("NOP"),
                   "abi stack-above-entry 4 +8"));
    CHECK(INSPECTS(S("TST         R4,R4") S("BF/S        _ret")
                       S("ADD         #-8,R15") "_ret:\n" S("RTS")
                           S("ADD         #12,R15"),
                   "abi stack-above-entry 7 +4"));

    // a branch moves no R15, so one reached above the entry, as the BT on
    // line 5 is from line 14, is no witness
    CHECK(INSPECTS(
        S("BRA         L3") S("NOP") "L1:\n" S("BT          L2") S("RTS")
            S("ADD         #-4,R15") "L2:\n" S("RTS")
                S("ADD         #-4,R15") "L3:\n" S("MOV.L       @R15+,R1")
                    S("BRA         L1") S("NOP"),
        "abi stack-above-entry 12 +4"));

    // joins whose offsets disagree, and loops that keep R15 or don't
    CHECK(INSPECTS(S("TST         R4,R4") S("BT          _ret")
                       S("ADD         #-8,R15") "_ret:\n" S("RTS") S("NOP"),
                   "none; stack-unknown 6"));
    CHECK(INSPECTS(S("TST         R4,R4") S("BT          _ret")
                       S("ADD         #8,R15") "_ret:\n" S("RTS") S("NOP"),
                   "abi stack-above-entry 4 +8; stack-unknown 6"));
    CHECK(INSPECTS("_loop:\n" S("ADD         #-4,R15") S("ADD         #4,R15")
                       S("DT          R4") S("BF          _loop") S("RTS")
                           S("NOP"),
                   "none"));
    CHECK(INSPECTS("_loop:\n" S("ADD         #-4,R15") S("DT          R4")
                       S("BF          _loop") S("RTS") S("NOP"),
                   "none; stack-unknown 3"));
    CHECK(INSPECTS("_loop:\n" S("BRA         _loop") S("NOP"), "none"));

    // a target that is no label asmsh defines, a slot, or data. A label
    // defined twice is its first definition.
    CHECK(INSPECTS(S("BRA         _g") S("NOP"), "none; branch-target 2"));
    CHECK(INSPECTS(S("BRA         _ret+2") S("NOP") "_ret:\n" S("RTS") S("NOP"),
                   "none; branch-target 2"));
    CHECK(INSPECTS(S("BRA         _ret") S("NOP") "_ret:\n" S("RTS")
                       S("NOP") "_ret:\n" S("RTS") S("ADD         #8,R15"),
                   "none"));
    CHECK(INSPECTS(S("BRA         _slot") S("NOP")
                       S("RTS") "_slot:\n" S("ADD         #8,R15"),
                   "none; entered-delay-slot 6"));
    CHECK(INSPECTS(S("BRA         L1") S("NOP") "L1:\n" S(".DATA.L     0"),
                   "none; data-flow 5"));
    CHECK(INSPECTS(S("BRA         L1") S("NOP")
                       S(".SECTION    Q,CODE,ALIGN=4") "L1:\n" S("RTS")
                           S("ADD         #8,R15"),
                   "none; section-change 2"));
    CHECK(INSPECTS(S("ADD         #-4,R15") S("BRA         $")
                       S("NOP") "$:\n" S("RTS") S("NOP"),
                   "none; branch-target 3"));

    // a branch out of the function's lines ends its path, past its slot
    // where it has one, and is no return, but a witness may stand at the
    // slot: a tail branch, one whose slot leaves R15 below the entry and one
    // whose slot leaves it above, one to a label above the entry, and a
    // conditional one, whose other path goes on, past BT/S's slot too
    static const struct {
        const char *in;
        size_t first;
        size_t end;
        const char *want;
    } out[] = {
        {F(S("MOV.L       R14,@-R15") S("BRA         _g")
               S("MOV.L       @R15+,R14") "_g:\n" S("RTS") S("NOP")),
         0, 4, "none"},
        {F(S("BRA         _g") S("ADD         #-4,R15") "_g:\n" S("RTS")
               S("NOP")),
         0, 3, "none"},
        {F(S("BRA         _g") S("ADD         #4,R15") "_g:\n" S("RTS")
               S("NOP")),
         0, 3, "abi stack-above-entry 3 +4"},
        {"_g:\n" S("RTS") S("NOP") F(S("BRA         _g") S("NOP")), 3, 0,
         "none"},
        {F(S("TST         R4,R4") S("BT          _g") S("RTS")
               S("ADD         #8,R15") "_g:\n" S("RTS") S("NOP")),
         0, 5, "abi stack-above-entry 5 +8"},
        {F(S("TST         R4,R4") S("BT/S        _g") S("ADD         #-4,R15")
               S("RTS") S("ADD         #4,R15") "_g:\n" S("RTS") S("NOP")),
         0, 6, "none"},

        // a slot past the function's last line, and a label above the
        // entry that marks an instruction in the function
        {F(S("BRA         _g") S("NOP") "_g:\n" S("RTS") S("NOP")), 0, 2,
         "none; delay-slot 2"},
        {"_g:\n" F(S("BRA         _g") S("NOP")), 1, 0,
         "none; branch-target 3"},

        // a branch across a .SECTION line to a label outside, below the
        // function and above it, as one to a label inside
        {F(S("BRA         _g") S("NOP") CODE "_g:\n" S("RTS") S("NOP")), 0, 3,
         "none; section-change 2"},
        {"_g:\n" S("RTS") S("NOP") CODE F(S("BRA         _g") S("NOP")), 4, 0,
         "none; section-change 6"},
    };
    for (size_t i = 0; i < sizeof out / sizeof out[0]; i++) {
        CHECK(inspects_lines(out[i].in, out[i].first, out[i].end, R10, ASM,
                             UNKNOWN, out[i].want));
    }

    // a label outside that marks no instruction: data, a directive that
    // writes none, a .SECTION line, and the end of the text
    static const char *const unlabelled[] = {
        S(".DATA.L     0"),
        S(".ALIGN      4") S("RTS") S("NOP"),
        CODE S("RTS") S("NOP"),
        "",
    };
    for (size_t i = 0; i < sizeof unlabelled / sizeof unlabelled[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, F(S("BRA         _g") S("NOP") "_g:\n%s"),
                       unlabelled[i]);
        CHECK(inspects_lines(in, 0, 3, R10, ASM, UNKNOWN,
                             "none; branch-target 2"));
    }

    // a witness past a branch the rule can't decide, and in a loop that may
    // not end
    CHECK(INSPECTS(S("CMP/EQ      R4,R4") S("BF          _dead") S("RTS")
                       S("NOP") "_dead:\n" S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 8 +8"));
    CHECK(INSPECTS("_loop:\n" S("DT          R4") S("BF          _loop")
                       S("RTS") S("ADD         #8,R15"),
                   "abi stack-above-entry 6 +8"));
}

static void test_inspect_returns(void)
{
    // R15 below its entry value once an RTS's slot has run, a witness at
    // the slot: an epilogue that leaves out a pop, against one that doesn't,
    // and an early return that leaves out the frame's ADD, beside a call
    static const char short_pop[] =
        F(S("MOV.L       R14,@-R15") S("STS.L       PR,@-R15")
              S("ADD         #-8,R15") S("MOV         R4,R0")
                  S("ADD         #8,R15") S("RTS") S("LDS.L       @R15+,PR"));
    CHECK(inspects(short_pop, R10, OBJ | ASM, UNKNOWN,
                   "abi return-below-entry 8 -4"));
    CHECK(inspects(short_pop, R10, OBJ, SHCKIT_SZ_1,
                   "abi return-below-entry 8 -4"));
    CHECK(inspects(short_pop, R32, OBJ | ASM, UNKNOWN, "none; abi-scope"));
    CHECK(INSPECTS(S("MOV.L       R14,@-R15") S("STS.L       PR,@-R15")
                       S("ADD         #-8,R15") S("MOV         R4,R0")
                           S("ADD         #8,R15") S("LDS.L       @R15+,PR")
                               S("RTS") S("MOV.L       @R15+,R14"),
                   "none"));
    CHECK(INSPECTS(S("MOV.L       R14,@-R15") S("MOV         R4,R14")
                       S("STS.L       PR,@-R15") S("TST         R14,R14")
                           S("ADD         #-4,R15") S("BT/S        L288")
                               S("MOV.L       R5,@R15") S("MOV         #0,R0")
                                   S("LDS.L       @R15+,PR") S("RTS")
                                       S("MOV.L       @R15+,R14") "L288:\n" S(
                                           "JSR         @R3") S("NOP"),
                   "abi return-below-entry 12 -4; call 14"));

    // on one path of two, the first witness by line of either stack rule,
    // and stack-written, listed first, at the same line
    CHECK(INSPECTS(S("TST         R4,R4") S("BT          _short")
                       S("MOV.L       R14,@-R15") S("RTS")
                           S("MOV.L       @R15+,R14") "_short:\n" S(
                               "ADD         #-4,R15") S("RTS") S("NOP"),
                   "abi return-below-entry 10 -4"));
    CHECK(INSPECTS(S("TST         R4,R4") S("BT          _other") S("RTS")
                       S("ADD         #4,R15") "_other:\n" S("RTS")
                           S("ADD         #-4,R15"),
                   "abi stack-above-entry 5 +4"));
    CHECK(INSPECTS(S("TST         R4,R4") S("BT          _other") S("RTS")
                       S("ADD         #-4,R15") "_other:\n" S("RTS")
                           S("ADD         #4,R15"),
                   "abi return-below-entry 5 -4"));
    CHECK(INSPECTS(S("ADD         #-4,R15") S("RTS") S("MOV         R15,R15"),
                   "abi stack-written 4"));

    // an FR push by the width SZ gives, which FSCHG flips
    static const char fr_push[] =
        F(S("FMOV.S      FR12,@-R15") S("RTS") S("NOP"));
    static const char flipped[] = F(S("FMOV.S      FR13,@-R15") S("FSCHG")
                                        S("RTS") S("FMOV.S      @R15+,FR13"));
    CHECK(inspects(fr_push, R10, ASM, SHCKIT_SZ_0,
                   "abi return-below-entry 4 -4"));
    CHECK(inspects(fr_push, R10, ASM, SHCKIT_SZ_1,
                   "abi return-below-entry 4 -8"));
    CHECK(inspects(fr_push, R10, ASM, UNKNOWN, "abi return-below-entry 4 -4"));
    CHECK(inspects(flipped, R10, ASM, SHCKIT_SZ_1,
                   "abi return-below-entry 5 -4"));
    CHECK(
        inspects(flipped, R10, ASM, SHCKIT_SZ_0, "abi stack-above-entry 5 +4"));
    CHECK(inspects(flipped, R10, ASM, UNKNOWN, "none; stack-unknown 5"));

    // R15 below the entry before the slot, in the slot by a pop, after RTE,
    // or past a limit is no witness
    CHECK(INSPECTS(S("ADD         #-8,R15") S("MOV.L       R4,@R15") S("RTS")
                       S("ADD         #8,R15"),
                   "none"));
    CHECK(INSPECTS(S("MOV.L       R14,@-R15") S("RTS")
                       S("MOV.L       @R15+,R14"),
                   "none"));
    CHECK(INSPECTS(S("MOV.L       R14,@-R15") S("RTE") S("NOP"), "none"));
    CHECK(INSPECTS(S("ADD         #-4,R15") S("BSR         _g") S("NOP")
                       S("RTS") S("NOP"),
                   "none; call 3"));
    CHECK(INSPECTS(S("ADD         #-4,R15") S("TRAPA       #0") S("RTS")
                       S("NOP"),
                   "none; opaque-transfer 3"));
    CHECK(INSPECTS(S("ADD         #-4,R15") S(".ALIGN      4") S("RTS")
                       S("NOP"),
                   "none; assembler-layout 3"));
    CHECK(INSPECTS(S("ADD         R4,R15") S("RTS") S("NOP"),
                   "none; stack-unknown 2"));
    CHECK(INSPECTS(S("TST         R4,R4") S("BT          _ret")
                       S("MOV.L       R14,@-R15") "_ret:\n" S("RTS") S("NOP"),
                   "none; stack-unknown 6"));

    // a #pragma inline_asm function whose text pushes two FR registers, as
    // R10 writes it into -code=asmcode text: the .ALIGN 4 below inline text
    // stops the rule, and once drop-inline-align drops it, the rule reaches
    // the RTS
    static const char copied[] = CODE "_f:\n"
                                      "    FRCHG\n"
                                      "    FMOV.S      FR0,@-R15\n"
                                      "    FMOV.S      FR1,@-R15\n"
                                      "    MOVA        #H'3B800000,R0\n"
                                      "    FMOV.S      @R0,FR0\n"
                                      "    FRCHG\n" ALIGN S("RTS") S("NOP");
    static const char dropped[] = CODE "_f:\n"
                                       "    FRCHG\n"
                                       "    FMOV.S      FR0,@-R15\n"
                                       "    FMOV.S      FR1,@-R15\n"
                                       "    MOVA        #H'3B800000,R0\n"
                                       "    FMOV.S      @R0,FR0\n"
                                       "    FRCHG\n" S("RTS") S("NOP");
    CHECK(rewrites(DROP, copied, dropped));
    CHECK(inspects_lines(copied, 1, 0, R10, ASM, SHCKIT_SZ_0,
                         "none; assembler-layout 9"));
    CHECK(inspects_lines(copied, 1, 0, R10, ASM, UNKNOWN,
                         "none; assembler-layout 9"));
    CHECK(inspects_lines(dropped, 1, 0, R10, ASM, SHCKIT_SZ_0,
                         "abi return-below-entry 10 -8"));
    CHECK(inspects_lines(dropped, 1, 0, R10, ASM, SHCKIT_SZ_1,
                         "abi return-below-entry 10 -16"));
}

static void test_inspect_pairs(void)
{
    // an FMOV between memory and FR8, FR10, FR12, FR14 or DR8 to DR14, after
    // an odd number of FSCHGs on every path from the entry, under R10 in both
    // modes and whatever --sz says: a run that stores DR12 and DR14 below an
    // aligned pointer and one that loads them back, each spelling, size and
    // memory operand, FR8, FR10, DR8 and DR10, an FSCHG in a slot, an FMOV in
    // one, a run whose closing FSCHG two paths share, and a loop
    static const struct {
        const char *in;
        const char *want;
    } pairs[] = {
        {F(S("FSCHG") S("ADD         #87,R4") S("MOV         #-8,R1")
               S("AND         R1,R4") S("FMOV        DR12,@-R4")
                   S("FMOV        DR14,@-R4") S("FMOV        XD0,@-R4") S("RTS")
                       S("FSCHG")),
         "operand fschg-pairs 6"},
        {F(S("FSCHG") S("ADD         #7,R4") S("MOV         #-8,R1")
               S("AND         R1,R4") S("FMOV        @R4+,XD14")
                   S("FMOV        @R4+,DR14") S("FMOV        @R4+,DR12")
                       S("RTS") S("FSCHG")),
         "operand fschg-pairs 7"},
        {F(S("FSCHG") S("FMOV.S      FR12,@-R4") S("FSCHG") S("RTS") S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV        FR14,@R4") S("FSCHG") S("RTS") S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV.S      FR12,@(R0,R4)") S("FSCHG") S("RTS")
               S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV.D      @R4,DR14") S("FSCHG") S("RTS") S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV.S      @R4+,FR12") S("FSCHG") S("RTS") S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV        @(R0,R4),DR12") S("FSCHG") S("RTS")
               S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("fschg") S("fmov.s      fr14,@-r4") S("fschg") S("rts") S("nop")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV.S      FR8,@-R4") S("FSCHG") S("RTS") S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV.S      @(R0,R4),FR10") S("FSCHG") S("RTS")
               S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV        @R4+,DR8") S("FSCHG") S("RTS") S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("FSCHG") S("FMOV        DR10,@R4") S("FSCHG") S("RTS") S("NOP")),
         "operand fschg-pairs 3"},
        {F(S("TST         R4,R4") S("BT/S        L1") S("FSCHG")
               S("NOP") "L1:\n" S("FMOV        DR12,@-R4") S("RTS") S("FSCHG")),
         "operand fschg-pairs 7"},
        {F(S("FSCHG") S("RTS") S("FMOV        DR12,@-R4")),
         "operand fschg-pairs 4"},
        {F(S("TST         R4,R4") S("BT          L1") S("FSCHG") S(
             "FMOV        @R5+,XD0") S("BRA         L2")
               S("NOP") "L1:\n" S("FSCHG") S("FMOV        XD0,@-R5") "L2:\n" S(
                   "FMOV        DR12,@-R4") S("FSCHG") S("RTS") S("NOP")),
         "operand fschg-pairs 12"},
        {F("L1:\n" S("FSCHG") S("FMOV        DR14,@R4") S("FSCHG")
               S("DT          R5") S("BF          L1") S("RTS") S("NOP")),
         "operand fschg-pairs 4"},
    };
    static const int szs[] = {UNKNOWN, SHCKIT_SZ_0, SHCKIT_SZ_1};
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        for (size_t j = 0; j < sizeof szs / sizeof szs[0]; j++) {
            CHECK(inspects(pairs[i].in, R10, OBJ, szs[j], pairs[i].want));
            CHECK(inspects(pairs[i].in, R10, ASM, szs[j], pairs[i].want));
            CHECK(inspects(pairs[i].in, R10, OBJ | ASM, szs[j], pairs[i].want));
        }
    }

    // pairs pushed and popped through R15, which comes back to its entry
    // value, and a pair's move outranking a stack witness at its line
    static const char stacked[] =
        F(S("FSCHG") S("FMOV        DR12,@-R15") S("FMOV        DR14,@-R15")
              S("FSCHG") S("FSCHG") S("FMOV        @R15+,DR14")
                  S("FMOV        @R15+,DR12") S("RTS") S("FSCHG"));
    static const char flipped[] = F(S("FMOV.S      FR12,@-R15") S("FSCHG")
                                        S("RTS") S("FMOV.S      @R15+,FR12"));
    CHECK(inspects(stacked, R10, OBJ | ASM, UNKNOWN, "operand fschg-pairs 3"));
    CHECK(inspects(stacked, R10, OBJ | ASM, SHCKIT_SZ_0,
                   "operand fschg-pairs 3"));
    CHECK(inspects(stacked, R10, OBJ | ASM, SHCKIT_SZ_1,
                   "operand fschg-pairs 3"));
    CHECK(inspects(flipped, R10, ASM, SHCKIT_SZ_0, "operand fschg-pairs 5"));
    CHECK(inspects(flipped, R10, ASM, SHCKIT_SZ_1, "operand fschg-pairs 5"));
    CHECK(inspects(flipped, R10, ASM, UNKNOWN,
                   "operand fschg-pairs 5; stack-unknown 5"));
    CHECK(inspects_all(flipped, R10, ASM, SHCKIT_SZ_0,
                       "operand fschg-pairs 5, abi stack-above-entry 5 +4"));
    CHECK(inspects_all(flipped, R10, ASM, UNKNOWN,
                       "operand fschg-pairs 5; stack-unknown 5"));

    // no witness: an even count, XD8 to XD14, moves between registers, a
    // join whose paths disagree, a write to FPSCR, before an FSCHG too, and
    // an interrupt function's pop of FPSCR, a line control never reaches and
    // one past a call
    static const struct {
        const char *in;
        const char *want;
    } singles[] = {
        {F(S("FSCHG") S("FSCHG") S("FMOV        DR12,@-R4") S("RTS") S("NOP")),
         "none"},
        {F(S("FSCHG") S("FMOV        XD8,@-R4") S("FMOV        XD10,@-R4")
               S("FMOV        XD12,@-R4") S("FMOV        XD14,@-R4") S("FSCHG")
                   S("RTS") S("NOP")),
         "none"},
        {F(S("FSCHG") S("FMOV        DR12,DR2") S("FMOV        XD2,DR14")
               S("FSCHG") S("RTS") S("NOP")),
         "none"},
        {F(S("TST         R5,R5") S("BT          L1")
               S("FSCHG") "L1:\n" S("FMOV        DR12,@-R4") S("RTS") S("NOP")),
         "none"},
        {F(S("FSCHG") S("LDS         R4,FPSCR") S("FMOV        DR12,@-R4")
               S("RTS") S("FSCHG")),
         "none"},
        {F(S("LDS         R4,FPSCR") S("FSCHG") S("FMOV        DR12,@-R4")
               S("RTS") S("FSCHG")),
         "none"},
        {F(S("FSCHG") S("STS.L       FPSCR,@-R15") S("LDS.L       @R15+,FPSCR")
               S("FMOV        DR12,@-R4") S("RTE") S("FSCHG")),
         "none"},
        {F(S("FSCHG") S("RTS") S("NOP") S("FMOV        @R15+,DR12") S("RTS")
               S("FSCHG")),
         "none"},
        {F(S("FSCHG") S("JSR         @R5") S("NOP") S("FMOV        DR12,@-R4")
               S("RTS") S("FSCHG")),
         "none; call 3"},
    };
    for (size_t i = 0; i < sizeof singles / sizeof singles[0]; i++) {
        for (size_t j = 0; j < sizeof szs / sizeof szs[0]; j++) {
            CHECK(inspects(singles[i].in, R10, OBJ | ASM, szs[j],
                           singles[i].want));
        }
    }

    // what R10 writes for C, under an SDK build's flags but the last's,
    // which is R10's for the SH-4 with no other option, gf12 and gf14 being
    // FR12 and FR14 by #pragma global_register: the second bank stored
    // below a pointer aligned to 8; FR12 and FR14 moved between
    // _builtin_st_ext's run and _builtin_ld_ext's, and the same words
    // spelled as pairs; FR8 to FR11 moved between those runs and after
    // _builtin_ftrv's; FR12 and FR14 stored with SZ set by
    // _builtin_set_fpscr; and a switch whose cases share a run's closing
    // FSCHG, where a count by line, not by path, is odd at the loads of
    // FR12 and FR14
    static const char *const compiled[] = {
        // void save_bank(float *save)
        // { save = (float *)(((unsigned long)save + 87) & ~7UL);
        //   _builtin_st_ext((void *)(save - 16)); }
        "_save_bank:\n"
        "          ADD         #87,R4\n"
        "          MOV         #-8,R3\n"
        "          AND         R3,R4\n"
        "          FSCHG\n"
        "          FMOV        XD14,@-R4\n"
        "          FMOV        XD12,@-R4\n"
        "          FMOV        XD10,@-R4\n"
        "          FMOV        XD8,@-R4\n"
        "          FMOV        XD6,@-R4\n"
        "          FMOV        XD4,@-R4\n"
        "          FMOV        XD2,@-R4\n"
        "          FMOV        XD0,@-R4\n"
        "          FSCHG\n"
        "          RTS\n"
        "          NOP\n",
        // void gap_push(float *p, float *m) { _builtin_st_ext((void *)m);
        //   *--p = gf12; *--p = gf14; _builtin_ld_ext((void *)m); }
        "_gap_push:\n"
        "          MOV         R5,R3\n"
        "          ADD         #64,R3\n"
        "          FSCHG\n"
        "          FMOV        XD14,@-R3\n"
        "          FMOV        XD12,@-R3\n"
        "          FMOV        XD10,@-R3\n"
        "          FMOV        XD8,@-R3\n"
        "          FMOV        XD6,@-R3\n"
        "          FMOV        XD4,@-R3\n"
        "          FMOV        XD2,@-R3\n"
        "          FMOV        XD0,@-R3\n"
        "          FSCHG\n"
        "          FMOV.S      FR12,@-R4\n"
        "          FMOV.S      FR14,@-R4\n"
        "          FSCHG\n"
        "          FMOV        @R5+,XD0\n"
        "          FMOV        @R5+,XD2\n"
        "          FMOV        @R5+,XD4\n"
        "          FMOV        @R5+,XD6\n"
        "          FMOV        @R5+,XD8\n"
        "          FMOV        @R5+,XD10\n"
        "          FMOV        @R5+,XD12\n"
        "          FMOV        @R5+,XD14\n"
        "          FSCHG\n"
        "          RTS\n"
        "          NOP\n",
        // the same code words, FR12's and FR14's spelled as DR12's and
        // DR14's
        "_gap_push:\n"
        "          MOV         R5,R3\n"
        "          ADD         #64,R3\n"
        "          FSCHG\n"
        "          FMOV        XD14,@-R3\n"
        "          FMOV        XD12,@-R3\n"
        "          FMOV        XD10,@-R3\n"
        "          FMOV        XD8,@-R3\n"
        "          FMOV        XD6,@-R3\n"
        "          FMOV        XD4,@-R3\n"
        "          FMOV        XD2,@-R3\n"
        "          FMOV        XD0,@-R3\n"
        "          FSCHG\n"
        "          FMOV        DR12,@-R4\n"
        "          FMOV        DR14,@-R4\n"
        "          FSCHG\n"
        "          FMOV        @R5+,XD0\n"
        "          FMOV        @R5+,XD2\n"
        "          FMOV        @R5+,XD4\n"
        "          FMOV        @R5+,XD6\n"
        "          FMOV        @R5+,XD8\n"
        "          FMOV        @R5+,XD10\n"
        "          FMOV        @R5+,XD12\n"
        "          FMOV        @R5+,XD14\n"
        "          FSCHG\n"
        "          RTS\n"
        "          NOP\n",
        // void gap_pop(float *p, float *m) { _builtin_st_ext((void *)m);
        //   gf14 = *p++; gf12 = *p++; _builtin_ld_ext((void *)m); }, cut
        // down
        "_gap_pop:\n"
        "          FSCHG\n"
        "          FMOV        XD2,@-R3\n"
        "          FMOV        XD0,@-R3\n"
        "          FSCHG\n"
        "          FMOV.S      @R4+,FR14\n"
        "          FMOV.S      @R4,FR12\n"
        "          FSCHG\n"
        "          FMOV        @R5+,XD0\n"
        "          FSCHG\n"
        "          RTS\n"
        "          NOP\n",
        // void st8(float *p, float *q)
        // { float a = q[0], b = q[1], ..., l = q[11]; _builtin_st_ext(mat);
        //   *--p = a; ... *--p = l; _builtin_ld_ext(mat);
        //   *--p = l * a; ... *--p = g * f; _builtin_ftrv(p, q);
        //   *--p = a + l; ... *--p = f + g; }, cut down to e to h, which
        // R10 keeps in FR8 to FR11
        "_st8:\n"
        "          FMOV.S      FR15,@-R15\n"
        "          FMOV.S      FR14,@-R15\n"
        "          FMOV.S      FR13,@-R15\n"
        "          FMOV.S      FR12,@-R15\n"
        "          MOV         #16,R0\n"
        "          FMOV.S      @(R0,R5),FR8\n"
        "          MOV         #20,R0\n"
        "          FMOV.S      @(R0,R5),FR9\n"
        "          MOV         #24,R0\n"
        "          FMOV.S      @(R0,R5),FR10\n"
        "          MOV         #28,R0\n"
        "          FMOV.S      @(R0,R5),FR11\n"
        "          FSCHG\n"
        "          FMOV        XD14,@-R3\n"
        "          FMOV        XD12,@-R3\n"
        "          FMOV        XD10,@-R3\n"
        "          FMOV        XD8,@-R3\n"
        "          FSCHG\n"
        "          FMOV.S      FR8,@-R4\n"
        "          FMOV.S      FR9,@-R4\n"
        "          FMOV.S      FR10,@-R4\n"
        "          FMOV.S      FR11,@-R4\n"
        "          FSCHG\n"
        "          FMOV        @R6+,XD8\n"
        "          FMOV        @R6+,XD10\n"
        "          FMOV        @R6+,XD12\n"
        "          FMOV        @R6+,XD14\n"
        "          FSCHG\n"
        "          FMOV.S      FR8,FR3\n"
        "          FMUL        FR11,FR3\n"
        "          FMOV.S      FR3,@-R4\n"
        "          FMOV.S      FR9,FR3\n"
        "          FMUL        FR10,FR3\n"
        "          FMOV.S      FR3,@-R4\n"
        "          MOV         R4,R3\n"
        "          FSCHG\n"
        "          FMOV        @R3+,DR0\n"
        "          FMOV        @R3+,DR2\n"
        "          FTRV        XMTRX,FV0\n"
        "          ADD         #16,R5\n"
        "          FMOV        DR2,@-R5\n"
        "          FMOV        DR0,@-R5\n"
        "          FSCHG\n"
        "          FADD        FR11,FR8\n"
        "          FMOV.S      FR8,@-R4\n"
        "          FADD        FR10,FR9\n"
        "          FMOV.S      FR9,@-R4\n"
        "          FMOV.S      @R15+,FR12\n"
        "          FMOV.S      @R15+,FR13\n"
        "          FMOV.S      @R15+,FR14\n"
        "          RTS\n"
        "          FMOV.S      @R15+,FR15\n",
        // void a1(float *p) { int s = _builtin_get_fpscr();
        //   _builtin_set_fpscr(s | 0x100000); *--p = gf12; *--p = gf14;
        //   _builtin_set_fpscr(s); }
        "_a1:\n"
        "          MOV.L       L241+2,R3\n"
        "          STS         FPSCR,R5\n"
        "          OR          R5,R3\n"
        "          LDS         R3,FPSCR\n"
        "          FMOV.S      FR12,@-R4\n"
        "          FMOV.S      FR14,@-R4\n"
        "          RTS\n"
        "          LDS         R5,FPSCR\n"
        "L241:\n"
        "          .RES.W      1\n"
        "          .DATA.L     H'00100000\n",
        // void sw(int k, float *p) { switch (k) {
        //   case 0: _builtin_st_ext((void *)p); break;
        //   case 1: _builtin_ld_ext((void *)p); p[0] = gf12; break;
        //   case 2: p[1] = gf14; _builtin_st_ext((void *)p); break;
        //   case 3: _builtin_ftrv(p, p + 4); gf12 = p[4]; break;
        //   default: gf14 = p[3]; _builtin_ld_ext((void *)p); break; } }
        "_sw:\n"
        "          MOV         R4,R0\n"
        "          CMP/EQ      #0,R0\n"
        "          BT          L305\n"
        "          CMP/EQ      #1,R0\n"
        "          BT          L292\n"
        "          CMP/EQ      #2,R0\n"
        "          BT          L293\n"
        "          CMP/EQ      #3,R0\n"
        "          BT          L294\n"
        "          BRA         L295\n"
        "          NOP\n"
        "L292:\n"
        "          MOV         R5,R2\n"
        "          FSCHG\n"
        "          FMOV        @R2+,XD0\n"
        "          FMOV        @R2+,XD2\n"
        "          FMOV        @R2+,XD4\n"
        "          FMOV        @R2+,XD6\n"
        "          FMOV        @R2+,XD8\n"
        "          FMOV        @R2+,XD10\n"
        "          FMOV        @R2+,XD12\n"
        "          FMOV        @R2+,XD14\n"
        "          FSCHG\n"
        "          BRA         L304\n"
        "          FMOV.S      FR12,@R5\n"
        "L293:\n"
        "          MOV         #4,R0\n"
        "          FMOV.S      FR14,@(R0,R5)\n"
        "L305:\n"
        "          ADD         #64,R5\n"
        "          FSCHG\n"
        "          FMOV        XD14,@-R5\n"
        "          FMOV        XD12,@-R5\n"
        "          FMOV        XD10,@-R5\n"
        "          FMOV        XD8,@-R5\n"
        "          FMOV        XD6,@-R5\n"
        "          FMOV        XD4,@-R5\n"
        "          FMOV        XD2,@-R5\n"
        "          FMOV        XD0,@-R5\n"
        "          BRA         L306\n"
        "          NOP\n"
        "L294:\n"
        "          MOV         R5,R2\n"
        "          ADD         #16,R2\n"
        "          MOV         R5,R3\n"
        "          FSCHG\n"
        "          FMOV        @R3+,DR0\n"
        "          FMOV        @R3+,DR2\n"
        "          FTRV        XMTRX,FV0\n"
        "          ADD         #16,R2\n"
        "          FMOV        DR2,@-R2\n"
        "          FMOV        DR0,@-R2\n"
        "          FSCHG\n"
        "          MOV         #16,R0\n"
        "          BRA         L304\n"
        "          FMOV.S      @(R0,R5),FR12\n"
        "L295:\n"
        "          MOV         #12,R0\n"
        "          FMOV.S      @(R0,R5),FR14\n"
        "          FSCHG\n"
        "          FMOV        @R5+,XD0\n"
        "          FMOV        @R5+,XD2\n"
        "          FMOV        @R5+,XD4\n"
        "          FMOV        @R5+,XD6\n"
        "          FMOV        @R5+,XD8\n"
        "          FMOV        @R5+,XD10\n"
        "          FMOV        @R5+,XD12\n"
        "          FMOV        @R5+,XD14\n"
        "L306:\n"
        "          FSCHG\n"
        "L304:\n"
        "          RTS\n"
        "          NOP\n",
    };
    for (size_t i = 0; i < sizeof compiled / sizeof compiled[0]; i++) {
        for (size_t j = 0; j < sizeof szs / sizeof szs[0]; j++) {
            CHECK(inspects(compiled[i], R10, OBJ | ASM, szs[j], "none"));
        }
    }

    // R10 alone: R32 writes no FSCHG, and a release the reading doesn't
    // cover takes no rule that reads FSCHG
    CHECK(inspects(pairs[0].in, R32, OBJ | ASM, UNKNOWN,
                   "opcode fschg 2; abi-scope"));
    CHECK(inspects(pairs[0].in, "5.0R31", OBJ | ASM, UNKNOWN,
                   "none; rule-scope 2"));
}

static void test_inspect_compiled(void)
{
    // what R10 and R32 write for C, cut down, which no rule may witness: a
    // variable C reads before it sets it, in R0 to R3 or FR0 to FR3, or in
    // R4 past a call, which may have changed it, and R32's double precision
    // moving a pop of DR14 above the pushes, which R10 never does
    static const char *const unset[] = {
        // int f(int a, int b, int c, int d)
        // { int x; x += a; x += b; return x + c + d; }
        S("ADD         R4,R1") S("ADD         R5,R1") S("MOV         R6,R0")
            S("NOP") S("ADD         R7,R0") S("RTS") S("ADD         R1,R0"),
        // float f(float a, float b, float c, float d, float e, float f,
        // float g, float h) { float x; x = x + a; return x + b + ... + h; }
        S("FMOV.S      FR1,FR3") S("FMOV.S      FR4,FR1")
            S("FADD        FR3,FR1") S("FADD        FR6,FR5")
                S("FMOV.S      FR5,FR0") S("RTS") S("FADD        FR1,FR0"),
        // int f(float a, ..., float h)
        // { float x; x += a; ... x += h; return x > 0.0f; }
        S("FADD        FR4,FR0") S("FLDI0       FR3") S("FADD        FR5,FR0")
            S("FCMP/GT     FR3,FR0") S("RTS") S("MOVT        R0"),
        // under R32 -fpu=double, double f(double a, double b)
        // { double x = a; x = x / b + a; ... return x; }
        S("FMOV.S      FR4,FR0") S("FADD        DR2,DR4")
            S("FMOV.S      FR5,FR3") S("FMOV.S      FR4,FR2") S("RTS")
                S("FMOV.S      FR5,FR1"),
    };
    // under R32 -fpu=double, double f(double a, double b, double c,
    // double d) { double x = 0.0; x /= a; ... return x; }, eight divisions
    static const char pops[] =
        F(S("FMOV.S      FR14,FR0") S("FMOV.S      @R15+,FR14")
              S("FMOV.S      FR15,FR1") S("FMOV.S      FR15,@-R15")
                  S("MOVA        L1,R0") S("FMOV.S      FR14,@-R15")
                      S("FMOV.S      @R0,FR14") S("FMOV.S      FR14,FR15")
                          S("FDIV        DR4,DR14") S("FDIV        DR6,DR14")
                              S("RTS") S("FMOV.S      @R15+,FR15") "L1:\n" S(
                                  ".DATA.L     H'00000000"));
    // int a1(int a) { int x; g(); x += a; return x; }, the same in both
    // modes and under an SDK build's flags
    static const char past[] =
        F(S("STS.L       PR,@-R15") S("ADD         #-4,R15")
              S("MOV.L       L1+2,R3") S("JSR         @R3")
                  S("MOV.L       R4,@R15") S("MOV.L       @R15,R2")
                      S("ADD         R2,R4") S("ADD         #4,R15")
                          S("LDS.L       @R15+,PR") S("RTS")
                              S("MOV         R4,R0") "L1:\n" S(".RES.W      1")
                                  S(".DATA.L     _g"));
    char in[512];
    for (size_t i = 0; i < sizeof unset / sizeof unset[0]; i++) {
        (void)snprintf(in, sizeof in, F("%s"), unset[i]);
        CHECK(inspects(in, R10, OBJ | ASM, SHCKIT_SZ_0, "none"));
        CHECK(inspects(in, R32, OBJ | ASM, SHCKIT_SZ_0, "none; abi-scope"));
    }
    CHECK(inspects(past, R10, OBJ, SHCKIT_SZ_0, "none; call 5"));
    CHECK(inspects(past, R10, ASM, SHCKIT_SZ_0, "none; call 5"));
    CHECK(inspects(past, R32, OBJ, SHCKIT_SZ_0, "none; abi-scope"));
    CHECK(inspects(past, R32, ASM, SHCKIT_SZ_0, "none; abi-scope"));
    CHECK(inspects(pops, R32, OBJ | ASM, SHCKIT_SZ_0, "none; abi-scope"));
    CHECK(inspects(pops, R10, OBJ | ASM, SHCKIT_SZ_0,
                   "abi stack-above-entry 3 +4"));

    // what R10 and R32 write for C that reads or sets T, with no SETT: the
    // first four R10's under an SDK build's flags, the last R32's for the
    // SH-4 with -fpu=single and -speed
    static const char *const carries[] = {
        // int k21(int x, int y) { return (x + y + 1) / 2; }
        S("MOV         R4,R0") S("NOP") S("ADD         R5,R0")
            S("ADD         #1,R0") S("MOV         #0,R3") S("CMP/GT      R0,R3")
                S("ADDC        R3,R0") S("RTS") S("SHAR        R0"),
        // unsigned char avg1(unsigned char a, unsigned char b)
        // { return (unsigned char)((a + b + 1) >> 1); }
        S("EXTU.B      R4,R0") S("EXTU.B      R5,R5") S("ADD         R5,R0")
            S("ADD         #1,R0") S("RTS") S("SHAR        R0"),
        // int gbrtst(void) { return _builtin_gbr_tst_byte(4, 1); }
        S("MOV         #4,R0") S("TST.B       #1,@(R0,GBR)") S("MOVT        R0")
            S("RTS") S("XOR         #1,R0"),
        // int tas3(char *p) { return !tas(p); }
        S("TAS.B       @R4") S("MOVT        R0") S("RTS")
            S("XOR         #1,R0"),
        // int k21(int x, int y) { return (x + y + 1) / 2; }
        S("MOV         R4,R0") S("ADD         R5,R0") S("MOV         #0,R3")
            S("ADD         #1,R0") S("CMP/GT      R0,R3") S("ADDC        R3,R0")
                S("RTS") S("SHAR        R0"),
    };
    for (size_t i = 0; i < sizeof carries / sizeof carries[0]; i++) {
        bool r32 = i + 1 == sizeof carries / sizeof carries[0];
        (void)snprintf(in, sizeof in, F("%s"), carries[i]);
        CHECK(inspects(in, r32 ? R32 : R10, OBJ | ASM, UNKNOWN,
                       r32 ? "none; abi-scope" : "none"));
    }

    // what R10 and R32 write for C past a call, the same in both modes and
    // under an SDK build's flags: SR read by STC SR,R0 for get_cr() and
    // set_imask(), and by TRAPA, which saves SR, for _builtin_trapa(), with
    // no T writer since the call, so C reads the T the callee left; and the
    // MOVT of _builtin_gbr_tst_byte(), past the TST.B that writes T
    static const char *const calls[] = {
        // int c1(void) { g(); return get_cr() & 1; }
        S("STS.L       PR,@-R15") S("MOV.L       L1,R3") S("JSR         @R3")
            S("NOP") S("LDS.L       @R15+,PR") S("STC         SR,R0") S("RTS")
                S("AND         #1,R0") "L1:\n" S(".DATA.L     _g"),
        // void c3(void) { g(); set_imask(15); }
        S("STS.L       PR,@-R15") S("MOV.L       L1+2,R3") S("JSR         @R3")
            S("NOP") S("STC         SR,R0") S("MOV.W       L1,R2")
                S("AND         R2,R0") S("OR          #240,R0")
                    S("LDC         R0,SR") S("LDS.L       @R15+,PR") S("RTS")
                        S("NOP") "L1:\n" S(".DATA.W     H'FF0F")
                            S(".DATA.L     _g"),
        // int b1(void) { g(); _builtin_trapa(5); return 0; }
        S("STS.L       PR,@-R15") S("MOV.L       L1,R3") S("JSR         @R3")
            S("NOP") S("TRAPA       #5") S("LDS.L       @R15+,PR") S("RTS")
                S("MOV         #0,R0") "L1:\n" S(".DATA.L     _g"),
        // int b2(int a) { g(); return _builtin_gbr_tst_byte(4, 1); }
        S("STS.L       PR,@-R15") S("MOV.L       L1,R3") S("JSR         @R3")
            S("NOP") S("MOV         #4,R0") S("TST.B       #1,@(R0,GBR)")
                S("LDS.L       @R15+,PR") S("MOVT        R0") S("RTS")
                    S("XOR         #1,R0") "L1:\n" S(".DATA.L     _g"),
    };
    static const unsigned modes[] = {OBJ, ASM};
    for (size_t i = 0; i < sizeof calls / sizeof calls[0]; i++) {
        (void)snprintf(in, sizeof in, F("%s"), calls[i]);
        for (size_t j = 0; j < sizeof modes / sizeof modes[0]; j++) {
            CHECK(inspects(in, R10, modes[j], UNKNOWN, "none; call 4"));
            CHECK(inspects(in, R32, modes[j], UNKNOWN, "none; abi-scope"));
        }
    }

    // int f(int a) { return g(a); }, g a #pragma inline_asm function of SETT
    // and ADDC R4,R0, as R10 copies it into -code=asmcode text: a witness
    // that stands on inline text, and the .ALIGN 4 below it stops the stack
    // rule
    static const char copied[] = S(".SECTION    PSG,CODE,ALIGN=4")
        F("                      SETT\n"
          " ADDC R4,R0 \n" ALIGN S("RTS") S("NOP") S(".END"));
    CHECK(inspects_lines(copied, 1, 7, R10, ASM, UNKNOWN,
                         "opcode sett 3; assembler-layout 5"));

    // R10's returns: a 16-byte frame freed with the last call's 8 bytes of
    // arguments by one ADD, past the call; an early return beside a call,
    // whose cleanup the exit's label keeps apart; FR, MACH and MACL saves; a
    // frame over 127 bytes; an interrupt's RTE; and _builtin_set_fpscr
    // between FR saves and their restores, which can change the restores'
    // width, so the rule loses SZ there
    static const struct {
        const char *in;
        const char *want;
    } returns[] = {
        {F(S("STS.L       PR,@-R15") S("ADD         #-16,R15")
               S("MOV.L       R4,@(12,R15)") S("MOV.L       @(12,R15),R3")
                   S("MOV.L       R3,@-R15") S("MOV.L       @(12,R15),R2")
                       S("MOV.L       L1,R3") S("MOV.L       R2,@-R15")
                           S("JSR         @R3") S("MOV.L       @(32,R15),R4") S(
                               "ADD         #24,R15") S("LDS.L       @R15+,PR")
                               S("RTS") S("NOP") "L1:\n" S(".DATA.L     _e6")),
         "none; call 10"},
        {F(S("MOV.L       R14,@-R15") S("MOV         R4,R14")
               S("STS.L       PR,@-R15") S("TST         R14,R14") S(
                   "ADD         #-4,R15") S("BT/S        L288")
                   S("MOV.L       R5,@R15") S("MOV         #0,R0") S(
                       "ADD         #4,R15") S("LDS.L       @R15+,PR") S("RTS")
                       S("MOV.L       @R15+,R14") "L288:\n" S(
                           "MOV.L       @R15,R2") S("MOV.L       R2,@-R15")
                           S("MOV.L       R14,@-R15") S(
                               "MOV.L       @(8,R15),R5") S("MOV.L       L1,R3")
                               S("JSR         @R3") S("MOV         R14,R4")
                                   S("ADD         #8,R15") "L289:\n" S(
                                       "ADD         #4,R15")
                                       S("LDS.L       @R15+,PR") S("RTS")
                                           S("MOV.L       @R15+,R14") "L1:\n" S(
                                               ".DATA.L     _e6")),
         "none; call 20"},
        {F(S("MOV.L       R14,@-R15") S("MOV.L       R13,@-R15") S(
             "FMOV.S      FR15,@-R15") S("FMOV.S      FR14,@-R15")
               S("STS.L       MACH,@-R15") S("STS.L       MACL,@-R15") S(
                   "ADD         #-4,R15") S("MOV.L       R4,@R15")
                   S("ADD         #4,R15") S("LDS.L       @R15+,MACL") S(
                       "LDS.L       @R15+,MACH") S("FMOV.S      @R15+,FR14")
                       S("FMOV.S      @R15+,FR15") S("MOV.L       @R15+,R13")
                           S("RTS") S("MOV.L       @R15+,R14")),
         "none"},
        {F(S("MOV.W       L1,R0") S("ADD         R0,R15")
               S("MOV         R15,R6") S("MOV.W       L1+2,R1")
                   S("MOV.L       @(R0,R6),R0") S("RTS")
                       S("ADD         R1,R15") "L1:\n" S(".DATA.W     H'FF60")
                           S(".DATA.W     H'00A0")),
         "none; stack-unknown 3"},
        {F(S("MOV.L       R3,@-R15") S("MOV.L       R2,@-R15")
               S("MOV.L       R1,@-R15") S("MOV.L       @R15+,R1")
                   S("MOV.L       @R15+,R2") S("MOV.L       @R15+,R3") S("RTE")
                       S("NOP")),
         "none"},
        {F(S("FMOV.S      FR15,@-R15") S("FMOV.S      FR14,@-R15")
               S("FMOV.S      FR13,@-R15") S("FMOV.S      FR12,@-R15")
                   S("ADD         #-20,R15") S("LDS         R4,FPSCR")
                       S("ADD         #20,R15") S("FMOV.S      @R15+,FR12") S(
                           "FMOV.S      @R15+,FR13") S("FMOV.S      @R15+,FR14")
                           S("RTS") S("FMOV.S      @R15+,FR15")),
         "none; stack-unknown 9"},
    };
    static const int szs[] = {UNKNOWN, SHCKIT_SZ_0, SHCKIT_SZ_1};
    for (size_t i = 0; i < sizeof returns / sizeof returns[0]; i++) {
        for (size_t j = 0; j < sizeof szs / sizeof szs[0]; j++) {
            CHECK(inspects(returns[i].in, R10, OBJ | ASM, szs[j],
                           returns[i].want));
        }
    }
}

static void test_inspect_all(void)
{
    // every witness, in line order and at one line in the rules table's
    // order: an instruction rule's at each instruction, the stack rule's
    // after each step that leaves R15 above its entry value, and its
    // fschg-pairs before stack-above-entry at one line, as the table lists
    // them
    CHECK(inspects_all(
        F(S("SETT") S("MOV.L       @R15+,R1") S("MOV.L       @(0,R4),R0")
              S("MOV.L       @R15+,R2") S("RTS") S("ADD         #-8,R15")),
        R10, ASM, UNKNOWN,
        "opcode sett 2, abi stack-above-entry 3 +4, operand "
        "zero-displacement 4, abi stack-above-entry 4 +4, abi "
        "stack-above-entry 5 +8"));
    CHECK(inspects_all(
        F(S("FSCHG") S("FMOV        @R15+,DR12") S("RTS") S("FSCHG")), R10, ASM,
        SHCKIT_SZ_0,
        "operand fschg-pairs 3, abi stack-above-entry 3 +8, abi "
        "stack-above-entry 5 +8"));

    // an instruction rule's witness before the stack rule's at one line and
    // kind, and a return's slot below the entry
    CHECK(inspects_all(F(S("LDS.L       @R15+,FPSCR") S("RTS") S("NOP")), R10,
                       ASM, UNKNOWN,
                       "abi fpscr-save 2, abi stack-above-entry 2 +4, abi "
                       "stack-above-entry 4 +4"));
    CHECK(inspects_all(F(S("ADD         #-4,R15") S("RTS") S("NOP")), R10, ASM,
                       UNKNOWN, "abi return-below-entry 4 -4"));

    // with SZ unknown, each witness both SZ find at one line, naming the
    // offset nearer the entry, and stack-unknown at line 5, whose witness is
    // SZ 1's alone
    static const char popped[] = F(S("FMOV.S      @R15+,FR4") S(
        "ADD         #-8,R15") S("RTS") S("ADD         #4,R15"));
    CHECK(inspects_all(popped, R10, ASM, SHCKIT_SZ_0,
                       "abi stack-above-entry 2 +4"));
    CHECK(inspects_all(popped, R10, ASM, SHCKIT_SZ_1,
                       "abi stack-above-entry 2 +8, abi stack-above-entry 5 "
                       "+4"));
    CHECK(inspects_all(popped, R10, ASM, UNKNOWN,
                       "abi stack-above-entry 2 +4; stack-unknown 5"));

    // no branch or return, even one reached above the entry, as the BT on
    // line 5 is from line 14
    CHECK(inspects_all(
        F(S("BRA         L3") S("NOP") "L1:\n" S("BT          L2") S("RTS")
              S("ADD         #-4,R15") "L2:\n" S("RTS")
                  S("ADD         #-4,R15") "L3:\n" S("MOV.L       @R15+,R1")
                      S("BRA         L1") S("NOP")),
        R10, ASM, UNKNOWN,
        "abi stack-above-entry 12 +4, abi stack-above-entry 14 +4"));
    CHECK(inspects_all(F(S("MOV.L       #1,R15") S("RTS") S("NOP")), R10, ASM,
                       UNKNOWN,
                       "operand small-constant 2 #1, abi stack-written 2; "
                       "stack-unknown 2"));

    // witnesses on both sides of a limit, none and a limit, and a release no
    // rule holds for
    CHECK(inspects_all(
        F(S("SETT") S("BSR         _g") S("NOP") S("SETT") S("RTS") S("NOP")),
        R10, ASM, UNKNOWN, "opcode sett 2, opcode sett 5; call 3"));
    CHECK(inspects_all(F(S("BSR         _g") S("NOP") S("RTS") S("NOP")), R10,
                       ASM, UNKNOWN, "none; call 2"));
    CHECK(inspects_all(F(S("SETT") S("RTS") S("NOP")), "6.0AD", ASM, UNKNOWN,
                       "none; producer-scope"));
}

static bool inspect_refuses(const struct shckit_text *text,
                            const struct shckit_producer *producer,
                            const struct shckit_function *function, int error)
{
    static const struct shckit_inspection none = {
        {SHCKIT_NO_WITNESS, NULL, SIZE_MAX, 0}, NULL, SIZE_MAX};
    struct shckit_inspection result;
    struct shckit_inspection all;
    struct shckit_witness stale;
    struct shckit_witness *found = &stale;
    size_t count = 1;
    memset(&result, 0xff, sizeof result);
    memset(&all, 0xff, sizeof all);
    return shckit_inspect(text, producer, function, &result) == error &&
           same_inspection(&result, &none) &&
           shckit_inspect_all(text, producer, function, &all, &found, &count) ==
               error &&
           same_inspection(&all, &none) && found == NULL && count == 0;
}

static void test_inspect_contracts(void)
{
    // a function inside a text, one whose last line is a branch with its
    // slot past it, then ranges, sz and modes inspect can't take, the .END
    // line among them, each leaving no witness or limit
    static const struct shckit_function bad[] = {
        {3, 3, SHCKIT_SZ_UNKNOWN},
        {4, 2, SHCKIT_SZ_UNKNOWN},
        {0, SIZE_MAX, SHCKIT_SZ_UNKNOWN},
        {SIZE_MAX, 1, SHCKIT_SZ_UNKNOWN},
        {0, 3, -1},
        {0, 3, SHCKIT_SZ_1 + 1},
        {4, 5, SHCKIT_SZ_UNKNOWN},
    };
    struct shckit_text *text =
        read_text(F(S("RTS") S("ADD         #8,R15")) "_end:\n" S(".END"));
    struct shckit_producer producer = {R10, ASM};
    struct shckit_function function = {0, 3, SHCKIT_SZ_UNKNOWN};
    struct shckit_inspection result;
    if (CHECK(text != NULL)) {
        CHECK(shckit_inspect(text, &producer, &function, &result) ==
                  SHCKIT_OK &&
              result.witness.kind == SHCKIT_ABI && result.witness.index == 2 &&
              result.witness.value == 8 && result.incomplete == NULL);
        function.end = 2;
        CHECK(shckit_inspect(text, &producer, &function, &result) ==
                  SHCKIT_OK &&
              result.witness.kind == SHCKIT_NO_WITNESS &&
              result.incomplete != NULL &&
              strcmp(result.incomplete, "delay-slot") == 0 &&
              result.incomplete_index == 1);
        function.end = 3;
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            CHECK(inspect_refuses(text, &producer, &bad[i],
                                  SHCKIT_INSPECT_INPUT));
        }
        producer.modes = 0;
        CHECK(
            inspect_refuses(text, &producer, &function, SHCKIT_INSPECT_INPUT));
        producer.modes = 4;
        CHECK(
            inspect_refuses(text, &producer, &function, SHCKIT_INSPECT_INPUT));
    }
    shckit_free(text);

    // a text check refuses
    text = read_text(F(S("FLURB       R0") S("RTS") S("NOP")));
    producer.modes = ASM;
    if (CHECK(text != NULL)) {
        CHECK(inspect_refuses(text, &producer, &function, SHCKIT_CHECK_FAILED));
    }
    shckit_free(text);
}

// translate's code and data sections, as it writes .SECTION P,CODE,ALIGN=4
// and .SECTION D,DATA,ALIGN=4.
#define GCODE S(".section    P,\"ax\",@progbits") S(".balign     4")
#define GDATA S(".section    D,\"aw\",@progbits") S(".balign     4")

static void show_translation(const struct shckit_text *text)
{
    size_t size = 0;
    size_t line = 0;
    int error = shckit_translate(text, NULL, 0, &size, &line);
    unsigned char *bytes = error == SHCKIT_OK ? malloc(size + 1) : NULL;
    if (bytes != NULL &&
        shckit_translate(text, bytes, size, NULL, NULL) == SHCKIT_OK) {
        printf("  got:\n%.*s", (int)size, (const char *)bytes);
    } else {
        printf("  refused: \"%s\" at line %zu\n", shckit_strerror(error), line);
    }
    free(bytes);
}

static bool translates(const char *in, const char *want)
{
    // translate turns in into exactly want, naming no line: a measure gives
    // its size, a buffer a byte short gets SHCKIT_FULL, the size, no line and
    // nothing written, and one that fits gets the text and nothing past it
    struct shckit_text *text = read_text(in);
    size_t len = strlen(want);
    size_t size = SIZE_MAX;
    size_t line = SIZE_MAX;
    size_t short_size = 0;
    unsigned char *bytes = malloc(len + 1);
    bool ok = false;
    if (bytes == NULL) {
        fputs("test: out of memory\n", stderr);
        exit(1);
    }
    memset(bytes, '#', len + 1);
    ok = text != NULL &&
         shckit_translate(text, NULL, 0, &size, &line) == SHCKIT_OK &&
         size == len && line == 0 && len != 0;
    line = SIZE_MAX;
    ok = ok &&
         shckit_translate(text, bytes, len - 1, &short_size, &line) ==
             SHCKIT_FULL &&
         short_size == len && line == 0;
    for (size_t i = 0; ok && i <= len; i++) {
        ok = bytes[i] == '#';
    }
    ok = ok && shckit_translate(text, bytes, len, NULL, NULL) == SHCKIT_OK &&
         memcmp(bytes, want, len) == 0 && bytes[len] == '#';
    if (!ok && text != NULL) {
        show_translation(text);
    }
    free(bytes);
    shckit_free(text);
    return ok;
}

static bool translate_refuses(const char *in, int error, size_t at)
{
    // translate refuses in by error, naming line at, and gives no size and
    // writes nothing
    struct shckit_text *text = read_text(in);
    unsigned char bytes[1024];
    size_t size = SIZE_MAX;
    size_t line = SIZE_MAX;
    int got = SHCKIT_NOMEM;
    bool ok = false;
    memset(bytes, '#', sizeof bytes);
    if (text != NULL) {
        got = shckit_translate(text, bytes, sizeof bytes, &size, &line);
    }
    ok = got == error && line == at && size == 0;
    for (size_t i = 0; ok && i < sizeof bytes; i++) {
        ok = bytes[i] == '#';
    }
    if (!ok) {
        printf("  got \"%s\" at line %zu\n", shckit_strerror(got), line);
    }
    shckit_free(text);
    return ok;
}

static void test_translate(void)
{
    // an instruction keeps its spelling, case and columns but for the size
    // GNU as writes, a number takes GNU as's radix, a comment's ; is !, and
    // a line keeps its fields' columns or follows the field before them past
    // one blank
    static const char comments[] = "; \x83\x5c a comment\r\n"
                                   "          .SECTION    P,CODE,ALIGN=4\n"
                                   "\n"
                                   "          NOP ; a NOP\n"
                                   "_f:  ; f\n"
                                   "    CLRMAC\t; inline\n";
    static const char comments_gnu[] =
        "! \x83\x5c a comment\n"
        "          .section    P,\"ax\",@progbits\n"
        "          .balign     4\n"
        "\n"
        "          NOP ! a NOP\n"
        "_f:  ! f\n"
        "    CLRMAC ! inline\n";
    CHECK(translates(CODE S("mov         @r4+,r5") S("dmuls       r1,r2")
                         S("Lds.l       @R15+,pr"),
                     GCODE S("mov.l       @r4+,r5") S("dmuls.l     r1,r2")
                         S("Lds.l       @R15+,pr")));
    CHECK(translates(CODE S(".DATA.L     H'1F,B'101,Q'17,D'12,012"),
                     GCODE S(".long       0x1F,0b101,017,12,12")));
    CHECK(translates(CODE S("MOV         #h'7f,R1") S("ADD         #-D'04,R15")
                         S("MOV.L       @( H'10 , R4 ),R0"),
                     GCODE S("MOV         #0x7f,R1") S("ADD         #-4,R15")
                         S("MOV.L       @( 0x10 , R4 ),R0")));
    CHECK(translates(comments, comments_gnu));

    // a ?label is named for its block: the label that opens it, or the
    // number of the .SECTION line that does, 0 above them all
    static const char blocks[] = "?a:\n"
                                 "          BRA         ?a\n"
                                 "          NOP\n"
                                 "          .SECTION    P,CODE,ALIGN=4\n"
                                 "?a:  ; a\n"
                                 "          BRA         ?a ; a branch\n"
                                 "          NOP\n";
    static const char blocks_gnu[] =
        "          .section    P,\"ax\",@progbits\n"
        "          .balign     4\n"
        ".L.0.a:\n"
        "          BRA         .L.0.a\n"
        "          NOP\n"
        "          .section    P\n"
        ".L.1.a: ! a\n"
        "          BRA         .L.1.a ! a branch\n"
        "          NOP\n";
    CHECK(translates(
        CODE "_f:\n" S("BT          ?out") "?out:\n" S("RTS") S("NOP"), GCODE
        "_f:\n" S("BT          .L_f.out") ".L_f.out:\n" S("RTS") S("NOP")));
    CHECK(translates(blocks, blocks_gnu));

    // data, a .RES's bytes, and a code .ALIGN's NOPs
    static const char data[] = "          .IMPORT     _x\n"
                               "          .EXPORT     _d\n"
                               "          .SECTION    D,DATA,ALIGN=4\n"
                               "_d:       .DATA.B     1,-1\n"
                               "          .DATA.W     H'8000,_x-_d\n"
                               "          .DATAB.B    3,H'AA\n"
                               "          .ALIGN      4\n"
                               "          .SDATA      \"a;b\",\"\"\n"
                               "          .ALIGN      4\n"
                               "          .FDATA.S    F'1.5S3,f'-.5s-1\n"
                               "N:        .EQU        2\n"
                               "          .RES.B      2\n"
                               "          .RES.W      1\n"
                               "          .RES.L      N\n"
                               "          .END\n"
                               "; done\n";
    static const char data_gnu[] = "          .extern     _x\n"
                                   "          .global     _d\n"
                                   "          .section    D,\"aw\",@progbits\n"
                                   "          .balign     4\n"
                                   "_d:       .byte       1,-1\n"
                                   "          .short      0x8000,_x-_d\n"
                                   "          .fill       3,1,0xAA\n"
                                   "          .balign     4\n"
                                   "          .ascii      \"a;b\",\"\"\n"
                                   "          .balign     4\n"
                                   "          .float      1.5e3,-.5e-1\n"
                                   "          .equ        N,2\n"
                                   "          .space      2\n"
                                   "          .space      (1)*2\n"
                                   "          .space      (N)*4\n"
                                   "          .end\n"
                                   "! done\n";
    CHECK(translates(data, data_gnu));
    CHECK(translates(CODE S("NOP") S(".ALIGN      16"),
                     GCODE S("NOP") S(".balignw    16,0x0009")));

    // a lone 0 before an .FDATA.S constant's S is 0., since GNU as reads 0e
    // as a radix's prefix
    CHECK(translates(DATA S(".FDATA.S    F'0S5,F'-0s2,F'00S1,F'.0S1,F'10S1"),
                     GDATA S(".float      0.e5,-0.e2,00e1,.0e1,10e1")));

    // a section's first line gives its flags, @nobits where no line of it
    // writes a byte, and its alignment; a section named again is resumed.
    // An empty string writes no byte.
    static const char sections[] = "          .SECTION    P,CODE,ALIGN=4\n"
                                   "          NOP\n"
                                   "          .SECTION    B,DATA,ALIGN=8\n"
                                   "_b:       .RES.L      1\n"
                                   "          .ALIGN      8\n"
                                   "          .SECTION    P,CODE,ALIGN=4\n"
                                   "          NOP\n"
                                   "          .SECTION    B,DATA,ALIGN=8\n"
                                   "          .RES.W      1\n";
    static const char sections_gnu[] =
        "          .section    P,\"ax\",@progbits\n"
        "          .balign     4\n"
        "          NOP\n"
        "          .section    B,\"aw\",@nobits\n"
        "          .balign     8\n"
        "_b:       .space      (1)*4\n"
        "          .balign     8\n"
        "          .section    P\n"
        "          NOP\n"
        "          .section    B\n"
        "          .space      (1)*2\n";
    CHECK(translates(sections, sections_gnu));
    CHECK(translates(DATA S(".SDATA      \"\",\"\"") S(".RES.B      8"),
                     S(".section    D,\"aw\",@nobits") S(".balign     4")
                         S(".ascii      \"\",\"\"") S(".space      8")));

    // the lines above any .SECTION are in P, written where the first of them
    // takes a place: a label, an instruction, data or an .ALIGN
    CHECK(translates("; f\n" S(".EXPORT     _f") "_f:\n" S("RTS") S("NOP"),
                     "! f\n" S(".global     _f") GCODE "_f:\n" S("RTS")
                         S("NOP")));
    CHECK(translates(S(".IMPORT     _x") S(".END"),
                     S(".extern     _x") S(".end")));
    CHECK(translates(S("NOP"), GCODE S("NOP")));
    CHECK(translates(S(".DATA.L     1"), GCODE S(".long       1")));
    CHECK(translates(S(".ALIGN      4"), GCODE S(".balignw    4,0x0009")));

    // expressions as written, but for the numbers in them and a unary ^,
    // which asmsh reads as ~. A sign binds to its operand in both. A >> or /
    // stays as written though GNU as computes it in 64 bits, a limit.
    CHECK(translates(DATA S(".DATA.L     (1+2)<<D'3,~1&H'F,-(3*4)/2,_d+4")
                         S(".DATA.L     -1<<2"),
                     GDATA S(".long       (1+2)<<3,~1&0xF,-(3*4)/2,_d+4")
                         S(".long       -1<<2")));
    CHECK(translates(DATA S(".DATA.L     ^3,1+^2,(^H'F0)"),
                     GDATA S(".long       ~3,1+~2,(~0xF0)")));
    CHECK(translates(DATA S(".DATA.L     H'80000000>>4,(H'FFFFFFFF+2)/2"),
                     GDATA S(".long       0x80000000>>4,(0xFFFFFFFF+2)/2")));
}

static void test_translate_forms(void)
{
    // each form of the opcode table, in each size it takes, as translate
    // writes it: in the size GNU as writes for the same word. A load of a
    // # literal and @(disp,PC) are tested apart.
    static const char *const forms[][2] = {
        {"ADD         #4,R5", "ADD         #4,R5"},
        {"ADD         R3,R5", "ADD         R3,R5"},
        {"ADD.L       #4,R5", "ADD         #4,R5"},
        {"ADD.L       R3,R5", "ADD         R3,R5"},
        {"ADDC        R3,R5", "ADDC        R3,R5"},
        {"ADDC.L      R3,R5", "ADDC        R3,R5"},
        {"ADDV        R3,R5", "ADDV        R3,R5"},
        {"ADDV.L      R3,R5", "ADDV        R3,R5"},
        {"CMP/GE      R3,R5", "CMP/GE      R3,R5"},
        {"CMP/GE.L    R3,R5", "CMP/GE      R3,R5"},
        {"CMP/GT      R3,R5", "CMP/GT      R3,R5"},
        {"CMP/GT.L    R3,R5", "CMP/GT      R3,R5"},
        {"CMP/HI      R3,R5", "CMP/HI      R3,R5"},
        {"CMP/HI.L    R3,R5", "CMP/HI      R3,R5"},
        {"CMP/HS      R3,R5", "CMP/HS      R3,R5"},
        {"CMP/HS.L    R3,R5", "CMP/HS      R3,R5"},
        {"CMP/STR     R3,R5", "CMP/STR     R3,R5"},
        {"CMP/STR.L   R3,R5", "CMP/STR     R3,R5"},
        {"DIV0S       R3,R5", "DIV0S       R3,R5"},
        {"DIV0S.L     R3,R5", "DIV0S       R3,R5"},
        {"DIV1        R3,R5", "DIV1        R3,R5"},
        {"DIV1.L      R3,R5", "DIV1        R3,R5"},
        {"NEG         R3,R5", "NEG         R3,R5"},
        {"NEG.L       R3,R5", "NEG         R3,R5"},
        {"NEGC        R3,R5", "NEGC        R3,R5"},
        {"NEGC.L      R3,R5", "NEGC        R3,R5"},
        {"NOT         R3,R5", "NOT         R3,R5"},
        {"NOT.L       R3,R5", "NOT         R3,R5"},
        {"SHAD        R3,R5", "SHAD        R3,R5"},
        {"SHAD.L      R3,R5", "SHAD        R3,R5"},
        {"SHLD        R3,R5", "SHLD        R3,R5"},
        {"SHLD.L      R3,R5", "SHLD        R3,R5"},
        {"SUB         R3,R5", "SUB         R3,R5"},
        {"SUB.L       R3,R5", "SUB         R3,R5"},
        {"SUBC        R3,R5", "SUBC        R3,R5"},
        {"SUBC.L      R3,R5", "SUBC        R3,R5"},
        {"SUBV        R3,R5", "SUBV        R3,R5"},
        {"SUBV.L      R3,R5", "SUBV        R3,R5"},
        {"XTRCT       R3,R5", "XTRCT       R3,R5"},
        {"XTRCT.L     R3,R5", "XTRCT       R3,R5"},
        {"DMULS       R3,R5", "DMULS.L     R3,R5"},
        {"DMULS.L     R3,R5", "DMULS.L     R3,R5"},
        {"DMULU       R3,R5", "DMULU.L     R3,R5"},
        {"DMULU.L     R3,R5", "DMULU.L     R3,R5"},
        {"MUL         R3,R5", "MUL.L       R3,R5"},
        {"MUL.L       R3,R5", "MUL.L       R3,R5"},
        {"AND         #4,@(R0,GBR)", "AND.B       #4,@(R0,GBR)"},
        {"AND.B       #4,@(R0,GBR)", "AND.B       #4,@(R0,GBR)"},
        {"OR          #4,@(R0,GBR)", "OR.B        #4,@(R0,GBR)"},
        {"OR.B        #4,@(R0,GBR)", "OR.B        #4,@(R0,GBR)"},
        {"TST         #4,@(R0,GBR)", "TST.B       #4,@(R0,GBR)"},
        {"TST.B       #4,@(R0,GBR)", "TST.B       #4,@(R0,GBR)"},
        {"XOR         #4,@(R0,GBR)", "XOR.B       #4,@(R0,GBR)"},
        {"XOR.B       #4,@(R0,GBR)", "XOR.B       #4,@(R0,GBR)"},
        {"AND         #4,R0", "AND         #4,R0"},
        {"AND         R3,R5", "AND         R3,R5"},
        {"AND.L       #4,R0", "AND         #4,R0"},
        {"AND.L       R3,R5", "AND         R3,R5"},
        {"CMP/EQ      #4,R0", "CMP/EQ      #4,R0"},
        {"CMP/EQ      R3,R5", "CMP/EQ      R3,R5"},
        {"CMP/EQ.L    #4,R0", "CMP/EQ      #4,R0"},
        {"CMP/EQ.L    R3,R5", "CMP/EQ      R3,R5"},
        {"OR          #4,R0", "OR          #4,R0"},
        {"OR          R3,R5", "OR          R3,R5"},
        {"OR.L        #4,R0", "OR          #4,R0"},
        {"OR.L        R3,R5", "OR          R3,R5"},
        {"TST         #4,R0", "TST         #4,R0"},
        {"TST         R3,R5", "TST         R3,R5"},
        {"TST.L       #4,R0", "TST         #4,R0"},
        {"TST.L       R3,R5", "TST         R3,R5"},
        {"XOR         #4,R0", "XOR         #4,R0"},
        {"XOR         R3,R5", "XOR         R3,R5"},
        {"XOR.L       #4,R0", "XOR         #4,R0"},
        {"XOR.L       R3,R5", "XOR         R3,R5"},
        {"BF          ?t", "BF          .L.1.t"},
        {"BT          ?t", "BT          .L.1.t"},
        {"BF/S        ?t", "BF/S        .L.1.t"},
        {"BSR         ?t", "BSR         .L.1.t"},
        {"BT/S        ?t", "BT/S        .L.1.t"},
        {"BRA         ?t", "BRA         .L.1.t"},
        {"BRAF        R5", "BRAF        R5"},
        {"BSRF        R5", "BSRF        R5"},
        {"CLRMAC", "CLRMAC"},
        {"CLRS", "CLRS"},
        {"CLRT", "CLRT"},
        {"DIV0U", "DIV0U"},
        {"FRCHG", "FRCHG"},
        {"FSCHG", "FSCHG"},
        {"LDTLB", "LDTLB"},
        {"NOP", "NOP"},
        {"SETS", "SETS"},
        {"SETT", "SETT"},
        {"SLEEP", "SLEEP"},
        {"CMP/PL      R5", "CMP/PL      R5"},
        {"CMP/PL.L    R5", "CMP/PL      R5"},
        {"CMP/PZ      R5", "CMP/PZ      R5"},
        {"CMP/PZ.L    R5", "CMP/PZ      R5"},
        {"MOVT        R5", "MOVT        R5"},
        {"MOVT.L      R5", "MOVT        R5"},
        {"ROTCL       R5", "ROTCL       R5"},
        {"ROTCL.L     R5", "ROTCL       R5"},
        {"ROTCR       R5", "ROTCR       R5"},
        {"ROTCR.L     R5", "ROTCR       R5"},
        {"ROTL        R5", "ROTL        R5"},
        {"ROTL.L      R5", "ROTL        R5"},
        {"ROTR        R5", "ROTR        R5"},
        {"ROTR.L      R5", "ROTR        R5"},
        {"SHAL        R5", "SHAL        R5"},
        {"SHAL.L      R5", "SHAL        R5"},
        {"SHAR        R5", "SHAR        R5"},
        {"SHAR.L      R5", "SHAR        R5"},
        {"SHLL        R5", "SHLL        R5"},
        {"SHLL.L      R5", "SHLL        R5"},
        {"SHLL16      R5", "SHLL16      R5"},
        {"SHLL16.L    R5", "SHLL16      R5"},
        {"SHLL2       R5", "SHLL2       R5"},
        {"SHLL2.L     R5", "SHLL2       R5"},
        {"SHLL8       R5", "SHLL8       R5"},
        {"SHLL8.L     R5", "SHLL8       R5"},
        {"SHLR        R5", "SHLR        R5"},
        {"SHLR.L      R5", "SHLR        R5"},
        {"SHLR16      R5", "SHLR16      R5"},
        {"SHLR16.L    R5", "SHLR16      R5"},
        {"SHLR2       R5", "SHLR2       R5"},
        {"SHLR2.L     R5", "SHLR2       R5"},
        {"SHLR8       R5", "SHLR8       R5"},
        {"SHLR8.L     R5", "SHLR8       R5"},
        {"DT          R5", "DT          R5"},
        {"EXTS        R3,R5", "EXTS.W      R3,R5"},
        {"EXTS.B      R3,R5", "EXTS.B      R3,R5"},
        {"EXTS.W      R3,R5", "EXTS.W      R3,R5"},
        {"EXTU        R3,R5", "EXTU.W      R3,R5"},
        {"EXTU.B      R3,R5", "EXTU.B      R3,R5"},
        {"EXTU.W      R3,R5", "EXTU.W      R3,R5"},
        {"SWAP        R3,R5", "SWAP.W      R3,R5"},
        {"SWAP.B      R3,R5", "SWAP.B      R3,R5"},
        {"SWAP.W      R3,R5", "SWAP.W      R3,R5"},
        {"FABS        DR4", "FABS        DR4"},
        {"FABS.D      DR4", "FABS        DR4"},
        {"FNEG        DR4", "FNEG        DR4"},
        {"FNEG.D      DR4", "FNEG        DR4"},
        {"FSQRT       DR4", "FSQRT       DR4"},
        {"FSQRT.D     DR4", "FSQRT       DR4"},
        {"FABS        FR5", "FABS        FR5"},
        {"FABS.S      FR5", "FABS        FR5"},
        {"FLDI0       FR5", "FLDI0       FR5"},
        {"FLDI0.S     FR5", "FLDI0       FR5"},
        {"FLDI1       FR5", "FLDI1       FR5"},
        {"FLDI1.S     FR5", "FLDI1       FR5"},
        {"FNEG        FR5", "FNEG        FR5"},
        {"FNEG.S      FR5", "FNEG        FR5"},
        {"FSQRT       FR5", "FSQRT       FR5"},
        {"FSQRT.S     FR5", "FSQRT       FR5"},
        {"FSRRA       FR5", "FSRRA       FR5"},
        {"FSRRA.S     FR5", "FSRRA       FR5"},
        {"FADD        DR2,DR4", "FADD        DR2,DR4"},
        {"FADD.D      DR2,DR4", "FADD        DR2,DR4"},
        {"FCMP/EQ     DR2,DR4", "FCMP/EQ     DR2,DR4"},
        {"FCMP/EQ.D   DR2,DR4", "FCMP/EQ     DR2,DR4"},
        {"FCMP/GT     DR2,DR4", "FCMP/GT     DR2,DR4"},
        {"FCMP/GT.D   DR2,DR4", "FCMP/GT     DR2,DR4"},
        {"FDIV        DR2,DR4", "FDIV        DR2,DR4"},
        {"FDIV.D      DR2,DR4", "FDIV        DR2,DR4"},
        {"FMUL        DR2,DR4", "FMUL        DR2,DR4"},
        {"FMUL.D      DR2,DR4", "FMUL        DR2,DR4"},
        {"FSUB        DR2,DR4", "FSUB        DR2,DR4"},
        {"FSUB.D      DR2,DR4", "FSUB        DR2,DR4"},
        {"FADD        FR3,FR5", "FADD        FR3,FR5"},
        {"FADD.S      FR3,FR5", "FADD        FR3,FR5"},
        {"FCMP/EQ     FR3,FR5", "FCMP/EQ     FR3,FR5"},
        {"FCMP/EQ.S   FR3,FR5", "FCMP/EQ     FR3,FR5"},
        {"FCMP/GT     FR3,FR5", "FCMP/GT     FR3,FR5"},
        {"FCMP/GT.S   FR3,FR5", "FCMP/GT     FR3,FR5"},
        {"FDIV        FR3,FR5", "FDIV        FR3,FR5"},
        {"FDIV.S      FR3,FR5", "FDIV        FR3,FR5"},
        {"FMUL        FR3,FR5", "FMUL        FR3,FR5"},
        {"FMUL.S      FR3,FR5", "FMUL        FR3,FR5"},
        {"FSUB        FR3,FR5", "FSUB        FR3,FR5"},
        {"FSUB.S      FR3,FR5", "FSUB        FR3,FR5"},
        {"FCNVDS      DR2,FPUL", "FCNVDS      DR2,FPUL"},
        {"FCNVDS.D    DR2,FPUL", "FCNVDS      DR2,FPUL"},
        {"FTRC        DR2,FPUL", "FTRC        DR2,FPUL"},
        {"FTRC.D      DR2,FPUL", "FTRC        DR2,FPUL"},
        {"FCNVSD      FPUL,DR4", "FCNVSD      FPUL,DR4"},
        {"FCNVSD.D    FPUL,DR4", "FCNVSD      FPUL,DR4"},
        {"FLOAT       FPUL,DR4", "FLOAT       FPUL,DR4"},
        {"FLOAT.D     FPUL,DR4", "FLOAT       FPUL,DR4"},
        {"FIPR        FV0,FV4", "FIPR        FV0,FV4"},
        {"FIPR.S      FV0,FV4", "FIPR        FV0,FV4"},
        {"FLDS        FR3,FPUL", "FLDS        FR3,FPUL"},
        {"FLDS.S      FR3,FPUL", "FLDS        FR3,FPUL"},
        {"FTRC        FR3,FPUL", "FTRC        FR3,FPUL"},
        {"FTRC.S      FR3,FPUL", "FTRC        FR3,FPUL"},
        {"FLOAT       FPUL,FR5", "FLOAT       FPUL,FR5"},
        {"FLOAT.S     FPUL,FR5", "FLOAT       FPUL,FR5"},
        {"FSTS        FPUL,FR5", "FSTS        FPUL,FR5"},
        {"FSTS.S      FPUL,FR5", "FSTS        FPUL,FR5"},
        {"FMAC        FR0,FR3,FR5", "FMAC        FR0,FR3,FR5"},
        {"FMAC.S      FR0,FR3,FR5", "FMAC        FR0,FR3,FR5"},
        {"FMOV        @(R0,R3),DR4", "FMOV        @(R0,R3),DR4"},
        {"FMOV        @(R0,R3),XD4", "FMOV        @(R0,R3),XD4"},
        {"FMOV        @R3,DR4", "FMOV        @R3,DR4"},
        {"FMOV        @R3,XD4", "FMOV        @R3,XD4"},
        {"FMOV        @R3+,DR4", "FMOV        @R3+,DR4"},
        {"FMOV        @R3+,XD4", "FMOV        @R3+,XD4"},
        {"FMOV        DR2,@(R0,R5)", "FMOV        DR2,@(R0,R5)"},
        {"FMOV        DR2,@-R5", "FMOV        DR2,@-R5"},
        {"FMOV        DR2,@R5", "FMOV        DR2,@R5"},
        {"FMOV        XD2,@(R0,R5)", "FMOV        XD2,@(R0,R5)"},
        {"FMOV        XD2,@-R5", "FMOV        XD2,@-R5"},
        {"FMOV        XD2,@R5", "FMOV        XD2,@R5"},
        {"FMOV.D      @(R0,R3),DR4", "FMOV.D      @(R0,R3),DR4"},
        {"FMOV.D      @(R0,R3),XD4", "FMOV.D      @(R0,R3),XD4"},
        {"FMOV.D      @R3,DR4", "FMOV.D      @R3,DR4"},
        {"FMOV.D      @R3,XD4", "FMOV.D      @R3,XD4"},
        {"FMOV.D      @R3+,DR4", "FMOV.D      @R3+,DR4"},
        {"FMOV.D      @R3+,XD4", "FMOV.D      @R3+,XD4"},
        {"FMOV.D      DR2,@(R0,R5)", "FMOV.D      DR2,@(R0,R5)"},
        {"FMOV.D      DR2,@-R5", "FMOV.D      DR2,@-R5"},
        {"FMOV.D      DR2,@R5", "FMOV.D      DR2,@R5"},
        {"FMOV.D      XD2,@(R0,R5)", "FMOV.D      XD2,@(R0,R5)"},
        {"FMOV.D      XD2,@-R5", "FMOV.D      XD2,@-R5"},
        {"FMOV.D      XD2,@R5", "FMOV.D      XD2,@R5"},
        {"FMOV        DR2,DR4", "FMOV        DR2,DR4"},
        {"FMOV        DR2,XD4", "FMOV        DR2,XD4"},
        {"FMOV        XD2,DR4", "FMOV        XD2,DR4"},
        {"FMOV        XD2,XD4", "FMOV        XD2,XD4"},
        {"FMOV.D      DR2,DR4", "FMOV        DR2,DR4"},
        {"FMOV.D      DR2,XD4", "FMOV        DR2,XD4"},
        {"FMOV.D      XD2,DR4", "FMOV        XD2,DR4"},
        {"FMOV.D      XD2,XD4", "FMOV        XD2,XD4"},
        {"FMOV        @(R0,R3),FR5", "FMOV        @(R0,R3),FR5"},
        {"FMOV        @R3,FR5", "FMOV        @R3,FR5"},
        {"FMOV        @R3+,FR5", "FMOV        @R3+,FR5"},
        {"FMOV        FR3,@(R0,R5)", "FMOV        FR3,@(R0,R5)"},
        {"FMOV        FR3,@-R5", "FMOV        FR3,@-R5"},
        {"FMOV        FR3,@R5", "FMOV        FR3,@R5"},
        {"FMOV.S      @(R0,R3),FR5", "FMOV.S      @(R0,R3),FR5"},
        {"FMOV.S      @R3,FR5", "FMOV.S      @R3,FR5"},
        {"FMOV.S      @R3+,FR5", "FMOV.S      @R3+,FR5"},
        {"FMOV.S      FR3,@(R0,R5)", "FMOV.S      FR3,@(R0,R5)"},
        {"FMOV.S      FR3,@-R5", "FMOV.S      FR3,@-R5"},
        {"FMOV.S      FR3,@R5", "FMOV.S      FR3,@R5"},
        {"FMOV        FR3,FR5", "FMOV        FR3,FR5"},
        {"FMOV.S      FR3,FR5", "FMOV        FR3,FR5"},
        {"FSCA        FPUL,DR4", "FSCA        FPUL,DR4"},
        {"FSCA.S      FPUL,DR4", "FSCA        FPUL,DR4"},
        {"FTRV        XMTRX,FV4", "FTRV        XMTRX,FV4"},
        {"FTRV.S      XMTRX,FV4", "FTRV        XMTRX,FV4"},
        {"JMP         @R5", "JMP         @R5"},
        {"JSR         @R5", "JSR         @R5"},
        {"LDC         @R3+,DBR", "LDC.L       @R3+,DBR"},
        {"LDC         @R3+,GBR", "LDC.L       @R3+,GBR"},
        {"LDC         @R3+,R5_BANK", "LDC.L       @R3+,R5_BANK"},
        {"LDC         @R3+,SPC", "LDC.L       @R3+,SPC"},
        {"LDC         @R3+,SSR", "LDC.L       @R3+,SSR"},
        {"LDC         @R3+,VBR", "LDC.L       @R3+,VBR"},
        {"LDC.L       @R3+,DBR", "LDC.L       @R3+,DBR"},
        {"LDC.L       @R3+,GBR", "LDC.L       @R3+,GBR"},
        {"LDC.L       @R3+,R5_BANK", "LDC.L       @R3+,R5_BANK"},
        {"LDC.L       @R3+,SPC", "LDC.L       @R3+,SPC"},
        {"LDC.L       @R3+,SSR", "LDC.L       @R3+,SSR"},
        {"LDC.L       @R3+,VBR", "LDC.L       @R3+,VBR"},
        {"LDC         R3,DBR", "LDC         R3,DBR"},
        {"LDC         R3,GBR", "LDC         R3,GBR"},
        {"LDC         R3,R5_BANK", "LDC         R3,R5_BANK"},
        {"LDC         R3,SPC", "LDC         R3,SPC"},
        {"LDC         R3,SSR", "LDC         R3,SSR"},
        {"LDC         R3,VBR", "LDC         R3,VBR"},
        {"LDC.L       R3,DBR", "LDC         R3,DBR"},
        {"LDC.L       R3,GBR", "LDC         R3,GBR"},
        {"LDC.L       R3,R5_BANK", "LDC         R3,R5_BANK"},
        {"LDC.L       R3,SPC", "LDC         R3,SPC"},
        {"LDC.L       R3,SSR", "LDC         R3,SSR"},
        {"LDC.L       R3,VBR", "LDC         R3,VBR"},
        {"LDC         @R3+,SR", "LDC.L       @R3+,SR"},
        {"LDC.L       @R3+,SR", "LDC.L       @R3+,SR"},
        {"LDC         R3,SR", "LDC         R3,SR"},
        {"LDC.L       R3,SR", "LDC         R3,SR"},
        {"LDS         @R3+,FPSCR", "LDS.L       @R3+,FPSCR"},
        {"LDS         @R3+,FPUL", "LDS.L       @R3+,FPUL"},
        {"LDS         @R3+,MACH", "LDS.L       @R3+,MACH"},
        {"LDS         @R3+,MACL", "LDS.L       @R3+,MACL"},
        {"LDS         @R3+,PR", "LDS.L       @R3+,PR"},
        {"LDS.L       @R3+,FPSCR", "LDS.L       @R3+,FPSCR"},
        {"LDS.L       @R3+,FPUL", "LDS.L       @R3+,FPUL"},
        {"LDS.L       @R3+,MACH", "LDS.L       @R3+,MACH"},
        {"LDS.L       @R3+,MACL", "LDS.L       @R3+,MACL"},
        {"LDS.L       @R3+,PR", "LDS.L       @R3+,PR"},
        {"LDS         R3,FPSCR", "LDS         R3,FPSCR"},
        {"LDS         R3,FPUL", "LDS         R3,FPUL"},
        {"LDS         R3,MACH", "LDS         R3,MACH"},
        {"LDS         R3,MACL", "LDS         R3,MACL"},
        {"LDS         R3,PR", "LDS         R3,PR"},
        {"LDS.L       R3,FPSCR", "LDS         R3,FPSCR"},
        {"LDS.L       R3,FPUL", "LDS         R3,FPUL"},
        {"LDS.L       R3,MACH", "LDS         R3,MACH"},
        {"LDS.L       R3,MACL", "LDS         R3,MACL"},
        {"LDS.L       R3,PR", "LDS         R3,PR"},
        {"MAC         @R3+,@R5+", "MAC.W       @R3+,@R5+"},
        {"MAC.W       @R3+,@R5+", "MAC.W       @R3+,@R5+"},
        {"MAC.L       @R3+,@R5+", "MAC.L       @R3+,@R5+"},
        {"MOV         #4,R5", "MOV         #4,R5"},
        {"MOV.B       #4,R5", "MOV         #4,R5"},
        {"MOV         @(R0,R3),R5", "MOV.L       @(R0,R3),R5"},
        {"MOV         @(8,GBR),R0", "MOV.L       @(8,GBR),R0"},
        {"MOV         @R3,R5", "MOV.L       @R3,R5"},
        {"MOV         @R3+,R5", "MOV.L       @R3+,R5"},
        {"MOV         R0,@(8,GBR)", "MOV.L       R0,@(8,GBR)"},
        {"MOV         R3,@(R0,R5)", "MOV.L       R3,@(R0,R5)"},
        {"MOV         R3,@-R5", "MOV.L       R3,@-R5"},
        {"MOV         R3,@R5", "MOV.L       R3,@R5"},
        {"MOV.B       @(R0,R3),R5", "MOV.B       @(R0,R3),R5"},
        {"MOV.B       @(2,GBR),R0", "MOV.B       @(2,GBR),R0"},
        {"MOV.B       @R3,R5", "MOV.B       @R3,R5"},
        {"MOV.B       @R3+,R5", "MOV.B       @R3+,R5"},
        {"MOV.B       R0,@(2,GBR)", "MOV.B       R0,@(2,GBR)"},
        {"MOV.B       R3,@(R0,R5)", "MOV.B       R3,@(R0,R5)"},
        {"MOV.B       R3,@-R5", "MOV.B       R3,@-R5"},
        {"MOV.B       R3,@R5", "MOV.B       R3,@R5"},
        {"MOV.W       @(R0,R3),R5", "MOV.W       @(R0,R3),R5"},
        {"MOV.W       @(4,GBR),R0", "MOV.W       @(4,GBR),R0"},
        {"MOV.W       @R3,R5", "MOV.W       @R3,R5"},
        {"MOV.W       @R3+,R5", "MOV.W       @R3+,R5"},
        {"MOV.W       R0,@(4,GBR)", "MOV.W       R0,@(4,GBR)"},
        {"MOV.W       R3,@(R0,R5)", "MOV.W       R3,@(R0,R5)"},
        {"MOV.W       R3,@-R5", "MOV.W       R3,@-R5"},
        {"MOV.W       R3,@R5", "MOV.W       R3,@R5"},
        {"MOV.L       @(R0,R3),R5", "MOV.L       @(R0,R3),R5"},
        {"MOV.L       @(8,GBR),R0", "MOV.L       @(8,GBR),R0"},
        {"MOV.L       @R3,R5", "MOV.L       @R3,R5"},
        {"MOV.L       @R3+,R5", "MOV.L       @R3+,R5"},
        {"MOV.L       R0,@(8,GBR)", "MOV.L       R0,@(8,GBR)"},
        {"MOV.L       R3,@(R0,R5)", "MOV.L       R3,@(R0,R5)"},
        {"MOV.L       R3,@-R5", "MOV.L       R3,@-R5"},
        {"MOV.L       R3,@R5", "MOV.L       R3,@R5"},
        {"MOV         @(8,R3),R5", "MOV.L       @(8,R3),R5"},
        {"MOV         R3,@(8,R5)", "MOV.L       R3,@(8,R5)"},
        {"MOV.L       @(8,R3),R5", "MOV.L       @(8,R3),R5"},
        {"MOV.L       R3,@(8,R5)", "MOV.L       R3,@(8,R5)"},
        {"MOV         R3,R5", "MOV         R3,R5"},
        {"MOV.L       R3,R5", "MOV         R3,R5"},
        {"MOV         ?t,R5", "MOV.L       .L.1.t,R5"},
        {"MOV.W       ?t,R5", "MOV.W       .L.1.t,R5"},
        {"MOV.L       ?t,R5", "MOV.L       .L.1.t,R5"},
        {"MOV.B       @(2,R3),R0", "MOV.B       @(2,R3),R0"},
        {"MOV.B       R0,@(2,R5)", "MOV.B       R0,@(2,R5)"},
        {"MOV.W       @(4,R3),R0", "MOV.W       @(4,R3),R0"},
        {"MOV.W       R0,@(4,R5)", "MOV.W       R0,@(4,R5)"},
        {"MOVA        ?t,R0", "MOVA        .L.1.t,R0"},
        {"MOVA.L      ?t,R0", "MOVA        .L.1.t,R0"},
        {"MOVCA       R0,@R5", "MOVCA.L     R0,@R5"},
        {"MOVCA.L     R0,@R5", "MOVCA.L     R0,@R5"},
        {"MULS        R3,R5", "MULS        R3,R5"},
        {"MULS.W      R3,R5", "MULS.W      R3,R5"},
        {"MULS.L      R3,R5", "MULS        R3,R5"},
        {"MULU        R3,R5", "MULU        R3,R5"},
        {"MULU.W      R3,R5", "MULU.W      R3,R5"},
        {"MULU.L      R3,R5", "MULU        R3,R5"},
        {"OCBI        @R5", "OCBI        @R5"},
        {"OCBP        @R5", "OCBP        @R5"},
        {"OCBWB       @R5", "OCBWB       @R5"},
        {"PREF        @R5", "PREF        @R5"},
        {"RTE", "RTE"},
        {"RTS", "RTS"},
        {"STC         DBR,@-R5", "STC.L       DBR,@-R5"},
        {"STC         GBR,@-R5", "STC.L       GBR,@-R5"},
        {"STC         R3_BANK,@-R5", "STC.L       R3_BANK,@-R5"},
        {"STC         SGR,@-R5", "STC.L       SGR,@-R5"},
        {"STC         SPC,@-R5", "STC.L       SPC,@-R5"},
        {"STC         SR,@-R5", "STC.L       SR,@-R5"},
        {"STC         SSR,@-R5", "STC.L       SSR,@-R5"},
        {"STC         VBR,@-R5", "STC.L       VBR,@-R5"},
        {"STC.L       DBR,@-R5", "STC.L       DBR,@-R5"},
        {"STC.L       GBR,@-R5", "STC.L       GBR,@-R5"},
        {"STC.L       R3_BANK,@-R5", "STC.L       R3_BANK,@-R5"},
        {"STC.L       SGR,@-R5", "STC.L       SGR,@-R5"},
        {"STC.L       SPC,@-R5", "STC.L       SPC,@-R5"},
        {"STC.L       SR,@-R5", "STC.L       SR,@-R5"},
        {"STC.L       SSR,@-R5", "STC.L       SSR,@-R5"},
        {"STC.L       VBR,@-R5", "STC.L       VBR,@-R5"},
        {"STC         DBR,R5", "STC         DBR,R5"},
        {"STC         GBR,R5", "STC         GBR,R5"},
        {"STC         R3_BANK,R5", "STC         R3_BANK,R5"},
        {"STC         SGR,R5", "STC         SGR,R5"},
        {"STC         SPC,R5", "STC         SPC,R5"},
        {"STC         SR,R5", "STC         SR,R5"},
        {"STC         SSR,R5", "STC         SSR,R5"},
        {"STC         VBR,R5", "STC         VBR,R5"},
        {"STC.L       DBR,R5", "STC         DBR,R5"},
        {"STC.L       GBR,R5", "STC         GBR,R5"},
        {"STC.L       R3_BANK,R5", "STC         R3_BANK,R5"},
        {"STC.L       SGR,R5", "STC         SGR,R5"},
        {"STC.L       SPC,R5", "STC         SPC,R5"},
        {"STC.L       SR,R5", "STC         SR,R5"},
        {"STC.L       SSR,R5", "STC         SSR,R5"},
        {"STC.L       VBR,R5", "STC         VBR,R5"},
        {"STS         FPSCR,@-R5", "STS.L       FPSCR,@-R5"},
        {"STS         FPUL,@-R5", "STS.L       FPUL,@-R5"},
        {"STS         MACH,@-R5", "STS.L       MACH,@-R5"},
        {"STS         MACL,@-R5", "STS.L       MACL,@-R5"},
        {"STS         PR,@-R5", "STS.L       PR,@-R5"},
        {"STS.L       FPSCR,@-R5", "STS.L       FPSCR,@-R5"},
        {"STS.L       FPUL,@-R5", "STS.L       FPUL,@-R5"},
        {"STS.L       MACH,@-R5", "STS.L       MACH,@-R5"},
        {"STS.L       MACL,@-R5", "STS.L       MACL,@-R5"},
        {"STS.L       PR,@-R5", "STS.L       PR,@-R5"},
        {"STS         FPSCR,R5", "STS         FPSCR,R5"},
        {"STS         FPUL,R5", "STS         FPUL,R5"},
        {"STS         MACH,R5", "STS         MACH,R5"},
        {"STS         MACL,R5", "STS         MACL,R5"},
        {"STS         PR,R5", "STS         PR,R5"},
        {"STS.L       FPSCR,R5", "STS         FPSCR,R5"},
        {"STS.L       FPUL,R5", "STS         FPUL,R5"},
        {"STS.L       MACH,R5", "STS         MACH,R5"},
        {"STS.L       MACL,R5", "STS         MACL,R5"},
        {"STS.L       PR,R5", "STS         PR,R5"},
        {"TAS         @R5", "TAS.B       @R5"},
        {"TAS.B       @R5", "TAS.B       @R5"},
        {"TRAPA       #4", "TRAPA       #4"},
        {"TRAPA.L     #4", "TRAPA       #4"},
    };
    for (size_t i = 0; i < sizeof forms / sizeof forms[0]; i++) {
        char in[128];
        char want[128];
        (void)snprintf(in, sizeof in, CODE S("%s") S("NOP"), forms[i][0]);
        (void)snprintf(want, sizeof want, GCODE S("%s") S("NOP"), forms[i][1]);
        if (!CHECK(translates(in, want))) {
            printf("  %s\n", forms[i][0]);
        }
    }
}

static void test_translate_pools(void)
{
    // each load of a # literal loads its label, and the literals wait for
    // the slot of a branch that never falls through, where they go out as
    // asmsh writes its pool: words, then zeros to a long's boundary, then
    // longs, each in the order of its first load
    static const char pool[] = "          .SECTION    P,CODE,ALIGN=4\n"
                               "          MOV.L       #_a,R1\n"
                               "          MOV.W       #H'80,R2\n"
                               "          MOV.L       #_a,R3\n"
                               "          RTS\n"
                               "          NOP\n";
    static const char pool_gnu[] = "          .section    P,\"ax\",@progbits\n"
                                   "          .balign     4\n"
                                   "          MOV.L       .Llit0,R1\n"
                                   "          MOV.W       .Llit1,R2\n"
                                   "          MOV.L       .Llit0,R3\n"
                                   "          RTS\n"
                                   "          NOP\n"
                                   ".Llit1:   .short      0x80\n"
                                   "          .balign     4,0\n"
                                   ".Llit0:   .long       _a\n";
    static const char pools[] = "          .SECTION    P,CODE,ALIGN=4\n"
                                "          MOV.W       # 4,R2\n"
                                "          JMP         @R2\n"
                                "          NOP ; slot\n"
                                "          MOVA.L      #D'4,R0\n"
                                "          RTE\n"
                                "          NOP\n";
    static const char pools_gnu[] = "          .section    P,\"ax\",@progbits\n"
                                    "          .balign     4\n"
                                    "          MOV.W       .Llit0,R2\n"
                                    "          JMP         @R2\n"
                                    "          NOP ! slot\n"
                                    ".Llit0:   .short      4\n"
                                    "          MOVA        .Llit1,R0\n"
                                    "          RTE\n"
                                    "          NOP\n"
                                    "          .balign     4,0\n"
                                    ".Llit1:   .long       4\n";
    CHECK(translates(pool, pool_gnu));
    CHECK(translates(pools, pools_gnu));

    // a pool follows BRA and BRAF too, but not BSR, BSRF, JSR, BT/S or BF/S,
    // and not a slot a label names or stands above
    static const char slots[] = "          .SECTION    P,CODE,ALIGN=4\n"
                                "          MOV.L       #1,R1\n"
                                "          BSR         _f\n"
                                "          NOP\n"
                                "          BSRF        R1\n"
                                "          NOP\n"
                                "          JSR         @R1\n"
                                "          NOP\n"
                                "          BT/S        _f\n"
                                "          NOP\n"
                                "          BF/S        _f\n"
                                "          NOP\n"
                                "          BRA         _f\n"
                                "_f:\n"
                                "          NOP\n"
                                "          BRA         _f\n"
                                "          NOP\n"
                                "          MOV.L       #2,R2\n"
                                "          BRAF        R1\n"
                                "          NOP\n";
    static const char slots_gnu[] = "          .section    P,\"ax\",@progbits\n"
                                    "          .balign     4\n"
                                    "          MOV.L       .Llit0,R1\n"
                                    "          BSR         _f\n"
                                    "          NOP\n"
                                    "          BSRF        R1\n"
                                    "          NOP\n"
                                    "          JSR         @R1\n"
                                    "          NOP\n"
                                    "          BT/S        _f\n"
                                    "          NOP\n"
                                    "          BF/S        _f\n"
                                    "          NOP\n"
                                    "          BRA         _f\n"
                                    "_f:\n"
                                    "          NOP\n"
                                    "          BRA         _f\n"
                                    "          NOP\n"
                                    "          .balign     4,0\n"
                                    ".Llit0:   .long       1\n"
                                    "          MOV.L       .Llit1,R2\n"
                                    "          BRAF        R1\n"
                                    "          NOP\n"
                                    "          .balign     4,0\n"
                                    ".Llit1:   .long       2\n";
    CHECK(translates(slots, slots_gnu));

    // loads of one size share a literal whose number is the same in 32 bits,
    // and a MOVA shares a long's; a number and a name, or two names, never
    // share
    static const char numbers[] = "          .SECTION    P,CODE,ALIGN=4\n"
                                  "          MOV.L       #17,R0\n"
                                  "          MOVA        #H'11,R0\n"
                                  "          MOV.W       #-1,R1\n"
                                  "          MOV.W       #H'FFFF,R2\n"
                                  "          MOV.W       #1,R3\n"
                                  "          MOV.L       #1,R4\n"
                                  "          MOV.L       #-1,R5\n"
                                  "          MOV.L       #H'FFFFFFFF,R6\n"
                                  "          MOV.L       #0,R7\n"
                                  "          MOV.L       #_z,R8\n"
                                  "          MOV.L       #_y,R9\n"
                                  "          RTS\n"
                                  "          NOP\n";
    static const char numbers_gnu[] =
        "          .section    P,\"ax\",@progbits\n"
        "          .balign     4\n"
        "          MOV.L       .Llit0,R0\n"
        "          MOVA        .Llit0,R0\n"
        "          MOV.W       .Llit1,R1\n"
        "          MOV.W       .Llit2,R2\n"
        "          MOV.W       .Llit3,R3\n"
        "          MOV.L       .Llit4,R4\n"
        "          MOV.L       .Llit5,R5\n"
        "          MOV.L       .Llit5,R6\n"
        "          MOV.L       .Llit6,R7\n"
        "          MOV.L       .Llit7,R8\n"
        "          MOV.L       .Llit8,R9\n"
        "          RTS\n"
        "          NOP\n"
        ".Llit1:   .short      -1\n"
        ".Llit2:   .short      0xFFFF\n"
        ".Llit3:   .short      1\n"
        "          .balign     4,0\n"
        ".Llit0:   .long       17\n"
        ".Llit4:   .long       1\n"
        ".Llit5:   .long       -1\n"
        ".Llit6:   .long       0\n"
        ".Llit7:   .long       _z\n"
        ".Llit8:   .long       _y\n";
    CHECK(translates(numbers, numbers_gnu));

    // loads of one size share a literal spelled alike, unless a line between
    // them defines or imports a name it holds, or for a ?label opens a block,
    // as asmsh keys a literal by what is defined or imported above its load.
    // Two spellings of one value take two, a limit.
    static const char alike[] = "          .SECTION    P,CODE,ALIGN=4\n"
                                "_f:\n"
                                "          MOV.L       #_g+4,R1\n"
                                "          MOV.L       #_g+4,R2\n"
                                "          MOV.L       #_f,R0\n"
                                "_g:\n"
                                "          MOV.L       #_f,R0\n"
                                "          MOV.L       #_g+4,R3\n"
                                "?a:\n"
                                "          MOV.L       #?a,R4\n"
                                "_h:       MOV.L       #?a,R5\n"
                                "?a:\n"
                                "X:        .EQU        17\n"
                                "          MOV.L       #X,R6\n"
                                "          MOV.L       #17,R7\n"
                                "          MOV.L       #_x,R8\n"
                                "          .IMPORT     _x\n"
                                "          MOV.L       #_x,R9\n"
                                "          RTS\n"
                                "          NOP\n";
    static const char alike_gnu[] = "          .section    P,\"ax\",@progbits\n"
                                    "          .balign     4\n"
                                    "_f:\n"
                                    "          MOV.L       .Llit0,R1\n"
                                    "          MOV.L       .Llit0,R2\n"
                                    "          MOV.L       .Llit1,R0\n"
                                    "_g:\n"
                                    "          MOV.L       .Llit1,R0\n"
                                    "          MOV.L       .Llit2,R3\n"
                                    ".L_g.a:\n"
                                    "          MOV.L       .Llit3,R4\n"
                                    "_h:       MOV.L       .Llit4,R5\n"
                                    ".L_h.a:\n"
                                    "          .equ        X,17\n"
                                    "          MOV.L       .Llit5,R6\n"
                                    "          MOV.L       .Llit6,R7\n"
                                    "          MOV.L       .Llit7,R8\n"
                                    "          .extern     _x\n"
                                    "          MOV.L       .Llit8,R9\n"
                                    "          RTS\n"
                                    "          NOP\n"
                                    "          .balign     4,0\n"
                                    ".Llit0:   .long       _g+4\n"
                                    ".Llit1:   .long       _f\n"
                                    ".Llit2:   .long       _g+4\n"
                                    ".Llit3:   .long       .L_g.a\n"
                                    ".Llit4:   .long       .L_h.a\n"
                                    ".Llit5:   .long       X\n"
                                    ".Llit6:   .long       17\n"
                                    ".Llit7:   .long       _x\n"
                                    ".Llit8:   .long       _x\n";
    CHECK(translates(alike, alike_gnu));
}

static void test_translate_branches(void)
{
    // a BRA or BSR to an import is its opcode and the relocation of its
    // displacement, which counts from 4 bytes past it. A label and a comment
    // go on the opcode's line.
    static const char tail[] = "          .SECTION    P,CODE,ALIGN=4\n"
                               "_f:       BRA         _x  ; tail\n"
                               "          NOP\n";
    static const char tail_gnu[] = "          .section    P,\"ax\",@progbits\n"
                                   "          .balign     4\n"
                                   "          .reloc      .,R_SH_IND12W,_x-4\n"
                                   "_f:       .short      0xA000 ! tail\n"
                                   "          NOP\n";
    CHECK(translates(S(".IMPORT     _x") CODE S("BSR         _x+8") S("NOP"),
                     S(".extern     _x")
                         GCODE S(".reloc      .,R_SH_IND12W,_x+8-4")
                             S(".short      0xB000") S("NOP")));
    CHECK(translates(tail, tail_gnu));

    // STARTOF X, SIZEOF X and their sum are names the link defines. A sum
    // with another section's size, or with more after it, adds the names.
    CHECK(translates(DATA S(".DATA.L     (STARTOF P)+(SIZEOF P)")
                         S(".DATA.L     (sizeof P),STARTOF P+4"),
                     GDATA S(".long       __endof_P")
                         S(".long       (__sizeof_P),__startof_P+4")));
    CHECK(translates(DATA S(".DATA.L     (STARTOF P)+(SIZEOF D)")
                         S(".DATA.L     (STARTOF P)+(SIZEOF P)+4"),
                     GDATA S(".long       (__startof_P)+(__sizeof_D)")
                         S(".long       (__startof_P)+(__sizeof_P)+4")));
}

static void test_translate_refusals(void)
{
    // what apply refuses before any patch, and a slot place_slots can't place
    CHECK(translate_refuses(CODE "* x\n", SHCKIT_UNREADABLE, 2));
    CHECK(translate_refuses(CODE S(".END") S("NOP"), SHCKIT_PAST_END, 3));
    CHECK(translate_refuses(CODE S("FOO         R1"), SHCKIT_NOT_HELD, 2));
    CHECK(translate_refuses(CODE S("RTS") S(".ALIGN      4") S("NOP"),
                            SHCKIT_SLOT, 2));
    CHECK(translate_refuses(CODE S("RTS") S(".DATA.W     H'0009"), SHCKIT_SLOT,
                            2));

    // a directive, operand or continuation translate doesn't write
    static const char *const untranslated[] = {
        S(".POOL"),
        S(".NOPOOL"),
        "X:        .ASSIGN     1\n",
        S(".DATAB.W    2,0"),
        S(".DATAB.L    2,0"),
        S(".END        _f"),
        S(".FDATA.S    H'3F800000"),
        S(".FDATA.S    F'1,H'3F800000"),
        S("MOV.L       @(4,PC),R1"),
        S("MOVA        @(4,PC),R0"),
    };
    for (size_t i = 0; i < sizeof untranslated / sizeof untranslated[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, CODE "%s", untranslated[i]);
        if (!CHECK(translate_refuses(in, SHCKIT_UNTRANSLATED, 2))) {
            printf("  %s", untranslated[i]);
        }
    }
    CHECK(translate_refuses(CODE S("MOV         R1,") "+           R2\n",
                            SHCKIT_UNTRANSLATED, 3));

    // $, the GOT, a word operator but STARTOF and SIZEOF, ~ or ^ between
    // operands, and operators the two assemblers group apart. A literal's
    // value is refused at its load.
    CHECK(translate_refuses(CODE S("MOV.L       #HIGH _a,R1") S("RTS") S("NOP"),
                            SHCKIT_WORD, 2));
    CHECK(translate_refuses(CODE S(".DATA.L     $"), SHCKIT_WORD, 2));
    CHECK(translate_refuses(DATA "_GLOBAL_OFFSET_TABLE_:\n" S(
                                ".DATA.L     _GLOBAL_OFFSET_TABLE_+4"),
                            SHCKIT_WORD, 3));
    CHECK(
        translate_refuses(CODE S("BRA         $+4") S("NOP"), SHCKIT_WORD, 2));
    CHECK(translate_refuses(CODE S(".DATA.L     1~3"), SHCKIT_GROUPING, 2));
    CHECK(translate_refuses(CODE S(".DATA.L     5^3"), SHCKIT_GROUPING, 2));
    CHECK(translate_refuses(CODE S("MOV.L       @(1+2<<2,R4),R0"),
                            SHCKIT_GROUPING, 2));

    // a name that can be one translate writes for the link: a label, an
    // .EQU's or an .ASSIGN's, a section's, or one in an operand, an
    // .IMPORT's, an .EXPORT's and a literal's among them. The bytes of a
    // comment or a string, a name in another case and __startof_ alone are
    // none.
    static const char *const reserved[] = {
        "__startof_P:\n",
        "__sizeof_P: .EQU     4\n",
        "__endof_P: .ASSIGN   4\n",
        S(".SECTION    __startof_D,DATA,ALIGN=4"),
        S(".IMPORT     __sizeof_D"),
        S(".EXPORT     __endof_D"),
        S(".DATA.L     STARTOF P,__startof_P"),
        S("MOV.L       #__endof_P+4,R1"),
    };
    for (size_t i = 0; i < sizeof reserved / sizeof reserved[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in, CODE "%s", reserved[i]);
        if (!CHECK(translate_refuses(in, SHCKIT_RESERVED, 2))) {
            printf("  %s", reserved[i]);
        }
    }
    CHECK(translates(
        DATA "__Startof_P: ; __startof_P\n" S(".SDATA      \"__sizeof_P\"")
            S(".DATA.L     __Startof_P,__startof_,STARTOF P"),
        GDATA "__Startof_P: ! __startof_P\n" S(".ascii      \"__sizeof_P\"")
            S(".long       __Startof_P,__startof_,__startof_P")));

    // a branch GNU as can't relocate as asmsh does
    CHECK(translate_refuses(S(".IMPORT     _x") CODE S("BT          _x"),
                            SHCKIT_BRANCH, 3));
    CHECK(translate_refuses(CODE S("BRA         _d") S("NOP") DATA
                            "_d:       .DATA.L     0\n",
                            SHCKIT_BRANCH, 2));
    CHECK(translate_refuses(CODE S("BRA         H'100") S("NOP"), SHCKIT_BRANCH,
                            2));
    CHECK(translate_refuses(CODE S("BSR         X")
                                S("NOP") "X:        .EQU        _f\n_f:\n",
                            SHCKIT_BRANCH, 2));

    // a branch's target is a name, alone or plus or minus a number: GNU as
    // builds a branch to any other expression that holds a name defined
    // below it to another place, with no message, as here to _f
    static const char *const branches[] = {"BRA", "BSR",  "BT",
                                           "BF",  "BT/S", "BF/S"};
    for (size_t i = 0; i < sizeof branches / sizeof branches[0]; i++) {
        char in[256];
        (void)snprintf(in, sizeof in,
                       CODE "_f:\n" S("%-11s _f+(_g-_f)")
                           S("NOP") "_g:\n" S("RTS") S("NOP"),
                       branches[i]);
        if (!CHECK(translate_refuses(in, SHCKIT_BRANCH, 3))) {
            printf("  %s\n", branches[i]);
        }
    }
    CHECK(translate_refuses(CODE "_f:\n" S("BRA         _f+STEP")
                                S("NOP") "STEP:     .EQU        6\n",
                            SHCKIT_BRANCH, 3));
    CHECK(translate_refuses(CODE "?a:\n" S("BRA         ?a+STEP")
                                S("NOP") "STEP:     .EQU        2\n",
                            SHCKIT_BRANCH, 3));
    CHECK(translates(
        CODE "_f:\n" S("BRA         _g-2")
            S("NOP") "_g:\n" S("BF          ?b+2") "?b:\n" S("RTS") S("NOP"),
        "          .section    P,\"ax\",@progbits\n"
        "          .balign     4\n"
        "_f:\n" S("BRA         _g-2") S("NOP") "_g:\n" S(
            "BF          .L_g.b+2") ".L_g.b:\n" S("RTS") S("NOP")));

    // a literal still waiting at .SECTION, .END or the end
    CHECK(translate_refuses(CODE S("MOV.L       #1,R1") S(".END"),
                            SHCKIT_WAITING, 3));
    CHECK(
        translate_refuses(CODE S("MOV.L       #1,R1") DATA, SHCKIT_WAITING, 3));
    CHECK(translate_refuses(CODE S("MOV.L       #1,R1")
                                S("RTS") "?a:\n" S("NOP"),
                            SHCKIT_WAITING, 2));

    // a string translate can't write
    static const char *const strings[] = {
        S(".SDATA      \"a\\\\b\""), S(".SDATA      \"a\"\"b\""),
        S(".SDATA      \"a\tb\""),   S(".SDATA      \"\x82\xa0\""),
        S(".SDATA      1"),          S(".SDATA      \"a\"<H'0D>"),
        S(".DATA.L     \"AB\""),     S("MOV         #\"A\",R1"),
    };
    for (size_t i = 0; i < sizeof strings / sizeof strings[0]; i++) {
        char in[128];
        (void)snprintf(in, sizeof in, DATA "%s", strings[i]);
        if (!CHECK(translate_refuses(in, SHCKIT_STRING, 2))) {
            printf("  %s", strings[i]);
        }
    }
}

static void test_translate_ranks(void)
{
    // two binary operators at one level are refused exactly where asmsh's
    // ranks and GNU as's, the tightest highest, order them apart. A level is
    // a pair of parentheses or an operand, so operators in two of them never
    // meet.
    static const char *const operators[] = {"*",  "/",  "+", "-",
                                            "<<", ">>", "&", "|"};
    static const int asmsh[] = {5, 5, 4, 4, 3, 3, 2, 1};
    static const int gnu[] = {3, 3, 1, 1, 3, 3, 2, 2};
    for (size_t i = 0; i < 8; i++) {
        for (size_t j = 0; j < 8; j++) {
            char in[128];
            char want[128];
            bool apart = (asmsh[i] < asmsh[j]) != (gnu[i] < gnu[j]) ||
                         (asmsh[i] > asmsh[j]) != (gnu[i] > gnu[j]);
            (void)snprintf(in, sizeof in, DATA S(".DATA.L     4%s2%s1"),
                           operators[i], operators[j]);
            (void)snprintf(want, sizeof want, GDATA S(".long       4%s2%s1"),
                           operators[i], operators[j]);
            if (!CHECK(apart ? translate_refuses(in, SHCKIT_GROUPING, 2)
                             : translates(in, want))) {
                printf("  4%s2%s1\n", operators[i], operators[j]);
            }
        }
    }
    CHECK(translates(DATA S(".DATA.L     (1+2)|(3<<1),1+(2<<1),1+2,3<<1"),
                     GDATA S(".long       (1+2)|(3<<1),1+(2<<1),1+2,3<<1")));
    CHECK(
        translate_refuses(DATA S(".DATA.L     (1+2)|3+1"), SHCKIT_GROUPING, 2));
}

static void test_translate_contracts(void)
{
    // dst NULL measures whatever cap is, out and line may be NULL, and a
    // text apply patched translates as its lines stand
    struct shckit_text *text = read_text(CODE S("NOP"));
    size_t size = 0;
    if (CHECK(text != NULL)) {
        CHECK(shckit_translate(text, NULL, 1, &size, NULL) == SHCKIT_OK &&
              size == strlen(GCODE S("NOP")));
        CHECK(shckit_translate(text, NULL, 0, NULL, NULL) == SHCKIT_OK);
    }
    shckit_free(text);
    text = read_text(CODE S(".POOL"));
    if (CHECK(text != NULL)) {
        CHECK(shckit_translate(text, NULL, 0, NULL, NULL) ==
              SHCKIT_UNTRANSLATED);
    }
    shckit_free(text);
    text = read_text(CODE "_f:\n" S("MOV.L       L10,R1") S("RTS") S("NOP")
                         L10 S(".DATA.L     _x"));
    if (CHECK(text != NULL) &&
        CHECK(shckit_apply(text, POOL, NULL, NULL) == SHCKIT_OK)) {
        static const char want[] =
            GCODE "_f:\n" S("MOV.L       L10,R1") S("RTS") S("NOP")
                S(".balignw    4,0x0009") "L10:\n" S(".long       _x");
        unsigned char got[256];
        size = 0;
        CHECK(shckit_translate(text, got, sizeof got, &size, NULL) ==
                  SHCKIT_OK &&
              size == strlen(want) && memcmp(got, want, size) == 0);
    }
    shckit_free(text);
}

static void test_strerror(void)
{
    // shckit_strerror describes ok and an unknown error, and shckit_free
    // takes NULL
    CHECK(strcmp(shckit_strerror(SHCKIT_OK), "ok") == 0);
    CHECK(strcmp(shckit_strerror(-1), "unknown error") == 0);
    shckit_free(NULL);
}

// A text as SHC writes it, CR LF line ends included, for drop-inline-align
// and align-pool, and what the two write.
static const char sample[] =
    "          .IMPORT     _a\r\n"
    "          .SECTION    PSG,CODE,ALIGN=4\r\n"
    "_f:                              ; function: f\r\n"
    "          MOV.L       L10+2,R3   ; _a\r\n"
    "    CLRMAC\r\n"
    "          .ALIGN      4\r\n"
    "          RTS\r\n"
    "          NOP\r\n"
    "L10:                              \r\n"
    "          .RES.W      1\r\n"
    "          .DATA.L     _a\r\n"
    "_g:                              ; function: g\r\n"
    "          MOV.W       L11,R1\r\n"
    "          MOV.L       L11+4,R2   ; _a\r\n"
    "          RTS\r\n"
    "          NOP\r\n"
    "L11:                              \r\n"
    "          .DATA.W     H'0080\r\n"
    "          .DATA.W     H'0100\r\n"
    "          .DATA.L     _a\r\n"
    "          .END\r\n";

static const char sample_want[] =
    "          .IMPORT     _a\r\n"
    "          .SECTION    PSG,CODE,ALIGN=4\r\n"
    "_f:                              ; function: f\r\n"
    "          MOV.L       L10+2,R3   ; _a\r\n"
    "    CLRMAC\r\n"
    "          RTS\r\n"
    "          NOP\r\n"
    "          .ALIGN      4\r\n"
    "          .RES.W      1\r\n"
    "L10:                              \r\n"
    "          .RES.W      1\r\n"
    "          .DATA.L     _a\r\n"
    "_g:                              ; function: g\r\n"
    "          MOV.W       L11,R1\r\n"
    "          MOV.L       L11+4,R2   ; _a\r\n"
    "          RTS\r\n"
    "          NOP\r\n"
    "          .ALIGN      4\r\n"
    "L11:                              \r\n"
    "          .DATA.W     H'0080\r\n"
    "          .DATA.W     H'0100\r\n"
    "          .DATA.L     _a\r\n"
    "          .END\r\n";

// A text as SHC writes it for use-assembler-pools, with -extra=a=400's NOP
// after a copy into R0, and what the patch writes.
static const char pools[] = "          .IMPORT     _a\r\n"
                            "          .SECTION    PSG,CODE,ALIGN=4\r\n"
                            "_f:                              ; function: f\r\n"
                            "          MOV.L       L10+2,R3   ; _a\r\n"
                            "          MOV         R4,R0\r\n"
                            "          NOP\r\n"
                            "          RTS\r\n"
                            "          MOV.L       @R3,R0\r\n"
                            "L10:                              \r\n"
                            "          .RES.W      1\r\n"
                            "          .DATA.L     _a\r\n"
                            "_g:                              ; function: g\r\n"
                            "          MOV.W       L11,R1\r\n"
                            "          MOV.L       L11+4,R2   ; _a\r\n"
                            "          RTS\r\n"
                            "          NOP\r\n"
                            "L11:                              \r\n"
                            "          .DATA.W     H'0080\r\n"
                            "          .RES.W      1\r\n"
                            "          .DATA.L     _a\r\n"
                            "          .END\r\n";

static const char pooled[] =
    "          .IMPORT     _a\r\n"
    "          .SECTION    PSG,CODE,ALIGN=4\r\n"
    "_f:                              ; function: f\r\n"
    "          MOV.L       #_a,R3     ; _a\r\n"
    "          MOV         R4,R0\r\n"
    "          NOP\r\n"
    "          RTS\r\n"
    "          MOV.L       @R3,R0\r\n"
    "          .NOPOOL\r\n"
    "          .ALIGN      4\r\n"
    "          .POOL\r\n"
    "_g:                              ; function: g\r\n"
    "          MOV.W       #H'0080,R1\r\n"
    "          MOV.L       #_a,R2     ; _a\r\n"
    "          RTS\r\n"
    "          NOP\r\n"
    "          .NOPOOL\r\n"
    "          .ALIGN      4\r\n"
    "          .POOL\r\n"
    "          .END\r\n";

// What use-assembler-pools and drop-r0-move-nop write for that text.
static const char moved[] = "          .IMPORT     _a\r\n"
                            "          .SECTION    PSG,CODE,ALIGN=4\r\n"
                            "_f:                              ; function: f\r\n"
                            "          MOV.L       #_a,R3     ; _a\r\n"
                            "          MOV         R4,R0\r\n"
                            "          RTS\r\n"
                            "          MOV.L       @R3,R0\r\n"
                            "          .NOPOOL\r\n"
                            "          .ALIGN      4\r\n"
                            "          .POOL\r\n"
                            "_g:                              ; function: g\r\n"
                            "          MOV.W       #H'0080,R1\r\n"
                            "          MOV.L       #_a,R2     ; _a\r\n"
                            "          RTS\r\n"
                            "          NOP\r\n"
                            "          .NOPOOL\r\n"
                            "          .ALIGN      4\r\n"
                            "          .POOL\r\n"
                            "          .END\r\n";

// A text as SHC writes it for fill-call-slot, a call with NOP in its slot
// and an add to R13 right below, and what use-assembler-pools,
// drop-r0-move-nop and fill-call-slot write for it.
static const char shapes[] =
    "          .IMPORT     _a\r\n"
    "          .SECTION    PSG,CODE,ALIGN=4\r\n"
    "_f:                              ; function: f\r\n"
    "          MOV.L       R13,@-R15\r\n"
    "          STS.L       PR,@-R15\r\n"
    "          MOV.L       @R4,R13\r\n"
    "          MOV.L       L10,R3     ; _a\r\n"
    "          MOV         R13,R0\r\n"
    "          NOP\r\n"
    "          MOV.L       R0,@(4,R4)\r\n"
    "          MOV.L       @R3,R3\r\n"
    "          JSR         @R3\r\n"
    "          NOP\r\n"
    "          ADD         #8,R13\r\n"
    "          MOV.L       R13,@R14\r\n"
    "          LDS.L       @R15+,PR\r\n"
    "          RTS\r\n"
    "          MOV.L       @R15+,R13\r\n"
    "L10:                              \r\n"
    "          .DATA.L     _a\r\n"
    "          .END\r\n";

static const char filled[] =
    "          .IMPORT     _a\r\n"
    "          .SECTION    PSG,CODE,ALIGN=4\r\n"
    "_f:                              ; function: f\r\n"
    "          MOV.L       R13,@-R15\r\n"
    "          STS.L       PR,@-R15\r\n"
    "          MOV.L       @R4,R13\r\n"
    "          MOV.L       #_a,R3     ; _a\r\n"
    "          MOV         R13,R0\r\n"
    "          MOV.L       R0,@(4,R4)\r\n"
    "          MOV.L       @R3,R3\r\n"
    "          JSR         @R3\r\n"
    "          ADD         #8,R13\r\n"
    "          MOV.L       R13,@R14\r\n"
    "          LDS.L       @R15+,PR\r\n"
    "          RTS\r\n"
    "          MOV.L       @R15+,R13\r\n"
    "          .NOPOOL\r\n"
    "          .ALIGN      4\r\n"
    "          .POOL\r\n"
    "          .END\r\n";

// A programmer's .ALIGN 4 in inline text, which drop-inline-align refuses.
static const char other[] = "          .SECTION    PSG,CODE,ALIGN=4\n"
                            "    CLRMAC\n"
                            "    .ALIGN  4\n"
                            "          .ALIGN      4\n";

// A text for translate, with CR LF line ends: a function that loads a
// word and a long and branches to a ?label and to an import, and a section's
// end, and what translate writes for it. And one whose literal still waits at
// .END, which translate refuses.
static const char to_translate[] =
    "          .IMPORT     _table,_next\r\n"
    "          .EXPORT     _get\r\n"
    "          .SECTION    P,CODE,ALIGN=4\r\n"
    "_get:                            ; function: get\r\n"
    "          MOV.L       #_table,R1\r\n"
    "          MOV.W       #H'80*4,R2\r\n"
    "          CMP/HS      R2,R4\r\n"
    "          BT          ?out\r\n"
    "          RTS\r\n"
    "          MOV.L       @(R0,R1),R0\r\n"
    "?out:\r\n"
    "          BRA         _next\r\n"
    "          NOP\r\n"
    "          .SECTION    D,DATA,ALIGN=4\r\n"
    "_end:     .DATA.L     (STARTOF D)+(SIZEOF D)\r\n"
    "          .END\r\n";
static const char translated[] =
    "          .extern     _table,_next\n"
    "          .global     _get\n"
    "          .section    P,\"ax\",@progbits\n"
    "          .balign     4\n"
    "_get:                            ! function: get\n"
    "          MOV.L       .Llit0,R1\n"
    "          MOV.W       .Llit1,R2\n"
    "          CMP/HS      R2,R4\n"
    "          BT          .L_get.out\n"
    "          RTS\n"
    "          MOV.L       @(R0,R1),R0\n"
    ".Llit1:   .short      0x80*4\n"
    "          .balign     4,0\n"
    ".Llit0:   .long       _table\n"
    ".L_get.out:\n"
    "          .reloc      .,R_SH_IND12W,_next-4\n"
    "          .short      0xA000\n"
    "          NOP\n"
    "          .section    D,\"aw\",@progbits\n"
    "          .balign     4\n"
    "_end:     .long       __endof_D\n"
    "          .end\n";
static const char waits[] = CODE S("MOV.L       #1,R1") S(".END");

static void test_sample(void)
{
    // drop-inline-align and align-pool on a text as SHC writes it,
    // use-assembler-pools, alone and with drop-r0-move-nop, on one of its
    // own, and those two with fill-call-slot on a third, and translate on a
    // fourth. Then selections that patch nothing, and where each line of the
    // first result came from.
    static const size_t inputs[] = {1,  2, 3,  4,  5,  7,  8,  0,
                                    0,  9, 10, 11, 12, 13, 14, 15,
                                    16, 0, 17, 18, 19, 20, 21};
    struct shckit_text *text = read_text(sample);
    size_t at = SIZE_MAX;
    int by = -2;
    CHECK(rewrites(DROP | POOL, sample, sample_want));
    CHECK(rewrites(USE, pools, pooled));
    CHECK(rewrites(USE | MOVE, pools, moved));
    CHECK(rewrites(USE | MOVE | FILL, shapes, filled));
    CHECK(translates(to_translate, translated));

    // a bit that names no patch fails before anything changes, at no line
    // and by no patch, and no bits patch nothing. Neither uses up the
    // text's apply.
    if (CHECK(text != NULL)) {
        CHECK(shckit_apply(text, BIT(SHCKIT_PATCHES), &at, &by) ==
                  SHCKIT_NO_SUCH_PATCH &&
              at == 0 && by == -1);
        CHECK(shckit_apply(text, DROP | POOL | BIT(31), NULL, NULL) ==
              SHCKIT_NO_SUCH_PATCH);
        CHECK(shckit_apply(text, 0, NULL, NULL) == SHCKIT_OK);
        CHECK(same_bytes(text, sample));
    }
    if (CHECK(text != NULL) &&
        CHECK(shckit_apply(text, DROP | POOL, NULL, NULL) == SHCKIT_OK) &&
        CHECK(shckit_count(text) == sizeof inputs / sizeof inputs[0])) {
        for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
            const struct shckit_line *line = shckit_line(text, i);
            CHECK(line->input == inputs[i]);
            CHECK(line->patch == (inputs[i] != 0 ? -1 : SHCKIT_ALIGN_POOL));
        }
        CHECK(typed_alike(text, sample_want));

        // once per text
        CHECK(shckit_apply(text, DROP | POOL, NULL, NULL) == SHCKIT_TWICE);
    }
    shckit_free(text);
}

static void write_sample(const char *base, const char *suffix,
                         const char *bytes)
{
    char path[4096];
    FILE *file = NULL;
    (void)snprintf(path, sizeof path, "%s.%s", base, suffix);
    file = fopen(path, "wb");
    if (file == NULL ||
        fwrite(bytes, 1, strlen(bytes), file) != strlen(bytes) ||
        fclose(file) != 0) {
        fprintf(stderr, "test: can't write %s\n", path);
        exit(1);
    }
}

// A text check refuses by data-in-flow, delay-slot and after-end.
static const char flawed[] = CODE "_f:\n" S("FIPR        FV4,FV4")
    S(".DATA.W     H'F77D") S("RTS") S("BRA         _f") S(".END") S("NOP");

// A text lint refuses by each rule but check's: an exported label below no
// comment, a mnemonic off its column, a tab, a trailing blank and a CR LF
// among LFs.
static const char ragged[] =
    S(".EXPORT     _f") CODE "_f:\n" S("NOP") "        RTS\n"
                                              "          NOP\t; c\n"
                                              "          NOP \n"
                                              "          NOP\r\n" S(".END");

// A function whose return's slot leaves R15 8 bytes above its entry, one
// that calls another first, and, with no label after them, one that loads a
// small constant, one that runs into a data section, one that runs to the
// text's end, one with a witness on each side of a call, and one that pops
// a width SZ sets, leaving R15 above its entry past the return's slot under
// SZ 1 alone.
static const char returns[] =
    "_entry:\n" S("RTS") S("ADD         #8,R15") "_end:\n" S(".END");
static const char calls[] = "_entry:\n" S("BSR         _g") S("NOP") S("RTS")
    S("ADD         #8,R15") "_end:\n" S(".END");
static const char loads[] =
    "_entry:\n" S("MOV.L       #0,R4") S("RTS") S("NOP") S(".END");
static const char blocks[] =
    "_entry:\n" S("NOP") DATA "_x:\n" S(".DATA.L     0") S(".END");
static const char last[] = "_entry:\n" S("RTS") S("ADD         #8,R15");
static const char witnesses[] = "_entry:\n" S("SETT") S("BSR         _g")
    S("NOP") S("SETT") S("RTS") S("NOP");
static const char pops[] = "_entry:\n" S("FMOV.S      @R15+,FR4")
    S("ADD         #-8,R15") S("RTS") S("ADD         #4,R15");
// A branch past a move of R15 to the label its two paths join at, and a loop
// back to that label, so that one inspection meets a join and a back edge.
static const char joins[] = "_entry:\n" S("BT          L1")
    S("ADD         #-4,R15") "L1:\n" S("DT          R4") S("BF          L1")
        S("RTS") S("ADD         #4,R15");

// A float constant R10 loads through MOVA.
static const char mova[] =
    CODE FUNCTION MOVA S("FMOV.S      @R0,FR3") S("RTS") S("NOP") FLOAT;

static void write_findings(const char *base, const char *suffix,
                           const char *path, const size_t *lines,
                           const int *rules, size_t count)
{
    // what the command writes for each line the rules refuse in path
    char findings[4096] = "";
    size_t len = 0;
    for (size_t i = 0; i < count; i++) {
        int put = snprintf(findings + len, sizeof findings - len,
                           "shckit: %s:%zu: %s: %s\n", path, lines[i],
                           shckit_rule_name(rules[i]),
                           shckit_rule_definition(rules[i]));
        if (put < 0 || (size_t)put >= sizeof findings - len) {
            fprintf(stderr, "test: can't write %s.%s\n", base, suffix);
            exit(1);
        }
        len += (size_t)put;
    }
    write_sample(base, suffix, findings);
}

static void write_samples(const char *base)
{
    // the samples make test's command checks run on, and the outputs they
    // expect. The Makefile's SEEDS names the samples make fuzz starts from.
    static const size_t flawed_lines[] = {4, 6, 8};
    static const int flawed_rules[] = {SHCKIT_DATA_IN_FLOW, SHCKIT_DELAY_SLOT,
                                       SHCKIT_AFTER_END};
    static const size_t ragged_lines[] = {3, 5, 6, 7, 8};
    static const int ragged_rules[] = {SHCKIT_EXPORT_COMMENT, SHCKIT_COLUMNS,
                                       SHCKIT_TAB, SHCKIT_TRAILING_BLANK,
                                       SHCKIT_LINE_ENDS};
    char path[4096];
    char refusal[4352];
    char conflict[256];
    char inspection[8448];
    (void)snprintf(refusal, sizeof refusal,
                   "shckit: %s.other:3: drop-inline-align: %s\n", base,
                   shckit_strerror(SHCKIT_OTHER_ALIGN));
    (void)snprintf(conflict, sizeof conflict, "shckit: %s\n",
                   shckit_strerror(SHCKIT_CONFLICT));
    write_sample(base, "src", sample);
    write_sample(base, "want", sample_want);
    write_sample(base, "pools", pools);
    write_sample(base, "pooled", pooled);
    write_sample(base, "moved", moved);
    write_sample(base, "shapes", shapes);
    write_sample(base, "filled", filled);
    write_sample(base, "two-pools", two_pools);
    write_sample(base, "resumed", resumed);
    write_sample(base, "other", other);
    write_sample(base, "refusal", refusal);
    write_sample(base, "conflict", conflict);
    write_sample(base, "bad", CODE "* x\n");
    write_sample(base, "unended", CODE "          .END");
    write_sample(base, "empty", "");
    write_sample(base, "flawed", flawed);
    (void)snprintf(path, sizeof path, "%s.flawed", base);
    write_findings(base, "checked", path, flawed_lines, flawed_rules,
                   sizeof flawed_lines / sizeof flawed_lines[0]);
    write_sample(base, "ragged", ragged);
    (void)snprintf(path, sizeof path, "%s.ragged", base);
    write_findings(base, "linted", path, ragged_lines, ragged_rules,
                   sizeof ragged_lines / sizeof ragged_lines[0]);
    write_sample(base, "mova", mova);
    write_sample(base, "returns", returns);
    (void)snprintf(inspection, sizeof inspection,
                   "%s.returns:3: _entry: abi: stack-above-entry (SP +8)\n",
                   base);
    write_sample(base, "returned", inspection);
    write_sample(base, "calls", calls);
    (void)snprintf(inspection, sizeof inspection,
                   "%s.calls:1: _entry: none\n"
                   "%s.calls:2: _entry: incomplete: call\n",
                   base, base);
    write_sample(base, "called", inspection);
    write_sample(base, "loads", loads);
    (void)snprintf(inspection, sizeof inspection,
                   "%s.loads:2: _entry: operand: small-constant (#0)\n", base);
    write_sample(base, "constant", inspection);
    (void)snprintf(inspection, sizeof inspection,
                   "%s.loads:1: _entry: none\n"
                   "%s.loads:2: _entry: incomplete: rule-scope\n",
                   base, base);
    write_sample(base, "scoped", inspection);
    write_sample(base, "blocks", blocks);
    (void)snprintf(inspection, sizeof inspection,
                   "%s.blocks:1: _entry: none\n"
                   "%s.blocks:2: _entry: incomplete: fallthrough\n",
                   base, base);
    write_sample(base, "fell", inspection);
    write_sample(base, "last", last);
    (void)snprintf(inspection, sizeof inspection,
                   "%s.last:3: _entry: abi: stack-above-entry (SP +8)\n", base);
    write_sample(base, "lasted", inspection);
    write_sample(base, "witnesses", witnesses);
    (void)snprintf(inspection, sizeof inspection,
                   "%s.witnesses:2: _entry: opcode: sett\n"
                   "%s.witnesses:5: _entry: opcode: sett\n"
                   "%s.witnesses:3: _entry: incomplete: call\n",
                   base, base, base);
    write_sample(base, "witnessed", inspection);
    write_sample(base, "pops", pops);
    write_sample(base, "joins", joins);
    write_sample(base, "translate", to_translate);
    write_sample(base, "translated", translated);
    write_sample(base, "waits", waits);
    (void)snprintf(refusal, sizeof refusal, "shckit: %s.waits:3: %s\n", base,
                   shckit_strerror(SHCKIT_WAITING));
    write_sample(base, "waited", refusal);
}

int main(int argc, char **argv)
{
    // run every test, whatever fails, so one run reports everything. Given
    // a path, also write the samples make test uses.
    test_kinds();
    test_strings();
    test_bytes();
    test_sections();
    test_pools();
    test_opcodes();
    test_check();
    test_lint();
    test_rules();
    test_drop_inline_align();
    test_align_pool();
    test_use_assembler_pools();
    test_drop_r0_move_nop();
    test_fill_call_slot();
    test_apply();
    test_patch_names();
    test_inspect_rules();
    test_inspect_operands();
    test_inspect_labels();
    test_inspect_stack_written();
    test_inspect_fpscr();
    test_inspect_fpscr_compiled();
    test_inspect_stack();
    test_inspect_branches();
    test_inspect_returns();
    test_inspect_pairs();
    test_inspect_compiled();
    test_inspect_all();
    test_inspect_contracts();
    test_translate();
    test_translate_forms();
    test_translate_pools();
    test_translate_branches();
    test_translate_refusals();
    test_translate_ranks();
    test_translate_contracts();
    test_strerror();
    test_sample();
    if (argc > 1) {
        write_samples(argv[1]);
    }
    if (failures != 0) {
        printf("test: %d failed\n", failures);
        return 1;
    }
    puts("test: all passed");
    return 0;
}
