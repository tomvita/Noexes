#include <stdio.h>
#include "gecko.h"
#include "errors.h"
#include "dmntcht.h"

//useful macros for reading/writeing to the socket
#define READ_CHECKED(ctx, to) {                                     \
    int i = ctx.conn.read(&to);                                     \
    if( i < 0 )                                                     \
        return MAKERESULT(Module_TCPGecko, TCPGeckoError_iofail);   \
}

#define READ_BUFFER_CHECKED(ctx, buffer, size) {                    \
    int i = ctx.conn.read(buffer, size);                            \
    if( i < 0 )                                                     \
        return MAKERESULT(Module_TCPGecko, TCPGeckoError_iofail);   \
}

#define WRITE_CHECKED(ctx, value) {                                 \
    int i = ctx.conn.write(value);                                  \
    if( i < 0)                                                      \
        return MAKERESULT(Module_TCPGecko, TCPGeckoError_iofail);   \
}

#define WRITE_BUFFER_CHECKED(ctx, buffer, size) {                   \
    int i = ctx.conn.write(buffer, size);                           \
    if( i < 0)                                                      \
        return MAKERESULT(Module_TCPGecko, TCPGeckoError_iofail);   \
}
static bool dmnt = false;
Result writeCompressed(Gecko::Context& ctx, u32 len) {
    static u8 tmp[GECKO_BUFFER_SIZE * 2];
    u32 pos = 0;

    for(u32 i = 0; i < len;i++){
        u8 value = ctx.buffer[i];
        u8 rle = 1;
        while(rle < 255 && i + 1 < len && ctx.buffer[i + 1] == value){
            rle++;
            i++;
        }
        tmp[pos++] = value;
        tmp[pos++] = rle;
    }
    u8 compressedFlag = pos > len ? 0 : 1;
    WRITE_CHECKED(ctx, compressedFlag);
    WRITE_CHECKED(ctx, len);
    if(!compressedFlag){
        WRITE_BUFFER_CHECKED(ctx, ctx.buffer, len);
    }else{
        WRITE_CHECKED(ctx, pos);
        WRITE_BUFFER_CHECKED(ctx, tmp, pos);
    }
    return 0;
}

//0x01
static Result _status(Gecko::Context& ctx){
    WRITE_CHECKED(ctx, (u8)ctx.status);
    WRITE_CHECKED(ctx, (u8)VER_MAJOR);
    WRITE_CHECKED(ctx, (u8)VER_MINOR);
    WRITE_CHECKED(ctx, (u8)VER_PATCH);
    return 0;
}

//0x02
static Result _poke8(Gecko::Context& ctx){
    u64 ptr;
    u8 val;
    READ_CHECKED(ctx, ptr);
    READ_CHECKED(ctx, val);
    if (dmnt) return dmntchtWriteCheatProcessMemory(ptr, &val, 1);
    else return ctx.dbg.writeMem(val, ptr);
}

//0x03
static Result _poke16(Gecko::Context& ctx){
    u64 ptr;
    u16 val;
    READ_CHECKED(ctx, ptr);
    READ_CHECKED(ctx, val);
    if (dmnt) return dmntchtWriteCheatProcessMemory(ptr, &val, 2);
    else return ctx.dbg.writeMem(val, ptr);
}

//0x04
static Result _poke32(Gecko::Context& ctx){
    u64 ptr;
    u32 val;
    READ_CHECKED(ctx, ptr);
    READ_CHECKED(ctx, val);
    if (dmnt) return dmntchtWriteCheatProcessMemory(ptr, &val, 4);
    else return ctx.dbg.writeMem(val, ptr);
}

//0x05
static Result _poke64(Gecko::Context& ctx){
    u64 ptr;
    u64 val;
    READ_CHECKED(ctx, ptr);
    READ_CHECKED(ctx, val);
    if (dmnt) return dmntchtWriteCheatProcessMemory(ptr, &val, 8);
    else return ctx.dbg.writeMem(val, ptr);
}

//0x06
static Result _readmem(Gecko::Context& ctx){
	Result rc;
	u64 addr;
	u32 size;
    u32 len;
    bool* out = nullptr;

    READ_CHECKED(ctx, addr);				
    READ_CHECKED(ctx, size);
    if (dmnt) rc = dmntchtHasCheatProcess(out);
    else rc = ctx.dbg.attached();
    WRITE_CHECKED(ctx, rc);
    if(R_SUCCEEDED(rc)){
        while(size > 0){
            len = size < GECKO_BUFFER_SIZE ? size : GECKO_BUFFER_SIZE;
            if (dmnt) rc = dmntchtReadCheatProcessMemory(addr, ctx.buffer, len);
            else rc = ctx.dbg.readMem(ctx.buffer, addr, len);
            WRITE_CHECKED(ctx, rc);
        
            if(R_FAILED(rc)){
                break;
            }
            rc = writeCompressed(ctx, len);
            if(R_FAILED(rc)){
                break;
            }
            addr += len;
            size -= len;
        }
    }
    return rc;
}

