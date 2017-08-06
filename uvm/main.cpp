/*
         ___________________________________________________________________________________________________________________________
        /  ______   __________    _____         _______             __      ___     ___      ____         _______      ______      /
       /  /  ___/  /___   ___/   /     |       /  _    /           |  |    /   |   /  /    /     |       /  _    /    /  ___/     /
      /  /  /         /  /      /  /|  |      /  /_/  /            |  |   /    |  /  /    /  /|  |      /  /_/  /    /  /        /
     /   \  \        /  /      /  /_|  |     /     __/             |  |  /  |  | /  /    /  /_|  |     /     __/     \  \       /
    /     \  \      /  /      /  /__|  |    /  /\  \               |  | /  /|  |/  /    /  /__|  |    /  /\  \        \  \     /
   /   ___/  /     /  /      /  /   |  |   /  /  \  \              |  |/  / |     /    /  /   |  |   /  /  \  \    ___/  /    /
  /   /_____/     /__/      /__/    |__|  /__/    \__\             |_____/  |____/    /__/    |__|  /__/    \__\  /_____/    /
 /__________________________________________________________________________________________________________________________/

                                                                                               __
                                                                                              /\ \
                                                                    ________--------____      \*\ \
                                                    ________--------                    ---___ \$\=\
                                    ________--------                         -=               --\*\ \_
                    ________--------                  -=                                       //\$\=\---____
    ________--------          -=                                                              ////\*\ \      ---
   <==========================================================================================/////\$\=\=========
    ----____..................................................................................//////\*\_\......_> **
            ----____.............-=..................................................................\/_/...._-
                    ----..................................................................................._-_] **
                            ----____.....................-=.............................................._-___] **
                                    ----____..........................................................._-
                                            ----____.............................-=.................._-
                                                    ----____......................................._-_] **
                                                            ----____............................._-___] **
                                                                    ----____.................. _-
                                                                            ----____ ........_-
                                                                                    ----____-

                                                                                                           STAR DESTROYER
*/
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include <semaphore.h>
#include "configreader.h"
#include "../libuvm/runtime.h"


#define GC_PIPE_NAME "/tmp/uvm_gc_pipe"
//#define SEM_NAME "/uvm_gc_semaphore"
#define EQ(a, b) (!(strcmp((a),(b))))

Runtime rt;
bool printCurrentOpcode = true;

#ifdef GC_DEBUG
    Runtime::MemoryManager *mman;
    int gc_fifo = -1;
    void initFifo();
    void unlinkFifo();
    void gc_trace_handler(const Runtime::MemoryManager::GCPoint &frame);
#endif

void setsighandlers();
void handleDebug(const DebugBundle &dbg);
bool debugRoutine(const string &comstr, Runtime *rt, Function *f, byte* fargs, byte* local_table, OpCode* code);
vector<string> split(const string& str, char brk);


int main(int argc, char* argv[])
{
    if(argc == 1)
    {
        cout << "Usage: " << argv[0] << " <module.sem>\n";
        return EXIT_FAILURE;
    }

    setsighandlers();

    try {
        rt.ParseCommandLine(--argc, ++argv);
#ifdef GC_DEBUG
        mman = rt.GetMemoryManager();
        if(mman->isGCDebugEnabled())
        {
            initFifo();
            mman->SetTracer(&gc_trace_handler);
        }
#else

#endif


        rt.Create();
        rt.SetDebugger(&handleDebug);
        rt.Load();
        rt.StartMain();
        rt.Unload();
#ifdef GC_DEBUG
        if(gc_fifo != -1)
            unlinkFifo();
#endif

        if(rt.HasReturnCode())
            return rt.GetReturnCode();
    }
    catch(int e)
    {
        switch(e)
        {
            case Runtime::_EXIT_FAILURE:
                return EXIT_FAILURE;
        }
    }
    catch(Module::InvalidMagicException ime) {
        cerr << ime.What() << endl << hex << ime.GetMagic() << dec << endl;
        cerr << "In file: " << argv[1] << endl;
        return EXIT_FAILURE;
    }
    catch(JITException jite) {
        cerr << jite.What() << endl;
        cerr << "In file: " << argv[1] << endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void configure(ConfigLine line)
{
    //if(EQ(line.key, "heap"))
    //{
        //for(int i = 0)
    //}
}

void setsighandlers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGABRT);

    sa.sa_sigaction = [](int signal, siginfo_t *si, void *arg)
    {
        //cout << "Segfault occured when trying to access memory at 0x" << hex << (size_t)si->si_addr << dec << endl;
        rt.log.SetType(Log::Error);
        switch(signal)
        {
            case SIGSEGV:
                rt.log << "Segfault occured when trying to access memory at 0x" << hex << (size_t)si->si_addr << dec << "\n";
                if(si->si_code == SEGV_ACCERR)
                    rt.log << "Invalid permissions\n";
                else
                    cerr << "Address not mapped\n";
                if(rt.GetState() & Runtime::Started)
                {
                    rt.Crash();
                }
                break;
            case SIGFPE:
                rt.ThrowAndDie(Runtime::FloatingPointException);
                break;
            case SIGABRT:
                rt.log << "SIGABRT was catched. Program will be terminated\n";
                rt.Crash();
                break;
            case SIGINT:
            {
                /*EventResult er = rt.RaiseEvent(Runtime::Interrupt);
                if(er.type == Type::BOOL)
                {
                    if(*(bool*)er.value)
                        return;
                }*/
                rt.Unload();
            }
                break;
            case SIGTERM:
            {
                /*EventResult er = rt.RaiseEvent(Runtime::Terminate);
                if(er.type == Type::BOOL)
                {
                    if(*(bool*)er.value)
                        return;
                }*/
                rt.Unload();
            }
                break;
            default:
                rt.log << "Unknown signal was catched. Terminating\n";
                raise(SIGTERM);
                break;
        }
        exit(EXIT_FAILURE);
    };


    sa.sa_flags = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

}

