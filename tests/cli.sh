#!/bin/sh
# Argument handling and exit codes. Nothing here can switch a device: every
# switch request is either malformed or names a device that does not exist.

bin=${1:-./csdwire3}
fail=0

expect() {
    want=$1
    shift
    "$bin" "$@" >/dev/null 2>&1
    got=$?
    if [ "$got" -ne "$want" ]; then
        echo "FAIL: csdwire3 $*: exit $got, want $want"
        fail=1
    fi
}

expect 2
expect 0 -h
expect 0 --help
expect 2 bogus
expect 2 list extra
expect 2 state extra
expect 2 state -s
expect 2 state --serial
expect 2 switch
expect 2 switch -s
expect 2 switch sideways
expect 2 switch -s no-such-device sideways
expect 2 switch host extra
expect 0 list
expect 1 state -s no-such-device
expect 1 state --serial no-such-device
expect 1 switch -s no-such-device host
expect 1 switch --serial no-such-device dut

if [ "$("$bin" list | head -n 1)" != \
    "PORT         SERIAL                   STATE    BLOCK" ]; then
    echo "FAIL: csdwire3 list: unexpected header"
    fail=1
fi

[ "$fail" -eq 0 ] && echo "cli.sh: all checks passed"
exit "$fail"
