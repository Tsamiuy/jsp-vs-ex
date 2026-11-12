/* rsx_env.h: same answers as the CRT getenv, and the cost per call. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
static const char* crt(const char* n) { return getenv(n); }
#include "rsx_env.h"

int main(void)
{
    const char* names[] = { "PATH", "VTX_DUMP", "BLENDDBG", "STRIP_EXPAND", "PERF", "USERNAME",
                            "FP_CONSTBUF", "NO_ALPHATEST", "VP_FIXPROJ", "SystemRoot" };
    const int N = sizeof names / sizeof names[0];
    int bad = 0;
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < N; i++) {
            const char* a = crt(names[i]); const char* b = getenv(names[i]);
            if ((a == NULL) != (b == NULL) || (a && strcmp(a, b))) { bad++; printf("MISMATCH %s\n", names[i]); }
        }
    /* same name from a different buffer must hit the same entry */
    char dyn[32]; strcpy(dyn, "PATH");
    if (getenv(dyn) != getenv("PATH")) { bad++; printf("MISMATCH dynamic name\n"); }
    LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f);
    volatile int hits = 0;
    QueryPerformanceCounter(&t0);
    for (int k = 0; k < 1000000; k++) if (crt(names[k % N])) hits++;
    QueryPerformanceCounter(&t1);
    double a = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
    QueryPerformanceCounter(&t0);
    for (int k = 0; k < 1000000; k++) if (getenv(names[k % N])) hits++;
    QueryPerformanceCounter(&t1);
    double b = (double)(t1.QuadPart - t0.QuadPart) / f.QuadPart;
    printf("answers %s; CRT getenv %.3f us/call, cached %.3f us/call\n",
           bad ? "DIFFER" : "identical", a, b);
    return bad;
}
