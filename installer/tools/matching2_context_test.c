/* Actual Matching2 + NP adapter with stubbed external seams: fake wire,
 * VM/coherence/NP-init hooks, signaling recorder and usually callback recorder.
 * sysutil-reentry forwards to the separately linked actual sysutil queue/drain.
 * No server or guest title. Each scenario runs in a fresh process. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { G_SEND, G_PEER, G_STATUS, G_CONNECT, G_REPLY, G_TICK, G_PUSH, G_RETURN, G_SEARCH, G_COUNT };
static const char *sites[] = { "send", "peer-lock", "request-status", "connect", "reply-commit", "tick-pop", "push-commit", "request-return", "search-commit" };
static struct gate { pthread_mutex_t m; pthread_cond_t cv; int on, hit, release; } gates[G_COUNT];
static void pause_site(const char *site)
{
    for (int i = 0; i < G_COUNT; ++i) if (!strcmp(sites[i], site)) {
        struct gate *g = &gates[i];
        pthread_mutex_lock(&g->m);
        if (g->on) {
            ++g->hit; pthread_cond_broadcast(&g->cv);
            while (!g->release) pthread_cond_wait(&g->cv, &g->m);
        }
        pthread_mutex_unlock(&g->m);
    }
}
static void gate_on(int n, int release)
{
    gates[n].on = 1; gates[n].release = release; gates[n].hit = 0;
}
static struct timespec deadline(void)
{
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t); t.tv_sec += 4; return t;
}
static void gate_wait(int n)
{
    struct gate *g = &gates[n]; struct timespec t = deadline();
    pthread_mutex_lock(&g->m);
    while (!g->hit) assert(pthread_cond_timedwait(&g->cv, &g->m, &t) == 0);
    pthread_mutex_unlock(&g->m);
}
static void gate_release(int n)
{
    pthread_mutex_lock(&gates[n].m); gates[n].release = 1;
    pthread_cond_broadcast(&gates[n].cv); pthread_mutex_unlock(&gates[n].m);
}

#define NP_PSNR_TEST_PAUSE(site) pause_site(site)
#define s_lock fixture_np_lock
#include NP_SOURCE
#undef s_lock
#undef LOCK
#undef UNLOCK

/* Track each exact op pointer independently, including rejected/failing ops. */
static struct { void *ptr; int frees; } watches[256];
static struct { void *ptr; int frees; } allocations[256];
static int nwatches, nallocations;
static void *observed_calloc(size_t, size_t);
static int watch(void *p) { assert(p && nwatches < 256); watches[nwatches].ptr = p; return nwatches++; }
static void observed_free(void *p)
{
    for (int i = nallocations-1; i >= 0; --i) if (allocations[i].ptr == p && !allocations[i].frees) {
        ++allocations[i].frees; break;
    }
    for (int i = 0; i < nwatches; ++i) if (watches[i].ptr == p && !watches[i].frees) {
        ++watches[i].frees; break;
    }
    free(p);
}
#define M2_TEST_PAUSE(site) pause_site(site)
#define free observed_free
#define calloc observed_calloc
#include M2_SOURCE
#undef calloc
#undef free
static void *observed_calloc(size_t n, size_t z)
{
    void *p=calloc(n,z); assert(p && nallocations<256 && n*z==sizeof(m2_op));
    allocations[nallocations++].ptr=p;return p;
}

uint8_t *vm_base;
int g_resv_store_active;
uint32_t g_ww_lo, g_ww_hi;
void ppu_resv_break_store(uint64_t a) { (void)a; }
int spu_coh_is_reserved(uint32_t a) { (void)a; return 0; }
void spu_lockline_lock(void) {}
void spu_lockline_unlock(void) {}
void spu_coh_notify_write(uint32_t a) { (void)a; }
void ps3_ww_report_inline(uint32_t a, uint64_t b, int c) { (void)a; (void)b; (void)c; }
s32 sceNpInit(u32 a, void *p) { (void)a; (void)p; return 0; }
s32 sceNpTerm(void) { return 0; }
const char *np_fake_username(void) { return "fixtureplayer"; }

static int cbn, sign, order, peer_at_callback, real_sysutil;
static u16 peer_ports[256];
extern s32 fixture_real_queue(u32,const u64[8]);
static struct { u32 opd; u64 a[8]; } callbacks[256];
static struct { char id[17]; int up; } signals[256];
s32 cellSysutilQueueGuestCallbackArgs(u32 opd, const u64 a[8])
{
    assert(cbn < 256); callbacks[cbn].opd = opd; memcpy(callbacks[cbn++].a, a, 64); ++order;
    struct peer *observed=peer_by_user(42);peer_ports[cbn-1]=observed?observed->port:0;
    /* Direct fixture inspection: do not acquire N again inside the commit. */
    if (a[2] == EV_CreateJoinRoom || a[2] == EV_JoinRoom) {
        struct peer *p = peer_by_user(42);
        if ((u32)a[4] == 0) { assert(p && p->port == 4000); ++peer_at_callback; }
    }
    return real_sysutil ? fixture_real_queue(opd,a) : 0;
}
void np_signaling_room_peer(const char *id, const u8 ip[4], u16 port, int up)
{
    (void)ip; (void)port; assert(sign < 256);
    snprintf(signals[sign].id, 17, "%s", id); signals[sign++].up = up; ++order;
}

