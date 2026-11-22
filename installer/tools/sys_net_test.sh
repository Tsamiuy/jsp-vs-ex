#!/usr/bin/env bash
# Host test of toolkit patch 7928 (no game, no GPU; loopback sockets only; needs a C compiler and the patched toolkit): the toolkit's own
# libs/network/tests/test_sys_net.c drives the real libnet handlers with guest memory: sys_net_abort_socket ends a receive blocked on
# another thread with EINTR in that thread's errno, an abort with no receiver ends the next receive once, SO_RCVTIMEO gives EWOULDBLOCK
# on time and a huge SO_RCVTIMEO means no timeout (it no longer wraps), closing a socket ends a blocked receive, and errno is per thread.
# On posix it builds with cc (or $CC). On Windows (Git bash: the game's own platform, Winsock) it builds a Windows exe with MINGW_CC (e.g.
# llvm-mingw's x86_64-w64-mingw32-clang), else x86_64-w64-mingw32-gcc or -clang on PATH; with none it says SKIP and passes, unless CI=1.
#   bash installer/tools/sys_net_test.sh TOOLKIT_DIR [SCRATCH_DIR]      (TOOLKIT_DIR: installer/tools/export_toolkit.py --out DIR)
set -euo pipefail
tk="${1:?usage: sys_net_test.sh TOOLKIT_DIR [SCRATCH_DIR]}"
src="$tk/libs/network/tests/test_sys_net.c"
[[ -f "$src" ]] || { echo "no $src (is the toolkit exported?)" >&2; exit 3; }
grep -q sys_net_abort_socket "$tk/libs/network/sysNet.c" || { echo "$tk/libs/network/sysNet.c has no sys_net_abort_socket (is patch 7928 applied?)" >&2; exit 3; }
out="${2:-}"
if [[ -z "$out" ]]; then out=$(mktemp -d "${TMPDIR:-/tmp}/jsp_sys_net_test.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
mkdir -p "$out"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        cc="${MINGW_CC:-$(command -v x86_64-w64-mingw32-gcc || command -v x86_64-w64-mingw32-clang || true)}"
        if [[ -z "$cc" ]]; then
            [[ "${CI:-}" == 1 ]] && { echo "no Windows C compiler (set MINGW_CC)" >&2; exit 2; }
            echo "SKIP sys_net_test: no Windows C compiler (set MINGW_CC)"; exit 0
        fi
        exe="$out/test_sys_net.exe"
        (cd "$tk" && "$cc" -std=gnu17 -O1 -UNDEBUG -Iinclude libs/network/tests/test_sys_net.c -lws2_32 -o "$exe")
        # the compiler's runtime DLLs next to the exe, as audio_lock_test.sh does
        bindir=$(dirname "$cc")
        for d in "$bindir/../x86_64-w64-mingw32/bin" "$bindir"; do
            for f in "$d"/libunwind.dll "$d"/libc++.dll "$d"/libwinpthread-1.dll; do [[ -f "$f" ]] && cp -f "$f" "$out/"; done
        done
        timeout 60 "$exe"
        ;;
    *)
        cc="${CC:-$(command -v cc || command -v gcc || command -v clang)}"
        # the flags of the toolkit's own runner (tools/test_sys_net.py), which skips on Windows
        (cd "$tk" && "$cc" -std=gnu17 -O1 -UNDEBUG -Iinclude libs/network/tests/test_sys_net.c -pthread -o "$out/test_sys_net")
        timeout 60 "$out/test_sys_net"
        ;;
esac
