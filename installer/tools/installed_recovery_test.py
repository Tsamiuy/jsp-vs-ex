#!/usr/bin/env python3
"""Compile the actual installed-launcher record parser; no game, GUI or user settings."""
import pathlib
import shutil
import subprocess
import tempfile

TEST = r''' 
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
static void check(int value, const char *why) { ++checks; if (!value) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
static void feed(struct recovery_parser *p, const char *s) { while (*s) recovery_byte(p, (unsigned char)*s++); }
static int state(struct recovery_parser *p, uint64_t high, uint64_t cur, int stopped) { return p->highest == high && p->current == cur && p->stopped == stopped; }
int main(void) {
    struct recovery_parser p = {0};
    unsigned i;
    const char *invalid[] = {
        "JSP_SESSION/1 START 0\n", "JSP_SESSION/1 START 01\n",
        "JSP_SESSION/1 START +2\n", "JSP_SESSION/1 START -2\n",
        "JSP_SESSION/1 START 18446744073709551616\n",
        "JSP_SESSION/1 START 2 extra\n", "JSP_SESSION/2 START 2\n",
        "noise JSP_SESSION/1 START 2\n", "JSP_SESSION/1  START 2\n",
        "JSP_SESSION/1 START 2\rX\n", "JSP_SESSION/1 START 2\r\r\n",
        "JSP_SESSION/1 UNKNOWN 999\n", "JSP_SESSION/1 RETIRE 999\n",
        "JSP_SESSION/1 STOPPED 999 CLOCK\n", "JSP_SESSION/1 STOPPED 1 OTHER\n"
    };
    feed(&p, "JSP_SESSION/1 STOPPED 1 CLOCK\n"); check(state(&p, 0, 0, 0), "failure without current start");
    feed(&p, "JSP_SESSION/1 START 1\n"); check(state(&p, 1, 1, 0), "current start");
    for (i=0; i < sizeof invalid / sizeof invalid[0]; ++i) { feed(&p, invalid[i]); check(state(&p,1,1,0), invalid[i]); }
    feed(&p, "JSP_SESSION/1 STOPPED 1 CLOCK"); check(!p.stopped, "unterminated failure is not admitted");
    feed(&p, "\r\n"); check(state(&p,1,1,1), "Windows CRLF failure");
    feed(&p, "JSP_SESSION/1 START 1\n"); check(p.stopped, "duplicate start cannot clear failure");
    feed(&p, "JSP_SESSION/1 STOPPED 1 CLOCK\n"); check(p.stopped, "duplicate failure remains one state");
    feed(&p, "JSP_SESSION/1 RETIRE 1\nJSP_SESSION/1 START 1\nJSP_SESSION/1 STOPPED 1 CLOCK\n");
    check(state(&p,1,0,0), "retired lifetime cannot resurrect");
    feed(&p, "JSP_SESSION/1 START 2\r\nJSP_SESSION/1 STOPPED 1 CLOCK\nJSP_SESSION/1 RETIRE 1\n");
    check(state(&p,2,2,0), "stale records cannot affect replacement");
    for (i=0;i<120;++i) recovery_byte(&p,'A');
    feed(&p,"JSP_SESSION/1 STOPPED 2 CLOCK\n"); check(!p.stopped,"oversized line cannot smuggle a failure");
    feed(&p,"JSP_SESSION/1 STOPPED 2 CLOCK\n"); check(p.stopped,"parser resumes after oversized line");
    feed(&p,"JSP_SESSION/1 RETIRE 2\nJSP_SESSION/1 START 3\n");
    feed(&p,"JSP_SESSION/1 STOPPED 3"); recovery_byte(&p,0); feed(&p," CLOCK\n");
    check(state(&p,3,3,0),"NUL record is discarded");
    feed(&p,"JSP_SESSION/1 STOPPED 3 CLOCK\nJSP_SESSION/1 RETIRE 3\nJSP_SESSION/1 START 4\n");
    check(state(&p,4,4,0),"latest batch state supersedes obsolete failure");
    feed(&p,"JSP_SESSION/1 START 18446744073709551615\nJSP_SESSION/1 STOPPED 4 CLOCK\n");
    check(state(&p,UINT64_MAX,UINT64_MAX,0),"uint64 maximum without overflow or stale failure");
    feed(&p,"JSP_SESSION/1 STOPPED 18446744073709551615 CLOCK\n");
    check(p.stopped,"uint64 maximum exact matching failure");
    printf("Installed recovery: %u actual C parser controls passed\n",checks);
    return 0;
}
'''

def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    source = (root / "installer/launcher/launcher.c").read_text(encoding="utf-8")
    parser = source.split("/* JSP_RECOVERY_PARSER_BEGIN", 1)[1].split("/* JSP_RECOVERY_PARSER_END */", 1)[0]
    parser = parser.split("*/", 1)[1]
    compiler = shutil.which("cc")
    if not compiler:
        raise SystemExit("A host C compiler is required")
    with tempfile.TemporaryDirectory(prefix="jsp-installed-recovery-") as tmp:
        d = pathlib.Path(tmp)
        c = d / "parser.c"
        c.write_text("#include <string.h>\n" + parser + TEST, encoding="utf-8")
        exe = d / "parser"
        subprocess.run([compiler,"-std=c11","-O2","-Wall","-Wextra","-Werror","-pedantic",str(c),"-o",str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__ == "__main__":
    main()