//0x07
static Result _writemem(Gecko::Context& ctx){
    u64 addr;
    u32 size;
    u32 len;
    Result rc = 0;
    bool * out = nullptr;
    READ_CHECKED(ctx, addr);
    READ_CHECKED(ctx, size);
    if (dmnt) rc = dmntchtHasCheatProcess(out);
    else rc = ctx.dbg.attached();
    WRITE_CHECKED(ctx, rc);
    if(R_SUCCEEDED(rc)){
        while(size > 0){
            len = size < GECKO_BUFFER_SIZE ? size : GECKO_BUFFER_SIZE;
            READ_BUFFER_CHECKED(ctx, ctx.buffer, len);
            if (dmnt) rc = dmntchtWriteCheatProcessMemory(addr, ctx.buffer, len);
            else ctx.dbg.writeMem(ctx.buffer, addr, len);
            addr += len;
            size -= len;
        }
    }
    return 0;
}

//0x08
static Result _resume(Gecko::Context& ctx){
    Result rc;
    if (dmnt) rc = dmntchtResumeCheatProcess();
    else rc = ctx.dbg.resume();
    if(R_SUCCEEDED(rc)){
        ctx.status = Gecko::Status::Running;
    }
    return rc;
}

//0x09
static Result _pause(Gecko::Context& ctx){
    Result rc;
    if (dmnt) rc = dmntchtPauseCheatProcess();
    else rc = ctx.dbg.pause();
    if(R_SUCCEEDED(rc)){
        ctx.status = Gecko::Status::Paused;
    }
    return rc;
}

//0x0A
static Result _attach(Gecko::Context& ctx){
    u64 pid;
    READ_CHECKED(ctx, pid);
    Result rc = ctx.dbg.attach(pid);
    if(R_SUCCEEDED(rc)){
        dmnt = false;
        ctx.status = Gecko::Status::Paused;
    } else {
        if (ctx.dbg.attached()) {    
            // dmntchtInitialize();
            DmntCheatProcessMetadata cht;
            dmntchtGetCheatProcessMetadata(&cht);
            if (cht.process_id == pid) {
                rc = dmntchtPauseCheatProcess();
                if(R_SUCCEEDED(rc)){
                    ctx.dbg.assign(pid);  
                    dmnt = true;
                    ctx.status = Gecko::Status::Paused;
                }
            } 
            else {
                // dmntchtExit();
            }
        }
    }
    return rc;
}

//0x0B
static Result _detatch(Gecko::Context& ctx){
    Result rc;
    if (dmnt) {rc = dmntchtResumeCheatProcess();  dmnt = false; ctx.dbg.assign(0);} //dmntchtExit();
    else rc = ctx.dbg.detatch();
    if(R_SUCCEEDED(rc)){
        ctx.status = Gecko::Status::Running;
    }
    return rc;
}

//0x0C
static Result _querymem_single(Gecko::Context& ctx){
    Result rc = 0;
    u64 addr;
    MemoryInfo info = {};
    
    READ_CHECKED(ctx, addr);

    if (dmnt) rc = dmntchtQueryCheatProcessMemory(&info, addr);
    else rc = ctx.dbg.query(&info, addr);

    WRITE_CHECKED(ctx, info.addr);
    WRITE_CHECKED(ctx, info.size);
    WRITE_CHECKED(ctx, info.type);
    WRITE_CHECKED(ctx, info.perm);
    return rc;
}

//0x0D
static Result _querymem_multi(Gecko::Context& ctx) {
    Result rc = 0;
    u64 addr;
    u32 requestCount;
    u32 count = 0;
    MemoryInfo info = {};
    READ_CHECKED(ctx, addr);
    READ_CHECKED(ctx, requestCount);
           
    for(count = 0; count < requestCount; count++){
        if (dmnt) rc = dmntchtQueryCheatProcessMemory(&info, addr);
        else rc =ctx.dbg.query(&info, addr);
        WRITE_CHECKED(ctx, info.addr);
        WRITE_CHECKED(ctx, info.size);
        WRITE_CHECKED(ctx, info.type);
        WRITE_CHECKED(ctx, info.perm);
        WRITE_CHECKED(ctx, rc);
        if(info.type == 0x10 || R_FAILED(rc)){
            break;
        }
        addr += info.size;
    }
    return rc;
}

