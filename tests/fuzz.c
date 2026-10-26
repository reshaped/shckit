// Fuzz the reader, the opcode table, check, lint, inspect, translate and
// every selection of patches, checking each result against what the header
// and the docs promise.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../shckit.h"

#define ALL ((1U << SHCKIT_PATCHES) - 1)
#define BIT(patch) (1U << (patch))
#define DROP BIT(SHCKIT_DROP_INLINE_ALIGN)
#define USE BIT(SHCKIT_USE_ASSEMBLER_POOLS)
#define MOVE BIT(SHCKIT_DROP_R0_MOVE_NOP)
#define FILL BIT(SHCKIT_FILL_CALL_SLOT)
#define POOLS (BIT(SHCKIT_ALIGN_POOL) | USE)

static unsigned char *render(const struct shckit_text *text, size_t *size)
{
    unsigned char *bytes = NULL;
    if (shckit_write(text, NULL, 0, size) != SHCKIT_OK) {
        abort();
    }
    bytes = malloc(*size != 0 ? *size : 1);
    if (bytes == NULL || shckit_write(text, bytes, *size, NULL) != SHCKIT_OK) {
        abort();
    }
    return bytes;
}

static void opcodes(const struct shckit_text *text)
{
    // abort unless only instructions have flow bits, every branch that never
    // falls through is delayed, and every delayed one is illegal in a slot
    for (size_t i = 0; i <= shckit_count(text); i++) {
        const struct shckit_line *line = shckit_line(text, i);
        int bits = shckit_flow(text, i);
        if (bits == -1) {
            continue;
        }
        if (line == NULL || line->kind != SHCKIT_INSTRUCTION || bits < 0 ||
            bits > (SHCKIT_DELAYED | SHCKIT_NO_FALL_THROUGH |
                    SHCKIT_SLOT_ILLEGAL) ||
            ((bits & SHCKIT_NO_FALL_THROUGH) != 0 &&
             (bits & SHCKIT_DELAYED) == 0) ||
            ((bits & SHCKIT_DELAYED) != 0 &&
             (bits & SHCKIT_SLOT_ILLEGAL) == 0)) {
            abort();
        }
    }
}

static bool orphan(const struct shckit_text *text, size_t index)
{
    // whether line index is a continuation that joins no statement. asmsh
    // joins one below a directive whose field above ends with a comma, and
    // any below an instruction with operands; shckit also requires a comma
    // to end the field above or start the continuation's.
    const struct shckit_line *head = NULL;
    size_t at = index;
    if (shckit_line(text, index)->kind != SHCKIT_CONTINUATION) {
        return false;
    }
    while (at > 0 && shckit_line(text, at - 1)->kind == SHCKIT_CONTINUATION) {
        at--;
    }
    head = at > 0 ? shckit_line(text, at - 1) : NULL;
    if (head == NULL ||
        (head->kind != SHCKIT_INSTRUCTION && head->kind != SHCKIT_DIRECTIVE)) {
        return true;
    }
    for (; at <= index; at++) {
        const struct shckit_line *above = shckit_line(text, at - 1);
        const struct shckit_line *line = shckit_line(text, at);
        bool ends =
            above->operands.len != 0 &&
            above->bytes[above->operands.at + above->operands.len - 1] == ',';
        bool starts = line->bytes[line->operands.at] == ',';
        if (head->kind == SHCKIT_DIRECTIVE
                ? !ends
                : head->operands.len == 0 || (!ends && !starts)) {
            return true;
        }
    }
    return false;
}

static bool opaque(const struct shckit_text *text, size_t index)
{
    // whether line index stops check's other rules: an unknown line, a
    // continuation that joins nothing, or an instruction outside the table
    const struct shckit_line *line = shckit_line(text, index);
    return line->kind == SHCKIT_UNKNOWN || orphan(text, index) ||
           (line->kind == SHCKIT_INSTRUCTION && shckit_flow(text, index) < 0);
}

static void sorted(const struct shckit_text *text,
                   const struct shckit_finding *found, size_t count)
{
    // abort unless the findings name lines of text, sorted by index and then
    // rule, each pair once
    for (size_t i = 0; i < count; i++) {
        if (found[i].index >= shckit_count(text) ||
            (i > 0 && (found[i].index < found[i - 1].index ||
                       (found[i].index == found[i - 1].index &&
                        found[i].rule <= found[i - 1].rule)))) {
            abort();
        }
    }
}

static void checked(const struct shckit_text *text)
{
    // abort unless check refuses lines in order, each by a rule at most
    // once. If a line up to the first .END stops the other rules, check
    // refuses exactly those lines there. Otherwise it refuses only data, an
    // instruction illegal in a slot or delayed with no slot after it, and
    // every line past the first .END but blanks and comments.
    struct shckit_finding *found = NULL;
    size_t count = 0;
    size_t next = 0;
    bool any = false;
    bool ended = false;
    if (shckit_check(text, &found, &count) != SHCKIT_OK) {
        abort();
    }
    for (size_t i = 0; i < shckit_count(text) && !ended; i++) {
        const struct shckit_line *line = shckit_line(text, i);
        any = any || opaque(text, i);
        ended = line->kind == SHCKIT_DIRECTIVE && line->directive == SHCKIT_END;
    }
    ended = false;
    sorted(text, found, count);
    for (size_t i = 0; i < shckit_count(text); i++) {
        const struct shckit_line *line = shckit_line(text, i);
        bool past =
            ended && line->kind != SHCKIT_BLANK && line->kind != SHCKIT_COMMENT;
        bool unread = false;
        while (next < count && found[next].index == i) {
            int rule = found[next++].rule;
            int bits = shckit_flow(text, i);
            unread = unread || rule <= SHCKIT_UNKNOWN_INSTRUCTION;
            if (any ? ended || !opaque(text, i) ||
                          rule > SHCKIT_UNKNOWN_INSTRUCTION
                    : (rule == SHCKIT_DATA_IN_FLOW &&
                       (line->kind != SHCKIT_DIRECTIVE || ended)) ||
                          (rule == SHCKIT_DELAY_SLOT &&
                           (bits < 0 || (bits & SHCKIT_SLOT_ILLEGAL) == 0 ||
                            ended)) ||
                          (rule == SHCKIT_NO_SLOT &&
                           (bits < 0 || (bits & SHCKIT_DELAYED) == 0 ||
                            ended)) ||
                          (rule == SHCKIT_AFTER_END && !past) ||
                          rule <= SHCKIT_UNKNOWN_INSTRUCTION ||
                          rule > SHCKIT_AFTER_END) {
                abort();
            }
            past = past && rule != SHCKIT_AFTER_END;
        }
        if ((!any && past) || (any && !ended && unread != opaque(text, i))) {
            abort(); // check let a line past .END, or an unread one, by
        }
        ended = ended || (line->kind == SHCKIT_DIRECTIVE &&
                          line->directive == SHCKIT_END);
    }
    free(found);
}