/* Fake only the wire transport. np_psnr_request/pump/locks/peer table are real. */
static struct { u8 type, body[4096]; u32 len, id; } sends[256];
static int nsends, fail_send, poll_disconnect;
static psnr_msg incoming[256];
static int qread, qwrite;
static u32 next_wire = 100;
psnr_client *psnr_connect(const char *h, uint16_t p, const char *c, const char *n,
                         uint16_t pp, uint32_t *uid, uint8_t ip[4])
{
    (void)h; (void)p; (void)c; (void)n; (void)pp;
    pause_site("connect"); *uid = 1; memset(ip, 0, 4); return (psnr_client *)(uintptr_t)1;
}
int psnr_connect_error(void) { return 0; }
void psnr_close(psnr_client *p) { (void)p; }
uint32_t psnr_send(psnr_client *c, uint8_t t, const void *b, uint32_t n)
{
    (void)c; pause_site("send"); assert(nsends < 256 && n <= 4096);
    int i = nsends++; sends[i].type = t; sends[i].len = n; memcpy(sends[i].body, b, n);
    sends[i].id = fail_send ? 0 : ++next_wire;
    return sends[i].id;
}
int psnr_poll(psnr_client *c, psnr_msg *m)
{
    (void)c;
    if (poll_disconnect) { poll_disconnect = 0; return -1; }
    if (qread == qwrite) return 0;
    *m = incoming[qread++]; return 1;
}
void psnr_msg_free(psnr_msg *m) { (void)m; }
void psnr_server_addr(const psnr_client *c, uint8_t ip[4], uint16_t *p) { (void)c; memset(ip, 0, 4); *p = 0; }
void psnr_probe_packet(const psnr_client *c, uint8_t b[PSNR_UDP_PROBE_LEN]) { (void)c; memset(b, 0, PSNR_UDP_PROBE_LEN); }
void psnr_punch_packet(const psnr_client *c, uint8_t b[PSNR_UDP_PUNCH_LEN]) { (void)c; memset(b, 0, PSNR_UDP_PUNCH_LEN); }
int psnr_is_control(const void *b, size_t n) { (void)b; (void)n; return 0; }
int psnr_probe_reply(const void *b, size_t n, uint8_t ip[4], uint16_t *p) { (void)b; (void)n; (void)ip; (void)p; return 0; }
int psnr_relay_available(const psnr_client *c) { (void)c; return 0; }
size_t psnr_relay_wrap(const psnr_client *c, uint32_t u, const void *b, size_t n, uint8_t *o, size_t cap)
{ (void)c; (void)u; (void)b; (void)n; (void)o; (void)cap; return 0; }
int psnr_relay_unwrap(const void *b, size_t n, uint32_t *u, const uint8_t **p, size_t *l)
{ (void)b; (void)n; (void)u; (void)p; (void)l; return 0; }
int64_t psnr_stream_connect(const psnr_client *c, uint32_t u, uint16_t p, int t) { (void)c; (void)u; (void)p; (void)t; return -1; }
int64_t psnr_stream_accept(const psnr_client *c, uint32_t i, int t) { (void)c; (void)i; (void)t; return -1; }

