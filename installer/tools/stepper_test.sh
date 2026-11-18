#!/usr/bin/env bash
# Host test of the experimental rollback netcode's tick stepper (installer/port/jsp/jsp_step_input.h, jsp_step_effects.h, jsp_stepper.h): canonical
# input table, side-effect gate and the control state machine against a fake game and a fake snapshot ring (randomized netplay model, 2 and 4
# slots, 1/2/4/8-tick windows, rapid consecutive corrections). No game, no GPU: needs only g++ (C++20).
#   bash installer/tools/stepper_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_stepper_test.XXXXXX")
trap 'rm -f "$out"' EXIT
g++ -std=c++20 -O2 -Wall -Wextra -I "$d/../port/jsp" -o "$out" "$d/stepper_test.cpp"
"$out"
