#include <stdio.h>
#include <windows.h>
#pragma comment(lib, "winmm.lib")
int main(void){ timeBeginPeriod(1);
  ULONGLONG last=GetTickCount64(); int n=0; ULONGLONG steps[64];
  LARGE_INTEGER f,a,b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
  while(n<40){ ULONGLONG t=GetTickCount64(); if(t!=last){ steps[n++]=t-last; last=t; } }
  QueryPerformanceCounter(&b);
  printf("GetTickCount64 steps (ms) with timeBeginPeriod(1):"); for(int i=0;i<40;i++) printf(" %llu",steps[i]);
  printf("\nover %.1f ms\n",(double)(b.QuadPart-a.QuadPart)*1e3/f.QuadPart);
  /* Sleep(4) actual */
  QueryPerformanceCounter(&a); for(int i=0;i<50;i++) Sleep(4); QueryPerformanceCounter(&b);
  printf("Sleep(4) average: %.2f ms\n",(double)(b.QuadPart-a.QuadPart)*1e3/f.QuadPart/50);
  return 0; }
