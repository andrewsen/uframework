#ifndef RUNTIME_H
#define RUNTIME_H

#include "log.h"
#include "module.h"
//#include "blockallocator.h"
#include "jit.h"
#include <sstream>
//#include <cstring>
//#include <stack>
#include <execinfo.h>
//#include <unistd.h>
#include <unordered_map>

#define rtThrow(...) rtThrowEx(__FILE__, __LINE__, __VA_ARGS__)

/*    Internal API    */
rtinternal findInternal(Runtime *rt, Function* f);
void* findJITInternal(Runtime *rt, Function* f);

void internalPrint(Runtime* rt, Function *f, byte* fargs);
void internalPrintHex(Runtime* rt, Function *f, byte* fargs);
void internalRead(Runtime* rt, Function *f, byte* fargs);
void internalExit(Runtime* rt, Function *f, byte* fargs);
void internalSqrt(Runtime* rt, Function *f, byte* fargs);
void internalFabs(Runtime* rt, Function *f, byte* fargs);
void internalCount(Runtime* rt, Function *f, byte* fargs);
void internalTerminate(Runtime* rt, Function *f, byte* fargs);
void internalSplitToChars(Runtime* rt, Function *f, byte* fargs);
void internalDebug(Runtime* rt, Function *f, byte* fargs);
void internalRaise(Runtime* rt, Function* f, byte* fargs);
void internalStartGuard(Runtime* rt, Function* f, byte* fargs);
void internalEndGuard(Runtime* rt, Function* f, byte* fargs);
void internalPow(Runtime* rt, Function *f, byte* fargs);
void internalPi(Runtime* rt, Function *f, byte* fargs);
void internalExp(Runtime* rt, Function *f, byte* fargs);
void internalRand(Runtime* rt, Function *f, byte* fargs);
void internalPrintStackTrace(Runtime* rt, Function *f, byte* fargs);
void internalCrash(Runtime* rt, Function *f, byte* fargs);

struct EventResult
{
    Type type;
    byte* value;
};

struct DebugBundle
{
    Runtime* rt;
    Function* currentFun;
    byte* fargs;
    byte* localTable;
    OpCode* code;

    DebugBundle(Runtime* rt, Function* currentFun, byte* fargs, byte* localTable, OpCode* code)
        : rt(rt), currentFun(currentFun), fargs(fargs), localTable(localTable), code(code)
    {

    }
};

class Runtime
{
public:

    /* PUBLIC CONSTATNTS */
    const static uint ARRAY_METADATA_SIZE = 5;
    const static uint MAX_ARRAY_SIZE = 0x80000000;
    const static int _EXIT_FAILURE = EXIT_FAILURE;
    const static int _EXIT_SUCCESS = EXIT_SUCCESS;
    /* PUBLIC ENUMS AND CLASES */
    enum RtExType {
        StackOverflow,
        GlobalMemOverflow,
        NotImplemented,
        MissingGlobalConstructor,
        CantExecute,
        IllegalOperation,
        AllocationError,
        InternalFunctionMissing,
        MetaIncorrectType,
        OutOfRange,
        IllegalType,
        FloatingPointException,
        OldVersion,
        InvalidModule,
        StackCorrupted,
        UnknownCommandArg,
        RuntimeInstanceException,
        JITException
    };

    /* PUBLIC TYPEDEFS */
    typedef void (*handler)(Runtime* rt, RtExType exception);
    typedef void (*debugger)(const DebugBundle& db);
    typedef int (*intJitMain0)();
    typedef void (*voidJitMain0)();
    //typedef int (*intJitMainArgs)();
    //typedef void (*voidJitMainArgs)();

    enum RtState : int {
        Interrupted = 0x1,
        Unknown = 0x2,
        Constructed = 0x4,
        Created = 0x8,
        Loaded = 0x10,
        Started = 0x20,
        Finished = 0x40,
        Unloaded = 0x80,
        Crashed = 0x100,
    };

    struct StackFrame
    {
        Function* func_ptr;
        byte* loc_ptr;
        byte* arg_ptr;
    };

    class MemoryManager
    {
        byte * memory_allocated;
        byte * memory_l1;
        byte * memory_trash;
        byte * mem_l1_ptr;
        byte * mem_trash_ptr;
        Runtime* rt;
    public:
#ifdef FW_DEBUG
        uint alloc_count = 0, gc_count = 0;
#endif
        MemoryManager();
        void SetRuntime(Runtime *rt);
        void Init();

        byte* Allocate(byte* type);
        byte* Allocate(Type type);
        byte* Allocate(uint size);
        byte* Allocate(Type type, uint size);
        byte* AllocateArray(Type type, uint count);
        byte* AllocateString(const char* str);

        void ArraySet(byte* arr, int idx, byte *value);

