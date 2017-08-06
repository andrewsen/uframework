#include "memorymanager.h"
#include <signal.h>

Runtime::MemoryManager::MemoryManager()
{

}

void Runtime::MemoryManager::SetRuntime(Runtime *rt)
{
    this->rt = rt;
}

void Runtime::MemoryManager::Init()
{
    memory_allocated = memory_l1 = (byte*)calloc(rt->current_mem_size*2, 1);//new byte[rt->current_mem_size];
    memory_trash = memory_l1 + rt->current_mem_size;
    mem_l1_ptr = memory_l1;
    mem_trash_ptr = memory_trash;
#ifdef GC_DEBUG
    if(tracer != nullptr)
        tracer(GCPoint(INIT, 0, rt->current_mem_size, FREE));
#endif
}

byte* Runtime::MemoryManager::Allocate(byte* type) //Checked
{
    rt->rtThrow(Runtime::NotImplemented);
    return nullptr;
}

byte* Runtime::MemoryManager::Allocate(uint size)
{
    //uint sz = rt->ARRAY_METADATA_SIZE;    
#ifdef FW_DEBUG
    ++alloc_count;
#endif
    uint total = (uint)memory_l1 + rt->current_mem_size;
    uint requested = (uint)mem_l1_ptr + size;
    if(total <= requested)
        MinorClean();

    total = (uint)memory_l1 + rt->current_mem_size;
    requested = (uint)mem_l1_ptr + size;
    if(total <= requested)
    {
#ifdef FW_DEBUG
        rt->log << "-D- Expanding memory\n";
#endif
        rt->current_mem_size = total + requested + 1024;
        byte* new_heap = (byte*)calloc(rt->current_mem_size*2, 1);
        memory_trash = new_heap + rt->current_mem_size;
        mem_trash_ptr = memory_trash;
        MinorClean();
        memory_trash = new_heap;
        mem_trash_ptr = memory_trash;
        free(memory_allocated);
        memory_allocated = new_heap;

        //memcpy(new_heap)

        //rt->rtThrow(Runtime::AllocationError);           /// OH YEAH AN ERROR WOOOW GOOD ERROR I LOVE ERRORS WOOOOW
        //rt->current_mem_size += requested - total + rt->mem_chunk_size; /// OPTIMIZE ALIGNMENT UGH
        //realloc(memory_l1, rt->current_mem_size);        /// WOOOOW SUCH SEGFAULT WOOOOW SO FAULT SUCH SEG WOOOW(I SHOULD UPDATE ALL REFS AFTER REALLOC)
    }

    byte* alloced = mem_l1_ptr;
#ifdef GC_DEBUG
    if(tracer != nullptr)
        tracer(GCPoint(
                   ALLOC,
                   (uint)(mem_l1_ptr - memory_l1),
                   size,
                   USED));
#endif
    mem_l1_ptr += size;
    return alloced;
}

byte *Runtime::MemoryManager::Allocate(Type type, uint size) //Checked
{
    return Allocate(size);
}

byte *Runtime::MemoryManager::AllocateArray(Type type, uint count)
{
    byte* arr = (byte*)Allocate(count * Sizeof(type) + ARRAY_METADATA_SIZE);// + ARRAY_METADATA_SIZE;

    *(Type*)arr = type;
    *(uint*)((uint)arr+1) = count;

    return arr;
}

byte *Runtime::MemoryManager::AllocateString(const char *str)
{
    auto len = strlen(str);
    char* mem_str = (char*)Allocate(len + ARRAY_METADATA_SIZE + 1 /* for '\0' */);

    strcpy(mem_str + ARRAY_METADATA_SIZE, str);
    *mem_str = (byte)Type::UTF8;
    *(uint*)(mem_str+1) = len+1;

    return (byte*)mem_str;
}

void Runtime::MemoryManager::ArraySet(byte *arr, int idx, byte* value)
{
    Type t = (Type)*arr;
    uint size = t != Type::CHAR ? Sizeof(t) : 1;
    mempcpy(arr + ARRAY_METADATA_SIZE + idx*size, value, size);
}

