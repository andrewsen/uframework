#ifndef MODULE_H
#define MODULE_H

#include <string>
#include <vector>
#include <fstream>

#include <iostream>

#include "../common/common.h"
#include "../common/opcodes.h"

using namespace std;

class Runtime;
class Module;
struct Function;

typedef void (*rtinternal)(Runtime* rt, Function*f, byte* args);
//typedef void (*debug_callback)(Runtime*rt, Function *f, byte* args, byte* local_table);
string modinfo(Module* mod);

static const uint SIGN_LENGTH = 80;

struct LocalVar
{
    uint addr;
    byte* type;
    bool inited = false;
};

struct Function
{
    static const uint ARGS_LENGTH = 15;
    static constexpr const char* INIT = "__init__"; // lol

    OpCode* bytecode = nullptr;
    uint bc_size = 0;
    LocalVar* locals = nullptr; //TODO: create local variabels memory
    uint local_mem_size = 0; //TODO: create local variabels memory
    uint locals_size = 0;
    bool isPrivate;
    bool has_jit;
    char sign[SIGN_LENGTH];
    uint argc = 0;
    uint args_size = 0;
    LocalVar args[ARGS_LENGTH];
    byte* ret = nullptr;
    byte flags = 0;
    byte* jit_code = nullptr;

    Module* module;

    rtinternal irep = nullptr;
};

struct Header {
    enum Type : byte {
        Bytecode=1, Strings, Imports, Metadata, Functions, Globals, Types, Properties, User
    } type;
    string name;
    uint begin;
    uint end;
};

struct GlobalVar {
    byte* addr;
    char name[SIGN_LENGTH];
    byte* type;
    bool isPrivate;
    bool inited = false;
};

class Module
{
    friend class Runtime;
    friend class MemoryManager;

    const static uint MOD_MAGIC = 0x4153DEC0;

    ModuleFlags mflags;

    Runtime * rt = nullptr;
    Module * included = nullptr;
    Function ** functions = nullptr;
    GlobalVar * globals = nullptr;
    char * strings = nullptr;
    byte* types = nullptr;

    uint func_count = 0;
    uint globals_count = 0;
    uint included_count = 0;
    uint strings_count = 0;

    vector<Header> headers;

    Function* __global_constructor__ = nullptr;

    string file;

    friend string modinfo(Module* mod);
public:
    enum ModuleEvents : int
    {
        OnLoad = 0x0,
        OnUnload,
        None,
    };

    class InvalidMagicException : public Exception{
        uint magic;

    public:
        InvalidMagicException(uint mg) : magic(mg)
        {
            what = "Invalid module magic";
        }

        uint GetMagic() {return magic;}
    };

    Module();
    Module(Runtime *rt);

    void Load(string file);
    void Unload();
    string GetFile() const;

private:
    Function* moduleEvents[None];

    void readSegmentHeaders(ifstream &ifs, uint hend);
    void readSegments(ifstream &ifs);

    static bool IsValid(uint mg)
    {
        return mg == Module::MOD_MAGIC;
    }

    static bool IsTypesEquals(byte* a1, byte* a2)
    {
        Type t1 = (Type)*a1, t2 = (Type)*a2;
        if(t1 != t2)
            return false;
        if(t1 < Type::ARRAY)
            return true;
        if(t1 == Type::ARRAY)
        {
            uint d1 = *(uint*)(a1+1);
            uint d2 = *(uint*)(a2+1);
            if(d1 != d2)
                return false;
            return *(a1+5) == *(a2+5);      /// TEST            <------------------------
        }
        return false;
    }
};

#endif // MODULE_H