        void MinorClean();
        void MajorClean();
        byte *MoveArray(byte *addr);
        byte *MoveString(byte* ptr);
        void Free();

        friend void internalDebug(Runtime* rt, Function *f, byte* fargs);

#ifdef GC_DEBUG
        enum GCActionType
        {
            ALLOC =              0b0,
            XCHANGE_HEAPS =      0b1,
            MINOR_CLEAN =       0b10,
            WIPE_H1 =          0b100,
            WIPE_H2 =         0b1000,
            INIT =           0b10000,
            HSIZE_CHANGED = 0b100000
        };

        enum GCHeapType
        {
            H1, H2
        };

        enum GCFrameType
        {
            USED,
            FREE,
            MOVED,
        };

        struct GCPoint
        {
            GCActionType action = ALLOC;
            uint start;
            int size;
            uint heapsize;
            GCFrameType frame_type = USED;
            GCHeapType heap_type = H1;

            GCPoint()
            {

            }

            GCPoint(GCActionType act, uint start, int size, GCFrameType ftype)
            {
                this->action = act;
                this->start = start;
                this->size = size;
                this->frame_type = ftype;
            }

            GCPoint(GCActionType act, uint start, int size, GCFrameType ftype, GCHeapType htype)
            {
                this->action = act;
                this->start = start;
                this->size = size;
                this->frame_type = ftype;
                this->heap_type = htype;
            }
        };

        //typedef void (*gc_action)(GCActionType type);
        typedef void (*gc_tracer)(const GCPoint &point);

        void SetTracer(gc_tracer tracer)
        {
            this->tracer = tracer;
        }
        //void SetAction(gc_action action)
        //{
        //    this->action = action;
        //}
        byte* GetH1Pointer()
        {
            return memory_l1;
        }
        byte* GetH2Pointer()
        {
            return memory_trash;
        }
        uint GetHeapSize()
        {
            return rt->current_mem_size;
        }
        bool isGCDebugEnabled() const
        {
            return gc_debug_enabled;
        }
        void setGCDebugEnabled(bool value)
        {
            gc_debug_enabled = value;
        }

    private:
        bool gc_debug_enabled = false;
        gc_tracer tracer = nullptr;
        //gc_action action = nullptr;
#endif
    };

    class EventHandlers
    {
        friend class Runtime;

        unordered_map<int, vector<handler>> events;
        Runtime *rt;

        EventHandlers(Runtime* rt)
        {
            this->rt = rt;
        }

    public:
        void Add(RtExType ex, handler h)
        {
            vector<handler>& vec = events[ex];
            if(vec.empty())
                vec.push_back(h);
            else
            {
                for(auto hand: vec)
                    if(hand == h)
                        return;
            }
            vec.push_back(h);
        }

        void Remove(RtExType ex)
        {
            events.erase(ex);
        }

        void Remove(RtExType ex, handler h)
        {
            auto& vec = events[ex];
            if(vec.empty())
                return;
            for(auto iter = vec.begin(); iter != vec.end(); iter++)
            {
                if(*iter == h)
                {
                    vec.erase(iter);
                    return;
                }
            }
        }
    private:
        void invoke(RtExType ex)
        {
            vector<handler>& vec = events[ex];
            if(!vec.empty())
                for(handler h: vec)
                    h(nullptr, ex);
        }
    };

    struct
    {
        bool jitEnabled = true;

        bool useGCCMul = true;
        bool useSSE = true;
        bool useLibGCC = true;

        void* udivdi3Ptr = nullptr;
        void* divdi3Ptr = nullptr;
    } jitConfig;

private:
    /* PRIVATE FIELDS */
    byte * program_stack;

    byte * stack_ptr;

    byte * global_var_mem;
    byte * globals_ptr;

    char** managed_argv;

    OpCode* current_cptr = nullptr;

    uint managed_argc;

    uint current_mem_size = 0x400; //0x40000; // 1Mb
    uint mem_chunk_size = 0x1000; //0x1000 // 1Mb
    uint max_mem_size = 0x80000000; //2Gb

    uint current_stack_size = 0x4000; // 256Kb
    uint max_stack_size = 0x800000; //8Mb

    uint global_size =  0x4000;
    uint global_max_size =  0x100000;

    string file;
    Module main_module;
    //JIT jit;
    Function nativeMain;

    vector<Module*> imported;
    vector<Function*> function_list;
    vector<StackFrame> callstack;

    debugger dbg_handler;

    int returnCode = 0;
    int exitStatus = _EXIT_SUCCESS;
    bool hasReturnCode = false;

    RtState state = Unknown;

    MemoryManager memoryManager;

    static Runtime* Instance;

