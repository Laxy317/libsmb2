#!/bin/sh

. ./functions.sh

echo "basic mkdir test"

echo -n "Testing prog_mkdir on root of share ... "
./prog_mkdir "${TESTURL}/testdir" > /dev/null || failure
success

exit 0