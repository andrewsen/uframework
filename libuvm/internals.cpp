#include <cmath>
#include <csignal>
#include <iomanip>
#include "runtime.h"

#define S(x) # x

using namespace std;

vector<uint> guards;

#if JIT_LEVEL == JIT_DISABLED
rtinternal findInternal(Runtime *rt, Function* f)
{
    switch (f->sign[0]) {
        case 'c':
            if(!strcmp(f->sign, "count"))
            {
                if(f->argc == 1)
                    return &internalCount;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 'g':
            if(!strcmp(f->sign, "getdebuginfo"))
            {
                if(f->argc == 1)
                    return &internalDebug;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 'e':
            if(!strcmp(f->sign, "exit"))
            {
                if(f->argc == 0 || (f->argc == 1 && (Type)*f->args[f->argc-1].type == Type::I32))
                    return &internalExit;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "exp"))
            {
                if(f->argc == 0 || (f->argc == 1 && (Type)*f->args[f->argc-1].type == Type::DOUBLE))
                    return &internalExp;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "endGuard"))
            {
                if(f->argc == 0)
                    return &internalEndGuard;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 'p': //P for PRINT
            if(!strcmp(f->sign, "print"))
            {
                Type t = (Type)*f->args[f->argc-1].type;
                if(t == Type::DOUBLE || t == Type::I32 || t == Type::UI32
                        || t == Type::UTF8 || t == Type::CHAR)
                    return &internalPrint;
                else
                    rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "printStackTrace") && f->argc == 0)
            {
                return &internalPrintStackTrace;
            }
            if(!strcmp(f->sign, "pow") && f->argc == 2)
            {
                Type t1 = (Type)*f->args[0].type;
                Type t2 = (Type)*f->args[1].type;
                if(t1 == Type::DOUBLE && t2 == Type::I32)
                    return &internalPow;
                else
                    rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            break;
        case 'P':
            if(!strcmp(f->sign, "Pi") && f->argc == 0)
            {
                return &internalPi;
            }
            rt->rtThrow(Runtime::InternalFunctionMissing);
        case 'r': //R for READ
            if(strstr(f->sign, "read") && f->argc == 0) /// @badcode FIXME
                return &internalRead;
            else if(strstr(f->sign, "raise") && f->argc == 1)
                return &internalRaise;            
            else if(strstr(f->sign, "rand") && f->argc == 0)
                return &internalRand;
            else
                rt->rtThrow(Runtime::InternalFunctionMissing);
        case 't':
            if(!strcmp(f->sign, "terminate"))
            {
                if(f->argc == 0)
                    return &internalTerminate;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 's':
            if(!strcmp(f->sign, "sqrt"))
            {
                if(f->argc == 1)
                    return &internalSqrt;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "splitToChars"))
            {
                if(f->argc == 1)
                    return &internalSplitToChars;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "startGuard"))
            {
                if(f->argc == 0)
                    return &internalStartGuard;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        default:
            rt->rtThrow(Runtime::InternalFunctionMissing);
            break;
    }
    return nullptr;
}
#else
rtinternal findInternal(Runtime *rt, Function* f)
{
    switch (f->sign[0]) {
        case 'c':
            if(!strcmp(f->sign, "count"))
            {
                if(f->argc == 1)
                    return &internalCount;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 'g':
            if(!strcmp(f->sign, "getdebuginfo"))
            {
                if(f->argc == 1)
                    return &internalDebug;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 'f':
            if(!strcmp(f->sign, "fabs"))
            {
                if(f->argc == 1)
                    return &internalFabs;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 'e':
            if(!strcmp(f->sign, "exit"))
            {
                if(f->argc == 0 || (f->argc == 1 && (Type)*f->args[f->argc-1].type == Type::I32))
                    return &internalExit;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "exp"))
            {
                if(f->argc == 0 || (f->argc == 1 && (Type)*f->args[f->argc-1].type == Type::DOUBLE))
                    return &internalExp;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "endGuard"))
            {
                if(f->argc == 0)
                    return &internalEndGuard;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 'p': //P for PRINT
            if(!strcmp(f->sign, "printHex"))
            {
                Type t = (Type)*f->args[f->argc-1].type;
                if(t == Type::I32 || t == Type::UI32 || t == Type::CHAR)
                    return &internalPrintHex;
                else
                    rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "print"))
            {
                Type t = (Type)*f->args[f->argc-1].type;
                if(t == Type::DOUBLE || t == Type::I32 || t == Type::UI32
                        || t == Type::UTF8 || t == Type::CHAR)
                    return &internalPrint;
                else
                    rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "printStackTrace") && f->argc == 0)
            {
                return &internalPrintStackTrace;
            }
            if(!strcmp(f->sign, "pow") && f->argc == 2)
            {
                Type t1 = (Type)*f->args[0].type;
                Type t2 = (Type)*f->args[1].type;
                if(t1 == Type::DOUBLE && t2 == Type::I32)
                    return &internalPow;
                else
                    rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            break;
        case 'P':
            if(!strcmp(f->sign, "Pi") && f->argc == 0)
            {
                return &internalPi;
            }
            rt->rtThrow(Runtime::InternalFunctionMissing);
        case 'r': //R for READ
            if(strstr(f->sign, "read") && f->argc == 0) /// @badcode FIXME
                return &internalRead;
            else if(strstr(f->sign, "raise") && f->argc == 1)
                return &internalRaise;
            else if(strstr(f->sign, "rand") && f->argc == 0)
                return &internalRand;
            else
                rt->rtThrow(Runtime::InternalFunctionMissing);
        case 't':
            if(!strcmp(f->sign, "terminate"))
            {
                if(f->argc == 0)
                    return &internalTerminate;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        case 's':
            if(!strcmp(f->sign, "sqrt"))
            {
                if(f->argc == 1)
                    return &internalSqrt;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "splitToChars"))
            {
                if(f->argc == 1)
                    return &internalSplitToChars;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
            if(!strcmp(f->sign, "startGuard"))
            {
                if(f->argc == 0)
                    return &internalStartGuard;
                rt->rtThrow(Runtime::InternalFunctionMissing);
            }
        default:
            rt->rtThrow(Runtime::InternalFunctionMissing);
            break;
    }
    return nullptr;
}
#endif

void internalPrint(Runtime* rt, Function *f, byte* fargs)
{
    Type arg = (Type)*f->args[f->argc-1].type;
    switch (arg) {
        case Type::UTF8:
            {
                if(*(size_t*)fargs == 0)
                {
                    cout << "{null}";
                    return;
                }
                char* str = (char*)(*(size_t*)fargs + rt->ARRAY_METADATA_SIZE);
                cout << str;
            }
            break;
        case Type::I32:
            cout << *(int*)fargs;
            break;
        case Type::UI32:
            cout << *(uint*)fargs;
            break;
        case Type::DOUBLE:
            cout << *(double*)fargs;
            break;
        case Type::CHAR:
            cout << *(char*)fargs;
            break;
        default:
            break;
    }
}

void internalPrintHex(Runtime* rt, Function *f, byte* fargs)
{
    Type arg = (Type)*f->args[f->argc-1].type;
    switch (arg) {
        case Type::I32:
            cout << hex << setfill('0') << setw(8) << *(int*)fargs << dec;
            break;
        case Type::UI32:
            cout << hex << setfill('0') << setw(8) << *(uint*)fargs << dec;
            break;
        case Type::CHAR:
            cout << hex << setfill('0') << setw(8) << *(char*)fargs << dec;
            break;
        default:
            break;
    }
}

[[noreturn]]
void internalExit(Runtime* rt, Function *f, byte* fargs)
{
    if(f->argc == 0)
    {
        rt->Unload();
        rt->returnCode = 0;
    }
    else
    {
        rt->returnCode = *(int*)fargs;
        rt->Unload();
    }
    exit(rt->GetReturnCode());
}

[[noreturn]]
void internalTerminate(Runtime *rt, Function *f, byte *fargs) //Checked
{
    abort();
    //terminate();
}

void internalRaise(Runtime* rt, Function* f, byte* fargs) //Checked
{
    string sig = (char*)(*(size_t*)fargs + rt->ARRAY_METADATA_SIZE);
    if(sig == "SIGSEGV")
        raise(SIGSEGV);
    else if(sig == "SIGKILL")
        raise(SIGKILL);
    else if(sig == "SIGTERM")
        raise(SIGTERM);
    else if(sig == "SIGABRT")
        raise(SIGABRT);

    return;
}

void internalRead(Runtime* rt, Function *f, byte* fargs) //Checked
{
    //allocMem(len);

    //strcpy((char*)mem_first_free, ptr);

    //stackalloc(5);
    Type ret = (Type)*f->ret;
    switch (ret) {
        case Type::UTF8:
        {
            //*(size_t*)++rt->stack_ptr = (size_t)(char*)rt->mem_first_free;

            char buf[1024];

            cin >> buf;//(char*)(rt->mem_first_free + rt->ARRAY_METADATA_SIZE);
            uint len = strlen(buf)+1;
            byte* addr = rt->memoryManager.Allocate(len + rt->ARRAY_METADATA_SIZE);
            *addr = (byte)Type::UTF8;
            *(uint*)(addr+1) = len;
            *(size_t*)++rt->stack_ptr = (uint)addr;
            strcpy((char*)(addr + rt->ARRAY_METADATA_SIZE), buf);

            //rt->mem_first_free += strlen((char*)rt->mem_first_free) + 1 + rt->ARRAY_METADATA_SIZE;

            rt->stack_ptr += 4;
            *rt->stack_ptr = (byte)Type::UTF8;
        }
            break;
        case Type::I32:
            cin >> *(int*)++rt->stack_ptr;

            rt->stack_ptr += 4;
            *rt->stack_ptr = (byte)Type::I32;
            break;
        case Type::UI32:
            cin >> *(uint*)++rt->stack_ptr;

            rt->stack_ptr += 4;
            *rt->stack_ptr = (byte)Type::UI32;
            break;
        case Type::CHAR:
            cin >> *(char*)++rt->stack_ptr;

            rt->stack_ptr += 4;
            *rt->stack_ptr = (byte)Type::CHAR;
            break;
        case Type::DOUBLE:
            cin >> *(double*)++rt->stack_ptr;

            rt->stack_ptr += 8;
            *rt->stack_ptr = (byte)Type::DOUBLE;
            break;
        default:
            break;
    }
}

void internalSqrt(Runtime* rt, Function *f, byte* fargs) //Checked
{
    double d = *(double*)fargs;

    *(double*)++rt->stack_ptr = sqrt(d);

    rt->stack_ptr += 8;
    *rt->stack_ptr = (byte)Type::DOUBLE;
}


void internalFabs(Runtime* rt, Function *f, byte* fargs) //Checked
{
    double d = *(double*)fargs;

    *(double*)++rt->stack_ptr = fabs(d);

    rt->stack_ptr += 8;
    *rt->stack_ptr = (byte)Type::DOUBLE;
}

void internalCount(Runtime* rt, Function *f, byte* fargs) //Checked
{
    byte* addr = (byte*)*(uint*)fargs;

    *(uint*)++rt->stack_ptr = *(uint*)(addr+1);

    rt->stack_ptr += 4;
    *rt->stack_ptr = (byte)Type::UI32;
}

void internalDebug(Runtime* rt, Function *f, byte* fargs) //Checked
{
    rt->log.SetType(Log::Info);
    char* key = (char*)(*(size_t*)fargs + rt->ARRAY_METADATA_SIZE);
    if(!strcmp(key, "all"))
    {
        rt->log << "Toolchain version: " << TOOLCHAIN_VERSION << "\n";
        rt->log << "Executable name: " << program_invocation_name << "\n";
        rt->log << "Internal structs:\n"
             << "\t" << S(Function) << ": " << sizeof(Function) << " bytes\n"
             << "\t" << S(OpCode) << ": " << sizeof(OpCode) << " bytes\n"
             << "\t" << S(Log) << ": " << sizeof(Log) << " bytes\n"
             << "\t" << S(Runtime) << ": " << sizeof(Runtime) << " bytes\n"
             << "\t" << S(Runtime::MemoryManager) << ": " << sizeof(Runtime::MemoryManager) << " bytes\n"
             << "\t" << S(Type) << ": " << sizeof(Type) << " bytes\n"
             << "\t" << S(LocalVar) << ": " << sizeof(LocalVar) << " bytes\n"
             << "\t" << S(GlobalVar) << ": " << sizeof(GlobalVar) << " bytes\n"
             << "\t" << S(Header) << ": " << sizeof(Header) << " bytes\n"
             << "\t" << S(Metadata) << ": " << sizeof(Metadata) << " bytes\n";
        rt->log << "Memory:\n"
             << "\tHeap used: " << (uint)(rt->memoryManager.mem_l1_ptr - rt->memoryManager.memory_l1) << "\n"
             << "\tStack used: " << (uint)(rt->stack_ptr - rt->program_stack) << "\n"
#ifdef FW_DEBUG
             << "\tAlloc counts: " << rt->memoryManager.alloc_count << ", gc count: " << rt->memoryManager.gc_count << "\n"
#endif
             ;
        rt->printStackTrace();
        rt->log << "Loaded modules:\n";
        rt->log << "Entry module:\n" << modinfo(&rt->main_module);
        rt->log << "Loaded modules:\n";
        for(Module* mod : rt->imported)
        {
            rt->log << modinfo(mod);
        }
    }
}

string modinfo(Module* mod)
{
    string res = "";
    res += mod->file + "\n"
            + "Headers:\n";
    for(Header& h : mod->headers)
    {
        res += "\t" + h.name + "\n";
    }

    return res;
}

void internalSplitToChars(Runtime* rt, Function *f, byte* fargs) //Checked
{
    byte* addr = (byte*)*(uint*)fargs;
    uint size = *(uint*)(addr+1) + rt->ARRAY_METADATA_SIZE-1;

    byte* array = rt->memoryManager.Allocate(size);

    memcpy(array, addr, size);

    *array = (byte)Type::CHAR;
    *(uint*)++rt->stack_ptr = (uint)array;
    rt->stack_ptr += 4;
    *rt->stack_ptr = (byte)Type::ARRAY;

}

void internalStartGuard(Runtime* rt, Function *f, byte* fargs) //Checked
{
    guards.push_back((uint)rt->stack_ptr);
}

void internalEndGuard(Runtime* rt, Function *f, byte* fargs) //Checked
{
    rt->log << "Guard #" << (guards.size()-1) << ": " << (uint)(rt->stack_ptr - guards[guards.size()-1]) << "\n";
    guards.pop_back();
}

void internalExp(Runtime* rt, Function *f, byte* fargs) //Checked
{
    double d = *(double*)fargs;

    *(double*)++rt->stack_ptr = exp(d);

    rt->stack_ptr += 8;
    *rt->stack_ptr = (byte)Type::DOUBLE;
}

void internalPi(Runtime* rt, Function *f, byte* fargs) //Checked
{
    *(double*)++rt->stack_ptr = M_PI;

    rt->stack_ptr += 8;
    *rt->stack_ptr = (byte)Type::DOUBLE;
}

void internalRand(Runtime* rt, Function *f, byte* fargs) //Checked
{
    *(int*)++rt->stack_ptr = rand();

    rt->stack_ptr += 4;
    *rt->stack_ptr = (byte)Type::I32;
}

void internalPow(Runtime* rt, Function *f, byte* fargs) //Checked
{
    double d = *(double*)fargs;
    int p = *(double*)(fargs + 9);

    *(double*)++rt->stack_ptr = pow(d, p);

    rt->stack_ptr += 8;
    *rt->stack_ptr = (byte)Type::DOUBLE;
}

void internalPrintStackTrace(Runtime* rt, Function *f, byte* fargs)
{
    rt->printStackTrace();
}

void internalCrash(Runtime* rt, Function *f, byte* fargs)
{
    rt->Crash();
}