    /* PRIVATE STRUCTS  */
    struct DebugOpts
    {
        bool debugMode = false;
        //bool printCurrentOpcode = true;
        bool continueNext = false;
        int skipIterations = 0;
        int skipOpcodes = 1;
        OpCode* currentOp = nullptr;
        string breakAt = "";
    } debugOpts;

public:
    /* PUBLIC FIELDS */
    EventHandlers Handlers = this;
    Log log;

    /* FRIEND CLASSES */
    friend class Module;
    //friend class JIT;

    /* FRIEND FUNCTIONS */
    friend void internalPrint(Runtime* rt, Function *f, byte* fargs);
    friend void internalPrintHex(Runtime* rt, Function *f, byte* fargs);
    friend void internalRead(Runtime* rt, Function *f, byte* fargs);
    friend void internalExit(Runtime* rt, Function *f, byte* fargs);
    friend void internalSqrt(Runtime* rt, Function *f, byte* fargs);
    friend void internalFabs(Runtime* rt, Function *f, byte* fargs);
    friend void internalCount(Runtime* rt, Function *f, byte* fargs);
    friend void internalTerminate(Runtime* rt, Function *f, byte* fargs);
    friend void internalSplitToChars(Runtime* rt, Function *f, byte* fargs);
    friend void internalDebug(Runtime* rt, Function *f, byte* fargs);
    friend void internalRaise(Runtime* rt, Function* f, byte* fargs);
    friend void internalStartGuard(Runtime* rt, Function* f, byte* fargs);
    friend void internalEndGuard(Runtime* rt, Function* f, byte* fargs);
    friend void internalPow(Runtime* rt, Function *f, byte* fargs);
    friend void internalPi(Runtime* rt, Function *f, byte* fargs);
    friend void internalExp(Runtime* rt, Function *f, byte* fargs);
    friend void internalRand(Runtime* rt, Function *f, byte* fargs);
    friend void internalPrintStackTrace(Runtime* rt, Function *f, byte* fargs);
    friend void internalCrash(Runtime* rt, Function *f, byte* fargs);
    friend rtinternal findInternal(Runtime *rt, Function* f);
    friend void* findJITInternal(Runtime *rt, Function* f);

    friend byte* __cdecl jitGCAllocStr(byte* ptr, uint len);
    friend byte* __cdecl jitGCArrayAllocHelper(uint type, uint count);

    friend byte* __cdecl jit_read_string();


    /**************************
     *   PUBLIC RUNTIME API   *
     **************************/

    /* PUBLIC CONSTRUCTORS */
    Runtime();

    /* PUBLIC DESTRUCTORS */
    virtual ~Runtime();

    /* GETTERS AND SETTERS */
    MemoryManager* GetMemoryManager()
    {
        return &memoryManager;
    }
    uint GetMaxHeapSize() const
    {
        return max_mem_size;
    }
    void SetMaxHeapSize(const uint& val)
    {
        max_mem_size = val;
    }

    uint GetMaxStackSize() const
    {
        return max_stack_size;
    }
    void SetMaxStackSize(const uint& val)
    {
        max_stack_size = val;
    }

    bool IsLoggingEnabled() const
    {
        ///STUB
        return false;
    }
    bool SetLoggingEnabled(const bool& val) //Checked
    {
        ///STUB
        return val;
    }

    debugger GetDebugger() const
    {
        return dbg_handler;
    }
    void SetDebugger(debugger dbg)
    {
        dbg_handler = dbg;
    }

    bool IsInDebugMode() const
    {
        return debugOpts.debugMode;
    }
    void EnterDebugMode()
    {
        debugOpts.debugMode = true;
    }

    int GetExitStatus() const
    {
        return exitStatus;
    }

    RtState GetState() const
    {
        return state;
    }

    string GetStackTrace();
    string GetManagedStackTrace();

    bool HasReturnCode() const
    {
        return this->hasReturnCode;
    }
    int GetReturnCode() const
    {
        return this->returnCode;
    }

    /* ROUTINE METHODS */
    void Create();
    void Create(string file);
    void ParseCommandLine(int argc, char **argv);
    void Load();
    void StartMain();
    void Start(uint entry);
    void Unload();

    /* FUNCTION SEARCH METHODS */
    uint FindFunctionId(const char* name) const;
    uint FindFunctionId(const char* name, RTType args[], uint count) const;

    /* EXCEPTION END ERROR PROVIDING METHODS */
    void RaiseEvent(RtExType event);
    void ThrowAndDie(RtExType ex);
    void Crash();

