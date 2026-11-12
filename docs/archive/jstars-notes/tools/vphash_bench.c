/* Cost of vf_input_mask's per-call work: size scan + word FNV over the VP. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>
typedef uint32_t u32; typedef uint8_t u8;
static u32 rd_le(const u8* p){u32 w;memcpy(&w,p,4);return w;}
static u32 size_instrs(const u8* uc, u32 max){u32 off=0,n=0;while(off+16<=max){u32 d3=rd_le(uc+off+12);n++;off+=16;if(d3&1u)return n;}return 0;}
static __declspec(noinline) u32 mask_hash(const u8* vuc, u32 vlen){
  u32 ni=size_instrs(vuc,vlen); if(ni) vlen=ni*16u;
  u32 h=2166136261u; for(u32 i=0;i+4<=vlen;i+=4){u32 w;memcpy(&w,vuc+i,4);h^=w;h*=16777619u;} return h;}
int main(void){ static u8 vp[512*16]; u32 s=1;
  for(int i=0;i<(int)sizeof vp;i++){s=s*1664525u+1013904223u;vp[i]=(u8)(s>>24);}
  int sizes[]={64,128,256,512};
  for(int k=0;k<4;k++){ int n=sizes[k]; for(int i=0;i<512;i++){ vp[i*16+12]&=~1u; } vp[(n-1)*16+12]|=1u;
    volatile u32 sink=0; unsigned long long t0=__rdtsc(); for(int r=0;r<20000;r++) sink+=mask_hash(vp,sizeof vp);
    unsigned long long t1=__rdtsc(); printf("VP %3d instrs (%4d B): %6.0f TSC cycles per call\n",n,n*16,(double)(t1-t0)/20000); }
  return 0; }
