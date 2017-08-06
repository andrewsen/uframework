#include "runtime.h"
#include <cstring>
#include <sstream>

Runtime* Runtime::Instance = nullptr;

Runtime::Runtime()
{
    imported.reserve(8);
    callstack.reserve(16);
    state = Runtime::Constructed;
    //callsta.reserve(50);
}

Runtime::~Runtime()
{

}

void Runtime::Create(string file)
{
    this->file = file;
    Create();
}

void Runtime::Create()
{
    if(Runtime::Instance != nullptr)
        rtThrow(Runtime::RuntimeInstanceException);
    Runtime::Instance = this;

    //program_stack = allocator.RawAllocate(current_stack_size);
    program_stack = new byte[current_stack_size]; //#OLDNEW
    stack_ptr = program_stack;
    global_var_mem = (byte*)calloc(global_size, 1);//new byte[global_size];
    globals_ptr = global_var_mem;

    memoryManager.SetRuntime(this);
    memoryManager.Init();
    state = RtState(state | Runtime::Created);

    //uint total = (uint)memory_l1;// + rt->current_mem_size;
    //uint requested = (uint)mem_l1_ptr;// + size;

    //cout << total << requested << endl;
    //signal(SIGSEGV, sig_handler)
}

void Runtime::ParseCommandLine(int argc, char *argv[])
{
    for(;argc != 0 && (*argv)[0] == '-'; --argc, ++argv)
    {
        if(!strcmp(*argv, "-d"))
            EnterDebugMode();
        else if(!strcmp(*argv, "--no-jit"))
            this->jitConfig.jitEnabled = false;
        else if(!strcmp(*argv, "--gc-debug"))
        {
#ifdef GC_DEBUG
            this->memoryManager.setGCDebugEnabled(true);
#else
            rtThrow(Runtime::UnknownCommandArg, "GC debug option disabled\n");
#endif
        }
        else
            rtThrow(Runtime::UnknownCommandArg);
    }
    this->file = *argv;
    managed_argc = argc;
    managed_argv = argv;
}

void Runtime::Load()
{
    main_module.rt = this;
    main_module.Load(file);
    state = RtState(state | Runtime::Loaded);

    //cout << "Modules loaded!" << endl;
}

void Runtime::StartMain()
{
    RTType args[] = {{1, Type::UTF8}};
    uint main_idx = FindFunctionId("main", args, 1);
    if(main_idx == UI32_NOT_FOUND)
    {
        if(JIT_LEVEL == JIT_DISABLED || !jitConfig.jitEnabled)
            Start(FindFunctionId("main", nullptr, 0));
        else
        {
            Function* main = main_module.functions[FindFunctionId("main", nullptr, 0)];
            auto jitmain = main->jit_code;
            if(jitmain == nullptr)
            {
                jitmain = main->jit_code = jitCompile(main);
            }
            if(jitmain == nullptr)
                rtThrow(CantExecute, "Jit compilation error: can't start `main`");

#ifdef FW_DEBUG
            log << "Executing jitted main ";
#endif
            if((Type)*main->ret == Type::VOID)
            {
#ifdef FW_DEBUG
                log << "(void typed)" << "\n";
#endif
                ((voidJitMain0)jitmain)();
            }
            else
            {
#ifdef FW_DEBUG
                log << "(int typed)" << "\n";
#endif
                returnCode = ((intJitMain0)jitmain)();
            }
#ifdef FW_DEBUG
            log << "Jitted main finished" << "\n";
#endif
        }
    }
    /*else
    {
        byte* arg_start = (byte*)memoryManager.Allocate(managed_argc * Sizeof(Type::UTF8) + ARRAY_METADATA_SIZE);// + ARRAY_METADATA_SIZE;

        *(Type*)arg_start = Type::UTF8;
        *(uint*)((uint)arg_start+1) = managed_argc;

        //byte* end = arg_start + managed_argc;// * Sizeof(Type::UTF8);
        for(auto argp = arg_start + ARRAY_METADATA_SIZE; managed_argc != 0; argp += 4, ++managed_argv, --managed_argc)
        {
            auto len = strlen(*managed_argv);
            char* opt = (char*)memoryManager.Allocate(len + ARRAY_METADATA_SIZE + 1);
            *(uint*)argp = (uint)opt;

            strcpy(opt + ARRAY_METADATA_SIZE, *managed_argv);
            opt[len+ARRAY_METADATA_SIZE] = '\0';
            *opt = (byte)Type::UTF8;
            *(uint*)(opt+1) = len+1;
        }
        *(uint*)stack_ptr = (uint)arg_start;
        *(stack_ptr += 4) = (byte)Type::ARRAY;
        Start(main);
    }
    */
    else
    {
        byte* arg_arr = memoryManager.AllocateArray(Type::UTF8, managed_argc);
        for(int i = 0; i < managed_argc; ++i)
        {
            byte* str_ptr = memoryManager.AllocateString(managed_argv[i]);
            memoryManager.ArraySet(arg_arr, i, (byte*)&str_ptr);
        }
        *(uint*)stack_ptr = (uint)arg_arr;
        *(stack_ptr += 4) = (byte)Type::ARRAY;
        Start(main_idx);
    }
}

void Runtime::Start(uint entryId)
{
    Function * entry = main_module.functions[entryId];

    union
    {
        byte b[4];
        uint addr;
    } conv;

    strcpy(nativeMain.sign, "__NativeMain__");
    //nativeMain.ret = Type::VOID;

    nativeMain.module = entry->module;
    nativeMain.argc = 0;
    nativeMain.flags |= FFlags::RTINTERNAL;
    conv.addr = entryId;

    byte nativeBytecode[] = {
        (byte)OpCode::CALL, conv.b[0], conv.b[1], conv.b[2], conv.b[3],
        (byte)OpCode::RET
    };

    nativeMain.bytecode = (OpCode*)nativeBytecode;

    if(!entry->module->mflags.executable_bit)
        rtThrow(Runtime::CantExecute);
    auto sf = StackFrame{&nativeMain, nullptr, nullptr};

    try {
        state = RtState(state | Runtime::Started);
        callstack.push_back(sf);
        execFunction(&nativeMain, nullptr, nullptr);
        callstack.pop_back();
        state = RtState(state & Runtime::Started);
        state = RtState(state | Runtime::Finished);

        auto retType = (Type)*entry->ret;
        if(retType == Type::I32)
        {
            this->returnCode = *(int*)(stack_ptr -= 4);
            this->hasReturnCode = true;
        }
    }
    catch(int e)
    {
        if(e == _EXIT_FAILURE)
            return;
    }

    //execFunction(entry, nullptr, nullptr, nullptr);
}

void Runtime::Unload()
{
    //STUB

#ifdef FW_DEBUG
    log.SetType(Log::Info);
    log << "\n---------------------\n";
    log << "Alloc count: " << memoryManager.alloc_count << "\n";
    log << "GC count: " << memoryManager.gc_count << "\n";
#endif

    callstack.clear();

    main_module.Unload();
    for(Module* mod : imported)
    {
        mod->Unload();
        delete mod;
    }
    for(Function* fun: function_list)
    {
        if(fun != nullptr)
            delete fun;
    }
    delete [] program_stack;
    free(global_var_mem);

    log.Close();
    memoryManager.Free();     //FIXME
    //allocator.Clean();
    state = RtState(state | Runtime::Unloaded);
}

uint Runtime::FindFunctionId(const char *name) const {
    ///TODO: arg types!!!
    for(uint i = 0; i < main_module.func_count; ++i)
        if(!strcmp(main_module.functions[i]->sign, name))
            return i;
    return UI32_NOT_FOUND;
}

uint Runtime::FindFunctionId(const char *name, RTType args[], uint count) const { ///TODO: FIX OPTIMIZE
    for(uint i = 0; i < main_module.func_count; ++i)
        if(!strcmp(main_module.functions[i]->sign, name))
        {
            if(main_module.functions[i]->argc == count)
            {
                auto fun = main_module.functions[i];
                for(uint t = 0; t < count; ++t)
                {
                    if(args[t].dimens != 0)
                    {
                        if((*fun->args[t].type != (byte)Type::ARRAY)
                           || (*(uint*)(fun->args[t].type+1) != args[t].dimens)
                           || (*(fun->args[t].type+5) != (byte)args[t].plain))
                            return UI32_NOT_FOUND;
                    }
                    else if(*fun->args[t].type != (byte)args[t].plain)
                        return UI32_NOT_FOUND;
                }
                return i;
            }
        }
    return UI32_NOT_FOUND;
}

void Runtime::RaiseEvent(RtExType event) //Checked
{
    Handlers.invoke(event);
}

[[noreturn]]
void Runtime::ThrowAndDie(Runtime::RtExType ex)
{
    rtThrow(ex);
    //Unload();
}

void Runtime::Crash()
{
    printStackTrace();
    state = RtState(state | Runtime::Crashed);
}

