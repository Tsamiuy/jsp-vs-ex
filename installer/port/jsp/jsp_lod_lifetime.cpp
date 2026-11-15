#include "jsp_lod_lifetime.h"
#include <array>
#include <atomic>
#include <cstring>
#include <string>

namespace {
std::atomic<bool> active{false};

// SHA-256 as specified by FIPS 180-4. Used only when binding the loaded image;
// no hashing occurs in a per-object callback or selection path.
class Sha256 {
    std::array<uint32_t,8> state{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    std::array<uint8_t,64> block{};
    uint64_t bytes=0;
    size_t used=0;
    static uint32_t rotate(uint32_t x,unsigned n) { return (x>>n)|(x<<(32-n)); }
    void transform() {
        static constexpr uint32_t k[64]={
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for(unsigned i=0;i<16;i++) w[i]=(uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|
            (uint32_t(block[i*4+2])<<8)|block[i*4+3];
        for(unsigned i=16;i<64;i++) {
            uint32_t a=w[i-15],b=w[i-2];
            w[i]=w[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+w[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
        }
        auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for(unsigned i=0;i<64;i++) {
            uint32_t t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            uint32_t t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    }
public:
    void update(const uint8_t* data,size_t n) {
        bytes+=n;
        while(n) {
            size_t take=n<64-used?n:64-used;
            std::memcpy(block.data()+used,data,take); used+=take;data+=take;n-=take;
            if(used==64) { transform();used=0; }
        }
    }
    std::string finish() {
        uint64_t bits=bytes*8;
        const uint8_t one=0x80,zero=0;
        update(&one,1); while(used!=56) update(&zero,1);
        uint8_t tail[8];for(unsigned i=0;i<8;i++) tail[i]=uint8_t(bits>>(56-i*8));
        update(tail,8);
        static const char hex[]="0123456789abcdef";
        std::string out;out.reserve(64);
        for(uint32_t word:state) for(int shift=28;shift>=0;shift-=4) out.push_back(hex[(word>>shift)&15]);
        return out;
    }
};

uint64_t be64(const uint8_t* p) { uint64_t v=0;for(int i=0;i<8;i++) v=(v<<8)|p[i];return v; }
uint32_t be32(const uint8_t* p) { return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3]; }
bool segment_identity(const uint8_t* data,size_t size) {
    if(size<64 || std::memcmp(data,"\x7f" "ELF\x02\x02",6)) return false;
    uint64_t offset=be64(data+32);
    unsigned entry=(unsigned(data[54])<<8)|data[55],count=(unsigned(data[56])<<8)|data[57];
    if(entry!=56 || count>64 || offset>size || uint64_t(entry)*count>size-offset) return false;
    Sha256 hash;
    for(unsigned i=0;i<count;i++) {
        const auto* ph=data+size_t(offset)+i*56;
        uint64_t at=be64(ph+8),n=be64(ph+32),mem=be64(ph+40);
        if(be32(ph)!=1 || !(n||mem)) continue;
        if(at>size || n>size-at) return false;
        hash.update(ph+16,8); hash.update(ph+32,16); // vaddr, filesz, memsz in big endian
        hash.update(data+size_t(at),size_t(n));
    }
    return hash.finish()==JSP_LOD_PROGRAM_SEGMENT_SHA256;
}
}

extern "C" bool jsp_lod_hooks_active(void) { return active.load(std::memory_order_acquire); }
extern "C" int jsp_lod_bind_loaded_image(const uint8_t* buffer,size_t size)
{
    active.store(false,std::memory_order_release);
    jsp_lod_lifetime_reset();
    if(!buffer) return 1; // revoke at the beginning of every loader attempt
    if(!JSP_LOD_ENTRY_HOOKS_VERIFIED) return 1;
    Sha256 hash;hash.update(buffer,size);
    if(hash.finish()!=JSP_LOD_PROGRAM_SHA256 && !segment_identity(buffer,size)) return 0;
    active.store(true,std::memory_order_release);
    return 1;
}