#ifdef GC_DEBUG
void initFifo()
{
    unlink(GC_PIPE_NAME);
    //sem_unlink(SEM_NAME);
    //unlink(COMMAND_PIPE_NAME);
    if((mkfifo(GC_PIPE_NAME, 0777)) == -1)
    {
        rt.log << "Can't create gc fifo\n";
    }
    //if((sem = sem_open(SEM_NAME, O_CREAT, 0777, 0)) == SEM_FAILED)
    //{
    //    rt.log << "Can't create semaphore\n";
    //}
    if((gc_fifo = open(GC_PIPE_NAME, O_WRONLY)) == - 1)
    {
        rt.log << "Can't open gc fifo\n";
    }

    /*if((mkfifo(COMMAND_PIPE_NAME, 0777)) == -1)
    {
        rt.log << "Can't create command fifo\n";
    }
    if((gc_fifo = open(COMMAND_PIPE_NAME, O_RDWR)) == - 1)
    {
        rt.log << "Can't open command fifo\n";
    }*/
}

void unlinkFifo()
{
    close(gc_fifo);
    unlink(GC_PIPE_NAME);
    //sem_close(sem);
    //sem_unlink(SEM_NAME);
}

void gc_trace_handler(const Runtime::MemoryManager::GCPoint &frame)
{
    cerr << "sizeof = " << sizeof(Runtime::MemoryManager::GCPoint) << endl;

    truncate(GC_PIPE_NAME, 0);
    write(gc_fifo, &frame, sizeof(Runtime::MemoryManager::GCPoint));
    cerr << "Waiting trace\n";
    raise(SIGSTOP);
    cerr << "Wait trace ended\n";
}
#endif

void handleDebug(const DebugBundle& dbg)
{
    if(printCurrentOpcode)
    {
        cout << "<0x" << hex << (uint)dbg.code << dec << ":" << Runtime::OpcodeToStr(*dbg.code) << "> ";
    }
    bool handle = true;
    while(handle)
    {
        string command;
        getline(cin, command);
        handle = debugRoutine(command, dbg.rt, dbg.currentFun, dbg.fargs, dbg.localTable, dbg.code);
    }
}

bool debugRoutine(const string &comstr, Runtime* rt, Function *f, byte* fargs, byte* local_table, OpCode* code)
{
    auto coms = split(comstr, ' ');
    if(coms.size() == 0)
        return true;
    auto com = coms[0];
    if(com == "st")
    {
        cout << rt->GetStackTrace();
    }
    else if(com == "mst")
    {
        cout << rt->GetManagedStackTrace();
    }
    else if(com == "exit")
    {
        rt->Unload();
        exit(EXIT_SUCCESS);
    }
    else if(com == "prtop")
    {
        printCurrentOpcode = !printCurrentOpcode;
    }
    else if(com == "s")
    {
        if(coms.size() == 2)
        {
            rt->DebugSkipOpcodes(atoi(coms[1].c_str()));
        }
        return false;
    }
    else if(com == "si")
    {
        if(coms.size() == 2)
        {
            rt->DebugSkipIterations(atoi(coms[1].c_str()));
        }
        return false;
    }
    else if(com == "cf")
    {
        cout << "[" << f->module->GetFile() << "] <" << f->sign  << ":0x" << hex << (uint)f->bytecode << ">" << dec << endl;
    }
    else if(com == "cop")
    {
        cout << "<0x" << hex << (uint)code << dec << ":" << rt->OpcodeToStr(*code) << "> ";
    }
    else if(com == "brk")
    {
        if(coms.size() == 2)
        {
            rt->DebugSetBreakAt(coms[1]);
        }
        return true;
    }
    else if(com == "c")
    {
        rt->DebugContinue();
        return false;
    }
    else if(com == "cmod")
    {
        cout << "[" << f->module->GetFile() << "]\n";
    }
    else if(com == "ss")
    {
        auto cs = rt->DebugGetCallstack();
        uint count = ((uint)rt->DebugGetStackPtr()) - (((uint)cs[cs.size()-1].loc_ptr) + f->local_mem_size);
        cout << count;
    }
    else if(com == "stc")
    {
        /*auto cs = rt->DebugGetCallstack();
        byte* stack = rt->DebugGetStackPtr();
        for(Runtime::StackFrame sf : cs)
        {
            for(int idx = 0; idx < sf.func_ptr->locals_size; ++idx)
            {
                LocalVar* lv = &local_table[idx];
                uint sz = Sizeof(lv->type);
                memcpy(++stack_ptr, locals + lv->addr, sz);
                //*(stack_ptr += sz) = (byte)lv->type;
                *(stack_ptr += sz) = *lv->type;
            }
        }*/
    }
    return true;
}

void printValue(Runtime* rt, byte* val, Type t)
{
    switch (t) {
        case Type::UTF8:
            {
                if(*(size_t*)val == 0)
                {
                    cout << "{null}";
                    return;
                }
                char* str = (char*)(*(size_t*)val + rt->ARRAY_METADATA_SIZE);
                cout << str;
            }
            break;
        case Type::I32:
            cout << *(int*)val;
            break;
        case Type::UI32:
            cout << *(uint*)val;
            break;
        case Type::DOUBLE:
            cout << *(double*)val;
            break;
        case Type::CHAR:
            cerr << *(char*)val;
            break;
        default:
            break;
    }
}

vector<string> split(const string &str, char brk)
{
    stringstream ins(str);
    string token;
    vector<std::string> res;

    while(getline(ins, token, brk))
    {
        if(!token.empty())
            res.push_back(token);
    }

    return res;
}