enum { CTXOUT=0x100, COMM=0x110, OPT=0x140, REQID=0x180, REQ=0x200, IDS=0x600, MSG=0x800 };
static u16 context;
static void setup(void)
{
    for (int i=0;i<G_COUNT;++i) { pthread_mutex_init(&gates[i].m,0); pthread_cond_init(&gates[i].cv,0); }
    vm_base = calloc(1, 1u << 20); assert(vm_base);
    setenv("PS3_NET_ONLINE", "1", 1); setenv("PSNR_SERVER", "127.0.0.1:1", 1);
    setenv("PS3_NP_SIGNALING_DELAY_MS", "60000", 1); /* fixture delay exceeds alarm */
    np_psnr_setup("fixtureplayer", "127.0.0.1:1");
    assert(sceNpMatching2Init2(0,0,0) == 0);
    memcpy(vm_base+COMM, "NPWR01444", 9);
    assert(sceNpMatching2CreateContext(0,COMM,0,CTXOUT,0) == 0); context=vm_read16(CTXOUT);
    vm_write32(OPT,0xCB10); vm_write32(OPT+4,0xAA10);
    assert(sceNpMatching2SetDefaultRequestOptParam(context,OPT) == 0);
    assert(sceNpMatching2RegisterContextCallback(context,0xCC10,0) == 0);
    assert(sceNpMatching2RegisterRoomEventCallback(context,0xCE10,0) == 0);
    assert(sceNpMatching2RegisterSignalingCallback(context,0xCF10,0) == 0);
    assert(sceNpMatching2ContextStart(context) == 0);
}
static void replace(int how)
{
    if (how == 0) {
        assert(sceNpMatching2DestroyContext(context)==0);
        assert(sceNpMatching2CreateContext(0,COMM,0,CTXOUT,0)==0); assert(vm_read16(CTXOUT)==context);
    } else if (how == 1) assert(sceNpMatching2ContextStop(context)==0);
    else {
        assert(sceNpMatching2Term()==0); assert(sceNpMatching2Init2(0,0,0)==0);
        assert(sceNpMatching2CreateContext(0,COMM,0,CTXOUT,0)==0); assert(vm_read16(CTXOUT)==context);
    }
    vm_write32(OPT,0xCB20); vm_write32(OPT+4,0xAA20);
    assert(sceNpMatching2SetDefaultRequestOptParam(context,OPT)==0);
    assert(sceNpMatching2RegisterRoomEventCallback(context,0xCE20,0)==0);
    assert(sceNpMatching2RegisterSignalingCallback(context,0xCF20,0)==0);
    assert(sceNpMatching2ContextStart(context)==0);
}
static u32 room_body(u8 *r, u64 room)
{
    u8 *p=psnr_put64(r,room); p=psnr_put16(p,1); p=psnr_put16(p,1); *p++=4;
    p=psnr_put32(p,0); p=psnr_put16(p,0); p=psnr_put16(p,0); *p++=2;
    for(int i=0;i<2;++i) {
        p=psnr_put16(p,(u16)(i+1)); p=psnr_put32(p,i?42:1);
        memset(p,0,16); memcpy(p,i?"peer":"self",4); p+=16;
        *p++=127;*p++=0;*p++=0;*p++=1;p=psnr_put16(p,4000);*p++=(u8)!i;p=psnr_put16(p,0);
    }
    return (u32)(p-r);
}
static void queue_reply(int send, u8 type, u8 *data, u32 len)
{
    assert(qwrite<256); incoming[qwrite++]=(psnr_msg){type,sends[send].id,data,len};
}
static int op_watch(int send)
{
    for(int i=0;i<PENDING_MAX;++i) if(s_pending[i].req==sends[send].id) return watch(s_pending[i].user);
    assert(!"missing pending owner"); return -1;
}
static int begin_join(void)
{
    memset(vm_base+REQ,0,112);vm_write32(REQ+16,4);int s=nsends;
    assert(sceNpMatching2CreateJoinRoom(context,REQ,0,REQID)==0);assert(nsends==s+1);return s;
}
static void join_room(u64 id)
{
    int s=begin_join(), w=op_watch(s); u8 b[128];u32 n=room_body(b,id);
    queue_reply(s,PSNR_ROOM_JOINED,b,n);np_psnr_pump();
    assert(watches[w].frees==1 && s_ctx[context].room.in && s_ctx[context].room.id==id);
    assert(callbacks[cbn-1].opd==s_ctx[context].def_cb && callbacks[cbn-1].a[6]==s_ctx[context].def_arg);
}
static void stale(const char *kind, int how)
{
    join_room(9); int s=nsends;
    if(!strcmp(kind,"join")) s=begin_join();
    else if(!strcmp(kind,"leave")) { vm_write64(REQ,9);assert(sceNpMatching2LeaveRoom(context,REQ,0,REQID)==0); }
    else { memset(vm_base+REQ,0,112);assert(sceNpMatching2SearchRoom(context,REQ,0,REQID)==0); }
    int w=op_watch(s);
    if(how<3) {replace(how);join_room(77);}
    else if(how==3) assert(sceNpMatching2ContextStop(context)==0);
    else assert(sceNpMatching2Term()==0);
    assert(watches[w].frees==0);m2_ctx before=s_ctx[context];
    u8 deferred[sizeof(s_sig)];memcpy(deferred,s_sig,sizeof(s_sig));
    int c=cbn,sg=sign,pa=peer_at_callback;u32 key=s_next_key;
    struct peer peers[MAX_PEERS];memcpy(peers,s_peers,sizeof(peers));
    u8 b[128];u32 n=room_body(b,88);
    if(!strcmp(kind,"join")) queue_reply(s,PSNR_ROOM_JOINED,b,n);
    else if(!strcmp(kind,"leave")) { psnr_put32(b,PSNR_E_NOT_IN_ROOM);queue_reply(s,PSNR_ERROR,b,4); }
    else { memset(b,0,4);queue_reply(s,PSNR_ROOM_LIST,b,4); }
    np_psnr_pump();
    assert(watches[w].frees==1 && cbn==c && sign==sg && s_next_key==key && peer_at_callback==pa);
    assert(!memcmp(&before,&s_ctx[context],sizeof(before)) && !memcmp(peers,s_peers,sizeof(peers)) && !memcmp(deferred,s_sig,sizeof(s_sig)));
}

