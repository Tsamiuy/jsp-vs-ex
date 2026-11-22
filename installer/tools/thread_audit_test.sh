#!/usr/bin/env bash
# Host test of the rollback snapshot's guest thread mask bit (installer/port/jsp/jsp_thread_bit.h: ids 1..64 -> bit t - 1, bounds-checked).
# No game, no GPU: needs only g++ (C++20). Built with -fsanitize=undefined when the compiler has it (a shift by 64 then fails the run).
# A source guard then checks that no raw thread id shift is back in jsp_net_game.cpp and jsp_snapshot*.{cpp,h}.
#   bash installer/tools/thread_audit_test.sh
set -e
d=$(cd "$(dirname "$0")" && pwd)
out=$(mktemp "${TMPDIR:-/tmp}/jsp_thread_audit_test.XXXXXX")
trap 'rm -f "$out"' EXIT
san="-fsanitize=undefined -fno-sanitize-recover=undefined"
echo 'int main(){return 0;}' | g++ -x c++ $san -o "$out" - 2>/dev/null || san=""
g++ -std=c++20 -O2 -Wall -Wextra $san -o "$out" "$d/thread_audit_test.cpp"
"$out"

# Source guard: every guest thread mask bit goes through jtb::thread_bit. Fails if a raw shift comes back in the rollback sources: a 64-bit one
# (1ull, 1ULL, (uint64_t)1, ...) shifted by a variable, or any 1 shifted by a thread id (t, tid, *_tid, thread*, id).
jsp="$d/../port/jsp"
raw='(\b1(ull|ULL|llu|LLU|ul|UL|ll|LL)\b|\((u?int64_t|unsigned long long)\)[[:space:]]*1\b|u?int64_t[({]1[)}]|UINT64_C\(1\))[[:space:]]*<<[[:space:]]*\(?[[:space:]]*[A-Za-z_]'
raw="$raw"'|\b1[uU]?[[:space:]]*<<[[:space:]]*\(?[[:space:]]*(t|tid|[A-Za-z_]*_tid|thread[A-Za-z_]*|id)\b'
if grep -nE "$raw" "$jsp"/jsp_net_game.cpp "$jsp"/jsp_snapshot*.cpp "$jsp"/jsp_snapshot*.h; then
    echo "FAIL: a raw shift by a thread id (above); use jtb::thread_bit (jsp_thread_bit.h)"
    exit 1
fi
echo "source guard: ok (no raw thread id shift in jsp_net_game.cpp, jsp_snapshot*.cpp/h)"
