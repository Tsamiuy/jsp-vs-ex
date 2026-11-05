/* Stand-in for cmake.exe in script_paths_test.py: appends its arguments (UTF-8, tab separated) to the file named by %FAKE_OUT%, then one ENV line with the
 * variables named in %FAKE_ENV% (comma separated) as NAME=value.
 * Build: x86_64-w64-mingw32-gcc -municode -O2 -s -o fake_tool.exe fake_tool.c   (or cl /DUNICODE fake_tool.c). No game code. */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
int wmain(int argc, wchar_t **argv)
{
    wchar_t o[1024];
    FILE *f;
    int i;
    char u[4096];
    if (!GetEnvironmentVariableW(L"FAKE_OUT", o, 1024) || !(f = _wfopen(o, L"ab"))) return 3;
    fputs("ARGS", f);
    for (i = 1; i < argc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, u, sizeof u, NULL, NULL);
        fprintf(f, "\t%s", n ? u : "?");
    }
    fputs("\n", f);
    if (GetEnvironmentVariableW(L"FAKE_ENV", o, 1024)) {
        wchar_t *names = o, *c, v[1024];
        fputs("ENV", f);
        while (names) {
            c = wcschr(names, L',');
            if (c) *c++ = 0;
            if (!GetEnvironmentVariableW(names, v, 1024)) v[0] = 0;
            WideCharToMultiByte(CP_UTF8, 0, v, -1, u, sizeof u, NULL, NULL);
            fputs("\t", f);
            { char nm[128]; WideCharToMultiByte(CP_UTF8, 0, names, -1, nm, sizeof nm, NULL, NULL); fprintf(f, "%s=%s", nm, u); }
            names = c;
        }
        fputs("\n", f);
    }
    fclose(f);
    return 0;
}
