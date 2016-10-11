#!/bin/sh

# Note: There are almost always leaks in glibc
#  => Ignore those
mtrace "$1" "$2"

exit 0
