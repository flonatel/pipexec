#!/bin/bash
#
# Copyright 2015,2022 by Andreas Florath
# SPDX-License-Identifier: GPL-2.0-or-later
#

set -e

PE=./bin/pipexec

function fail() {
    echo "ERROR: test failed"
    exit 1
}

echo "TEST: run without any arguments"
if ${PE} 2>/dev/null; then
    fail
fi

echo "TEST: check return code when all childs succeed"
if ! ${PE} -- [ A /bin/true ]; then
    fail
fi

echo "TEST: check return code when one child fails"
if ${PE} -- [ A /bin/true ] [ B /bin/false ] [ C /bin/true ]; then
    fail
fi

echo "TEST: simple pipe"
RES=$(./bin/pipexec -- [ ECHO /bin/echo Hello World ] [ CAT /bin/cat ] [ GREP /bin/grep Hello ] '{ECHO:1>CAT:0}' '{CAT:1>GREP:0}')
if test "${RES}" != "Hello World"; then
    fail
fi
