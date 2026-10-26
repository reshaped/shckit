#!/bin/sh
# tests/examples.sh DOC DIR SHCKIT: run a doc page's worked examples and
# compare what they make with the page's. Each fenced block follows its
# marker line:
#
#   <!-- example ID: input NAME -->   a file the commands read
#   <!-- example ID: run -->          the commands
#   <!-- example ID: output -->       their standard output and error together
#   <!-- example ID: result NAME -->  a file they write
#
# The commands run in DIR, with SHCKIT copied to build/shckit there, the name
# the examples call. A page without examples passes.
doc=$1
dir=$2
shckit=$3

block() {
    # the fenced block right after the line <!-- example $1 -->
    awk -v marker="<!-- example $1 -->" '
        $0 == marker { found = 1; next }
        found && /^```/ { if (fences++) exit; next }
        found && fences { print }
    ' "$doc"
}

status=0
for id in $(sed -n 's/^<!-- example \([^:]*\): run -->$/\1/p' "$doc"); do
    rm -rf "$dir"
    mkdir -p "$dir/build" "$dir/want"
    cp "$shckit" "$dir/build/shckit"
    for name in $(sed -n "s/^<!-- example $id: input \\(.*\\) -->\$/\\1/p" "$doc"); do
        block "$id: input $name" > "$dir/$name"
    done
    block "$id: run" > "$dir/run.sh"
    (cd "$dir" && sh run.sh > got 2>&1)
    block "$id: output" > "$dir/want/output"
    if ! cmp -s "$dir/want/output" "$dir/got"; then
        echo "$doc: example $id's output differs:" >&2
        diff "$dir/want/output" "$dir/got" >&2
        status=1
    fi
    for name in $(sed -n "s/^<!-- example $id: result \\(.*\\) -->\$/\\1/p" "$doc"); do
        block "$id: result $name" > "$dir/want/$name"
        if ! cmp -s "$dir/want/$name" "$dir/$name"; then
            echo "$doc: example $id's $name differs:" >&2
            diff "$dir/want/$name" "$dir/$name" >&2
            status=1
        fi
    done
done
exit $status
