#!/usr/bin/env python

import sys
import os
import subprocess

if not os.path.exists("./unit-test"):
    print("Can't find 'unit-test', run make first")
    sys.exit(1)

outputTest = "test.txt"
os.environ['FLLOC_CONFIG'] = "FILE={};GUARD=128".format(outputTest)
if os.path.exists(outputTest):
    os.unlink(outputTest)
expectedCorruptions = "expected-corruptions.txt"
if os.path.exists(expectedCorruptions):
    os.unlink(expectedCorruptions)
expectedLeaks = "expected-leaks.txt"
if os.path.exists(expectedLeaks):
    os.unlink(expectedLeaks)

subprocess.check_call(["./unit-test"])

if not os.path.exists(outputTest):
    print("'unit-test' did not produce a '{}' file".format(outputTest))
if not os.path.exists(expectedCorruptions):
    print("'unit-test' did not produce a '{}' file".format(expectedCorruptions))
if not os.path.exists(expectedLeaks):
    print("'unit-text' did not produce a '{}' file".format(expectedLeaks))

f = open(outputTest)
corruptions = ""
leaks = ""
for line in f:
    if "corruption" in line.lower():
        corruptions += line
    elif "leak" in line.lower():
        leaks += line
    else:
        print("Unknown line in flloc output: {}".format(line.strip()))
        sys.exit(1)
f.close()

# Check memory corruption detection
ok = True
f = open(expectedCorruptions)
for line in f:
    line = line.strip().lower()
    if not line in corruptions:
        print("UNIT TEST FAIL: flloc failed to detect memory corruption at {}"
                .format(line))
        ok = False
f.close()

# Check memory leak detection
f = open(expectedLeaks)
for line in f :
    line = line.strip().lower()
    if not line in leaks:
        print("UNIT TEST FAIL: flloc failed to detect memory leak at {}"
                .format(line))
        ok = False
f.close()

if not ok:
    sys.exit(1)

# Check for memory leaks inside flloc
os.environ['MALLOC_TRACE'] = "mtrace.txt"
subprocess.check_call(["./unit-test"])
mtrace = subprocess.check_output(["./run-mtrace.sh", "unit-test", "mtrace.txt"])
if "flloc.c" in mtrace:
    print("UNIT TEST FAIL: Memory leaks detected inside flloc itself!")
    print("Run `mtrace unit-test mtrace.txt` for more information.")
    sys.exit(1)

print("All unit tests passed!")
