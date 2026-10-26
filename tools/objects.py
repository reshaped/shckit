#!/usr/bin/env python3
"""Check that shckit's output assembles to each case's reference object.

    tools/objects.py SHCKIT CORPUS ASMSH...

For each case directory in CORPUS, apply the patches listed in its patches
file, one per line, to its in.src with SHCKIT. Assemble the result with
ASMSH -cpu=sh4 -endian=little -sjis -private, and compare the object with
the case's own. Objects are compared after normalizing the assembler's two
timestamp fields and recomputing the containing records' checksums; every
other byte must match.

ASMSH is the command that runs Hitachi's asmsh, such as a loader followed
by asmsh.exe, and it runs in a temporary directory. SHCKIT and the first
word of ASMSH are looked up on PATH unless they contain a slash, in which
case they are relative to the current directory. Any other argument of
ASMSH that names a file must be absolute.

asmsh records the object's file name as the module name, so each case keeps
its object under the name it was built with: CORPUS/CASE/MODULE.obj. A case
without an object is reported and skipped. A case with more than one, or
one that cannot be read or run, fails.
"""

import os
import re
import subprocess
import sys
import tempfile

STAMP = re.compile(rb"[0-9]{4}(?:[0-9]{2}| [1-9])[0-9]{6}")


def blank_stamps(data):
    """Return data with both timestamps zeroed and their checksums recomputed.

    The object starts with a 33-byte 0x80 module header record. The first
    timestamp follows the header in an 0x84 record; the second follows the
    producer name A_SH in the 0x86 record after that. A record's second byte
    is its total length, and its last byte makes its byte sum 0xff. Each
    timestamp is YYMMDDhhmmss, with the day possibly space-padded.

    Only the two 12-byte timestamps and the two records' checksum bytes
    change. Raise ValueError if the header or either record is missing,
    truncated, or has a bad checksum, or if a timestamp is malformed.
    """
    out = bytearray(data)
    if len(out) < 0x21 or out[:2] != b"\x80\x21" or sum(out[:0x21]) & 0xFF != 0xFF:
        raise ValueError("no module header")
    first = 0x21
    if len(out) < first + 3 or out[first] != 0x84 or out[first + 2] != 0x28:
        raise ValueError("no time stamp record")
    second = first + out[first + 1]
    if len(out) < second + 3 or out[second] != 0x86 or out[second + 2] != 0x40:
        raise ValueError("no producer record")
    producer = out.find(b"\x04A_SH", second + 3, second + out[second + 1] - 1)
    if producer < 0:
        raise ValueError("no A_SH producer")
    for record, stamp in ((first, first + 3), (second, producer + 5)):
        end = record + out[record + 1] - 1
        if end >= len(out) or stamp + 12 > end:
            raise ValueError("a record runs past its end")
        if not STAMP.fullmatch(bytes(out[stamp : stamp + 12])):
            raise ValueError("no time stamp")
        if sum(out[record : end + 1]) & 0xFF != 0xFF:
            raise ValueError("a record's checksum is wrong")
        out[stamp : stamp + 12] = b"0" * 12
        out[end] = (0xFF - sum(out[record:end])) & 0xFF
    return bytes(out)


def first_difference(one, two):
    for i, (a, b) in enumerate(zip(one, two)):
        if a != b:
            return i
    return min(len(one), len(two))


def command_path(name):
    """Return name for PATH lookup, or as an absolute path if it has a slash.

    The assembler runs in a temporary directory, where a relative path no
    longer names the file the caller meant.
    """
    if os.sep in name or (os.altsep and os.altsep in name):
        return os.path.abspath(name)
    return name


def check(shckit, case, asmsh, work):
    """Return None for a case without an object, else "same, N bytes" or the problem."""
    objects = sorted(name for name in os.listdir(case) if name.endswith(".obj"))
    if not objects:
        return None
    if len(objects) > 1:
        return f"{len(objects)} objects where a case holds one: " + ", ".join(objects)
    module = objects[0]
    try:
        with open(os.path.join(case, "patches"), encoding="ascii") as file:
            patches = file.read().split()
    except UnicodeDecodeError as error:
        return f"patches isn't ASCII: {error}"
    command = [shckit, "apply"]
    for patch in patches:
        command += ["-p", patch]
    command += ["-o", os.path.join(work, "x.src"), os.path.join(case, "in.src")]
    run = subprocess.run(
        command, capture_output=True, text=True, errors="replace", check=False
    )
    if run.returncode != 0:
        return "shckit refused: " + run.stderr.strip()
    env = dict(os.environ, SHC_TMP=work)
    run = subprocess.run(
        asmsh
        + ["x.src", "-object=" + module, "-cpu=sh4", "-endian=little", "-sjis", "-private"],
        cwd=work,
        env=env,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    errors = re.search(r"TOTAL ERRORS +(\d+)", run.stdout)
    if run.returncode != 0 or errors is None or errors.group(1) != "0":
        first = re.search(r"^.*\(E\).*$", run.stdout, re.M)
        return (
            f"asmsh exit {run.returncode}, "
            f"{errors.group(1) if errors else 'no'} errors, the first: "
            + (first.group(0).strip() if first else "none printed")
        )
    blanked = []
    for owner, folder in (("asmsh's", work), ("the case's", case)):
        with open(os.path.join(folder, module), "rb") as file:
            data = file.read()
        try:
            blanked.append(blank_stamps(data))
        except ValueError as error:
            return f"{owner} {module} is malformed: {error}"
    made, kept = blanked
    if made != kept:
        return f"differs at {first_difference(made, kept):#x}, {len(made)} bytes against {len(kept)}"
    return f"same, {len(made)} bytes"


def main(argv):
    if len(argv) < 4:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    shckit, corpus = command_path(argv[1]), argv[2]
    asmsh = [command_path(argv[3])] + argv[4:]
    same = cases = skipped = 0
    try:
        names = sorted(os.listdir(corpus))
    except OSError as error:
        print(f"can't read corpus: {error}", file=sys.stderr)
        return 1
    for name in names:
        case = os.path.join(corpus, name)
        if not os.path.isdir(case):
            continue
        with tempfile.TemporaryDirectory() as work:
            try:
                result = check(shckit, case, asmsh, work)
            except OSError as error:
                result = f"can't read or run: {error}"
        if result is None:
            skipped += 1
            print(f"{name}: no object, skipped")
            continue
        cases += 1
        same += result.startswith("same")
        print(f"{name}: {result}")
    print(f"{cases} objects: {same} same, {cases - same} not; skipped {skipped} without one")
    return 0 if cases != 0 and same == cases else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