static void linted(const struct shckit_text *text)
{
    // abort unless lint refuses lines in order, each by a rule at most once:
    // every unknown line, tab and trailing blank; only the line ends of fewer
    // than half the lines, or of half when they differ from the first
    // line's; columns only on statement lines; and an exported label only
    // below a line that isn't a comment
    struct shckit_finding *found = NULL;
    size_t count = 0;
    size_t next = 0;
    size_t ends = 0;
    size_t styled = 0;
    bool crlf = false;
    if (shckit_lint(text, &found, &count) != SHCKIT_OK) {
        abort();
    }
    sorted(text, found, count);
    for (size_t i = 0; i < shckit_count(text); i++) {
        const struct shckit_line *line = shckit_line(text, i);
        const struct shckit_line *above =
            i > 0 ? shckit_line(text, i - 1) : NULL;
        bool unknown = line->kind == SHCKIT_UNKNOWN || orphan(text, i);
        bool tab = memchr(line->bytes, '\t', line->len) != NULL;
        bool trailing = line->len != 0 && (line->bytes[line->len - 1] == ' ' ||
                                           line->bytes[line->len - 1] == '\t');
        while (next < count && found[next].index == i) {
            int rule = found[next++].rule;
            if (rule == SHCKIT_UNKNOWN_LINE) {
                unknown = !unknown;
            } else if (rule == SHCKIT_TAB) {
                tab = !tab;
            } else if (rule == SHCKIT_TRAILING_BLANK) {
                trailing = !trailing;
            } else if (rule == SHCKIT_LINE_ENDS) {
                if (ends != 0 && line->crlf != crlf) {
                    abort();
                }
                crlf = line->crlf;
                ends++;
            } else if (rule == SHCKIT_COLUMNS) {
                if (line->kind != SHCKIT_INSTRUCTION &&
                    line->kind != SHCKIT_DIRECTIVE) {
                    abort();
                }
            } else if (rule != SHCKIT_EXPORT_COMMENT || line->label.len == 0 ||
                       (above != NULL && above->kind == SHCKIT_COMMENT)) {
                abort();
            }
        }
        if (unknown || tab || trailing) {
            abort(); // a refusal, or a miss, its bytes contradict
        }
    }
    if (ends * 2 > shckit_count(text) ||
        (ends * 2 == shckit_count(text) && ends != 0 &&
         shckit_line(text, 0)->crlf == crlf)) {
        abort();
    }
    for (size_t i = 0; ends != 0 && i < shckit_count(text); i++) {
        styled += shckit_line(text, i)->crlf == crlf ? 1 : 0;
    }
    if (styled != ends) {
        abort(); // a line with the refused end let by
    }
    free(found);
}

// The releases a rule holds for, by bit, as far as the producers inspected()
// tries tell them apart: 5.0 R10, 5.0 R32, and each 5.0 and 5.1 release.
enum { R10 = 1, R32 = 2, FIVES = 4 };

// Each witness inspect's rules find, with the mnemonics it can stand at, or
// NULL for any, and the releases its row in docs/inspect.md names under
// each mode; and each limit, but instruction-effect: every instruction in the
// opcode table has an effect the stack rule knows.
static const struct {
    const char *rule;
    const char *names;
    int kind;
    unsigned machinecode;
    unsigned asmcode;
} witnesses[] = {
    {"rotcl", "ROTCL", SHCKIT_OPCODE, FIVES, R10 | R32},
    {"rotcr", "ROTCR", SHCKIT_OPCODE, FIVES, R10 | R32},
    {"swap", "SWAP", SHCKIT_OPCODE, FIVES, R10 | R32},
    {"dmuls", "DMULS", SHCKIT_OPCODE, FIVES, FIVES},
    {"dmulu", "DMULU", SHCKIT_OPCODE, FIVES, FIVES},
    {"movca", "MOVCA", SHCKIT_OPCODE, R10 | R32, R10 | R32},
    {"fschg", "FSCHG", SHCKIT_OPCODE, R32, R32},
    {"cache-line", "OCBI OCBP OCBWB", SHCKIT_OPCODE, R10 | R32, R10 | R32},
    {"sett", "SETT", SHCKIT_OPCODE, R10 | R32, R10 | R32},
    {"fipr-registers", "FIPR", SHCKIT_OPERAND, R10 | R32, R10 | R32},
    {"ftrv-registers", "FTRV", SHCKIT_OPERAND, R10 | R32, R10 | R32},
    {"fsca-registers", "FSCA", SHCKIT_OPERAND, R10, R10},
    {"small-constant", "MOV", SHCKIT_OPERAND, R10, R10},
    {"zero-displacement", "MOV", SHCKIT_OPERAND, R10 | R32, R10 | R32},
    {"fschg-pairs", "FMOV", SHCKIT_OPERAND, R10, R10},
    {"stack-written", NULL, SHCKIT_ABI, R10 | R32, R10 | R32},
    {"fpscr-save", "STS LDS", SHCKIT_ABI, R10, R10},
    {"stack-above-entry", NULL, SHCKIT_ABI, R10, R10},
    {"return-below-entry", NULL, SHCKIT_ABI, R10, R10},
};
static const char *const limits[] = {
    "producer-scope",  "abi-scope",      "rule-scope",         "call",
    "opaque-transfer", "branch-target",  "entered-delay-slot", "delay-slot",
    "data-flow",       "section-change", "assembler-layout",   "continued",
    "fallthrough",     "stack-unknown",  "pool-word",
};

static bool mnemonic_in(const struct shckit_line *line, const char *names)
{
    // whether the line's mnemonic, the name before any dot, is one of the
    // blank-separated names, ignoring case. NULL names any.
    size_t len = 0;
    if (names == NULL) {
        return true;
    }
    while (len < line->name.len && line->bytes[line->name.at + len] != '.') {
        len++;
    }
    while (*names != '\0') {
        size_t name = strcspn(names, " ");
        bool same = name == len;
        for (size_t i = 0; same && i < len; i++) {
            unsigned byte = line->bytes[line->name.at + i];
            same = (byte >= 'a' && byte <= 'z' ? byte - 'a' + 'A' : byte) ==
                   (unsigned char)names[i];
        }
        if (same) {
            return true;
        }
        names += name;
        names += *names == ' ' ? 1 : 0;
    }
    return false;
}

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

static size_t rule_of(const char *rule)
{
    // the rule's row in witnesses, or the table's size for none
    size_t i = 0;
    while (i < sizeof witnesses / sizeof witnesses[0] &&
           strcmp(rule, witnesses[i].rule) != 0) {
        i++;
    }
    return i;
}