void Runtime::MemoryManager::MinorClean()
{
#ifdef FW_DEBUG
    ++gc_count;
    rt->log << "-D- Running GC\n";
#endif
#ifdef GC_DEBUG
    if(tracer != nullptr)
        tracer(GCPoint(MINOR_CLEAN, 0, 0, FREE));
#endif
    for(auto frame : rt->callstack)
    {
        if(frame.loc_ptr != nullptr)
        {
            auto locals = frame.loc_ptr;
            for(uint i = 0; i < frame.func_ptr->locals_size; ++i)
            {
                auto type = *(Type*)frame.func_ptr->locals[i].type;
                if(type == Type::ARRAY) /// ADD CLASS
                {
                    *(uint*)(locals + frame.func_ptr->locals[i].addr) =
                            (uint)MoveArray((byte*)*(uint*)(locals + frame.func_ptr->locals[i].addr));
                }
                else if(type == Type::UTF8)
                {
                    *(uint*)(locals + frame.func_ptr->locals[i].addr) =
                            (uint)MoveString((byte*)*(uint*)(locals + frame.func_ptr->locals[i].addr));
                }
            }
        }

        if(frame.arg_ptr != nullptr)
        {
            auto args = frame.arg_ptr;
            for(uint i = 0; i < frame.func_ptr->argc; ++i)
            {
                auto type = *(Type*)frame.func_ptr->args[i].type;
                if(type == Type::ARRAY) /// ADD CLASS
                {
                    *(uint*)(args + frame.func_ptr->args[i].addr) =
                            (uint)MoveArray((byte*)*(uint*)(args + frame.func_ptr->args[i].addr));
                }
                else if(type == Type::UTF8)
                {
                    *(uint*)(args + frame.func_ptr->args[i].addr) =
                            (uint)MoveString((byte*)*(uint*)(args + frame.func_ptr->args[i].addr));
                }
            }
        }
    }

    /**** RUNNING MODULE ****/

    for(uint i = 0; i < rt->main_module.globals_count; ++i)
    {
        GlobalVar& gv = rt->main_module.globals[i];

        if(gv.inited && *(Type*)gv.type == Type::ARRAY) /// ADD CLASS
        {
            *(uint*)gv.addr = (uint)MoveArray((byte*)*(uint*)(gv.addr));
        }
        else if(gv.inited && *(Type*)gv.type == Type::UTF8)
        {
            *(uint*)gv.addr = (uint)MoveString((byte*)*(uint*)(gv.addr));
        }
    }

    /**** IMPORTED MODULES ****/
    for(Module* mod : rt->imported)
    {
        for(uint i = 0; i < mod->globals_count; ++i)
        {
            GlobalVar& gv = mod->globals[i];

            if(gv.inited && *(Type*)gv.type == Type::ARRAY) /// ADD CLASS
            {
                *(uint*)gv.addr = (uint)MoveArray((byte*)*(uint*)(gv.addr));
            }
            else if(gv.inited && *(Type*)gv.type == Type::UTF8)
            {
                *(uint*)gv.addr = (uint)MoveString((byte*)*(uint*)(gv.addr));
            }
        }
    }

    auto l1 = memory_l1;
    memory_l1 = memory_trash;
    memory_trash = l1;
#ifdef GC_DEBUG
    if(tracer != nullptr)
        tracer(GCPoint(XCHANGE_HEAPS, 0, 0, FREE));
#endif

    //auto ptr = mem_l1_ptr;
    mem_l1_ptr = mem_trash_ptr;
    mem_trash_ptr = memory_trash;
}

byte* Runtime::MemoryManager::MoveArray(byte* addr) ///     CHECK FOR addr ADDR or PTR TO ADDR
{
    //byte* addr = (byte*)*(uint*)ptr;
    auto type = *(Type*)(addr);
    uint count = *(uint*)(addr+1);// + rt->ARRAY_METADATA_SIZE;

    if(type == Type::GC_MOVED)
    {
        return (byte*)count;
    }

    uint size = count * Runtime::Sizeof(type) + rt->ARRAY_METADATA_SIZE;

    memcpy(mem_trash_ptr, addr, size);
    auto new_addr = mem_trash_ptr;
#ifdef GC_DEBUG
    if(tracer != nullptr)
    {
        tracer(GCPoint(ALLOC, (uint)(addr - memory_l1), size, MOVED, H1));
        tracer(GCPoint(ALLOC, (uint)(mem_trash_ptr - memory_trash), size, USED, H2));
    }
#endif
    mem_trash_ptr += size;

    *(Type*)addr = Type::GC_MOVED;
    *(uint*)(addr+1) = (uint)mem_trash_ptr;
    if(type == Type::ARRAY) ///                 ADD CLASS
    {
        uint* addrs = (uint*)(new_addr+rt->ARRAY_METADATA_SIZE);
        for(uint i = 0; i < count; ++i)
        {
            addrs[i] = (uint)MoveArray((byte*)(addrs[i]));
        }
    }
    else if(type == Type::UTF8)
    {
        uint* addrs = (uint*)(new_addr+rt->ARRAY_METADATA_SIZE);
        for(uint i = 0; i < count; ++i)
        {
            addrs[i] = (uint)MoveString((byte*)(addrs[i]));
        }
    }

    return new_addr;
}

byte* Runtime::MemoryManager::MoveString(byte* ptr)
{
    byte* addr = ptr;
    if((Type)*addr == Type::GC_MOVED)
    {
        return (byte*)*(uint*)(addr+1);
    }
    //auto type = *(Type*)(addr);
    uint size = *(uint*)(addr+1) + rt->ARRAY_METADATA_SIZE;

    //if(type == Type::GC_MOVED)
    //{
    //    return (byte*)count;
    //}

    //uint size = count * Runtime::Sizeof(type) + rt->ARRAY_METADATA_SIZE;

    memcpy(mem_trash_ptr, addr, size);
    auto new_addr = mem_trash_ptr;

    *(Type*)addr = Type::GC_MOVED;
    *(uint*)(addr+1) = (uint)mem_trash_ptr;
#ifdef GC_DEBUG
    if(tracer != nullptr)
    {
        tracer(GCPoint(ALLOC, (uint)(ptr - memory_l1), size, MOVED));
        tracer(GCPoint(ALLOC, (uint)(mem_trash_ptr - memory_trash), size, USED, H2));
    }
#endif

    mem_trash_ptr += size;

    /*if(type == Type::ARRAY) ///                 ADD CLASS
    {
        uint* addrs = (uint*)new_addr+rt->ARRAY_METADATA_SIZE;
        for(uint i = 0; i < count; ++i)
        {
            addrs[i] = (uint)MoveArray((byte*)addrs[i]);
        }
    }*/

    return new_addr;
}

void Runtime::MemoryManager::Free()
{
#ifdef GC_DEBUG
    if(tracer != nullptr)
        tracer(GCPoint(GCActionType(WIPE_H1 | WIPE_H2), 0, 0, FREE));
#endif
    free(memory_allocated);
}

void Runtime::MemoryManager::MajorClean()
{

}

