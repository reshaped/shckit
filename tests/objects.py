#!/usr/bin/env python3
"""Tests for tools/objects.py: what the object comparison normalizes, what it
refuses, and how it finds the commands it runs. Synthetic objects and
stand-in commands replace Hitachi's tools.

    python3 tests/objects.py
"""

import contextlib
import importlib.util
import io
import os
import tempfile
import unittest
from unittest import mock

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "objects.py")
spec = importlib.util.spec_from_file_location("objects", TOOL)
objects = importlib.util.module_from_spec(spec)
spec.loader.exec_module(objects)

# copies its last argument to the argument after -o
SHCKIT = """#!/bin/sh
while [ $# -gt 1 ]; do [ "$1" = -o ] && out=$2; shift; done
cp "$1" "$out"
"""

# writes $OBJECT to the -object= path and reports no errors, as asmsh does
ASMSH = """#!/bin/sh
for arg; do case $arg in -object=*) cp "$OBJECT" "${arg#-object=}";; esac; done
echo 'TOTAL ERRORS      0'
"""


def record(kind, body):
    """A record: its kind, total length, body and checksum byte."""
    data = bytearray([kind, len(body) + 3]) + body
    return bytes(data + bytes([(0xFF - sum(data)) & 0xFF]))


def build(
    stamp=b"261019120000", producer=None, tail=b"\x00", module=b"M", payload=b"\x01\x02"
):
    """An object with a module header, both timestamp records and a payload.

    tail follows the producer record's timestamp.
    """
    producer = stamp if producer is None else producer
    return (
        record(0x80, module.ljust(30, b"\0"))
        + record(0x84, b"\x28" + stamp)
        + record(0x86, b"\x40\x04A_SH" + producer + tail)
        + record(0x88, payload)
    )


class BlankStamps(unittest.TestCase):
    def test_timestamps_and_their_checksums(self):
        one = build(b"261019120000", b"261019120001")
        two = build(b"2610 9235959", b"991231000000")
        self.assertNotEqual(one, two)
        self.assertEqual(objects.blank_stamps(one), objects.blank_stamps(two))

    def test_idempotent(self):
        blanked = objects.blank_stamps(build())
        self.assertEqual(objects.blank_stamps(blanked), blanked)

    def test_other_bytes_survive(self):
        base = objects.blank_stamps(build())
        for other in (
            build(module=b"N"),
            build(tail=b"\x01"),
            build(payload=b"\x01\x03"),
            build() + record(0x88, b"\x00"),
        ):
            self.assertNotEqual(objects.blank_stamps(other), base)

    def test_truncated(self):
        data = build()
        records = 0x21 + 16 + 22
        for size in range(records):
            with self.assertRaises(ValueError):
                objects.blank_stamps(data[:size])
        objects.blank_stamps(data[:records])

    def test_bad_checksums(self):
        for at in (0x20, 0x21 + 15, 0x21 + 16 + 21):
            data = bytearray(build())
            data[at] ^= 1
            with self.assertRaises(ValueError):
                objects.blank_stamps(bytes(data))

    def test_bad_timestamps(self):
        for stamp in (b"26101912000x", b"2610  120000", b"2610 0120000"):
            with self.assertRaises(ValueError):
                objects.blank_stamps(build(stamp))


class Commands(unittest.TestCase):
    def test_command_path(self):
        self.assertEqual(objects.command_path("wine"), "wine")
        self.assertEqual(objects.command_path("./asmsh"), os.path.abspath("asmsh"))
        self.assertEqual(objects.command_path("/opt/asmsh"), "/opt/asmsh")

    def test_relative_commands(self):
        # asmsh runs in a temporary directory, so ./asmsh must be resolved
        # before it gets there
        with tempfile.TemporaryDirectory() as root:
            case = os.path.join(root, "corpus", "case")
            os.makedirs(case)
            for name, text in (("shckit", SHCKIT), ("asmsh", ASMSH)):
                path = os.path.join(root, name)
                with open(path, "w", encoding="ascii") as file:
                    file.write(text)
                os.chmod(path, 0o755)
            with open(os.path.join(case, "in.src"), "w", encoding="ascii") as file:
                file.write("          .END\n")
            with open(os.path.join(case, "patches"), "w", encoding="ascii") as file:
                file.write("align-pool\n")
            with open(os.path.join(case, "M.obj"), "wb") as file:
                file.write(build(b"261019120000"))
            for made, status in ((build(b"270101000000"), 0), (build(payload=b"\x00"), 1)):
                with open(os.path.join(root, "made.obj"), "wb") as file:
                    file.write(made)
                got, printed = run_in(root, os.path.join(root, "made.obj"))
                self.assertEqual(got, status, printed)


def run_in(directory, made):
    """Run objects.py on ./corpus with ./shckit and ./asmsh from directory."""
    cwd = os.getcwd()
    printed = io.StringIO()
    try:
        os.chdir(directory)
        with mock.patch.dict(os.environ, OBJECT=made), contextlib.redirect_stdout(printed):
            status = objects.main(["objects.py", "./shckit", "corpus", "./asmsh"])
    finally:
        os.chdir(cwd)
    return status, printed.getvalue()


if __name__ == "__main__":
    unittest.main()