static bool witnessed(const struct shckit_text *text,
                      const struct shckit_function *function,
                      const struct shckit_witness *witness)
{
    // whether the witness is one of its kind's rules, at an instruction in
    // code in the function that the rule names
    const struct shckit_line *line = shckit_line(text, witness->index);
    size_t rule = rule_of(witness->rule);
    if (witness->index < function->first || witness->index >= function->end ||
        !line->code || line->kind != SHCKIT_INSTRUCTION ||
        rule == sizeof witnesses / sizeof witnesses[0]) {
        return false;
    }
    return witness->kind == witnesses[rule].kind &&
           mnemonic_in(line, witnesses[rule].names);
}

static bool limited(const struct shckit_function *function,
                    const struct shckit_inspection *result)
{
    // whether the limit is none, or one inspect names: the first two, a
    // scope's, at no line, and any other at a line of the function
    if (result->incomplete == NULL) {
        return result->incomplete_index == SIZE_MAX;
    }
    for (size_t i = 0; i < sizeof limits / sizeof limits[0]; i++) {
        if (strcmp(result->incomplete, limits[i]) == 0) {
            return i < 2 ? result->incomplete_index == SIZE_MAX
                         : result->incomplete_index >= function->first &&
                               result->incomplete_index < function->end;
        }
    }
    return false;
}

static bool named(const struct shckit_witness *witness,
                  const struct shckit_producer *producer)
{
    // whether the witness's rule holds for the release and every mode asked,
    // and the witness names what its rule does: R15's offset above the
    // entry, or below it after a return, a constant from -128 to 127, or
    // nothing
    unsigned releases = 0;
    size_t rule = 0;
    if (strcmp(producer->release, "5.0R10") == 0) {
        releases = R10 | FIVES;
    } else if (strcmp(producer->release, "5.0R32") == 0) {
        releases = R32 | FIVES;
    } else if (strcmp(producer->release, "5.1R13") == 0) {
        releases = FIVES;
    }
    if (witness->kind == SHCKIT_NO_WITNESS) {
        return witness->value == 0;
    }
    rule = rule_of(witness->rule);
    if (((producer->modes & SHCKIT_MACHINECODE) != 0 &&
         (witnesses[rule].machinecode & releases) == 0) ||
        ((producer->modes & SHCKIT_ASMCODE) != 0 &&
         (witnesses[rule].asmcode & releases) == 0)) {
        return false;
    }
    if (strcmp(witness->rule, "stack-above-entry") == 0) {
        return witness->value > 0;
    }
    if (strcmp(witness->rule, "return-below-entry") == 0) {
        return witness->value < 0;
    }
    if (strcmp(witness->rule, "small-constant") == 0) {
        return witness->value >= -128 && witness->value <= 127;
    }
    return witness->value == 0;
}

static bool sound(const struct shckit_text *text,
                  const struct shckit_function *function,
                  const struct shckit_producer *producer,
                  const struct shckit_inspection *result)
{
    // a witness its rule allows, naming what named() allows, and a limit
    // limited() allows
    const struct shckit_witness *witness = &result->witness;
    if (witness->kind == SHCKIT_NO_WITNESS
            ? witness->rule != NULL || witness->index != SIZE_MAX
            : witness->rule == NULL || !witnessed(text, function, witness)) {
        return false;
    }
    return named(witness, producer) && limited(function, result);
}