    /* DATA TYPES INFORMATION METHODS */
    inline static uint Sizeof(byte* tptr) {
        Type t = (Type)*tptr;
        switch (t) {
            case Type::BOOL:
            case Type::UI8:
                return 1;
            case Type::I16:
                return 2;
            case Type::I32:
            case Type::UI32:
            case Type::UTF8:
            case Type::CHAR:
            case Type::ARRAY:
            case Type::CLASS:
                return 4;
            case Type::DOUBLE:
            case Type::I64:
            case Type::UI64:
                return 8;
            default:
                return 0;
        }
    }
    inline static uint Sizeof(Type t) {
        switch (t) {
            case Type::BOOL:
            case Type::UI8:
                return 1;
            case Type::I16:
                return 2;
            case Type::I32:
            case Type::UI32:
            case Type::UTF8:
            case Type::ARRAY:
            case Type::CHAR:
            case Type::CLASS:
                return 4;
            case Type::DOUBLE:
            case Type::I64:
            case Type::UI64:
                return 8;
            default:
                if((byte)t & (byte)Type::REF)
                    return 4;
                else
                    return 0;
        }
    }

    inline static uint SizeOnStack(Type t) {
        switch (t) {
            case Type::BOOL:
            case Type::UI8:
            case Type::I16:
            case Type::I32:
            case Type::UI32:
            case Type::UTF8:
            case Type::ARRAY:
            case Type::CHAR:
            case Type::CLASS:
                return 4;
            case Type::DOUBLE:
            case Type::I64:
            case Type::UI64:
                return 8;
            default:
                if((byte)t & (byte)Type::REF)
                    return 4;
                else
                    return 0;
        }
    }

    static const char *OpcodeToStr(OpCode op);

    /* DEBUG API */
    void DebugSkipOpcodes(int count)
    {
        debugOpts.skipOpcodes = count;
        if(debugOpts.skipOpcodes < 1)
            debugOpts.skipOpcodes = 1;
    }
    void DebugSkipIterations(int count)
    {
        debugOpts.skipIterations = count;
        if(debugOpts.skipIterations < 1)
            debugOpts.skipIterations = 1;
    }
    void DebugSetBreakAt(string fun)
    {
        debugOpts.breakAt = fun;
    }
    void DebugContinue()
    {
        debugOpts.continueNext = true;
    }
    const vector<StackFrame> DebugGetCallstack() const
    {
        return callstack;
    }
    byte* DebugGetStackPtr() const
    {
        return stack_ptr;
    }


private:
    /* PRIVATE METHODS */
    void stackalloc(uint need);
    void printStackTrace();
    void printManagedStackTrace();
    void allocGlobalMem();

    void rtThrowEx(const char* file, int line, RtExType t, string message = "");
    //void rtThrow(RtExType t, string message = "");
    string typeToStr(byte *t) ;
    static string typeToStr(Type t);
    void execFunction(Function *f, byte *fargs, byte *local_table);
    void dump_stack();
    void dump_memory();

    byte *jitCompile(Function* f);
    //inline JCompType jitGetLastPop2(JCompType ts[MAX_STACK_COUNT]);
    //inline JCompType jitGetLastPop(JCompType ts[MAX_STACK_COUNT]);
    inline void jitLdfld(Function *f, uint idx, vector<byte> &x86code, JITTypeStack &ts);
    inline void jitStfld(Function *f, uint idx, vector<byte> &x86code, JITTypeStack &ts);
    inline void jitLdelem(JITTypeStack &ts, vector<byte> &x86code);
    inline void jitStelem(JITTypeStack &types, vector<byte> &x86code);
    inline void jitGenerateEnter(Function *f, vector<byte>& x86asm);
    inline void jitGenerateLeave(Function *f, vector<byte>& x86asm);
    inline void jitPushImm32(uint imm, vector<byte>& x86asm);
    inline void* jitFindLibGCCHelper(const char* sign);

    inline void ld_aref();
    inline void ld_byref();
    inline void ldarg(uint idx, Function *f, byte *fargs);
    inline void ldfld(uint idx, Function *f);
    inline void ldloc(uint idx, Function *f, byte* locals);
    inline void ldelem();

    inline void st_byref();
    inline void starg(uint idx, Function *f, byte* fargs);
    inline void stfld(uint idx, Function *f);
    inline void stloc(uint idx, Function *f, byte *locals);
    inline void stelem();
    inline void conv_ui8();
    inline void conv_i16();
    inline void conv_chr();
    inline void conv_i32();
    inline void conv_ui32();
    inline void conv_i64();
    inline void conv_ui64();
    //void jitTypeStackPush(JCompType ts[], int &idx, JCompType &&val);
    //JCompType jitTypeStackPop(JCompType ts[], int &idx);
    //JCompType jitTypeStackLast(JCompType ts[], int &idx);
    //JCompType jitTypeStackPop2(JCompType ts[], int &idx);
    void resolveJumps(byte *code, const vector<uint> &jump_table, const unordered_map<uint, uint> &label_table);
};

//#define SETSTATE(s) ((state) = (RtState((state) | (s))))

#endif // RUNTIME_H