//0x0E
static Result _current_pid(Gecko::Context& ctx){
    u64 pid;
    bool dmnthascht;
    Result rc;
    dmntchtHasCheatProcess(&dmnthascht);
    if (dmnthascht) {
        DmntCheatProcessMetadata cht;
        rc = dmntchtGetCheatProcessMetadata(&cht);
        pid = cht.process_id;
    } else rc = pmdmntGetApplicationProcessId(&pid);
    WRITE_CHECKED(ctx, pid);
    // printf("pid = %lx\n",pid);
    return rc;
}

//0x0F
static Result _attached_pid(Gecko::Context& ctx){
    WRITE_CHECKED(ctx, ctx.dbg.attachedPid());
    return 0;
}

//0x10
static Result _list_pids(Gecko::Context& ctx){
    Result rc;
	int maxpids = GECKO_BUFFER_SIZE / sizeof(u64);
	s32 count;
    rc = ctx.dbg.listPids((u64*)ctx.buffer, &count, maxpids);
    WRITE_CHECKED(ctx, count);
    WRITE_BUFFER_CHECKED(ctx, ctx.buffer, count * sizeof(u64));
    return rc;
}

//0x11
static Result _get_titleid(Gecko::Context& ctx){
    Result rc;
    u64 pid;
    u64 title_id;
    
    READ_CHECKED(ctx, pid);
    
	rc = pminfoGetProgramId(&title_id, pid);
	if (R_FAILED(rc)) {
        title_id = 0;
	}
    WRITE_CHECKED(ctx, title_id);
    return rc;
}

//0x12
static Result _disconnect(Gecko::Context& ctx){
    ctx.status = Gecko::Status::Stopping;
    return 0;
}


//0x13
static Result _readmem_multi(Gecko::Context& ctx){
    Result rc = 0;
    u32 size;
    u32 data_size;
    u32 count = 0;
    u64 addr;
    bool* out = nullptr;
    READ_CHECKED(ctx, size);
    READ_CHECKED(ctx, data_size);
    if (dmnt) rc = dmntchtHasCheatProcess(out);
    else rc = ctx.dbg.attached();
    if(R_SUCCEEDED(rc)){
        if(data_size > GECKO_BUFFER_SIZE){
            rc = MAKERESULT(Module_TCPGecko, TCPGeckoError_buffer_too_small);
        }
    }
    WRITE_CHECKED(ctx, rc);
    if(R_SUCCEEDED(rc)){
        for(count = 0; count < size; count++){
            READ_CHECKED(ctx, addr);
            if (dmnt) rc = dmntchtReadCheatProcessMemory(addr, ctx.buffer, data_size);
            else rc = ctx.dbg.readMem(ctx.buffer, addr, data_size);
            WRITE_CHECKED(ctx, rc);
            if(R_FAILED(rc)){
                break;
            }
            WRITE_BUFFER_CHECKED(ctx,ctx.buffer, data_size);
        }
    }
    return rc;
}

//0x14
static Result _set_breakpoint(Gecko::Context& ctx){
    u32 id;
    u64 addr;
    u64 flags;
    READ_CHECKED(ctx, id);
    READ_CHECKED(ctx, addr);
    READ_CHECKED(ctx, flags);
    return ctx.dbg.setBreakpoint(id, flags, addr);
}

//0x15
static Result _freeze_address(Gecko::Context& ctx){
    u64 out_value;
    u64 addr;
    u64 width;
    READ_CHECKED(ctx, addr);
    READ_CHECKED(ctx, width);
    Result rc = dmntchtEnableFrozenAddress(addr,width,&out_value);
    WRITE_CHECKED(ctx, out_value);
    return rc;
}

//0x16
static Result _search_local(Gecko::Context& ctx){
    u32 id;
    u64 addr;
    u64 flags;
    READ_CHECKED(ctx, id);
    READ_CHECKED(ctx, addr);
    READ_CHECKED(ctx, flags);
    return ctx.dbg.setBreakpoint(id, flags, addr);
}

//0x17
static Result _fetch_result(Gecko::Context& ctx){
    u32 id;
    u64 addr;
    u64 flags;
    READ_CHECKED(ctx, id);
    READ_CHECKED(ctx, addr);
    READ_CHECKED(ctx, flags);
    return ctx.dbg.setBreakpoint(id, flags, addr);
}

//0x18
static Result _detach_dmnt(Gecko::Context& ctx){
    return dmntchtForceCloseCheatProcess();
}

//0x19
static Result _attach_dmnt(Gecko::Context& ctx){
    return dmntchtForceOpenCheatProcess();
}

static u64 m_heap_start, m_heap_end, m_main_start, m_main_end;
static u8 outbuffer[GECKO_BUFFER_SIZE];