static bool all_sound(const struct shckit_text *text,
                      const struct shckit_function *function,
                      const struct shckit_producer *producer, int error,
                      const struct shckit_inspection *result)
{
    // whether shckit_inspect_all returns what shckit_inspect did, error and
    // result, leaves nothing on failure, and finds witnesses sound() allows,
    // in line order and at one line in witnesses' order, the result's first
    // among them by kind, then line, then that order
    struct shckit_inspection all;
    struct shckit_witness *found = NULL;
    size_t count = 0;
    size_t first = 0;
    bool ok = shckit_inspect_all(text, producer, function, &all, &found,
                                 &count) == error &&
              same_inspection(&all, result) &&
              (count == 0) == (result->witness.kind == SHCKIT_NO_WITNESS) &&
              (count == 0) == (found == NULL);
    for (size_t i = 0; ok && i < count; i++) {
        ok = found[i].kind != SHCKIT_NO_WITNESS && found[i].rule != NULL &&
             witnessed(text, function, &found[i]) &&
             named(&found[i], producer) &&
             (i == 0 || found[i - 1].index < found[i].index ||
              (found[i - 1].index == found[i].index &&
               rule_of(found[i - 1].rule) < rule_of(found[i].rule)));
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

static bool rests_on_sz(const struct shckit_witness *witness)
{
    // whether the witness names R15's offset, which an FMOV moves by SZ
    return strcmp(witness->rule, "stack-above-entry") == 0 ||
           strcmp(witness->rule, "return-below-entry") == 0;
}

static bool same_limit(const struct shckit_inspection *one,
                       const struct shckit_inspection *two)
{
    // whether the two name one limit at one line
    return one->incomplete_index == two->incomplete_index &&
           (one->incomplete == NULL
                ? two->incomplete == NULL
                : two->incomplete != NULL &&
                      strcmp(one->incomplete, two->incomplete) == 0);
}

static const struct shckit_witness *partner(const struct shckit_witness *list,
                                            size_t count,
                                            const struct shckit_witness *seen)
{
    // list's witness of seen's rule at its line, or NULL
    for (size_t i = 0; i < count; i++) {
        if (list[i].index == seen->index && list[i].rule == seen->rule) {
            return &list[i];
        }
    }
    return NULL;
}

static bool both_sz(const struct shckit_text *text,
                    const struct shckit_function *function,
                    const struct shckit_producer *producer)
{
    // whether inspect, with SZ unknown, finds what SZ 0 and SZ 1 both find:
    // each witness of a rule that doesn't rest on SZ under both, and of one
    // that does where both find one at a line, naming the offset nearer the
    // entry; and the limit of the two at the lower line, or stack-unknown at
    // a lower one where only one finds a witness of a rule that does
    struct shckit_function under = *function;
    struct shckit_inspection result;
    struct shckit_inspection runs[2];
    struct shckit_witness *found = NULL;
    struct shckit_witness *lists[2] = {NULL, NULL};
    size_t count = 0;
    size_t counts[2] = {0, 0};
    size_t kept = 0;
    size_t lone = SIZE_MAX;
    bool ok = shckit_inspect_all(text, producer, function, &result, &found,
                                 &count) == SHCKIT_OK;
    for (int i = 0; ok && i < 2; i++) {
        under.sz = i == 0 ? SHCKIT_SZ_0 : SHCKIT_SZ_1;
        ok = shckit_inspect_all(text, producer, &under, &runs[i], &lists[i],
                                &counts[i]) == SHCKIT_OK;
    }
    for (size_t i = 0; ok && i < counts[0]; i++) {
        const struct shckit_witness *sz0 = &lists[0][i];
        const struct shckit_witness *sz1 = partner(lists[1], counts[1], sz0);
        if (sz1 == NULL) {
            ok = rests_on_sz(sz0);
            lone = sz0->index < lone ? sz0->index : lone;
            continue;
        }
        ok = kept < count && found[kept].kind == sz0->kind &&
             found[kept].rule == sz0->rule && found[kept].index == sz0->index &&
             found[kept].value == (llabs(sz1->value) < llabs(sz0->value)
                                       ? sz1->value
                                       : sz0->value) &&
             (rests_on_sz(sz0) || same_witness(sz0, sz1));
        kept++;
    }
    for (size_t j = 0; ok && j < counts[1]; j++) {
        const struct shckit_witness *sz1 = &lists[1][j];
        if (partner(lists[0], counts[0], sz1) == NULL) {
            ok = rests_on_sz(sz1);
            lone = sz1->index < lone ? sz1->index : lone;
        }
    }
    if (ok) {
        const struct shckit_inspection *lower =
            runs[1].incomplete_index < runs[0].incomplete_index ? &runs[1]
                                                                : &runs[0];
        ok = kept == count &&
             (lone < lower->incomplete_index
                  ? result.incomplete != NULL &&
                        strcmp(result.incomplete, "stack-unknown") == 0 &&
                        result.incomplete_index == lone
                  : same_limit(&result, lower));
    }
    free(found);
    free(lists[0]);
    free(lists[1]);
    return ok;
}

static void inspected_over(const struct shckit_text *text,
                           const struct shckit_producer *producer,
                           const struct shckit_function *function, bool refused)
{
    // abort unless inspect over function fails exactly when check refused
    // the text, answers alike twice, leaves nothing on failure and finds
    // what sound() allows, shckit_inspect_all what all_sound() allows, and
    // with SZ unknown what both_sz() allows
    struct shckit_inspection result;
    struct shckit_inspection again;
    int error = shckit_inspect(text, producer, function, &result);
    if (shckit_inspect(text, producer, function, &again) != error ||
        !same_inspection(&result, &again) ||
        (error != SHCKIT_OK && error != SHCKIT_CHECK_FAILED) ||
        (error == SHCKIT_CHECK_FAILED) != refused) {
        abort();
    }
    if (error != SHCKIT_OK ? result.witness.kind != SHCKIT_NO_WITNESS ||
                                 result.incomplete != NULL ||
                                 result.incomplete_index != SIZE_MAX
                           : !sound(text, function, producer, &result)) {
        abort();
    }
    if (!all_sound(text, function, producer, error, &result)) {
        abort();
    }
    if (error == SHCKIT_OK && function->sz == SHCKIT_SZ_UNKNOWN &&
        !both_sz(text, function, producer)) {
        abort();
    }
}

static void inspected(const struct shckit_text *text)
{
    // inspected_over, from each of the first few labels in code under a few
    // producers and each sz, to the next label, and to the end of its
    // .SECTION block, as the command reads without --end, so that a branch
    // to a label inside the function joins its paths
    static const struct shckit_producer producers[] = {
        {"5.0R10", SHCKIT_ASMCODE},
        {"5.0R10", SHCKIT_MACHINECODE | SHCKIT_ASMCODE},
        {"5.0R10", SHCKIT_MACHINECODE},
        {"5.0R32", SHCKIT_ASMCODE},
        {"5.1R13", SHCKIT_MACHINECODE},
        {"6.0AD", SHCKIT_ASMCODE},
    };
    const size_t kinds = sizeof producers / sizeof producers[0];
    struct shckit_finding *found = NULL;
    size_t count = 0;
    size_t tried = 0;
    if (shckit_check(text, &found, &count) != SHCKIT_OK) {
        abort();
    }
    free(found);
    for (size_t i = 0; i < shckit_count(text) && tried < kinds; i++) {
        const struct shckit_line *line = shckit_line(text, i);
        struct shckit_function function = {i, i + 1, (int)(tried % 3)};
        struct shckit_function block = function;
        if (line->label.len == 0 || !line->code ||
            (line->kind != SHCKIT_LABEL && line->kind != SHCKIT_INSTRUCTION)) {
            continue;
        }
        while (function.end < shckit_count(text) &&
               shckit_line(text, function.end)->label.len == 0) {
            function.end++;
        }
        while (block.end < shckit_count(text) &&
               shckit_line(text, block.end)->directive != SHCKIT_SECTION &&
               shckit_line(text, block.end)->directive != SHCKIT_END) {
            block.end++;
        }
        inspected_over(text, &producers[tried], &function, count != 0);
        if (block.end != function.end) {
            inspected_over(text, &producers[tried], &block, count != 0);
        }
        tried++;
    }
}

static void translated(const struct shckit_text *text)
{
    // abort unless translate writes the same bytes twice, its measure is
    // what it writes, a buffer a byte short gets SHCKIT_FULL and nothing
    // written, each line ends with LF and holds no CR, and a refusal names a
    // line of the text and gives no size
    size_t size = 0;
    size_t again = 0;
    size_t line = 0;
    unsigned char *one = NULL;
    unsigned char *two = NULL;
    int error = shckit_translate(text, NULL, 0, &size, &line);
    if (error != SHCKIT_OK) {
        if (error == SHCKIT_FULL || error == SHCKIT_NOMEM || size != 0 ||
            line == 0 || line > shckit_count(text)) {
            abort();
        }
        return;
    }
    one = malloc(size + 1);
    two = malloc(size + 1);
    if (one == NULL || two == NULL || line != 0) {
        abort();
    }
    memset(one, 0xa5, size + 1);
    if (size != 0 &&
        (shckit_translate(text, one, size - 1, &again, NULL) != SHCKIT_FULL ||
         again != size)) {
        abort();
    }
    for (size_t i = 0; i <= size; i++) {
        if (one[i] != 0xa5) {
            abort();
        }
    }
    if (shckit_translate(text, one, size, &again, NULL) != SHCKIT_OK ||
        again != size ||
        shckit_translate(text, two, size, NULL, NULL) != SHCKIT_OK ||
        memcmp(one, two, size) != 0 || one[size] != 0xa5 ||
        (size != 0 && one[size - 1] != '\n') || memchr(one, '\r', size)) {
        abort();
    }
    free(one);
    free(two);
}

// SHCASM's .ALIGN 4 and .RES.W 1, and the .NOPOOL and .POOL
// use-assembler-pools writes in its columns.
static const char align_line[] = "          .ALIGN      4";
static const char reserve_line[] = "          .RES.W      1";
static const char nopool_line[] = "          .NOPOOL";
static const char pool_line[] = "          .POOL";

// SHCASM's NOP, the start of its copy into R0, and the starts of its call
// through a register and its add of an immediate.
static const char nop_line[] = "          NOP";
static const char move_head[] = "          MOV         R";
static const char call_head[] = "          JSR         @R";
static const char add_head[] = "          ADD         #";

static bool is_line(const struct shckit_line *line, const char *text)
{
    return line->len == strlen(text) &&
           memcmp(line->bytes, text, line->len) == 0;
}

static bool same_span(struct shckit_span one, struct shckit_span two)
{
    return one.at == two.at && one.len == two.len;
}

static bool same_typing(const struct shckit_line *one,
                        const struct shckit_line *two)
{
    // whether two lines have the same bytes and typing, pool included
    return one->len == two->len &&
           memcmp(one->bytes, two->bytes, one->len) == 0 &&
           one->crlf == two->crlf && one->code == two->code &&
           one->kind == two->kind && one->directive == two->directive &&
           same_span(one->label, two->label) &&
           same_span(one->name, two->name) &&
           same_span(one->operands, two->operands) &&
           same_span(one->comment, two->comment) && one->pool == two->pool;
}

static bool after_code(const struct shckit_text *text, size_t index)
{
    // whether the nearest line above index, past blanks and comments, is an
    // instruction or a line a patch wrote
    while (index > 0) {
        const struct shckit_line *above = shckit_line(text, --index);
        if (above->kind != SHCKIT_BLANK && above->kind != SHCKIT_COMMENT) {
            return above->input == 0 || above->kind == SHCKIT_INSTRUCTION;
        }
    }
    return false;
}

static bool r0_copy(const struct shckit_line *line)
{
    // SHCASM's MOV Rm,R0 in a code section, m from 1 to 15
    size_t len = strlen(move_head);
    const unsigned char *number = line->bytes + len;
    size_t digits = line->len - len - 3;
    return line->code && line->len > len + 3 && line->len <= len + 5 &&
           memcmp(line->bytes, move_head, len) == 0 &&
           memcmp(line->bytes + line->len - 3, ",R0", 3) == 0 &&
           number[0] >= '1' && number[0] <= '9' &&
           (digits == 1 ||
            (number[0] == '1' && number[1] >= '0' && number[1] <= '5'));
}

static bool register_named(const unsigned char *digits, size_t len, size_t *reg)
{
    // the rest of a line from a register's number, 0 to 15 as SHCASM writes
    // it, without a leading zero
    bool one = len == 1 && digits[0] >= '0' && digits[0] <= '9';
    bool two =
        len == 2 && digits[0] == '1' && digits[1] >= '0' && digits[1] <= '5';
    *reg = 0;
    if (one) {
        *reg = (size_t)(digits[0] - '0');
    }
    if (two) {
        *reg = 10 + (size_t)(digits[1] - '0');
    }
    return one || two;
}

static bool register_call(const struct shckit_line *line, size_t *target)
{
    // SHCASM's JSR @Rm in a code section, m from 0 to 15
    size_t len = strlen(call_head);
    *target = 0;
    return line->code && line->len > len &&
           memcmp(line->bytes, call_head, len) == 0 &&
           register_named(line->bytes + len, line->len - len, target);
}

static bool kept_add(const struct shckit_line *line, size_t target)
{
    // SHCASM's ADD #imm,Rn in a code section: imm in decimal from -128 to
    // 127 without a leading zero, and not -0, and n from 8 to 14 and not the
    // call's register
    size_t len = strlen(add_head);
    const unsigned char *end = line->bytes + line->len;
    const unsigned char *digits = line->bytes + len;
    const unsigned char *comma = NULL;
    bool minus = false;
    unsigned value = 0;
    size_t added = 0;
    if (!line->code || line->len <= len ||
        memcmp(line->bytes, add_head, len) != 0) {
        return false;
    }
    minus = *digits == '-';
    digits += minus;
    for (comma = digits;
         comma < end && comma - digits < 4 && *comma >= '0' && *comma <= '9';
         comma++) {
        value = (value * 10) + (unsigned)(*comma - '0');
    }
    return comma > digits &&
           (digits[0] != '0' || (comma - digits == 1 && !minus)) &&
           value <= (minus ? 128U : 127U) && end - comma > 2 &&
           comma[0] == ',' && comma[1] == 'R' &&
           register_named(comma + 2, (size_t)(end - comma - 2), &added) &&
           added >= 8 && added <= 14 && added != target;
}

static bool is_section(const struct shckit_line *line)
{
    return line->kind == SHCKIT_DIRECTIVE && line->directive == SHCKIT_SECTION;
}

static size_t opening(const struct shckit_text *text, size_t index)
{
    // the index of the .SECTION line that opens line index's block, or
    // SIZE_MAX above any
    for (size_t i = index + 1; i > 0; i--) {
        if (is_section(shckit_line(text, i - 1))) {
            return i - 1;
        }
    }
    return SIZE_MAX;
}

static bool same_section(const struct shckit_text *text, size_t one, size_t two)
{
    // whether the .SECTION lines at one and two name one section, SIZE_MAX
    // standing for P above any .SECTION
    static const unsigned char named_p[] = "P";
    const size_t lines[2] = {one, two};
    const unsigned char *names[2] = {named_p, named_p};
    size_t lens[2] = {1, 1};
    for (size_t i = 0; i < 2; i++) {
        if (lines[i] != SIZE_MAX) {
            const struct shckit_line *line = shckit_line(text, lines[i]);
            struct shckit_span name;
            size_t at = 0;
            if (!shckit_operand(line, &at, &name)) {
                abort();
            }
            names[i] = line->bytes + name.at;
            lens[i] = name.len;
        }
    }
    return lens[0] == lens[1] && memcmp(names[0], names[1], lens[0]) == 0;
}

static size_t instruction_above(const struct shckit_text *text, size_t index)
{
    // the index of the nearest instruction above index in its section, past
    // labels, blanks, comments, continuations, directives that write no data
    // and the blocks of other sections, or SIZE_MAX if data or .END comes
    // first. A section named again resumes where it stood.
    size_t section = opening(text, index);
    bool inside = true;
    while (index > 0) {
        const struct shckit_line *line = shckit_line(text, --index);
        if (is_section(line)) {
            inside = same_section(
                text, index > 0 ? opening(text, index - 1) : SIZE_MAX, section);
            continue;
        }
        if (line->kind == SHCKIT_DIRECTIVE && line->directive == SHCKIT_END) {
            return SIZE_MAX;
        }
        if (!inside) {
            continue;
        }
        if (line->kind == SHCKIT_INSTRUCTION) {
            return index;
        }
        if (line->kind == SHCKIT_DIRECTIVE &&
            line->directive >= SHCKIT_DATA_B &&
            line->directive <= SHCKIT_DATAB_L) {
            return SIZE_MAX;
        }
    }
    return SIZE_MAX;
}

static bool may_be_slot(const struct shckit_text *text, size_t index)
{
    // whether line index may be a delay slot: the instruction above it may
    // be a delayed branch, and may be in no slot itself, since a branch in a
    // slot opens none. That depends on the instruction above it in turn, so
    // the walk climbs two instructions at a time.
    for (;;) {
        size_t above = instruction_above(text, index);
        int bits = shckit_flow(text, above);
        if (above == SIZE_MAX || (bits >= 0 && (bits & SHCKIT_DELAYED) == 0)) {
            return false;
        }
        index = instruction_above(text, above);
        bits = shckit_flow(text, index);
        if (bits < 0 || (bits & SHCKIT_DELAYED) == 0) {
            return true;
        }
    }
}

static bool fills_no_slot(const struct shckit_text *text, size_t index)
{
    // whether no .ALIGN, .POOL or .NOPOOL stands where a slot is or may be
    // due, from index to the next .SECTION or .END. Whether one writes a NOP
    // into the slot depends on the address.
    for (size_t i = index; i < shckit_count(text); i++) {
        const struct shckit_line *line = shckit_line(text, i);
        if (line->kind == SHCKIT_DIRECTIVE &&
            (line->directive == SHCKIT_SECTION ||
             line->directive == SHCKIT_END)) {
            return true;
        }
        if (line->kind == SHCKIT_DIRECTIVE &&
            (line->directive == SHCKIT_ALIGN ||
             line->directive == SHCKIT_POOL ||
             line->directive == SHCKIT_NOPOOL) &&
            may_be_slot(text, i)) {
            return false;
        }
    }
    return true;
}

static void dropped(const struct shckit_text *read,
                    const struct shckit_text *text, const bool *pooled,
                    const size_t *kept, size_t from, size_t to,
                    unsigned patches)
{
    // abort unless each original line from..to-1, which the result lacks, is
    // one a selected patch drops: SHCASM's .ALIGN 4 in a code section
    // (drop-inline-align), a pool's line (use-assembler-pools), or SHCASM's
    // NOP in a code section right below SHCASM's copy into R0
    // (drop-r0-move-nop). The copy must remain, in no slot, with no line
    // from there to the next .SECTION or .END that may fill a slot. Or
    // SHCASM's NOP in a code section between SHCASM's JSR @Rm and its add
    // to R8 to R14 but Rm, with no continuation below the add
    // (fill-call-slot): the call must remain, in no slot, with the add right
    // below it and no line from there to the next .SECTION or .END that may
    // fill a slot.
    for (size_t i = from; i < to; i++) {
        const struct shckit_line *gone = shckit_line(read, i - 1);
        const struct shckit_line *above =
            i > 1 ? shckit_line(read, i - 2) : NULL;
        const struct shckit_line *below = shckit_line(read, i);
        size_t target = 0;
        if (!((patches & DROP) != 0 && gone->code &&
              is_line(gone, align_line)) &&
            !((patches & USE) != 0 && pooled[i]) &&
            !((patches & MOVE) != 0 && gone->code && is_line(gone, nop_line) &&
              above != NULL && r0_copy(above) && kept[i - 1] != 0 &&
              !may_be_slot(text, kept[i - 1] - 1) &&
              fills_no_slot(text, kept[i - 1])) &&
            !((patches & FILL) != 0 && gone->code && is_line(gone, nop_line) &&
              above != NULL && register_call(above, &target) && below != NULL &&
              kept_add(below, target) &&
              (i + 1 >= shckit_count(read) ||
               shckit_line(read, i + 1)->kind != SHCKIT_CONTINUATION) &&
              kept[i - 1] != 0 && kept[i + 1] == kept[i - 1] + 1 &&
              !may_be_slot(text, kept[i - 1] - 1) &&
              fills_no_slot(text, kept[i - 1]))) {
            abort();
        }
    }
}

static bool after_stop(const struct shckit_text *text, size_t index)
{
    // whether line index is in the delay slot of a branch that never falls
    // through: its statement is an instruction, and the nearest instruction
    // above that, past labels, blanks, comments, continuations and .EXPORT,
    // .IMPORT and .EQU lines, is such a branch
    size_t at = index;
    while (at > 0 && shckit_line(text, at)->kind == SHCKIT_CONTINUATION) {
        at--;
    }
    if (shckit_line(text, at)->kind != SHCKIT_INSTRUCTION) {
        return false;
    }
    while (at > 0) {
        const struct shckit_line *above = shckit_line(text, --at);
        int bits = shckit_flow(text, at);
        if (above->kind == SHCKIT_INSTRUCTION) {
            return bits >= 0 && (bits & SHCKIT_DELAYED) != 0 &&
                   (bits & SHCKIT_NO_FALL_THROUGH) != 0;
        }
        if (above->kind == SHCKIT_DIRECTIVE &&
            above->directive != SHCKIT_EXPORT &&
            above->directive != SHCKIT_IMPORT &&
            above->directive != SHCKIT_EQU) {
            return false;
        }
    }
    return false;
}

static void written(const struct shckit_text *text, size_t index, bool waits)
{
    // abort unless a line use-assembler-pools wrote is .NOPOOL right after
    // the last line of a no-fall-through branch's slot while a literal
    // waits, or .ALIGN 4 and .POOL together. Past blanks and comments, the
    // line above .ALIGN 4 is that .NOPOOL or, when no literal waited, the
    // slot itself.
    const struct shckit_line *line = shckit_line(text, index);
    const struct shckit_line *after = shckit_line(text, index + 1);
    const struct shckit_line *above = NULL;
    size_t at = index;
    if (is_line(line, nopool_line)) {
        if (!waits || index == 0 || !after_stop(text, index - 1) ||
            (after != NULL && after->kind == SHCKIT_CONTINUATION)) {
            abort();
        }
        return;
    }
    if (is_line(line, pool_line)) {
        above = index > 0 ? shckit_line(text, index - 1) : NULL;
        if (above == NULL || above->patch != SHCKIT_USE_ASSEMBLER_POOLS ||
            !is_line(above, align_line)) {
            abort();
        }
        return;
    }
    while (at > 0 && (shckit_line(text, at - 1)->kind == SHCKIT_BLANK ||
                      shckit_line(text, at - 1)->kind == SHCKIT_COMMENT)) {
        at--;
    }
    above = at > 0 ? shckit_line(text, at - 1) : NULL;
    if (!is_line(line, align_line) || after == NULL ||
        after->patch != SHCKIT_USE_ASSEMBLER_POOLS ||
        !is_line(after, pool_line) || above == NULL ||
        !((above->patch == SHCKIT_USE_ASSEMBLER_POOLS &&
           is_line(above, nopool_line)) ||
          after_stop(text, at - 1))) {
        abort();
    }
}

static size_t item_of(const struct shckit_text *mid,
                      const struct shckit_line *load)
{
    // the index in mid of the item a load reads. mid is the text
    // use-assembler-pools read; the load's first operand is label or label+n
    // of one of its pools, and the item of the load's size, a long for a MOVA,
    // starts n bytes past the label. Abort if there is none.
    const unsigned char *bytes = load->bytes;
    struct shckit_span operand;
    size_t at = 0;
    size_t len = 0;
    size_t offset = 0;
    size_t label = SIZE_MAX;
    unsigned size = bytes[load->name.at + load->name.len - 1] & ~0x20U;
    if (load->kind != SHCKIT_INSTRUCTION ||
        !shckit_operand(load, &at, &operand)) {
        abort();
    }
    while (len < operand.len && bytes[operand.at + len] != '+') {
        len++;
    }
    for (size_t i = len + 1; i < operand.len; i++) {
        if (offset > 1000000) {
            abort();
        }
        offset = (offset * 10) + (bytes[operand.at + i] - (unsigned)'0');
    }
    for (size_t i = 0; i < shckit_count(mid) && label == SIZE_MAX; i++) {
        const struct shckit_line *line = shckit_line(mid, i);
        if (line->pool != 0 && line->kind == SHCKIT_LABEL &&
            line->label.len == len &&
            memcmp(line->bytes, bytes + operand.at, len) == 0) {
            label = i;
        }
    }
    at = 0;
    for (size_t i = label + 1;
         label != SIZE_MAX && i < shckit_count(mid) &&
         shckit_line(mid, i)->pool == shckit_line(mid, label)->pool;
         i++) {
        int directive = shckit_line(mid, i)->directive;
        if (at == offset) {
            if (directive != (size == 'W' ? SHCKIT_DATA_W : SHCKIT_DATA_L)) {
                abort();
            }
            return i;
        }
        at += directive == SHCKIT_DATA_L ? 4 : 2;
    }
    abort();
}

static void rewritten(const struct shckit_text *read,
                      const struct shckit_text *mid,
                      const struct shckit_line *line)
{
    // abort unless a line use-assembler-pools rewrote has its original
    // bytes, but # and the item's value as its first operand, and its
    // comment at its original column or, if the operands now reach that,
    // right after them
    const struct shckit_line *was = shckit_line(read, line->input - 1);
    const struct shckit_line *item = shckit_line(mid, item_of(mid, was));
    struct shckit_span operand;
    size_t at = 0;
    size_t rest = 0;
    size_t end = 0;
    size_t column = 0;
    if (!shckit_operand(was, &at, &operand)) {
        abort();
    }
    rest = was->operands.at + was->operands.len - (operand.at + operand.len);
    end = operand.at + 1 + item->operands.len + rest;
    column = was->comment.at > end ? was->comment.at : end;
    if (line->crlf != was->crlf || line->code != was->code || line->len < end ||
        memcmp(line->bytes, was->bytes, operand.at) != 0 ||
        line->bytes[operand.at] != '#' ||
        memcmp(line->bytes + operand.at + 1, item->bytes + item->operands.at,
               item->operands.len) != 0 ||
        memcmp(line->bytes + end - rest, was->bytes + operand.at + operand.len,
               rest) != 0) {
        abort();
    }
    if (was->comment.len != 0
            ? line->comment.at != column ||
                  line->len != column + was->comment.len ||
                  memcmp(line->bytes + column, was->bytes + was->comment.at,
                         was->comment.len) != 0
            : line->len !=
                      end + (was->len - was->operands.at - was->operands.len) ||
                  memcmp(line->bytes + end,
                         was->bytes + was->operands.at + was->operands.len,
                         line->len - end) != 0) {
        abort();
    }
    for (size_t i = end; was->comment.len != 0 && i < line->comment.at; i++) {
        if (line->bytes[i] != ' ') {
            abort();
        }
    }
}

static bool check_refuses(const struct shckit_text *text, size_t index,
                          int rule)
{
    // whether check refuses line index, or any line when index is SIZE_MAX,
    // by rule, or when rule is -1 by unknown-line or unknown-instruction,
    // which stop the other rules
    struct shckit_finding *found = NULL;
    size_t count = 0;
    bool refused = false;
    if (shckit_check(text, &found, &count) != SHCKIT_OK) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        refused =
            refused || ((index == SIZE_MAX || found[i].index == index) &&
                        (rule < 0 ? found[i].rule <= SHCKIT_UNKNOWN_INSTRUCTION
                                  : found[i].rule == rule));
    }
    free(found);
    return refused;
}

static void left(const uint8_t *data, size_t size,
                 const struct shckit_text *text)
{
    // abort unless a text apply refused is, line for line, the text as read
    struct shckit_text *earlier = NULL;
    if (shckit_read(&earlier, data, size, NULL) != SHCKIT_OK ||
        shckit_count(earlier) != shckit_count(text)) {
        abort();
    }
    for (size_t i = 0; i < shckit_count(text); i++) {
        const struct shckit_line *one = shckit_line(text, i);
        const struct shckit_line *two = shckit_line(earlier, i);
        if (!same_typing(one, two) || one->input != two->input ||
            one->patch != two->patch) {
            abort();
        }
    }
    shckit_free(earlier);
}

static void patched(const uint8_t *data, size_t size, unsigned patches)
{
    // apply the patches and abort unless the result keeps original lines in
    // order and unchanged, but for the lines use-assembler-pools rewrites;
    // drops only lines a selected patch drops; writes only SHCASM's .ALIGN 4
    // and .RES.W 1 before a pool's label below an instruction or another
    // written line, or .NOPOOL, .ALIGN 4 and .POOL where use-assembler-pools
    // writes them; leaves no pool after use-assembler-pools; keeps each
    // continuation under its statement; and reads back as it is typed. A
    // refusal must leave the text as read and free for another apply, which
    // refuses it alike, and align-pool with use-assembler-pools must fail
    // before any patch.
    struct shckit_text *read = NULL;
    struct shckit_text *mid = NULL;
    struct shckit_text *text = NULL;
    struct shckit_text *again = NULL;
    unsigned char *bytes = NULL;
    bool *pooled = NULL;
    size_t *kept = NULL;
    size_t len = 0;
    size_t next = 1; // the next original line number not yet seen
    size_t line = 0;
    int patch = 0;
    int error = SHCKIT_OK;
    bool waits = false; // a rewritten line's literal waits for a .POOL

    if (shckit_read(&read, data, size, NULL) != SHCKIT_OK ||
        shckit_read(&text, data, size, NULL) != SHCKIT_OK) {
        abort();
    }
    // a bit that names no patch fails first and changes nothing
    if (shckit_apply(text, patches | BIT(SHCKIT_PATCHES), &line, &patch) !=
            SHCKIT_NO_SUCH_PATCH ||
        line != 0 || patch != -1) {
        abort();
    }
    left(data, size, text);
    if ((patches & POOLS) == POOLS) {
        if (shckit_apply(text, patches, &line, &patch) != SHCKIT_CONFLICT ||
            line != 0 || patch != -1) {
            abort();
        }
        left(data, size, text);
        shckit_free(read);
        shckit_free(text);
        return;
    }
    // apply's preflight refusals name no patch and a line check refuses
    // too: one it can't read, or one past .END, unless an unknown instruction
    // stops check first. Every other refusal is a patch's own, and a text
    // that passes the preflight holds no line check finds unreadable or
    // past .END.
    error = shckit_apply(text, patches, &line, &patch);
    if (error == SHCKIT_UNREADABLE) {
        if (patch != -1 || line == 0 || line > shckit_count(read) ||
            !check_refuses(read, line - 1, SHCKIT_UNKNOWN_LINE)) {
            abort();
        }
    } else if (error == SHCKIT_PAST_END) {
        if (patch != -1 || line == 0 || line > shckit_count(read) ||
            !(check_refuses(read, line - 1, SHCKIT_AFTER_END) ||
              check_refuses(read, SIZE_MAX, -1))) {
            abort();
        }
    } else if (check_refuses(read, SIZE_MAX, SHCKIT_UNKNOWN_LINE) ||
               check_refuses(read, SIZE_MAX, SHCKIT_AFTER_END) ||
               (error != SHCKIT_OK &&
                (error == SHCKIT_NOMEM || patch < 0 ||
                 patch >= SHCKIT_PATCHES || (patches & BIT(patch)) == 0 ||
                 line == 0 || line > shckit_count(read)))) {
        abort();
    }
    if (error != SHCKIT_OK) {
        size_t again = 0;
        int again_by = 0;
        left(data, size, text);
        if (shckit_apply(text, patches, &again, &again_by) != error ||
            again != line || again_by != patch) {
            abort();
        }
        left(data, size, text);
        shckit_free(read);
        shckit_free(text);
        return;
    }
    // pooled[n] is whether original line n is on a pool in the text
    // use-assembler-pools read, after drop-inline-align if selected
    pooled = calloc(shckit_count(read) + 1, sizeof *pooled);
    kept = calloc(shckit_count(read) + 1, sizeof *kept);
    if (pooled == NULL || kept == NULL ||
        shckit_read(&mid, data, size, NULL) != SHCKIT_OK ||
        ((patches & DROP) != 0 &&
         shckit_apply(mid, DROP, NULL, NULL) != SHCKIT_OK)) {
        abort();
    }
    for (size_t i = 0; i < shckit_count(mid); i++) {
        pooled[shckit_line(mid, i)->input] = shckit_line(mid, i)->pool != 0;
    }
    // kept[n] is the 1-based position of original line n in the result, or 0
    for (size_t i = 0; i < shckit_count(text); i++) {
        kept[shckit_line(text, i)->input] = i + 1;
    }
    for (size_t i = 0; i < shckit_count(text); i++) {
        const struct shckit_line *each = shckit_line(text, i);
        const struct shckit_line *after = shckit_line(text, i + 1);
        const struct shckit_line *was =
            each->input != 0 ? shckit_line(read, each->input - 1) : NULL;
        if (each->kind == SHCKIT_UNKNOWN ||
            ((patches & USE) != 0 && each->pool != 0)) {
            abort(); // an unknown line, or a pool use-assembler-pools left
        }
        // asmsh writes waiting literals after a no-fall-through branch's
        // slot unless .NOPOOL follows it. The first .POOL below a load
        // takes its literal, even when that .POOL replaces another pool.
        if (each->patch == SHCKIT_USE_ASSEMBLER_POOLS) {
            waits = each->input != 0 || (waits && !is_line(each, pool_line));
        }
        if (waits && after_stop(text, i) &&
            (after == NULL || (after->kind != SHCKIT_CONTINUATION &&
                               (after->patch != SHCKIT_USE_ASSEMBLER_POOLS ||
                                !is_line(after, nopool_line))))) {
            abort(); // a literal left to asmsh's pool after this slot
        }
        if (each->input == 0 && each->patch == SHCKIT_USE_ASSEMBLER_POOLS &&
            (patches & USE) != 0) {
            written(text, i, waits);
            continue;
        }
        if (each->input == 0) {
            bool label = after != NULL && after->pool != 0 &&
                         after->kind == SHCKIT_LABEL;
            bool pad = after != NULL && after->input == 0 &&
                       is_line(after, reserve_line);
            if (each->patch != SHCKIT_ALIGN_POOL ||
                (patches & BIT(SHCKIT_ALIGN_POOL)) == 0 ||
                !((is_line(each, align_line) && (label || pad)) ||
                  (is_line(each, reserve_line) && label)) ||
                !after_code(text, i)) {
                abort();
            }
            continue;
        }
        if (each->input < next) {
            abort();
        }
        if (each->patch == SHCKIT_USE_ASSEMBLER_POOLS && (patches & USE) != 0) {
            rewritten(read, mid, each);
        } else if (each->patch != -1 || each->len != was->len ||
                   memcmp(each->bytes, was->bytes, was->len) != 0 ||
                   each->crlf != was->crlf) {
            abort();
        }
        if (each->kind == SHCKIT_CONTINUATION &&
            (i == 0 ? each->input != 1
                    : shckit_line(text, i - 1)->input != each->input - 1)) {
            abort(); // a patch took a continuation from its statement
        }
        dropped(read, text, pooled, kept, next, each->input, patches);
        next = each->input + 1;
    }
    dropped(read, text, pooled, kept, next, shckit_count(read) + 1, patches);
    // a text of nothing but SHCASM's .ALIGN 4 lines, all in P, comes out
    // empty, which the reader refuses
    bytes = render(text, &len);
    if (len != 0 && (shckit_read(&again, bytes, len, NULL) != SHCKIT_OK ||
                     shckit_count(again) != shckit_count(text))) {
        abort();
    }
    for (size_t i = 0; i < shckit_count(text); i++) {
        if (!same_typing(shckit_line(text, i), shckit_line(again, i))) {
            abort();
        }
    }
    free(kept);
    free(pooled);
    shckit_free(again);
    shckit_free(mid);
    shckit_free(read);
    shckit_free(text);
    free(bytes);
}

// libfuzzer calls this by name, so it keeps the name and can't be static
// NOLINTNEXTLINE(misc-use-internal-linkage,readability-identifier-naming)
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // a text that reads must render back to its bytes. Then fuzz the opcode
    // table, check, lint, inspect and translate on it, and every selection of
    // patches.
    struct shckit_text *text = NULL;
    unsigned char *bytes = NULL;
    size_t len = 0;

    if (shckit_read(&text, data, size, NULL) != SHCKIT_OK) {
        if (text != NULL) {
            abort();
        }
        return 0;
    }
    bytes = render(text, &len);
    if (len != size || memcmp(bytes, data, size) != 0) {
        abort();
    }
    free(bytes);
    opcodes(text);
    checked(text);
    linted(text);
    inspected(text);
    translated(text);
    shckit_free(text);
    for (unsigned patches = 1; patches <= ALL; patches++) {
        patched(data, size, patches);
    }
    return 0;
}