static s32 worker_result;
static atomic_int lifecycle_done;
static pthread_mutex_t done_m=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t done_cv=PTHREAD_COND_INITIALIZER;
static int lifecycle_kind;
static void *lifecycle(void *unused)
{
    (void)unused;
    s32 r=lifecycle_kind==0?sceNpMatching2ContextStop(context):lifecycle_kind==1?sceNpMatching2DestroyContext(context):sceNpMatching2Term();
    assert(r==0);pthread_mutex_lock(&done_m);atomic_store(&lifecycle_done,1);pthread_cond_broadcast(&done_cv);pthread_mutex_unlock(&done_m);return 0;
}
static void done_before_release(void)
{
    struct timespec t=deadline();pthread_mutex_lock(&done_m);
    while(!atomic_load(&lifecycle_done)) assert(pthread_cond_timedwait(&done_cv,&done_m,&t)==0);
    pthread_mutex_unlock(&done_m);
}
static void *pump_thread(void *unused) { (void)unused;np_psnr_pump();return 0; }
static void *send_thread(void *unused) { (void)unused;np_psnr_request(PSNR_HEARTBEAT,"",0,0,0);return 0; }
static void *status_thread(void *unused) { (void)unused;worker_result=sceNpMatching2SetRoomDataExternal(context,REQ,0,REQID);return 0; }
static void *start_thread(void *unused) { (void)unused;worker_result=sceNpMatching2ContextStart(context);return 0; }
static void *tick_thread(void *unused) { (void)unused;m2_tick();return 0; }
typedef struct { np_psnr_reply_fn fn;void *user;psnr_msg m; } claimed_t;
static void *claimed_thread(void *p)
{
    claimed_t *c=p;c->fn(c->user,&c->m);return 0;
}
static void blocked_np(int how)
{
    int s=begin_join(),w=op_watch(s);u8 b[128];queue_reply(s,PSNR_ROOM_JOINED,b,room_body(b,9));
    /* A directly claimed completion models pump's already-unlocked callback. */
    np_psnr_reply_fn fn=0;void *user=0;
    for(int i=0;i<PENDING_MAX;++i) if(s_pending[i].req==sends[s].id) { fn=s_pending[i].fn;user=s_pending[i].user;s_pending[i].req=0;break; }
    assert(fn);qread=qwrite;
    claimed_t claim={fn,user,{PSNR_ROOM_JOINED,sends[s].id,b,room_body(b,9)}};
    gate_on(G_SEND,0);pthread_t send,reply,life;
    assert(!pthread_create(&send,0,send_thread,0));gate_wait(G_SEND); /* real np_psnr_request owns N */
    gate_on(G_PEER,1);
    assert(!pthread_create(&reply,0,claimed_thread,&claim));gate_wait(G_PEER);
    lifecycle_kind=how;assert(!pthread_create(&life,0,lifecycle,0));done_before_release();
    assert(watches[w].frees==0);int c=cbn,sg=sign;u32 key=s_next_key;
    gate_release(G_SEND);pthread_join(send,0);pthread_join(reply,0);pthread_join(life,0);
    assert(watches[w].frees==1 && cbn==c && sign==sg && s_next_key==key && !peer_by_user(42));
}
static void status_gap(void)
{
    join_room(9);memset(vm_base+REQ,0,112);vm_write64(REQ,9);
    gate_on(G_STATUS,0);pthread_t t;assert(!pthread_create(&t,0,status_thread,0));gate_wait(G_STATUS);
    /* Disable only future hits: replacement uses public Start and fresh Join. */
    pthread_mutex_lock(&gates[G_STATUS].m);gates[G_STATUS].on=0;pthread_mutex_unlock(&gates[G_STATUS].m);
    replace(0);join_room(77);int n=nsends,c=cbn;u32 key=s_next_key;
    gate_release(G_STATUS);pthread_join(t,0);
    assert(worker_result!=0 && nsends==n && cbn==c && s_next_key==key && s_ctx[context].room.id==77);
}
static void delayed_start(int exhaustion)
{
    assert(sceNpMatching2ContextStop(context)==0);
    if(exhaustion) s_owner[context]=UINT64_MAX;
    /* Let real connect run despite prior successful setup. */
    s_client=0;s_tried=0;gate_on(G_CONNECT,0);pthread_t t;assert(!pthread_create(&t,0,start_thread,0));gate_wait(G_CONNECT);
    assert(sceNpMatching2ContextStop(context)==0);int c=cbn;
    gate_release(G_CONNECT);pthread_join(t,0);
    assert(worker_result!=0 && !s_ctx[context].started && cbn==c);
    if(exhaustion) {assert(s_exhausted[context]);assert(sceNpMatching2ContextStart(context)!=0);}
}
static void local_reject(int hold)
{
    /* Holding N in THIS thread turns accidental transport access into timeout. */
    if(hold) pthread_mutex_lock(&fixture_np_lock);
    assert(sceNpMatching2SearchRoom(0,REQ,0,0)==(s32)SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_FOUND);
    assert(sceNpMatching2ContextStop(context)==0);
    assert(sceNpMatching2SearchRoom(context,REQ,0,0)==(s32)SCE_NP_MATCHING2_ERROR_SERVER_NOT_AVAILABLE);
    assert(sceNpMatching2Term()==0);
    assert(sceNpMatching2SearchRoom(context,REQ,0,0)==(s32)SCE_NP_MATCHING2_ERROR_NOT_INITIALIZED);
    if(hold) pthread_mutex_unlock(&fixture_np_lock);
}
static void setters(int fail)
{
    join_room(9);memset(vm_base+REQ,0,112);vm_write64(REQ,9);vm_write32(REQ+8,0xFF);vm_write32(REQ+12,0x5A);
    int n=nsends,na=nallocations,c=cbn;fail_send=fail;
    s32 r=sceNpMatching2SetRoomDataInternal(context,REQ,0,REQID);
    assert(nsends==n+2 && sends[n].len==15 && sends[n].type==PSNR_SET_ROOM_DATA);
    assert(psnr_get64(sends[n].body)==9 && sends[n].body[8]==2 && psnr_get16(sends[n].body+9)==4 && psnr_get32(sends[n].body+11)==0x5A);
    assert(sends[n+1].body[8]==1 && s_ctx[context].room.flags==0x5A);
    if(fail) assert(r==(s32)SCE_NP_MATCHING2_ERROR_NOT_CONNECTED && nallocations==na+1 && allocations[na].frees==1 && cbn==c);
    else {
        int w=op_watch(n+1);queue_reply(n+1,PSNR_OK,0,0);np_psnr_pump();
        assert(watches[w].frees==1 && cbn==c+2);
        assert(callbacks[c].opd==0xCB10 && callbacks[c].a[0]==context && callbacks[c].a[1]==vm_read32(REQID) && callbacks[c].a[2]==EV_SetRoomDataInternal && callbacks[c].a[6]==0xAA10 && callbacks[c].a[4]==0);
        assert(callbacks[c+1].opd==0xCE10 && callbacks[c+1].a[0]==context && callbacks[c+1].a[1]==9 && callbacks[c+1].a[2]==ROOM_EV_UpdatedRoomDataInternal && callbacks[c+1].a[4]==0);
        assert(sceNpMatching2GetEventData(context,(u32)callbacks[c+1].a[3],0x10000,4096)>0);
        assert(vm_read32(vm_read32(0x10004))==0x5A && vm_read32(vm_read32(0x10008))==0);
        assert(vm_read64(vm_read32(0x10000)+16)==9 && vm_read32(vm_read32(0x10000)+60)==0x5A);
    }
}
static void messages(int zero, int fail)
{
    memset(vm_base+REQ,0,112);vm_write64(REQ,9);vm_write8(REQ+8,CAST_MULTICAST);
    vm_write32(REQ+12,zero?0:IDS);vm_write32(REQ+16,zero?0:3);
    for(int i=0;i<3;++i)vm_write16(IDS+2*i,(u16)(i+2));
    memcpy(vm_base+MSG,"abc",3);vm_write32(REQ+20,MSG);vm_write32(REQ+24,3);vm_write32(REQID,0x12345678);
    int n=nsends,c=cbn,na=nallocations;u32 nr=s_next_req;fail_send=fail;
    s32 r=sceNpMatching2SendRoomMessage(context,REQ,0,REQID);
    if(zero) {assert(!r && nsends==n && cbn==c && s_next_req==nr && nallocations==na && vm_read32(REQID)==0x12345678);return;}
    assert(nsends==n+3);
    for(int i=0;i<3;++i)assert(psnr_get16(sends[n+i].body+8)==i+2 && sends[n+i].len==15 && !memcmp(sends[n+i].body+12,"abc",3));
    if(fail) assert(r==(s32)SCE_NP_MATCHING2_ERROR_NOT_CONNECTED && nallocations==na+1 && allocations[na].frees==1 && cbn==c);
    else {int w=op_watch(n+2);queue_reply(n+2,PSNR_OK,0,0);np_psnr_pump();assert(watches[w].frees==1 && cbn==c+1 && callbacks[cbn-1].a[2]==EV_SendRoomMessage);}
}
static void epoch_exhaustion(int how)
{
    int s=begin_join(),w=op_watch(s);s_owner[context]=UINT64_MAX;
    /* Make the already owned op carry the last valid token for invalidation. */
    ((m2_op*)watches[w].ptr)->owner=UINT64_MAX;
    if(how==0) assert(sceNpMatching2ContextStop(context)==0);
    else if(how==1) assert(sceNpMatching2DestroyContext(context)==0);
    else assert(sceNpMatching2Term()==0);
    assert(s_exhausted[context]);
    assert(sceNpMatching2ContextStart(context)!=0);
    if(how==0) assert(sceNpMatching2DestroyContext(context)==0);
    else if(how==2) assert(sceNpMatching2Init2(0,0,0)==0);
    assert(sceNpMatching2CreateContext(0,COMM,0,CTXOUT,0)==0);assert(vm_read16(CTXOUT)!=context);
    assert(sceNpMatching2Term()==0 && sceNpMatching2Init2(0,0,0)==0);
    assert(sceNpMatching2CreateContext(0,COMM,0,CTXOUT,0)==0 && vm_read16(CTXOUT)!=context);
    int c=cbn;u8 b[128];queue_reply(s,PSNR_ROOM_JOINED,b,room_body(b,88));np_psnr_pump();assert(cbn==c && watches[w].frees==1);
}
static void tick_case(int stale_owner)
{
    join_room(9);int slot=-1;
    for(int i=0;i<SIG_PENDING_MAX;++i)if(s_sig[i].used){s_sig[i].due=0;slot=i;break;}
    assert(slot>=0);
    if(stale_owner) {s_sig[slot].owner--;int c=cbn,sg=sign;m2_tick();assert(cbn==c && sign==sg);return;}
    gate_on(G_TICK,0);pthread_t t;assert(!pthread_create(&t,0,tick_thread,0));gate_wait(G_TICK);
    assert(pthread_mutex_trylock(&s_state_lock)==EBUSY); /* check/commit remains serialized */
    pthread_t life;lifecycle_kind=0;assert(!pthread_create(&life,0,lifecycle,0));
    gate_release(G_TICK);pthread_join(t,0);pthread_join(life,0);
    assert(sign==1 && signals[0].up==1);
    int c=cbn,sg=sign;m2_tick();assert(cbn==c && sign==sg);
}
static void current_controls(void)
{
    join_room(9);assert(peer_at_callback==1);
    int s=nsends;vm_write64(REQ,9);assert(sceNpMatching2LeaveRoom(context,REQ,0,REQID)==0);
    int w=op_watch(s);queue_reply(s,PSNR_OK,0,0);np_psnr_pump();
    assert(watches[w].frees==1 && !s_ctx[context].room.in && callbacks[cbn-1].opd==0xCB10);
    s=begin_join();w=op_watch(s);int c=cbn;poll_disconnect=1;np_psnr_pump();
    assert(watches[w].frees==1 && cbn==c+1 && (u32)callbacks[cbn-1].a[4]!=0);
}
static void repeat_start(void)
{
    int s=begin_join(),w=op_watch(s);assert(sceNpMatching2ContextStart(context)==0);int c=cbn;
    u8 b[128];queue_reply(s,PSNR_ROOM_JOINED,b,room_body(b,9));np_psnr_pump();
    assert(watches[w].frees==1 && cbn==c+1 && callbacks[c].opd==0xCB10 && s_ctx[context].room.id==9);
    s=nsends;vm_write64(REQ,9);assert(sceNpMatching2LeaveRoom(context,REQ,0,REQID)==0);w=op_watch(s);
    assert(sceNpMatching2ContextStart(context)==0);c=cbn;queue_reply(s,PSNR_OK,0,0);np_psnr_pump();
    assert(watches[w].frees==1 && cbn>c && callbacks[cbn-1].opd==0xCB10 && !s_ctx[context].room.in);
}
static void search_controls(int explicit_opt, int error, int disconnect)
{
    memset(vm_base+REQ,0,112);int s=nsends,c=cbn;
    vm_write32(OPT,0xCA30);vm_write32(OPT+4,0xAA30);
    assert(sceNpMatching2SearchRoom(context,REQ,explicit_opt?OPT:0,REQID)==0);
    int w=op_watch(s);u8 b[4]={0};
    if(disconnect) poll_disconnect=1;
    else if(error) {psnr_put32(b,PSNR_E_NOT_FOUND);queue_reply(s,PSNR_ERROR,b,4);}
    else queue_reply(s,PSNR_ROOM_LIST,b,4);
    np_psnr_pump();assert(watches[w].frees==1 && cbn==c+1);
    assert(callbacks[c].opd==(explicit_opt?0xCA30:0xCB10) && callbacks[c].a[6]==(explicit_opt?0xAA30:0xAA10));
    assert(((u32)callbacks[c].a[4]!=0)==(error||disconnect));
    if(error) assert((u32)callbacks[c].a[4]==SCE_NP_MATCHING2_SERVER_ERROR_NO_SUCH_ROOM);
    if(disconnect) assert((u32)callbacks[c].a[4]==SCE_NP_MATCHING2_ERROR_NOT_CONNECTED);
    if(!error && !disconnect) {
        assert(callbacks[c].a[3] && callbacks[c].a[5]>=16);
        assert(sceNpMatching2GetEventData(context,(u32)callbacks[c].a[3],0x10000,4096)>0);
    }
}
static void done_disconnect(void)
{
    join_room(9);int s=nsends,c=cbn;vm_write64(REQ,9);
    assert(sceNpMatching2LeaveRoom(context,REQ,0,REQID)==0);int w=op_watch(s);
    poll_disconnect=1;np_psnr_pump();assert(watches[w].frees==1 && cbn>c && (u32)callbacks[cbn-1].a[4]!=0);
}
static void current_error(int join)
{
    int s=nsends,c=cbn;
    if(join) s=begin_join();
    else {join_room(9);s=nsends;c=cbn;vm_write64(REQ,9);assert(sceNpMatching2LeaveRoom(context,REQ,0,REQID)==0);}
    int w=op_watch(s);u8 b[4];psnr_put32(b,PSNR_E_ROOM_FULL);queue_reply(s,PSNR_ERROR,b,4);np_psnr_pump();
    assert(watches[w].frees==1 && cbn>c && (u32)callbacks[cbn-1].a[4]==SCE_NP_MATCHING2_SERVER_ERROR_ROOM_FULL);
}
static void send_failure(const char *kind)
{
    int na=nallocations,c=cbn;fail_send=1;s32 r;
    memset(vm_base+REQ,0,112);
    if(!strcmp(kind,"join")) r=sceNpMatching2JoinRoom(context,REQ,0,REQID);
    else if(!strcmp(kind,"leave")) r=sceNpMatching2LeaveRoom(context,REQ,0,REQID);
    else r=sceNpMatching2SearchRoom(context,REQ,0,REQID);
    assert(r==(s32)SCE_NP_MATCHING2_ERROR_NOT_CONNECTED && nallocations==na+1 && allocations[na].frees==1 && cbn==c);
}
static int reentries;
static void reenter(void)
{
    assert(!pthread_mutex_trylock(&fixture_np_lock));pthread_mutex_unlock(&fixture_np_lock);
    (void)np_psnr_connected();np_psnr_request(PSNR_HEARTBEAT,"",0,0,0);++reentries;
}
static void reentry_push(const psnr_msg *m) { (void)m;reenter(); }
static void reentry_reply(void *u,const psnr_msg *m) { (void)u;(void)m;reenter(); }
static void transport_reentry(void)
{
    np_psnr_on_tick(reenter);np_psnr_on_push(reentry_push);
    u32 id=np_psnr_request(PSNR_HEARTBEAT,"",0,reentry_reply,0);
    incoming[qwrite++]=(psnr_msg){PSNR_OK,id,0,0};incoming[qwrite++]=(psnr_msg){PSNR_ROOM_LIST,0,0,0};
    np_psnr_pump();assert(reentries==3);
    id=np_psnr_request(PSNR_HEARTBEAT,"",0,reentry_reply,0);assert(id);poll_disconnect=1;
    np_psnr_pump();assert(reentries==5);
}
static void *commit_thread(void *unused) { (void)unused;np_psnr_pump();return 0; }
static void commit_serialization(void)
{
    int s=begin_join(),w=op_watch(s);u8 b[128];queue_reply(s,PSNR_ROOM_JOINED,b,room_body(b,9));
    gate_on(G_REPLY,0);pthread_t t;assert(!pthread_create(&t,0,commit_thread,0));gate_wait(G_REPLY);
    assert(pthread_mutex_trylock(&s_state_lock)==EBUSY);
    pthread_t life;lifecycle_kind=1;assert(!pthread_create(&life,0,lifecycle,0));
    gate_release(G_REPLY);pthread_join(t,0);pthread_join(life,0);
    assert(watches[w].frees==1 && !s_ctx[context].in_use && peer_at_callback==1);
}
static void search_commit(void)
{
    memset(vm_base+REQ,0,112);int s=nsends,c=cbn;assert(sceNpMatching2SearchRoom(context,REQ,0,REQID)==0);
    int w=op_watch(s);u8 b[4]={0};queue_reply(s,PSNR_ROOM_LIST,b,4);
    gate_on(G_SEARCH,0);pthread_t reply,life;assert(!pthread_create(&reply,0,pump_thread,0));gate_wait(G_SEARCH);
    assert(pthread_mutex_trylock(&s_state_lock)==EBUSY);lifecycle_kind=1;
    assert(!pthread_create(&life,0,lifecycle,0));gate_release(G_SEARCH);pthread_join(reply,0);pthread_join(life,0);
    assert(watches[w].frees==1 && cbn==c+1 && callbacks[c].a[3] && !s_ctx[context].in_use);
}
/* Actual cellSysutil queue/drain, linked separately to preserve its own globals. */
#include "ps3emu/guest_call.h"
void cellMsgDialog_tick(void) {}
void cellOskDialog_tick(void) {}
void ps3_np_eula_tick(void) {}
static int delivered;
static void guest_callback(u32 opd,u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h)
{
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;
    assert(!pthread_mutex_trylock(&fixture_np_lock));pthread_mutex_unlock(&fixture_np_lock);
    assert(!pthread_mutex_trylock(&s_state_lock));pthread_mutex_unlock(&s_state_lock);
    ++delivered;
    if(opd==0xCB10) {
        assert(np_psnr_connected());assert(sceNpMatching2RegisterContextCallback(context,0,0)==0);
        assert(sceNpMatching2ContextStop(context)==0);
        const u64 next[8]={0};assert(fixture_real_queue(0xEE20,next)==0);
        cellSysutilCheckCallback();assert(delivered==1); /* TLS drain guard */
    } else assert(opd==0xEE20);
}
static void sysutil_reentry(void)
{
    real_sysutil=1;g_ps3_guest_caller=guest_callback;
    int s=begin_join(),w=op_watch(s);u8 b[128];queue_reply(s,PSNR_ROOM_JOINED,b,room_body(b,9));
    assert(cellSysutilCheckCallback()==0 && delivered==1 && watches[w].frees==1);
    assert(cellSysutilCheckCallback()==0 && delivered==2);
}
static void member_push(psnr_msg *m, u8 *body, u16 id, u16 port)
{
    u8 *p=psnr_put64(body,9);p=psnr_put16(p,id);p=psnr_put32(p,42);
    memset(p,0,16);memcpy(p,"peer",4);p+=16;*p++=127;*p++=0;*p++=0;*p++=1;
    p=psnr_put16(p,port);*p++=0;p=psnr_put16(p,0);*m=(psnr_msg){PSNR_MEMBER_JOINED,0,body,(u32)(p-body)};
}
static void *push_thread(void *p) {on_push(p);return 0;}
static void endpoint_order(void)
{
    join_room(9);u8 a[64],b[64];psnr_msg ma,mb;member_push(&ma,a,3,5000);member_push(&mb,b,4,6000);
    gate_on(G_PUSH,0);gate_on(G_PEER,1);pthread_t ta,tb;
    assert(!pthread_create(&ta,0,push_thread,&ma));gate_wait(G_PUSH); /* owns N then M */
    assert(!pthread_create(&tb,0,push_thread,&mb));
    struct timespec t=deadline();pthread_mutex_lock(&gates[G_PEER].m);
    while(gates[G_PEER].hit<2)assert(!pthread_cond_timedwait(&gates[G_PEER].cv,&gates[G_PEER].m,&t));
    pthread_mutex_unlock(&gates[G_PEER].m);int c=cbn;
    gate_release(G_PUSH);pthread_join(ta,0);pthread_join(tb,0);
    assert(cbn==c+2 && peer_ports[c]==5000 && peer_ports[c+1]==6000 && peer_by_user(42)->port==6000);
}
static void *join_thread(void *p) {(void)p;worker_result=sceNpMatching2CreateJoinRoom(context,REQ,0,REQID);return 0;}
static void reply_before_return(void)
{
    memset(vm_base+REQ,0,112);vm_write32(REQ+16,4);int s=nsends;
    gate_on(G_RETURN,0);pthread_t t;assert(!pthread_create(&t,0,join_thread,0));gate_wait(G_RETURN);
    int w=op_watch(s);u8 b[128];queue_reply(s,PSNR_ROOM_JOINED,b,room_body(b,9));np_psnr_pump();
    assert(watches[w].frees==1 && s_ctx[context].room.id==9);
    gate_release(G_RETURN);pthread_join(t,0);assert(worker_result==0 && watches[w].frees==1);
}
static void *leave_rejoin(void *unused)
{
    (void)unused;u8 b[64],j[64];psnr_put16(psnr_put16(psnr_put64(b,9),2),1);
    psnr_msg left={PSNR_MEMBER_LEFT,0,b,12};on_push(&left);
    psnr_msg joined;member_push(&joined,j,2,4000);on_push(&joined);return 0;
}
static void tick_member(void)
{
    join_room(9);for(int i=0;i<SIG_PENDING_MAX;++i)if(s_sig[i].used)s_sig[i].due=0;
    gate_on(G_TICK,0);pthread_t tick,mutate;assert(!pthread_create(&tick,0,tick_thread,0));gate_wait(G_TICK);
    assert(pthread_mutex_trylock(&s_state_lock)==EBUSY);
    assert(!pthread_create(&mutate,0,leave_rejoin,0));gate_release(G_TICK);
    pthread_join(tick,0);pthread_join(mutate,0);
    assert(sign==2 && signals[0].up==1 && signals[1].up==0 && room_member(&s_ctx[context].room,2));
    for(int i=0;i<SIG_PENDING_MAX;++i)if(s_sig[i].used)s_sig[i].due=0;
    m2_tick();assert(sign==3 && signals[2].up==1);
}
/* Patch 7924: the room view and the join target (Discord join): a search lists only the target, and the target's fate is recorded. */
static u8 list_buf[256];
static u32 room_list(u64 a, u8 cur_a, u64 b, u8 cur_b)
{
    u8 *p=psnr_put16(list_buf,2);p=psnr_put16(p,2);
    const u64 id[2]={a,b};const u8 cur[2]={cur_a,cur_b};
    for(int i=0;i<2;++i){p=psnr_put64(p,id[i]);memset(p,0,16);memcpy(p,"host",4);p+=16;*p++=cur[i];*p++=2;p=psnr_put32(p,0);p=psnr_put16(p,0);}
    *p++=0;*p++=0;return (u32)(p-list_buf);
}
static u32 search_total(u64 a, u8 cur_a, u64 b, u8 cur_b)
{
    memset(vm_base+REQ,0,112);vm_write32(REQ+20,10);int s=nsends,c=cbn;
    assert(sceNpMatching2SearchRoom(context,REQ,0,REQID)==0);
    queue_reply(s,PSNR_ROOM_LIST,list_buf,room_list(a,cur_a,b,cur_b));np_psnr_pump();
    assert(cbn==c+1 && (u32)callbacks[c].a[4]==0);
    assert(sceNpMatching2GetEventData(context,(u32)callbacks[c].a[3],0x10000,4096)>0);
    return vm_read32(0x10000+4);
}
static void join_target(void)
{
    u64 room=1;int members=-1,max=-1,own=-1;char peer[17]="x";
    assert(ps3_np_matching2_room(&room,&members,&max,&own,peer,sizeof peer)==1 && room==0 && members==0 && !peer[0]);
    assert(ps3_np_matching2_join_target(&room)==PS3_M2_JOIN_NONE && room==0);
    assert(search_total(54,1,55,1)==2);                                     /* no target: every room */
    ps3_np_matching2_set_join_target(55);
    assert(ps3_np_matching2_join_target(&room)==PS3_M2_JOIN_PENDING && room==55);
    assert(search_total(54,1,55,1)==1 && ps3_np_matching2_join_target(0)==PS3_M2_JOIN_LISTED);
    assert(search_total(54,1,56,1)==0 && ps3_np_matching2_join_target(0)==PS3_M2_JOIN_NOT_FOUND);   /* stale room */
    search_total(54,1,55,2);assert(ps3_np_matching2_join_target(0)==PS3_M2_JOIN_FULL);           /* full room */
    int s=nsends;memset(vm_base+REQ,0,64);vm_write64(REQ,55);
    assert(sceNpMatching2JoinRoom(context,REQ,0,REQID)==0);u8 e[4];psnr_put32(e,PSNR_E_ROOM_FULL);
    queue_reply(s,PSNR_ERROR,e,4);np_psnr_pump();assert(ps3_np_matching2_join_target(0)==PS3_M2_JOIN_FULL);
    s=nsends;assert(sceNpMatching2JoinRoom(context,REQ,0,REQID)==0);
    u8 b[128];queue_reply(s,PSNR_ROOM_JOINED,b,room_body(b,55));np_psnr_pump();
    assert(ps3_np_matching2_join_target(0)==PS3_M2_JOIN_JOINED);
    assert(ps3_np_matching2_room(&room,&members,&max,&own,peer,sizeof peer)==2 && room==55 && members==2 && max==4 && own==1 && !strcmp(peer,"peer"));
    ps3_np_matching2_set_join_target(55);assert(ps3_np_matching2_join_target(0)==PS3_M2_JOIN_JOINED);   /* already there */
    ps3_np_matching2_set_join_target(0);assert(ps3_np_matching2_join_target(&room)==PS3_M2_JOIN_NONE && room==0);
}
int main(int argc,char **argv)
{
    assert(argc==2);alarm(15);setup();const char *s=argv[1];
    if(!strncmp(s,"stale-",6)){char kind[16];int how;assert(sscanf(s,"stale-%15[^-]-%d",kind,&how)==2);stale(kind,how);}
    else if(!strncmp(s,"blocked-",8))blocked_np(atoi(s+8));
    else if(!strcmp(s,"status-gap"))status_gap();
    else if(!strcmp(s,"delayed-start"))delayed_start(0);
    else if(!strcmp(s,"max-start"))delayed_start(1);
    else if(!strcmp(s,"local-reject"))local_reject(1);
    else if(!strcmp(s,"local-errors"))local_reject(0);
    else if(!strcmp(s,"set-internal"))setters(0);
    else if(!strcmp(s,"set-failure"))setters(1);
    else if(!strcmp(s,"message-zero"))messages(1,0);
    else if(!strcmp(s,"message-multi"))messages(0,0);
    else if(!strcmp(s,"message-failure"))messages(0,1);
    else if(!strncmp(s,"exhaustion-",11))epoch_exhaustion(atoi(s+11));
    else if(!strcmp(s,"tick"))tick_case(0);
    else if(!strcmp(s,"tick-stale"))tick_case(1);
    else if(!strcmp(s,"current"))current_controls();
    else if(!strcmp(s,"commit"))commit_serialization();
    else if(!strcmp(s,"search-default"))search_controls(0,0,0);
    else if(!strcmp(s,"search-explicit"))search_controls(1,0,0);
    else if(!strcmp(s,"search-error"))search_controls(0,1,0);
    else if(!strcmp(s,"search-disconnect"))search_controls(0,0,1);
    else if(!strcmp(s,"done-disconnect"))done_disconnect();
    else if(!strncmp(s,"fail-",5))send_failure(s+5);
    else if(!strcmp(s,"transport-reentry"))transport_reentry();
    else if(!strcmp(s,"sysutil-reentry"))sysutil_reentry();
    else if(!strcmp(s,"endpoint-order"))endpoint_order();
    else if(!strcmp(s,"reply-before-return"))reply_before_return();
    else if(!strcmp(s,"tick-member"))tick_member();
    else if(!strcmp(s,"join-error"))current_error(1);
    else if(!strcmp(s,"done-error"))current_error(0);
    else if(!strcmp(s,"search-commit"))search_commit();
    else if(!strcmp(s,"repeat-start"))repeat_start();
    else if(!strcmp(s,"join-target"))join_target();
    else assert(!"unknown scenario");
    printf("PASS %s: actual Matching2 + NP adapter\n",s);return 0;
}
