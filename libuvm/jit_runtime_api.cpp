#include <iomanip>
#include "runtime.h"

#include "jit.h"

using namespace std;

void* findJITInternal(Runtime *rt, Function* f)
{
    if(!strcmp(f->sign, "printHex"))
    {
        return (void*)&jit_print_hex;
    }
    else if(!strcmp(f->sign, "print"))
    {
        auto type = (Type)*f->args[0].type;
        switch (type)
        {
            case Type::UTF8:
                return (void*)&jit_print;
            case Type::I16:
            case Type::I32:
                return (void*)&jit_print_int;
            case Type::UI8:
            case Type::UI32:
                return (void*)&jit_print_uint;
            case Type::CHAR:
                return (void*)&jit_print_char;
            case Type::BOOL:
                return (void*)&jit_print_bool;
            case Type::I64:
                return (void*)&jit_print_long;
            case Type::UI64:
                return (void*)&jit_print_ulong;
        }
    }
    else if(!strcmp(f->sign, "reads"))
    {
        return (void*)&jit_read_string;
    }
    else if(!strcmp(f->sign, "readi"))
    {
        return (void*)&jit_read_int;
    }
    else if(!strcmp(f->sign, "rand"))
    {
        return (void*)&rand;
    }
    else if(!strcmp(f->sign, "readui"))
    {
        return (void*)&jit_read_uint;
    }
    else
        rt->rtThrow(Runtime::InternalFunctionMissing);
}

void jit_print(byte* ptr)
{
    if(ptr == 0)
    {
        cout << "{null}";
        return;
    }
    char* str = (char*)(ptr + Runtime::ARRAY_METADATA_SIZE);
    cout << str;
}

void jit_print_hex(uint ui)
{
    cout << hex << setfill('0') << setw(8) << ui << dec;
}

void jit_print_int(int i)
{
    cout << i;
}

void jit_print_uint(uint ui)
{
    cout << ui;
}

void jit_print_long(__int64_t l)
{
    cout << l;
}

void jit_print_ulong(__uint64_t ul)
{
    cout << ul;
}

void jit_print_char(uint ch)
{
    cout << (char)ch;
}

void jit_print_bool(uint b)
{
    cout << (b == 0 ? "false" : "true");
}

int jit_read_int()
{
    int val;
    cin >> val;
    return val;
}

uint jit_read_uint()
{
    uint val;
    cin >> val;
    return val;
}

byte* jit_read_string()
{
    string val;
    //cin >> val;
    getline(cin, val);

    auto rt = Runtime::Instance;

    return rt->memoryManager.AllocateString(val.c_str());
}
