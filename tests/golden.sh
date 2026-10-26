#!/bin/sh
# compare shckit's output with each case's expected text, byte for byte.
# tools/objects.py compares assembled objects where the corpus has them
#
#   tests/golden.sh SHCKIT CORPUS
#
# CORPUS holds a directory per case: in.src, the compiler's text; patches,
# the patches to apply, one per line; and out.src, the expected output.
# SHA256SUMS lists the corpus files
set -u
shckit=$1
corpus=$2
out=build/golden
failed=0
cases=0

mkdir -p "$out"
(cd "$corpus" && sha256sum -c --quiet SHA256SUMS) || exit 1
for dir in "$corpus"/*/; do
    [ -d "$dir" ] || continue
    name=$(basename "$dir")
    cases=$((cases + 1))
    set --
    for patch in $(cat "$dir/patches"); do
        set -- "$@" -p "$patch"
    done
    if ! "$shckit" apply "$@" -o "$out/out.src" "$dir/in.src"; then
        echo "refused  $name"
        failed=$((failed + 1))
    elif ! cmp -s "$out/out.src" "$dir/out.src"; then
        echo "differs  $name"
        failed=$((failed + 1))
    fi
done

echo "$cases cases: $((cases - failed)) same, $failed not"
[ "$cases" -ne 0 ] && [ "$failed" -eq 0 ]
