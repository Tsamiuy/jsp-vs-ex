/* Runs the SDK's positive fixture before the delayed-reply regression.
 * Works against both the previous SDK and the guarded SDK. No server/title. */
#define main sdk_baseline_main
#include JSP_MATCHING2_TEST_FILE
#undef main

static void joined_room(u64 id)
{
    u8 body[128], *w=body;
    w=psnr_put64(w,id); w=psnr_put16(w,1); w=psnr_put16(w,1); *w++=4;
    w=psnr_put32(w,0); w=psnr_put16(w,0); w=psnr_put16(w,0); *w++=1;
    w=psnr_put16(w,1); w=psnr_put32(w,77);
    memset(w,0,16); memcpy(w,"fixture",7); w+=16;
    *w++=127; *w++=0; *w++=0; *w++=1; w=psnr_put16(w,3658); *w++=1;
    w=psnr_put16(w,0);
    reply(PSNR_ROOM_JOINED,body,(u32)(w-body));
}

int main(int argc,char **argv)
{
    assert(argc==2);assert(sdk_baseline_main()==0);u16 ctx=vm_read16(CTXID);
    int positive=!strcmp(argv[1],"same-context-positive");
    int join=!strcmp(argv[1],"join-reuse");
    assert(positive || join || !strcmp(argv[1],"leave-reuse"));
    memset(vm_base+REQ,0,112);vm_write64(REQ,9);
    if(join) assert(sceNpMatching2JoinRoom(ctx,REQ,OPT,REQID)==0);
    else assert(sceNpMatching2LeaveRoom(ctx,REQ,OPT,REQID)==0);
    np_psnr_reply_fn old_fn=s_fn;void *old_user=s_user;
    if(!positive) {
        assert(sceNpMatching2DestroyContext(ctx)==0);
        assert(sceNpMatching2CreateContext(0,COMM,0,CTXID,0)==0);
        assert(vm_read16(CTXID)==ctx);assert(sceNpMatching2ContextStart(ctx)==0);
        memset(vm_base+REQ,0,112);vm_write32(REQ+16,4);
        assert(sceNpMatching2CreateJoinRoom(ctx,REQ,OPT,REQID)==0);joined_room(77);
        assert(s_ctx[ctx].room.in && s_ctx[ctx].room.id==77);
    }
    if(join) {s_fn=old_fn;s_user=old_user;joined_room(88);}
    else {psnr_msg ok={PSNR_OK,1,NULL,0};old_fn(old_user,&ok);}
    int pass=positive?!s_ctx[ctx].room.in:s_ctx[ctx].room.in && s_ctx[ctx].room.id==77;
    printf("CONTROL %s %s room_in=%d room_id=%llu\n",argv[1],pass?"PASS":"FAIL",s_ctx[ctx].room.in,(unsigned long long)s_ctx[ctx].room.id);
    return pass?0:1;
}