/*void Runtime::stackalloc(uint need) {
    if(((size_t)stack_ptr - (size_t)program_stack + need) < current_stack_size) return;

    uint old_size = current_stack_size;

    if(old_size >= max_stack_size)
    {
        //cin.get();
        rtThrow(StackOverflow);
    }

    size_t ptr = (size_t)stack_ptr - (size_t)program_stack;

    current_stack_size *= 2;
    byte * newstack = n ew byte[current_stack_size];
    memcpy(newstack, program_stack, old_size);
    d elete [] program_stack;
    program_stack = newstack;
    stack_ptr = newstack + ptr;
}*/

string Runtime::GetStackTrace()
{
    stringstream ss;
    void * strace[25];
    int size;

    size = backtrace(strace, 25);
    ss << "Unmanaged code:\n";
    char** traces = backtrace_symbols(strace, size);
    for(int i = 0; i < size; ++i)
    {
        ss << "\t" << traces[i] << endl;
    }

    ss << "Managed code:\n" << GetManagedStackTrace();
    return ss.str();
}

void Runtime::printStackTrace()
{
    log << GetStackTrace();
}

string Runtime::GetManagedStackTrace()
{
    stringstream ss;
    try
    {
        //StackFrame last = callstack.at(callstack.size()-1);

        for(const StackFrame& sf : callstack)
        {
            ss << "\tat: ";
            Function* fun = sf.func_ptr;
            if(fun == nullptr)
                continue;
            if(!(fun->flags & FFlags::RTINTERNAL))
                ss << "[" << fun->module->file << "] ";
            else
                ss << "[" << program_invocation_name << "] ";
            ss << typeToStr(fun->ret) << " " << fun->sign << "(";
            for(uint i = 0; i < fun->argc; ++i)
            {
                ss << typeToStr(fun->args[i].type);
                if(i != fun->argc-1)
                    ss << " ";
            }
            ss << ")\n";
        }
    }
    catch(...)
    {}
    return ss.str();
}

void Runtime::printManagedStackTrace()
{
    log.SetType(Log::Info);
    log << GetManagedStackTrace();
}


string Runtime::typeToStr(byte* tptr)
{
    if(tptr == nullptr)
        return "";
    Type t = (Type)*tptr;
    switch (t)
    {
        case Type::BOOL:
           return "bool";
        case Type::UI8:
           return "byte";
        case Type::I16:
           return "short";
        case Type::UI32:
           return "uint";
        case Type::I32:
           return "int";
        case Type::UI64:
           return "ulong";
        case Type::I64:
           return "long";
        case Type::DOUBLE:
           return "double";
        case Type::UTF8:
           return "string";
        case Type::VOID:
           return "void";
        case Type::ARRAY:
        {
            string s = "";
            uint dims = *(uint*)(tptr+1);
            for(uint i = 0; i < dims; ++i)
                s += "[]";
            return typeToStr(tptr+ARRAY_METADATA_SIZE) + s;
        }
        default:
            return "<usertype>";
    }
}

string Runtime::typeToStr(Type t)
{
    switch (t)
    {
        case Type::BOOL:
           return "bool";
        case Type::UI8:
           return "byte";
        case Type::I16:
           return "short";
        case Type::UI32:
           return "uint";
        case Type::I32:
           return "int";
        case Type::UI64:
           return "ulong";
        case Type::I64:
           return "long";
        case Type::DOUBLE:
           return "double";
        case Type::UTF8:
           return "string";
        case Type::VOID:
           return "void";
        case Type::ARRAY:
            return "array";
        default:
            return "<usertype>";
    }
}

void Runtime::allocGlobalMem()
{
    //uint old_size = global_size;
    /*if(old_size >= global_max_size)*/ rtThrow(GlobalMemOverflow);
    //global_size *= 2;

    //byte * new_globals = new byte [global_size];
    //memcpy(new_globals, global_var_mem, old_size);
    //delete [] global_var_mem;
    //global_var_mem = new_globals;
}

/*void Runtime::allocMem(uint need)
{
    if(((size_t)this->mem_l1_ptr - (size_t)this->memory_l1) < this->max_mem_size) return;

    size_t used = (size_t)this->mem_l1_ptr - (size_t)this->memory_l1;

    uint old_size = this->current_mem_size;

    if(old_size >= max_mem_size) rtThrow(StackOverflow);

    current_mem_size += mem_chunk_size;
    byte * newmem = new byte[current_mem_size];
    memcpy(newmem, memory_l1, old_size);
    delete [] memory_l1;
    memory_l1 = newmem;
    mem_l1_ptr = memory_l1 + used;
}*/

[[noreturn]]
void Runtime::rtThrowEx(const char* file, int line, Runtime::RtExType t, string message)
{
    //debug
    //throw t;
    //debug
    log.SetType(Log::Error);
    log << "Exception " << file << ":" << line << ":\n";
    switch (t) {
        case Runtime::IllegalOperation:
            log << "Illegal operation" << "\n";
            break;
        case Runtime::AllocationError:
            log << "Allocation error" << "\n";
            break;
        case Runtime::CantExecute:
            log << "Start module is not executable" << "\n";
            break;
        case Runtime::GlobalMemOverflow:
            log << "Global memory overflow" << "\n";
            break;
        case Runtime::IllegalType:
            log << "Illegal type" << "\n";
            break;
        case Runtime::InternalFunctionMissing:
            log << "Internal function is missing" << "\n";
            break;
        case Runtime::MetaIncorrectType:
            log << "Metadata incorrect type" << "\n";
            break;
        case Runtime::MissingGlobalConstructor:
            log << "Global constructor is missing" << "\n";
            break;
        case Runtime::NotImplemented:
            log << "Feature is not implemented" << "\n";
            break;
        case Runtime::OldVersion:
            log << "Module version is too old" << "\n";
            break;
        case Runtime::OutOfRange:
            log << "Index is out of range" << "\n";
            break;
        case Runtime::StackOverflow:
            log << "Stack overflow" << "\n";
            break;
        case Runtime::FloatingPointException:
            log << "Floating point exception (check zero division)" << "\n";
            break;
        case Runtime::StackCorrupted:
            log << "Stack corrupted" << "\n";
            break;
        case Runtime::InvalidModule:
            log << "Invalid module" << "\n";
            break;
        default:
            log << "Unknown error" << "\n";
            break;
    }
    if(message != "")
        log << "Description: " << message << "\n";
    printStackTrace();
    //this->Unload();
    throw Runtime::_EXIT_FAILURE;
}