static Result getmeminfo(Gecko::Context& ctx) {
    Result rc = 0;
    u64 addr;
    u32 requestCount;
    u32 count = 0;
    MemoryInfo info = {};
    addr = 0;
    requestCount = 10000;
    m_heap_start = 0;
    m_main_start = 0;
    u32 mod = 0;
    for(count = 0; count < requestCount; count++){
        if (dmnt) rc = dmntchtQueryCheatProcessMemory(&info, addr);
        else rc =ctx.dbg.query(&info, addr);
        // printf("info.addr %lx ,info.size %lx ,info.type %x\n",info.addr,info.size,info.type );
        if (info.type == MemType_Heap){
            if (m_heap_start == 0) m_heap_start = info.addr;
            m_heap_end = info.addr + info.size;
        }
        if (info.type == MemType_CodeStatic && info.perm == Perm_Rx){
            if (mod == 1) m_main_start = info.addr;
            mod += 1;
        }
        if (info.type == MemType_CodeMutable){
            if (mod ==2 ) m_main_end = info.addr + info.size;
        }
        if(info.type == 0x10 || R_FAILED(rc)){
            break;
        }
        addr += info.size;
    }
    return rc;
}

static Result process(Gecko::Context &ctx, u64 m_start, u64 m_end) {
    Result rc = 0;
    u32 size, len;
    u64 addr,from,to;
    MemoryInfo info = {}; 
    u32 out_index =0;
    addr = m_start;
    while (addr < m_end){
        if (dmnt) rc = dmntchtQueryCheatProcessMemory(&info, addr);
        else rc =ctx.dbg.query(&info, addr);
        size = info.size;
        while(size > 0){
            len = size < GECKO_BUFFER_SIZE ? size : GECKO_BUFFER_SIZE;
            if (dmnt) rc = dmntchtReadCheatProcessMemory(addr, ctx.buffer, len);
            else rc = ctx.dbg.readMem(ctx.buffer, addr, len);
            WRITE_CHECKED(ctx, rc);
            if(R_FAILED(rc)){
                break;
            }
            // screening
            for (u32 i = 0; i < len; i += 4) {
                to = *reinterpret_cast<u64 *>(&ctx.buffer[i]);
                if (to >= m_heap_start && to <= m_heap_end) {
                    from = addr + i;
                    // Fill buffer
                    *reinterpret_cast<u64 *>(outbuffer[out_index]) = from;
                    *reinterpret_cast<u64 *>(outbuffer[out_index + 8]) = to;
                    out_index += 16;
                    if (out_index == GECKO_BUFFER_SIZE) {
                        WRITE_BUFFER_CHECKED(ctx, outbuffer, out_index);
                        out_index = 0;
                    }
                }
            }
            if(R_FAILED(rc)){
                break;
            }
            addr += len;
            size -= len;
        }
        addr += info.size;
    }
    if (out_index != 0) {
        WRITE_BUFFER_CHECKED(ctx, outbuffer, out_index);
        out_index = 0;
    }
    return rc;
}

//0x1A
static Result _dump_ptr(Gecko::Context& ctx){
    Result rc = getmeminfo(ctx);
    printf("main start = %lx, main end = %lx, heap start = %lx, heap end = %lx \n",m_main_start,m_main_end,m_heap_start,m_heap_end );
    WRITE_BUFFER_CHECKED(ctx, &m_main_start, 8);
    WRITE_BUFFER_CHECKED(ctx, &m_main_end, 8);
    WRITE_BUFFER_CHECKED(ctx, &m_heap_start, 8);
    WRITE_BUFFER_CHECKED(ctx, &m_heap_end, 8);
    return rc;
    if (R_SUCCEEDED(rc)) rc = process(ctx, m_main_start, m_main_end); else return rc;
    if (R_SUCCEEDED(rc)) rc = process(ctx, m_heap_start, m_heap_end);
    return rc;
}
Result cmd_decode(Gecko::Context& ctx, int cmd){
    static Result (*cmds[255])(Gecko::Context&) =   {NULL, _status, _poke8, _poke16, _poke32, _poke64, _readmem,
                                                    _writemem, _resume, _pause, _attach, _detatch, _querymem_single,
                                                    _querymem_multi, _current_pid, _attached_pid, _list_pids,
                                                    _get_titleid, _disconnect, _readmem_multi, _set_breakpoint, _freeze_address,
                                                    _search_local, _fetch_result, _detach_dmnt, _attach_dmnt, _dump_ptr};
    Result rc = 0;
    if(cmds[cmd]){
        rc = cmds[cmd](ctx);
    }else{
        rc = MAKERESULT(Module_TCPGecko, TCPGeckoError_invalid_cmd);
    }
    WRITE_CHECKED(ctx, rc);
    return rc;
}
