/* Producer posts small jobs to 3 workers like vfa_fetch_list, with ~gap us of its own work
 * between posts; then syncs (helping) like vfa_sync. mode 0 = old (ReleaseSemaphore per job,
 * Wait(sem,0) per helped job); mode 1 = wake only a sleeping worker, workers spin `spin` pauses
 * before sleeping. Reports the producer's time per job spent in post+sync (excl. its own gap work). */
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#define NJ 512
static volatile LONG posted, taken, done, sleepers;
static HANDLE sem; static int mode, spin; static volatile double sink;
static double now(void){ static LARGE_INTEGER f; LARGE_INTEGER t; if(!f.QuadPart)QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t); return (double)t.QuadPart*1e6/f.QuadPart; }
static void work(double us){ double t=now(); double x=1; while(now()-t<us) x=x*1.0000001+1e-9; sink=x; }
static double job_us = 4.0;
static int try_take(void){ for(;;){ LONG t=taken; if(t>=posted) return 0; if(InterlockedCompareExchange(&taken,t+1,t)==t){ work(job_us); InterlockedIncrement(&done); return 1; } } }
static int claim_sleeper(void){ LONG s=sleepers; while(s>0){ LONG o=InterlockedCompareExchange(&sleepers,s-1,s); if(o==s) return 1; s=o; } return 0; }
static DWORD WINAPI worker(LPVOID a){ (void)a;
  for(;;){
    if(mode==0){ WaitForSingleObject(sem,INFINITE); try_take(); continue; }
    if(try_take()) continue;
    int got=0; for(int i=0;i<spin;i++){ _mm_pause(); if(taken<posted){ got=1; break; } }
    if(got) continue;
    InterlockedIncrement(&sleepers);
    if(taken<posted){ if(claim_sleeper()) continue; }
    WaitForSingleObject(sem,INFINITE);
  } }
int main(int argc,char**argv){
  mode=atoi(argv[1]); spin=argc>2?atoi(argv[2]):0; double gap=argc>3?atof(argv[3]):10.0; if(argc>4) job_us=atof(argv[4]);
  sem=CreateSemaphoreA(NULL,0,1<<20,NULL);
  for(int i=0;i<3;i++) CloseHandle(CreateThread(NULL,0,worker,NULL,0,NULL));
  Sleep(50);
  double ovh=0, ovp=0; int reps=200; FILETIME a,b,c,k0,u0,k1,u1; GetThreadTimes(GetCurrentThread(),&a,&b,&k0,&u0);
  for(int r=0;r<reps;r++){
    for(int j=0;j<NJ;j++){
      work(gap);
      double t0=now();
      InterlockedIncrement(&posted);
      if(mode==0) ReleaseSemaphore(sem,1,NULL); else if(claim_sleeper()) ReleaseSemaphore(sem,1,NULL);
      ovp+=now()-t0;
    }
    double t0=now();
    while(1){ LONG t=taken; if(t>=posted) break; if(InterlockedCompareExchange(&taken,t+1,t)==t){ if(mode==0) WaitForSingleObject(sem,0); work(job_us); InterlockedIncrement(&done);} }
    while(done!=posted) _mm_pause();
    posted=taken=done=0;
    ovh+=now()-t0;
    Sleep(2);   /* between batches: workers go to sleep */
  }
  GetThreadTimes(GetCurrentThread(),&a,&b,&k1,&u1);
  double kms=((((ULONGLONG)k1.dwHighDateTime<<32)|k1.dwLowDateTime)-(((ULONGLONG)k0.dwHighDateTime<<32)|k0.dwLowDateTime))/1e4;
  { double t=now(); for(int i=0;i<100000;i++) _mm_pause(); printf("pause %.1f ns; ", (now()-t)*10); }
  printf("post %.2f us/job; ", ovp/(reps*NJ));
  printf("mode %d spin %d gap %.0f job %.0f: producer post+sync %.2f us/job, producer kernel %.1f ms total (%.2f us/job)\n",
         mode,spin,gap,job_us,(ovh+ovp)/(reps*NJ),kms,kms*1000/(reps*NJ));
  return 0; }