void Runtime::execFunction(Function *f, byte* fargs, byte* local_table)
{
    //cout << "Executing function " << f->sign << endl;
    /*if((uint)(stack_ptr - program_stack) > current_stack_size/4*3)
    {
        if(current_stack_size *= 2 > max_stack_size)
        {
            rtThrow(StackOverflow);
        }
        uint pos = stack_ptr - program_stack;
        if((program_stack = realloc(program_stack, current_stack_size)) == NULL)
        {
            rtThrow(StackOverflow | AllocationError);
        }
        stack_ptr = current_stack_size + pos;
        log.SetType(Log::Info);
        log << "new stack size: " << current_stack_size << "\n";
    }*/
    OpCode* code = f->bytecode;
    if(code == nullptr)         ///@todo REMOVE IT
        rtThrow(Runtime::IllegalOperation, "bytecode of non-native function is empty");
    //if(f->has_jit)
    //    jit.Call(f, fargs, local_table);
    //jit.Compile(f);
    //byte* stack_guard = stack_ptr;
    while(true) {
        this->current_cptr = code;

        if(this->debugOpts.debugMode && (!debugOpts.continueNext || debugOpts.breakAt == f->sign))
        {
            if(this->debugOpts.skipIterations > 0)
            {
                if(debugOpts.currentOp == code)
                    --debugOpts.skipIterations;
            }
            if(this->debugOpts.skipIterations == 0 || debugOpts.breakAt == f->sign)
            {
                debugOpts.currentOp = code;
                if(this->debugOpts.skipOpcodes == 1 || debugOpts.breakAt == f->sign)
                {
                    dbg_handler(DebugBundle(this, f, fargs, local_table, code));
                    debugOpts.skipIterations = 0;
                    debugOpts.skipOpcodes = 1;
                }
                else if(this->debugOpts.skipOpcodes > 1)
                    --debugOpts.skipOpcodes;
                else
                    debugOpts.skipOpcodes = 1;
            }
            else if(this->debugOpts.skipIterations < 0)
                debugOpts.skipIterations = 0;
        }

        switch (*code) {
            case OpCode::ADD: {
                Type type = (Type)*stack_ptr;
                switch (type)
                {
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 + a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = a1 + a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 + a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 + a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 + a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 + a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 + a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                //0x89ABCDEF: [EF|CD|AB|89];
            }
                break;
            case OpCode::ADDF: {
                double a2 = *(double*)(stack_ptr -= 8);
                double a1 = *(double*)(stack_ptr -= 9);

                *(double*)(stack_ptr) = a1 + a2;
                stack_ptr += 8;
            }
                break;
            case OpCode::AND: {
                Type type = (Type)*stack_ptr;
                switch (type)
                {
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 & a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = a1 & a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 & a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 & a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 & a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 & a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 & a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::BOOL:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 & a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, typeToStr(stack_ptr) + " with " + OpcodeToStr(*code));
                        break;
                    /*default:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 & a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }*/
                }

                /*if(type != Type::UI64 && type != Type::I64) {
                    uint a2 = *(uint*)(stack_ptr -= 4);
                    uint a1 = *(uint*)(stack_ptr -= 5);

                    *(uint*)(stack_ptr) = a1 & a2;
                    stack_ptr += 4; //sizeof(int) + 1 byte for type
                }
                else {
                    __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                    __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                    *(__uint64_t*)(stack_ptr) = a1 & a2;
                    stack_ptr += 8; //sizeof(int) + 1 byte for type
                }*/
            }
                break;
            case OpCode::BREAK:
            {
                //if(brk_cb != nullptr)
                //    brk_cb(this, f, fargs, local_table);
            }
                break;
            case OpCode::CALL:
            {
            /*
             * [122|...|42|1|23.3|0x43|34]<-
             * call f(int, double, string, int)
             * [122|...|42]<-
             */
                uint faddr = *(uint*)(++code);
                code += 3;
                Function* next = f->module->functions[faddr];

                byte* current_stack;// = stack_ptr;

                byte* args = nullptr;
                if(next->args_size != 0)
                {
                    args = stack_ptr - next->args_size+1;
                }

                current_stack = stack_ptr - next->args_size;

                if(!(next->flags & FFlags::RTINTERNAL))
                {
                    byte* locs = ++stack_ptr;
                    stack_ptr += next->local_mem_size-1;
                    auto sf = StackFrame{next, locs, args};
                    callstack.push_back(sf);
                    execFunction(next, args, locs);
                    stack_ptr -= next->local_mem_size;
                }
                else
                {
                    if(next->irep == nullptr)
                    {
                        next->irep = findInternal(this, next);
                    }

                    auto sf = StackFrame{next, nullptr, args};
                    callstack.push_back(sf);
                    next->irep(this, next, args);
                }

                if(*(Type*)next->ret != Type::VOID)
                {
                    uint len = SizeOnStack(*(Type*)next->ret);
                    memcpy(current_stack+1, stack_ptr+next->local_mem_size-len, len+1);
                    stack_ptr = current_stack + len+1;
                }
                else
                    stack_ptr = current_stack;

                callstack.pop_back();
            }
                break;
            case OpCode::CONV_F:
            {
                //stackalloc(4);
                switch((Type)*stack_ptr){
                    case Type::UI8:
                    case Type::UI32: {
                        uint arg = *(uint*)(stack_ptr -= 4);
                        *(double*)stack_ptr = (double)arg;
                        stack_ptr += 8;
                        break;
                    }
                    case Type::I16:
                    case Type::I32: {
                        int arg = *(int*)(stack_ptr -= 4);
                        *(double*)stack_ptr = (double)arg;
                        stack_ptr += 8;
                        break;
                    }
                    case Type::I64:{
                        __int64_t arg = *(__int64_t*)(stack_ptr -= 8);
                        *(double*)stack_ptr = (double)arg;
                        stack_ptr += 8;
                        break;
                    }
                    case Type::UI64:{
                        __uint64_t arg = *(__uint64_t*)(stack_ptr -= 8);
                        *(double*)stack_ptr = (double)arg;
                        stack_ptr += 8;
                        break;
                    }
                    case Type::DOUBLE:
                        break;
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
                *stack_ptr = (byte)Type::DOUBLE;
            }
                break;
            /*:TODO:*/
            case OpCode::CONV_I16:
                conv_i16();
                break;
            case OpCode::CONV_CHR:
                conv_chr();
                break;
            case OpCode::CONV_I32:
                conv_i32();
                break;
            case OpCode::CONV_I64:
                conv_i64();
                break;
            case OpCode::CONV_UI8:
                conv_ui8();
                break;
            case OpCode::CONV_UI32:
                conv_ui32();
                break;
            case OpCode::CONV_UI64:
                conv_ui64();
                break;
            case OpCode::DEC:
            {
                switch((Type)*stack_ptr){
                    case Type::UI8:
                    {
                        --*(byte*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::CHAR:
                    {
                        --*(char*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::UI32:
                    {
                        --*(uint*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::I16:
                    {
                        --*(short*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::I32:
                    {
                        --*(int*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::I64:
                    {
                        --*(__int64_t*)(stack_ptr - 8);
                        ++code;
                        continue;
                    }
                    case Type::UI64:
                    {
                        --*(__uint64_t*)(stack_ptr - 8);
                        ++code;
                        continue;
                    }
                    case Type::DOUBLE:
                    {
                        --*(double*)(stack_ptr - 8);
                        ++code;
                        continue;
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
            }
                break;
            case OpCode::DIV: {
                Type type = (Type)*stack_ptr;
                switch (type) {
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = __int64_t(a1 / a2);
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 / a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = byte(a1 / a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = char(a1 / a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = short(a1 / a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = uint(a1 / a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = int(a1 / a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
                //0x89ABCDEF: [EF|CD|AB|89];
            }
                break;
            case OpCode::DIVF: {
                //|12345678|T|12345678|T|
                double a2 = *(double*)(stack_ptr -= 8);
                double a1 = *(double*)(stack_ptr -= 9);

                *(double*)(stack_ptr) = a1 / a2;
                stack_ptr += 8;
            }
                break;
            case OpCode::DUP: {
                uint sz = SizeOnStack((Type)*stack_ptr);
                switch (sz) {
                    case 1:
                        *(stack_ptr+2) = *stack_ptr;
                        *(stack_ptr+1) = *(stack_ptr-1);
                        stack_ptr += 2;
                        break;
                    case 2:
                    {
                        auto ptr = stack_ptr-2;
                        *(++stack_ptr) = *ptr;
                        *(++stack_ptr) = *(ptr+1);
                        *(++stack_ptr) = *(ptr+2);
                    }
                        break;
                    case 4:
                    {
                        auto ptr = stack_ptr-4;
                        *(++stack_ptr) = *ptr;
                        *(++stack_ptr) = *(ptr+1);
                        *(++stack_ptr) = *(ptr+2);
                        *(++stack_ptr) = *(ptr+3);
                        *(++stack_ptr) = *(ptr+4);
                    }
                        break;
                    case 8:
                    {
                        auto ptr = stack_ptr-8;
                        *(++stack_ptr) = *ptr;
                        *(++stack_ptr) = *(ptr+1);
                        *(++stack_ptr) = *(ptr+2);
                        *(++stack_ptr) = *(ptr+3);
                        *(++stack_ptr) = *(ptr+4);
                        *(++stack_ptr) = *(ptr+5);
                        *(++stack_ptr) = *(ptr+6);
                        *(++stack_ptr) = *(ptr+7);
                        *(++stack_ptr) = *(ptr+8);
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
            }
                break;
            case OpCode::EQ: {
                Type type = (Type)*stack_ptr;
                switch (type) {
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 == a2;
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 == a2;
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 == a2;
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 == a2;
                    }
                        break;
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 == a2;
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 == a2;
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        double a2 = *(double*)(stack_ptr -= 8);
                        double a1 = *(double*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 == a2;
                    }
                        break;
                    case Type::UTF8:
                    {
                        char* a2 = (char*)*(size_t*)(stack_ptr -= 4);
                        char* a1 = (char*)*(size_t*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = (uint)(bool)!strcmp(a2+ARRAY_METADATA_SIZE, a1+ARRAY_METADATA_SIZE);
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 == a2;
                    }
                        break;
                    case Type::BOOL:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 == a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::PTR_NULL:
                    {
                        stack_ptr -= 9;
                        *(uint*)stack_ptr = *((uint*)stack_ptr) == 0u;
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                stack_ptr += 4;
                *stack_ptr = (byte)Type::BOOL;
            }
                break;
            /*case OpCode::FREELOC:
            {
                :TODO:
            }*/

            case OpCode::GT: {
                Type type = (Type)*stack_ptr;
                switch (type) {
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 > a2;
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 > a2;
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 > a2;
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 > a2;
                    }
                        break;
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 > a2;
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 > a2;
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        double a2 = *(double*)(stack_ptr -= 8);
                        double a1 = *(double*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 > a2;
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 > a2;
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
                /*if(type != Type::UI64 && type != Type::I64) {
                    uint a2 = *(uint*)(stack_ptr -= 4);
                    uint a1 = *(uint*)(stack_ptr -= 5);

                    *(uint*)(stack_ptr) = a1 != a2;
                }
                else if(type == Type::DOUBLE) {
                    double a2 = *(double*)(stack_ptr -= 8);
                    double a1 = *(double*)(stack_ptr -= 9);

                    *(uint*)(stack_ptr) = a1 != a2;
                }
                else {
                    __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                    __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                    *(uint*)(stack_ptr) = a1 > a2;
                }*/
                stack_ptr += 4;
                *stack_ptr = (byte)Type::BOOL;
            }
                break;
            case OpCode::GTE: {
                Type type = (Type)*stack_ptr;
                switch (type) {
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 >= a2;
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 >= a2;
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 >= a2;
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 >= a2;
                    }
                        break;
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 >= a2;
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 >= a2;
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        double a2 = *(double*)(stack_ptr -= 8);
                        double a1 = *(double*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 >= a2;
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 >= a2;
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
                stack_ptr += 4;
                *stack_ptr = (byte)Type::BOOL;
            }
                break;
            case OpCode::INC:
            {
                switch((Type)*stack_ptr){
                    case Type::UI8:{
                        ++*(byte*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::CHAR:{
                        ++*(char*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::UI32:{
                        ++*(uint*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::I16:{
                        ++*(short*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::I32: {
                        ++*(int*)(stack_ptr - 4);
                        ++code;
                        continue;
                    }
                    case Type::I64:{
                        ++*(__int64_t*)(stack_ptr - 8);
                        ++code;
                        continue;
                    }
                    case Type::UI64:{
                        ++*(__uint64_t*)(stack_ptr - 8);
                        ++code;
                        continue;
                    }
                    case Type::DOUBLE:{
                        ++*(double*)(stack_ptr - 8);
                        ++code;
                        continue;
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
            }
                break;

            case OpCode::INV: {
                *(uint*)(stack_ptr - 4) = (uint)~*(bool*)(stack_ptr - 4);
                switch((Type)*stack_ptr){
                    case Type::UI8:
                        *(byte*)(stack_ptr - 1) = ~*(byte*)(stack_ptr - 1);
                        break;
                    case Type::CHAR:
                        *(char*)(stack_ptr - 1) = ~*(char*)(stack_ptr - 1);
                        break;
                    case Type::UI32:
                        *(uint*)(stack_ptr - 4) = ~*(uint*)(stack_ptr - 4);
                        break;
                    case Type::I16:
                        *(short*)(stack_ptr - 2) = ~*(short*)(stack_ptr - 2);
                        break;
                    case Type::I32:
                        *(int*)(stack_ptr - 4) = ~*(int*)(stack_ptr - 4);
                        break;
                    case Type::I64:
                        *(__int64_t*)(stack_ptr - 8) = ~*(__int64_t*)(stack_ptr - 8);
                        break;
                    case Type::UI64:
                        *(__uint64_t*)(stack_ptr - 8) = ~*(__uint64_t*)(stack_ptr - 8);
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
            }
                break;
            case OpCode::JMP:
            {
                code = f->bytecode + (*(uint*)++code); //Checked
            }
                continue;
            case OpCode::JF:
            case OpCode::JZ:
            {
                if(!*(uint*)(stack_ptr -= 4))
                    code = f->bytecode + (*(uint*)++code); //Checked
                else
                    code += 5;
                --stack_ptr;
            }
                continue;
            case OpCode::JT:
            case OpCode::JNZ:
            {
                if(*(uint*)(stack_ptr -= 4))
                    code = f->bytecode + (*(uint*)++code); //Checked
                else
                    code += 5;
                --stack_ptr;
            }
                continue;
            case OpCode::JNULL:
            case OpCode::JNNULL:
            {
                rtThrow(NotImplemented);
            }
                break;

            case OpCode::LD_AREF: {
                ld_aref();
            }
                break;
            case OpCode::LD_BYREF:
            {
                ld_byref();
            }
                break;
            case OpCode::LDARG: {
                ldarg(*(uint*)(++code), f, fargs);
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::LDARG_0:
                ldarg(0, f, fargs);
                break;
            case OpCode::LDARG_1:
                ldarg(1, f, fargs);
                break;
            case OpCode::LDARG_2:
                ldarg(2, f, fargs);
                break;

            case OpCode::LDFLD: {
                ldfld(*(uint*)(++code), f);
                code += 3;
            }
                break;
            case OpCode::LDFLD_0:
                ldfld(0, f);
                break;
            case OpCode::LDFLD_1:
                ldfld(1, f);
                break;
            case OpCode::LDFLD_2:
                ldfld(2, f);
                break;

            case OpCode::LDLOC: {
                ldloc(*(uint*)(++code), f, local_table);
                code += 3;
            }
                break;
            case OpCode::LDLOC_0:
                ldloc(0, f, local_table);
                break;
            case OpCode::LDLOC_1:
                ldloc(1, f, local_table);
                break;
            case OpCode::LDLOC_2:
                ldloc(2, f, local_table);
                break;
            case OpCode::LDELEM:
                ldelem();
                break;
            case OpCode::LD_0: {
                //stackalloc(5);
                *(int*)(++stack_ptr) = 0;
                *(stack_ptr += 4) = (byte)Type::I32;
            }
                break;
            case OpCode::LD_1: {
                //stackalloc(5);
                *(int*)(++stack_ptr) = 1;
                *(stack_ptr += 4) = (byte)Type::I32;
            }
                break;
            case OpCode::LD_2: {
                //stackalloc(5);
                *(int*)(++stack_ptr) = 2;
                *(stack_ptr += 4) = (byte)Type::I32;
            }
                break;
            case OpCode::LD_0U: {
                //stackalloc(5);
                *(uint*)(++stack_ptr) = 0u;
                *(stack_ptr += 4) = (byte)Type::UI32;
            }
                break;
            case OpCode::LD_1U: {
                //stackalloc(5);
                *(uint*)(++stack_ptr) = 1u;
                *(stack_ptr += 4) = (byte)Type::UI32;
            }
                break;
            case OpCode::LD_2U: {
                //stackalloc(5);
                *(uint*)(++stack_ptr) = 2u;
                *(stack_ptr += 4) = (byte)Type::UI32;
            }
                break;

            case OpCode::LD_F:
            {
                //stackalloc(9);
                *(double*)(++stack_ptr) = *(double*)(++code);
                code += 7;
                *(stack_ptr += 8) = (byte)Type::DOUBLE;
            }
                break;
            case OpCode::LD_FALSE:
            {
                //stackalloc(5);
                *(uint*)(++stack_ptr) = 0;
                *(stack_ptr += 4) = (byte)Type::BOOL;
            }
                break;
            case OpCode::LD_TRUE:
            {
                //stackalloc(5);
                //stackalloc(5);
                *(uint*)(++stack_ptr) = 1;
                *(stack_ptr += 4) = (byte)Type::BOOL;
            }
                break;
            case OpCode::LD_UI8:
            {
                //stackalloc(5);
                *(byte*)(++stack_ptr) = *(byte*)(++code);
                code += 3;
                *(stack_ptr += 4) = (byte)Type::UI8;
                break;
            }

            case OpCode::LD_I16:
            {
                //stackalloc(5);
                *(short*)(++stack_ptr) = *(short*)(++code);
                code += 3;
                *(stack_ptr += 4) = (byte)Type::I16;
                break;
            }
            case OpCode::LD_CHR:
            {
                *(int*)(++stack_ptr) = *(int*)(++code);
                code += 3;
                *(stack_ptr += 4) = (byte)Type::CHAR;
            }
                break;

            case OpCode::LD_I32:{
                //stackalloc(5);
                *(int*)(++stack_ptr) = *(int*)(++code);
                code += 3;
                *(stack_ptr += 4) = (byte)Type::I32;
            }
                break;
            case OpCode::LD_UI32:
            {
                //stackalloc(5);
                *(uint*)(++stack_ptr) = *(uint*)(++code);
                code += 3;
                *(stack_ptr += 4) = (byte)Type::UI32;
                break;
            }

            case OpCode::LD_I64:
            {
                //stackalloc(9);
                *(__int64_t*)(++stack_ptr) = *(__int64_t*)(++code);
                code += 7;
                *(stack_ptr += 8) = (byte)Type::I64;
                break;
            }

            case OpCode::LD_UI64:
            {
                //stackalloc(9);
                *(__uint64_t*)(++stack_ptr) = *(__uint64_t*)(++code);
                code += 7;
                *(stack_ptr += 8) = (byte)Type::UI64;
                break;
            }
            case OpCode::LD_NULL: {
                *(uint*)(++stack_ptr) = 0;
                *(stack_ptr += 4) = (byte)Type::PTR_NULL;
                //rtThrow(NotImplemented);
            }
                break;
            case OpCode::LD_STR: {
                char* ptr = f->module->strings + *(uint*)(++code);
                uint len = strlen(ptr)+1;

                //allocMem(len);
                byte* addr = memoryManager.Allocate(Type::UTF8, len + ARRAY_METADATA_SIZE);

                strcpy((char*)addr + ARRAY_METADATA_SIZE, ptr);
                *addr = (byte)Type::UTF8;
                *(uint*)(addr+1) = len;

                //stackalloc(5);
                //++stack_ptr;
                *(size_t*)++stack_ptr = (size_t)addr;

                //cout << "STR ADDR: " << hex << (size_t)(char*)mem_first_free << dec << endl;

                //mem_first_free += len;

                *(stack_ptr += 4) = (byte)Type::UTF8;
                code += 3;
                //f->module->strings
            }
                break;

            case OpCode::LT: {
                Type type = (Type)*stack_ptr;

                switch (type) {
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 < a2;
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 < a2;
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 < a2;
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 < a2;
                    }
                        break;
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 < a2;
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 < a2;
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        double a2 = *(double*)(stack_ptr -= 8);
                        double a1 = *(double*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 < a2;
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 < a2;
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
                *(stack_ptr += 4) = (byte)Type::BOOL;
            }
                break;
            case OpCode::LTE: {
                Type type = (Type)*stack_ptr;
                switch (type) {
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 <= a2;
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 <= a2;
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 <= a2;
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 <= a2;
                    }
                        break;
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 <= a2;
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 <= a2;
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        double a2 = *(double*)(stack_ptr -= 8);
                        double a1 = *(double*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 <= a2;
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 <= a2;
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
                *(stack_ptr += 4) = (byte)Type::BOOL;
            }
                break;
            case OpCode::MUL: {
                Type type = (Type)*stack_ptr;
                switch (type) {
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = __int64_t(a1 * a2);
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 * a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(byte*)(stack_ptr) = byte(a1 * a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(char*)(stack_ptr) = char(a1 * a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(short*)(stack_ptr) = short(a1 * a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = uint(a1 * a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = int(a1 * a2);
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                        ++code;
                        continue;
                    }
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

                /*if(type != Type::UI64 && type != Type::I64)
                {
                    uint a2 = *(uint*)(stack_ptr -= 4);
                    uint a1 = *(uint*)(stack_ptr -= 5);

                    *(uint*)(stack_ptr) = a1 * a2;
                    stack_ptr += 4; //sizeof(int) + 1 byte for type
                }
                else {
                    __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                    __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                    *(__uint64_t*)(stack_ptr) = a1 * a2;
                    stack_ptr += 8; //sizeof(int) + 1 byte for type
                }*/
                //0x89ABCDEF: [EF|CD|AB|89];
            }
                break;
            case OpCode::MULF: {
                double a2 = *(double*)(stack_ptr -= 8);
                double a1 = *(double*)(stack_ptr -= 9);

                *(double*)(stack_ptr) = a1 * a2;
                stack_ptr += 8;
            }
                break;
            case OpCode::NEG: {
                switch((Type)*stack_ptr){
                    case Type::UI8:
                    case Type::CHAR:
                    case Type::UI32:{
                        *(uint*)(stack_ptr - 4) *= -1; // = -*(uint*)(stack_ptr - 4);
                        break;
                    }
                    case Type::I16:
                    case Type::I32: {
                        *(int*)(stack_ptr - 4) *= -1; //*(int*)(stack_ptr - 4);
                        break;
                    }
                    case Type::I64:{
                        *(__int64_t*)(stack_ptr - 8) *= -1; //*(__int64_t*)(stack_ptr - 8);
                        break;
                    }
                    case Type::UI64:{
                        *(__uint64_t*)(stack_ptr - 8) *= -1; //*(__uint64_t*)(stack_ptr - 8);
                        break;
                    }
                    case Type::DOUBLE:{
                        *(double*)(stack_ptr - 8) *= -1.0; //*(double*)(stack_ptr - 8);
                        break;
                    }
                    default:
                        rtThrow(Runtime::NotImplemented);
                        break;
                }
            }
                break;
            case OpCode::NEQ: {
                Type type = (Type)*stack_ptr;

                switch (type) {
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 != a2;
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 != a2;
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 != a2;
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 != a2;
                    }
                        break;
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 != a2;
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 != a2;
                    }
                        break;
                    case Type::UTF8:
                    {
                        char* a2 = (char*)*(size_t*)(stack_ptr -= 4);
                        char* a1 = (char*)*(size_t*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = (uint)(bool)strcmp(a2, a1);
                    }
                        break;
                    case Type::DOUBLE:
                    {
                        double a2 = *(double*)(stack_ptr -= 8);
                        double a1 = *(double*)(stack_ptr -= 9);

                        *(uint*)(stack_ptr) = a1 != a2;
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 != a2;
                    }
                        break;
                    case Type::BOOL:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 != a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                    case Type::PTR_NULL:
                    {
                        stack_ptr -= 9;
                        *(uint*)stack_ptr = *((uint*)stack_ptr) != 0u;
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
                *(stack_ptr += 4) = (byte)Type::BOOL;
            }
                break;
            /*case OpCode::NEWLOC: {
                Type t = *(Type*)(++code);
                switch (t) {
                    case Type::DOUBLE:
                    case Type::I64:
                    case Type::UI64:
                        allocMem(9);
                        break;
                    default:
                        allocMem(5);
                        break;
                }
            }
                break;*/
            case OpCode::NEWARR:
            {
                uint count = *(uint*)(stack_ptr -= 4);
                byte* type = *(uint*)(++code) + f->module->types;
                code += 3;

                Type t = (Type)*type;
                uint size;
                switch(t)
                {
                    //case Type::ARRAY:
                    //    break;
                    case Type::CLASS:
                        break;
                    default:
                        size = count * Sizeof(t) + ARRAY_METADATA_SIZE;
                        auto ptr = memoryManager.Allocate(size);
                        *ptr = (byte)t;
                        *(uint*)(ptr+1) = count;
                        *(uint*)stack_ptr = (uint)ptr;
                        *(stack_ptr += 4) = (byte)Type::ARRAY;
                        break;
                }


                //uint as = *(uint*)(type+1);
            }
                break;
            case OpCode::NOP: {
                //Just as planned
            }
                break;
            case OpCode::NOT: {
                *(uint*)(stack_ptr - 4) = (uint)!*(bool*)(stack_ptr - 4);
            }
                break;
            case OpCode::OR: {
                Type type = (Type)*stack_ptr;
                switch (type)
                {
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 | a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = a1 | a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 | a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 | a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 | a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 | a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 | a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::BOOL:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 | a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    /*default:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 | a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }*/
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
            }
                break;
            case OpCode::POP: {
                Type type = (Type)*stack_ptr;
                if(type < Type::UI64)
                    stack_ptr -= 5;
                else if((byte)type & (byte)Type::REF)
                    stack_ptr -= 5;
                else
                    stack_ptr -= 9;
            }
                break;
            case OpCode::POS: {
                rtThrow(Runtime::NotImplemented);
            }
                break;
            case OpCode::REM: {
                Type type = (Type)*stack_ptr;
                switch (type)
                {
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 % a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = a1 % a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 % a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 % a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 % a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 % a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 % a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

            }
                break;
            case OpCode::REMF: {
                rtThrow(Runtime::NotImplemented);
            }
                break;
            case OpCode::RET: {
                //if(stack_guard != stack_ptr-Sizeof(f->ret))
                //{
                //    stringstream ss;
                //    ss << "stack_ptr - guard = " << (int)(stack_ptr - stack_guard-Sizeof(f->ret)) << " in " << f->sign;
                //    rtThrow(StackCorrupted, ss.str());
                //}
                return;
            }
            case OpCode::SHL: {
                Type type = (Type)*stack_ptr;
                switch (type)
                {
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 << a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = a1 << a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 << a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 << a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 << a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 << a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 << a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }
            }
                break;
            case OpCode::SHR: {
                Type type = (Type)*stack_ptr;
                switch (type)
                {
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 >> a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = a1 >> a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 >> a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 >> a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 >> a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 >> a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 >> a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

            }
                break;
            case OpCode::SIZEOF: {
                Type t = *(Type*)(++code);
                switch (t) {
                    case Type::DOUBLE:
                    case Type::I64:
                    case Type::UI64:
                        stack_ptr -= 8;
                        break;
                    default:
                        stack_ptr -= 4;
                        break;
                }
                *(uint*)stack_ptr = Runtime::Sizeof(t);
                *(stack_ptr += 4) = (byte)Type::I32;
            }
                break;
            case OpCode::ST_BYREF:
            {
                st_byref();
            }
                break;
            case OpCode::STARG: {
                starg(*(uint*)(++code), f, fargs);
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::STARG_0: {
                starg(0, f, fargs);
            }
                break;
            case OpCode::STARG_1: {
                starg(1, f, fargs);
            }
                break;
            case OpCode::STARG_2: {
                starg(2, f, fargs);
            }
                break;
            case OpCode::STFLD: {
                stfld(*(uint*)(++code), f);
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::STFLD_0: {
                stfld(0, f);
            }
                break;
            case OpCode::STFLD_1: {
                stfld(1, f);
            }
                break;
            case OpCode::STFLD_2: {
                stfld(2, f);
            }
                break;
            case OpCode::STLOC: {
                stloc(*(uint*)(++code), f, local_table);
                code += 3; //FIXME: May fail
            }
                break;
            case OpCode::STLOC_0: {
                stloc(0, f, local_table);
            }
                break;
            case OpCode::STLOC_1: {
                stloc(1, f, local_table);
            }
                break;
            case OpCode::STLOC_2: {
                stloc(2, f, local_table);
            }
                break;
            case OpCode::STELEM: {
                stelem();
            }
                break;
            case OpCode::SUB: {
                Type type = (Type)*stack_ptr;

                switch (type)
                {
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 - a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = a1 - a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 - a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 - a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 - a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 - a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 - a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

            }
                break;
            case OpCode::SUBF: {
                double a2 = *(double*)(stack_ptr -= 8);
                double a1 = *(double*)(stack_ptr -= 9);

                *(double*)(stack_ptr) = a1 - a2;
                stack_ptr += 8;
            }
                break;
            case OpCode::XOR: {
                Type type = (Type)*stack_ptr;
                switch (type)
                {
                    case Type::UI64:
                    {
                        __uint64_t a2 = *(__uint64_t*)(stack_ptr -= 8);
                        __uint64_t a1 = *(__uint64_t*)(stack_ptr -= 9);

                        *(__uint64_t*)(stack_ptr) = a1 ^ a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I64:
                    {
                        __int64_t a2 = *(__int64_t*)(stack_ptr -= 8);
                        __int64_t a1 = *(__int64_t*)(stack_ptr -= 9);

                        *(__int64_t*)(stack_ptr) = a1 ^ a2;
                        stack_ptr += 8; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI8:
                    {
                        byte a2 = *(byte*)(stack_ptr -= 4);
                        byte a1 = *(byte*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 ^ a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::CHAR:
                    {
                        char a2 = *(char*)(stack_ptr -= 4);
                        char a1 = *(char*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 ^ a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I16:
                    {
                        short a2 = *(short*)(stack_ptr -= 4);
                        short a1 = *(short*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 ^ a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::UI32:
                    {
                        uint a2 = *(uint*)(stack_ptr -= 4);
                        uint a1 = *(uint*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 ^ a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::I32:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(int*)(stack_ptr) = a1 ^ a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    case Type::BOOL:
                    {
                        int a2 = *(int*)(stack_ptr -= 4);
                        int a1 = *(int*)(stack_ptr -= 5);

                        *(uint*)(stack_ptr) = a1 ^ a2;
                        stack_ptr += 4; //sizeof(int) + 1 byte for type
                    }
                        break;
                    default:
                        rtThrow(Runtime::IllegalType, OpcodeToStr(*code));
                        break;
                }

            }

                break;
            default:
                log.SetType(Log::Error);
                log << "\nUnknown instruction 0x" << hex << (uint)(byte)*code << "\n";
                log << "  address: 0x" << (uint)(code - f->bytecode) << dec << "\n";
                log << "Call stack:\n";
                //printStackTrace("  at: ");
                rtThrow(IllegalOperation);
                break;
        }
        ++code;
    }
}

const char* Runtime::OpcodeToStr(OpCode op)
{
    switch (op) {
        case OpCode::NOP:
            return "NOP";
        case OpCode::DUP:
            return "DUP";
        case OpCode::BAND:
            return "BAND";
        case OpCode::BOR:
            return "BOR";
        case OpCode::ADD:
            return "ADD";
        case OpCode::ADDF:
            return "ADDF";
        case OpCode::SUB:
            return "SUB";
        case OpCode::SUBF:
            return "SUBF";
        case OpCode::MUL:
            return "MUL";
        case OpCode::MULF:
            return "MULF";
        case OpCode::DIV:
            return "DIV";
        case OpCode::DIVF:
            return "DIVF";
        case OpCode::REM:
            return "REM";
        case OpCode::REMF:
            return "REMF";
        case OpCode::CONV_UI8:
            return "CONV_UI8";
        case OpCode::CONV_I16:
            return "CONV_I16";
        case OpCode::CONV_CHR:
            return "CONV_CHR";
        case OpCode::CONV_I32:
            return "CONV_I32";
        case OpCode::CONV_UI32:
            return "CONV_UI32";
        case OpCode::CONV_I64:
            return "CONV_I64";
        case OpCode::CONV_UI64:
            return "CONV_UI64";
        case OpCode::CONV_F:
            return "CONV_F";
        case OpCode::JMP:
            return "JMP";
        case OpCode::JZ:
            return "JZ";
        case OpCode::JT:
            return "JT";
        case OpCode::JNZ:
            return "JNZ";
        case OpCode::JF:
            return "JF";
        case OpCode::JNULL:
            return "JNULL";
        case OpCode::JNNULL:
            return "JNNULL";
        case OpCode::CALL:
            return "CALL";
        case OpCode::NEWARR:
            return "NEWARR";
        case OpCode::LDLOC:
            return "LDLOC";
        case OpCode::LDLOC_0:
            return "LDLOC_0";
        case OpCode::LDLOC_1:
            return "LDLOC_1";
        case OpCode::LDLOC_2:
            return "LDLOC_2";
        case OpCode::STLOC:
            return "STLOC";
        case OpCode::STLOC_0:
            return "STLOC_0";
        case OpCode::STLOC_1:
            return "STLOC_1";
        case OpCode::STLOC_2:
            return "STLOC_2";
        case OpCode::LDELEM:
            return "LDELEM";
        case OpCode::LDELEM_0:
            return "LDELEM_0";
        case OpCode::LDELEM_1:
            return "LDELEM_1";
        case OpCode::LDELEM_2:
            return "LDELEM_2";
        case OpCode::LD_AREF:
            return "LD_AREF";
        case OpCode::LD_BYREF:
            return "LD_BYREF";
        case OpCode::STELEM:
            return "STELEM";
        case OpCode::STELEM_0:
            return "STELEM_0";
        case OpCode::STELEM_1:
            return "STELEM_1";
        case OpCode::STELEM_2:
            return "STELEM_2";
        case OpCode::ST_BYREF:
            return "ST_BYREF";
        case OpCode::LDARG:
            return "LDARG";
        case OpCode::LDARG_0:
            return "LDARG_0";
        case OpCode::LDARG_1:
            return "LDARG_1";
        case OpCode::LDARG_2:
            return "LDARG_2";
        case OpCode::STARG:
            return "STARG";
        case OpCode::STARG_0:
            return "STARG_0";
        case OpCode::STARG_1:
            return "STARG_1";
        case OpCode::STARG_2:
            return "STARG_2";
        case OpCode::LDFLD:
            return "LDFLD";
        case OpCode::LDFLD_0:
            return "LDFLD_0";
        case OpCode::LDFLD_1:
            return "LDFLD_1";
        case OpCode::LDFLD_2:
            return "LDFLD_2";
        case OpCode::STFLD:
            return "STFLD";
        case OpCode::STFLD_0:
            return "STFLD_0";
        case OpCode::STFLD_1:
            return "STFLD_1";
        case OpCode::STFLD_2:
            return "STFLD_2";
        case OpCode::LD_0:
            return "LD_0";
        case OpCode::LD_1:
            return "LD_1";
        case OpCode::LD_2:
            return "LD_2";
        case OpCode::LD_0U:
            return "LD_0U";
        case OpCode::LD_1U:
            return "LD_1U";
        case OpCode::LD_2U:
            return "LD_2U";
        case OpCode::LD_STR:
            return "LD_STR";
        case OpCode::LD_UI8:
            return "LD_UI8";
        case OpCode::LD_I16:
            return "LD_I16";
        case OpCode::LD_CHR:
            return "LD_CHR";
        case OpCode::LD_I32:
            return "LD_I32";
        case OpCode::LD_UI32:
            return "LD_UI32";
        case OpCode::LD_I64:
            return "LD_I64";
        case OpCode::LD_UI64:
            return "LD_UI64";
        case OpCode::LD_F:
            return "LD_F";
        case OpCode::LD_TRUE:
            return "LD_TRUE";
        case OpCode::LD_FALSE:
            return "LD_FALSE";
        case OpCode::LD_NULL:
            return "LD_NULL";
        case OpCode::AND:
            return "AND";
        case OpCode::OR:
            return "OR";
        case OpCode::EQ:
            return "EQ";
        case OpCode::NEQ:
            return "NEQ";
        case OpCode::NOT:
            return "NOT";
        case OpCode::INV:
            return "INV";
        case OpCode::XOR:
            return "XOR";
        case OpCode::NEG:
            return "NEG";
        case OpCode::POS:
            return "POS";
        case OpCode::INC:
            return "INC";
        case OpCode::DEC:
            return "DEC";
        case OpCode::SHL:
            return "SHL";
        case OpCode::SHR:
            return "SHR";
        case OpCode::POP:
            return "POP";
        case OpCode::GT:
            return "GT";
        case OpCode::GTE:
            return "GTE";
        case OpCode::LT:
            return "LT";
        case OpCode::LTE:
            return "LTE";
        case OpCode::SIZEOF:
            return "SIZEOF";
        case OpCode::TYPEOF:
            return "TYPEOF";
        case OpCode::RET:
            return "RET";
        case OpCode::CALL_INTERNAL:
            return "CALL_INTERNAL";
        case OpCode::BREAK:
            return "BREAK";
        case OpCode::UNDEFINED:
        default:
            return "UNDEFINED";
    }
    return "UNDEFINED";
}

void Runtime::ldarg(uint idx, Function* f, byte* fargs)
{
    LocalVar* arg = &f->args[idx];
    uint sz = Sizeof(arg->type);
    memcpy(++stack_ptr, fargs + arg->addr, sz);
    *(stack_ptr += sz) = *arg->type;
}


void Runtime::ldfld(uint idx, Function *f)
{
    GlobalVar* gv = &f->module->globals[idx];
    uint sz = Sizeof(gv->type);
    memcpy(++stack_ptr, gv->addr, sz);
    //*(stack_ptr += sz) = (byte)gv->type;
    *(stack_ptr += sz) = *gv->type;
}


void Runtime::ldloc(uint idx, Function *f, byte* locals)
{
    LocalVar* lv = &f->locals[idx];
    uint sz = Sizeof(lv->type);
    memcpy(++stack_ptr, locals + lv->addr, sz);
    //*(stack_ptr += sz) = (byte)lv->type;
    *(stack_ptr += sz) = *lv->type;
}


void Runtime::ldelem()
{
    uint idx;
    Type t = *(Type*)stack_ptr;
    switch(t)
    {
        case Type::UI8:
        case Type::UI32:
            idx = *(uint*)(stack_ptr -= 4);
            break;
        case Type::I16:
        case Type::I32:
            idx = *(uint*)(stack_ptr -= 4);
            break;
        case Type::I64:
        case Type::UI64:
        {
            uint64_t i = *(uint64_t*)(stack_ptr -= 8);
            if(i > MAX_ARRAY_SIZE)
                rtThrow(Runtime::OutOfRange);
            idx = (uint)i;
            break;
        }
           break;
        default:
            rtThrow(Runtime::IllegalType);
            break;
    }

    //cout << "Index: " << idx << endl;
    // pop eax -- index
    // pop edx -- array_addr
    // mov ecx, [eax+5]
    // push ecx
    // call jitSizeOf
    //

    byte* addr = (byte*)*(uint*)(stack_ptr -= 5);
    t = *(Type*)addr;
    uint count = *(uint*)(addr+1);

    if(idx >= count) //OPTIMIZE
        rtThrow(Runtime::OutOfRange);
    uint size = Sizeof(t);

    if(t == Type::CHAR)
        memcpy(stack_ptr, addr + ARRAY_METADATA_SIZE + idx, 1);         ///@todo: CHECK TYPE OVERRIDE (*stack_ptr)
    else
        memcpy(stack_ptr, addr + ARRAY_METADATA_SIZE + idx*size, size);
    *(stack_ptr += SizeOnStack(t)) = (byte)t;
}

void Runtime::ld_aref()
{
    // addr idx t
    uint idx;
    Type t = *(Type*)stack_ptr;
    switch(t)
    {
        case Type::UI8:
        case Type::UI32:
            idx = *(uint*)(stack_ptr -= 4);
            break;
        case Type::I16:
        case Type::I32:
            idx = *(uint*)(stack_ptr -= 4);
            break;
        case Type::I64:
        case Type::UI64:
        {
            uint64_t i = *(uint64_t*)(stack_ptr -= 4);
            if(i > MAX_ARRAY_SIZE)
                rtThrow(Runtime::OutOfRange);
            idx = (uint)i;
            break;
        }
           break;
        default:
            rtThrow(Runtime::IllegalType);
            break;
    }

    //cout << "Index: " << idx << endl;

    byte* addr = (byte*)*(uint*)(stack_ptr -= 5);
    t = *(Type*)addr;
    uint count = *(uint*)(addr+1);

    if(idx >= count) //OPTIMIZE
        rtThrow(Runtime::OutOfRange);
    uint size = Sizeof(t);

    if(t == Type::CHAR)
        *(uint*)stack_ptr = (uint)(addr + ARRAY_METADATA_SIZE + idx);
        //memcpy(stack_ptr, addr + ARRAY_METADATA_SIZE + idx, 1);
    else
        *(uint*)stack_ptr = (uint)(addr + ARRAY_METADATA_SIZE + idx*size);
    *(stack_ptr += 4) = (byte)t | (byte)Type::REF;
}

void Runtime::ld_byref()
{
    //uint idx;
    Type t = (Type)(((byte)*(Type*)(stack_ptr)) ^ ((byte)Type::REF));

    byte* addr = (byte*)*(uint*)(stack_ptr -= 4);

    uint size = Sizeof(t);

    if(t == Type::CHAR)
    {
        *(char*)stack_ptr = *addr;
        *(stack_ptr += 4) = (byte)Type::CHAR;
    }
    else
    {
        memcpy(stack_ptr, addr, size);
        *(stack_ptr += SizeOnStack(t)) = (byte)t;
    }
}

void Runtime::st_byref()
{
    //
    
    Type vtype = *(Type*)(stack_ptr);
    uint vsize = Sizeof(vtype);
    byte* val = (stack_ptr -= SizeOnStack(vtype));

    Type t = (Type)(*(byte*)(stack_ptr-1) ^ (byte)Type::REF);
    //Type t = *(Type*)stack_ptr;

    //cout << "Index: " << idx << endl;

    byte* addr = (byte*)*(uint*)(stack_ptr -= 5);
    //Type t = *(Type*)addr;
    //uint size = Sizeof(t);

    if(t == Type::CHAR)
        *addr = (char)*stack_ptr;
    else
        memcpy(addr, val, vsize);
    --stack_ptr;
}

void Runtime::starg(uint idx, Function *f, byte* fargs) ///TODO
{
    LocalVar* arg = &f->args[idx];
    arg->inited = true;
    uint sz = Sizeof(arg->type);
    stack_ptr -= sz;
    memcpy(fargs + arg->addr, stack_ptr, sz);
    --stack_ptr;
}

void Runtime::stfld(uint idx, Function *f)
{
    GlobalVar* gv = &f->module->globals[idx];
    gv->inited = true;
    uint sz = Sizeof(gv->type);
    stack_ptr -= sz;
    memcpy(gv->addr, stack_ptr, sz);
    --stack_ptr;
}

void Runtime::stloc(uint idx, Function *f, byte* locals)
{
    LocalVar* lv = &f->locals[idx];
    lv->inited = true;
    uint sz = Sizeof(lv->type);
    stack_ptr -= sz;
    memcpy(locals + lv->addr, stack_ptr, sz);
    --stack_ptr;
}

void Runtime::stelem()
{
    Type vtype = *(Type*)(stack_ptr);
    uint vsize = Sizeof(vtype);
    byte* val = (stack_ptr -= SizeOnStack(vtype));

    uint idx;
    Type t = *(Type*)(stack_ptr-1);
    //Type t = *(Type*)stack_ptr;
    switch(t)
    {
        case Type::UI8:
        case Type::UI32:
            idx = *(uint*)(stack_ptr -= 5);
            break;
        case Type::I16:
        case Type::I32:
            idx = *(uint*)(stack_ptr -= 5);
            break;
        case Type::I64:
        case Type::UI64:
        {
            uint64_t i = *(uint64_t*)(stack_ptr -= 9);
            if(i > MAX_ARRAY_SIZE)
                rtThrow(Runtime::OutOfRange);
            idx = (uint)i;
            break;
        }
        default:
            rtThrow(Runtime::IllegalType);
    }
    //cout << "Index: " << idx << endl;

    byte* addr = (byte*)*(uint*)(stack_ptr -= 5);
    //Type t = *(Type*)addr;
    uint count = *(uint*)(addr+1);

    if(idx >= count) //OPTIMIZE
        rtThrow(Runtime::OutOfRange);
    //uint size = Sizeof(t);

    if(t == Type::CHAR)
        memcpy(addr + ARRAY_METADATA_SIZE + idx, val, 1);
    else
        memcpy(addr + ARRAY_METADATA_SIZE + idx*vsize, val, vsize);
    --stack_ptr;
    //*(stack_ptr += vsize) = (byte)vtype;
}

void Runtime::conv_ui8()
{
    //stackalloc(4);
    switch((Type)*stack_ptr){
        case Type::UI32: {
            uint arg = *(uint*)(stack_ptr -= 4);
            *(byte*)stack_ptr = (byte)arg;
            stack_ptr += 4;
            break;
        }
        case Type::CHAR:{
            char arg = *(char*)(stack_ptr -= 4);
            *(byte*)stack_ptr = (byte)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I16:{
            short arg = *(short*)(stack_ptr -= 4);
            *(byte*)stack_ptr = (byte)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I32: {
            int arg = *(int*)(stack_ptr -= 4);
            *(byte*)stack_ptr = (byte)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I64:{
            __int64_t arg = *(__int64_t*)(stack_ptr -= 8);
            *(byte*)stack_ptr = (byte)arg;
            stack_ptr += 4;
            break;
        }
        case Type::UI64:{
            __uint64_t arg = *(__uint64_t*)(stack_ptr -= 8);
            *(byte*)stack_ptr = (byte)arg;
            stack_ptr += 4;
            break;
        }
        case Type::DOUBLE:{
            double arg = *(double*)(stack_ptr -= 8);
            *(byte*)stack_ptr = (byte)arg;
            stack_ptr += 4;
            break;
        }
        default:
            rtThrow(Runtime::NotImplemented);
            break;
    }
    *stack_ptr = (byte)Type::UI8;
}

void Runtime::conv_i16()
{
    //stackalloc(4);
    switch((Type)*stack_ptr){
        case Type::UI8: {
            byte arg = *(byte*)(stack_ptr -= 4);
            *(short*)stack_ptr = (short)arg;
            stack_ptr += 4;
            break;
        }
        case Type::CHAR: {
            char arg = *(char*)(stack_ptr -= 4);
            *(short*)stack_ptr = (short)arg;
            stack_ptr += 4;
            break;
        }
        case Type::UI32: {
            uint arg = *(uint*)(stack_ptr -= 4);
            *(short*)stack_ptr = (short)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I32: {
            int arg = *(int*)(stack_ptr -= 4);
            *(short*)stack_ptr = (short)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I64:{
            __int64_t arg = *(__int64_t*)(stack_ptr -= 8);
            *(short*)stack_ptr = (short)arg;
            stack_ptr += 4;
            break;
        }
        case Type::UI64:{
            __uint64_t arg = *(__uint64_t*)(stack_ptr -= 8);
            *(short*)stack_ptr = (short)arg;
            stack_ptr += 4;
            break;
        }
        case Type::DOUBLE:{
            double arg = *(double*)(stack_ptr -= 8);
            *(short*)stack_ptr = (short)arg;
            stack_ptr += 4;
            break;
        }
        default:
            rtThrow(Runtime::NotImplemented);
            break;
    }
    *stack_ptr = (byte)Type::I16;
}

void Runtime::conv_i32()
{
    //stackalloc(4);
    switch((Type)*stack_ptr){
        case Type::UI8: {
            byte arg = *(byte*)(stack_ptr -= 4);
            *(int*)stack_ptr = (int)arg;
            stack_ptr += 4;
            break;
        }
        case Type::CHAR: {
            char arg = *(char*)(stack_ptr -= 4);
            *(int*)stack_ptr = (int)arg;
            stack_ptr += 4;
            break;
        }
        case Type::UI32: {
            uint arg = *(uint*)(stack_ptr -= 4);
            *(int*)stack_ptr = (int)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I16: {
            short arg = *(short*)(stack_ptr -= 4);
            *(int*)stack_ptr = (int)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I64:{
            __int64_t arg = *(__int64_t*)(stack_ptr -= 8);
            *(int*)stack_ptr = (int)arg;
            stack_ptr += 4;
            break;
        }
        case Type::UI64:{
            __uint64_t arg = *(__uint64_t*)(stack_ptr -= 8);
            *(int*)stack_ptr = (int)arg;
            stack_ptr += 4;
            break;
        }
        case Type::DOUBLE:{
            double arg = *(double*)(stack_ptr -= 8);
            *(int*)stack_ptr = (int)arg;
            stack_ptr += 4;
            break;
        }
        default:
            rtThrow(Runtime::NotImplemented);
            break;
    }
    *stack_ptr = (byte)Type::I32;
}

void Runtime::conv_chr()
{
    //stackalloc(4);
    switch((Type)*stack_ptr){
        case Type::UI32: {
            uint arg = *(uint*)(stack_ptr -= 4);
            *(char*)stack_ptr = (char)arg;
            stack_ptr += 4;
            break;
        }
        case Type::UI8:{
            byte arg = *(byte*)(stack_ptr -= 4);
            *(char*)stack_ptr = (char)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I16:{
            short arg = *(short*)(stack_ptr -= 4);
            *(char*)stack_ptr = (char)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I32: {
            int arg = *(int*)(stack_ptr -= 4);
            *(char*)stack_ptr = (char)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I64:{
            __int64_t arg = *(__int64_t*)(stack_ptr -= 8);
            *(char*)stack_ptr = (char)arg;
            stack_ptr += 4;
            break;
        }
        case Type::UI64:{
            __uint64_t arg = *(__uint64_t*)(stack_ptr -= 8);
            *(char*)stack_ptr = (char)arg;
            stack_ptr += 4;
            break;
        }
        default:
            //rtThrow(Runtime::NotImplemented);
            break;
    }
    *stack_ptr = (byte)Type::CHAR;
}

void Runtime::conv_ui32()
{
    //stackalloc(4);
    switch((Type)*stack_ptr){
        case Type::UI8: {
            byte arg = *(byte*)(stack_ptr -= 4);
            *(uint*)stack_ptr = (uint)arg;
            stack_ptr += 4;
            break;
        }
        case Type::CHAR: {
            char arg = *(char*)(stack_ptr -= 4);
            *(uint*)stack_ptr = (uint)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I16: {
            short arg = *(short*)(stack_ptr -= 4);
            *(uint*)stack_ptr = (uint)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I32: {
            int arg = *(int*)(stack_ptr -= 4);
            *(uint*)stack_ptr = (uint)arg;
            stack_ptr += 4;
            break;
        }
        case Type::I64:{
            __int64_t arg = *(__int64_t*)(stack_ptr -= 8);
            *(uint*)stack_ptr = (uint)arg;
            stack_ptr += 4;
            break;
        }
        case Type::UI64:{
            __uint64_t arg = *(__uint64_t*)(stack_ptr -= 8);
            *(uint*)stack_ptr = (uint)arg;
            stack_ptr += 4;
            break;
        }
        case Type::DOUBLE:{
            double arg = *(double*)(stack_ptr -= 8);
            *(uint*)stack_ptr = (uint)arg;
            stack_ptr += 4;
            break;
        }
        default:
            rtThrow(Runtime::NotImplemented);
            break;
    }
    *stack_ptr = (byte)Type::UI32;
}

void Runtime::conv_i64()
{
    //stackalloc(4);
    switch((Type)*stack_ptr) {
        case Type::UI8: {
            byte arg = *(byte*)(stack_ptr -= 4);
            *(__int64_t*)stack_ptr = (__int64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::CHAR: {
            char arg = *(char*)(stack_ptr -= 4);
            *(__int64_t*)stack_ptr = (__int64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::UI32: {
            uint arg = *(uint*)(stack_ptr -= 4);
            *(__int64_t*)stack_ptr = (__int64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::I16: {
            short arg = *(short*)(stack_ptr -= 4);
            *(__int64_t*)stack_ptr = (__int64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::I32: {
            int arg = *(int*)(stack_ptr -= 4);
            *(__int64_t*)stack_ptr = (__int64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::UI64:{
            __uint64_t arg = *(__uint64_t*)(stack_ptr -= 8);
            *(__int64_t*)stack_ptr = (__int64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::DOUBLE:{
            double arg = *(double*)(stack_ptr -= 8);
            *(__int64_t*)stack_ptr = (__int64_t)arg;
            stack_ptr += 8;
            break;
        }
        default:
            rtThrow(Runtime::NotImplemented);
            break;
    }
    *stack_ptr = (byte)Type::I64;
}

void Runtime::conv_ui64()
{
    //stackalloc(4);
    switch((Type)*stack_ptr) {
        case Type::UI8: {
            byte arg = *(byte*)(stack_ptr -= 4);
            *(__uint64_t*)stack_ptr = (__uint64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::CHAR: {
            char arg = *(char*)(stack_ptr -= 4);
            *(__uint64_t*)stack_ptr = (__uint64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::UI32: {
            uint arg = *(uint*)(stack_ptr -= 4);
            *(__uint64_t*)stack_ptr = (__uint64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::I16: {
            short arg = *(short*)(stack_ptr -= 4);
            *(__uint64_t*)stack_ptr = (__uint64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::I32: {
            int arg = *(int*)(stack_ptr -= 4);
            *(__uint64_t*)stack_ptr = (__uint64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::I64:{
            __int64_t arg = *(__int64_t*)(stack_ptr -= 8);
            *(__uint64_t*)stack_ptr = (__uint64_t)arg;
            stack_ptr += 8;
            break;
        }
        case Type::DOUBLE:{
            double arg = *(double*)(stack_ptr -= 8);
            *(__uint64_t*)stack_ptr = (__uint64_t)arg;
            stack_ptr += 8;
            break;
        }
        default:
            rtThrow(Runtime::NotImplemented);
            break;
    }
    *stack_ptr = (byte)Type::UI64;
}
