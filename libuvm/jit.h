#ifndef JIT_H
#define JIT_H

#include <string>
//#include <sys/types.h>
#include <sys/mman.h>
#include <dlfcn.h>
//#include <cpuid.h>
#include "../common/opcodes.h"
#include "../common/common.h"

using namespace std;

typedef uint addr_t;
typedef void (*jit_function)(...);


extern "C" ATTR_USED int  __cdecl jitCheckSetPriority(void *fun_ptr);
extern "C" ATTR_USED int  __cdecl jitIsCompiled(void *fun_ptr);

extern "C" ATTR_USED byte*  __cdecl jitGCAllocStr(byte* ptr, uint len);
extern "C" ATTR_USED byte*  __cdecl jitGCArrayAllocHelper(uint type, uint count);

extern "C" ATTR_USED __uint64_t __cdecl jitUI64DivisionHelper(__uint64_t p1, __uint64_t p2);
extern "C" ATTR_USED __int64_t __cdecl jitI64DivisionHelper(__int64_t p1, __int64_t p2);
extern "C" ATTR_USED __uint64_t __cdecl jitUI64ModHelper(__uint64_t p1, __uint64_t p2);
extern "C" ATTR_USED __int64_t __cdecl jitI64ModHelper(__int64_t p1, __int64_t p2);

extern "C" ATTR_USED uint  __cdecl jitStringECompareHelper(const char* str1, const char* str2);
extern "C" ATTR_USED uint  __cdecl jitStringNECompareHelper(const char* str1, const char* str2);

extern "C" void __cdecl jitCallHelper(void *fun_ptr); //ASM Procedure


extern "C" void __cdecl jit_print(byte* ptr);
extern "C" void __cdecl jit_print_hex(uint ui);
extern "C" void __cdecl jit_print_int(int i);
extern "C" void __cdecl jit_print_uint(uint ui);
extern "C" void __cdecl jit_print_long(__int64_t l);
extern "C" void __cdecl jit_print_ulong(__uint64_t ul);
extern "C" void __cdecl jit_print_char(uint ch);
extern "C" void __cdecl jit_print_bool(uint b);
extern "C" int __cdecl jit_read_int();
extern "C" uint __cdecl jit_read_uint();
extern "C" byte* __cdecl jit_read_string();

/***************************/
/*       JIT DEFINES       */
/***************************/

#define JIT_DISABLED 0
#define JIT_ENABLED 1
#define JIT_RESTRICTED_1 2
#define JIT_LEVEL JIT_ENABLED
#define JIT_NO_FUNC_PRECALL_CHECK

#define PREFIX_16 0x66

/* 8-bit registers */
#define AH 0b100
#define AL 0b000
#define CH 0b101
#define CL 0b001
#define DH 0b110
#define DL 0b010
#define BH 0b111
#define BL 0b011

/* 16-bit registers */
#define AX 0b000
#define CX 0b001
#define DX 0b010
#define BX 0b011
#define SP 0b100
#define BP 0b101
#define SI 0b110
#define DI 0b111

/* 32-bit registers */
#define EAX 0b000
#define ECX 0b001
#define EDX 0b010
#define EBX 0b011
#define ESP 0b100
#define EBP 0b101
#define ESI 0b110
#define EDI 0b111

#define PUSH_REG(reg) (0x50 + (reg))
#define POP_REG(reg) (0x58 + (reg))

#define PUSH_CONST_8 0x6A
#define PUSH_CONST_32 0x68

#define CMODE_16 0x66

#define MRM(m, r, rm) (((m) << 6) | ((r) << 3) | (rm))
#define NNN(m, r, rm) (((m) << 6) | ((r) << 3) | (rm))

#define MAX_STACK_COUNT 25

#define ARG_32(idx) (8 + (idx)*4)
#define LOC_32(idx) (-(4 + (idx)*4))

struct JCompType
{
    Type base;
    uint dimens = 0;
    byte *type_ptr;

    JCompType()
        : base(Type::PTR_NULL)
    {
    }

    JCompType(Type t)
        : base(t)
    {
    }

    JCompType(Type t, uint d)
        : base(t), dimens(d)
    {
    }

    JCompType(byte* addr)
        : type_ptr(addr)
    {
        base = *(Type*)addr;
        if(base == Type::ARRAY)
        {
            dimens = *(uint*)(addr+1);
            base = *(Type*)(addr + 1 + sizeof(uint));
        }
    }

    operator byte*()
    {
        return type_ptr;
    }

    operator Type*()
    {
        return (Type*)type_ptr;
    }

    operator Type()
    {
        return dimens == 0 ? base : Type::ARRAY;
    }

    bool isArray()
    {
        return (bool)dimens;
    }
};

class JITException : public Exception
{
public:
    JITException(const string &msg)
    {
        what = msg;
    }

    JITException(const string &msg, const char* where)
    {
        what = msg + " in " + where;
    }
};

class JITRegStack
{
    byte regmap = 0b00111111;
    uint free_regs[6] = {EAX, EDX, ECX, EBX, ESI, EDI};
    int used_idx = 0;

public:
    uint Use()
    {
        int reg = -1;
        for(int i = 0; i < 6; ++i)
        {
            if(regmap >> i)
            {
                reg = free_regs[i];
                regmap ^= (1 << i);
            }
        }
        if(reg == -1)
            throw JITException("All registers are used", __FUNCTION__);

        return reg;
    }
    uint Use(uint reg)
    {
        //int reg = -1;
        for(int i = 0; i < 6; ++i)
        {
            if(free_regs[i] == reg)
            {
                if(!(regmap >> i))
                    throw JITException("Register already used", __FUNCTION__);
                regmap ^= (1 << i);
            }
        }
        return reg;
    }
    void Free(uint reg)
    {
        for(int i = 0; i < 6; ++i)
        {
            if(free_regs[i] == reg)
            {
                if(regmap >> i)
                    throw JITException("Register already freed", __FUNCTION__);
                regmap |= (1 << i);
            }
        }
    }
    uint Free()
    {
        int reg = -1;
        for(int i = 5; i >= 0; --i)
        {
            if(!(regmap >> i))
            {
                regmap |= (1 << i);
                reg = free_regs[i];
            }
        }
        if(reg == -1)
            throw JITException("All registers freed", __FUNCTION__);
        return reg;
    }
    uint FreeRev()
    {
        int reg = -1;
        for(int i = 0; i < 6; ++i)
        {
            if(!(regmap >> i))
            {
                regmap |= (1 << i);
                reg = free_regs[i];
            }
        }
        if(reg == -1)
            throw JITException("All registers freed", __FUNCTION__);
        return reg;
    }

    uint GetReg()
    {
        int reg = -1;
        for(int i = 5; i >= 0; --i)
        {
            if(!(regmap >> i))
                reg = free_regs[i];
        }
        if(reg == -1)
            throw JITException("All registers freed", __FUNCTION__);
        return reg;
    }

    uint GetRegRev()
    {
        int reg = -1;
        for(int i = 0; i < 6; ++i)
        {
            if(!(regmap >> i))
                reg = free_regs[i];
        }
        if(reg == -1)
            throw JITException("All registers freed", __FUNCTION__);
        return reg;
    }

    bool IsUsed(uint reg)
    {
        for(int i = 0; i < 6; ++i)
        {
            if(free_regs[i] == reg && !(regmap >> i))
                return true;
        }
        return false;
    }

    bool IsAllUsed()
    {
        return regmap == 0;
    }
};

class JITTypeStack
{
    JCompType types[MAX_STACK_COUNT];
    int idx = 0;

public:
    void Push(JCompType &&val)
    {
        if(idx >= MAX_STACK_COUNT)
            throw JITException("Stack overflow", __FUNCTION__);
        types[idx++] = val;
    }

    void Push(JCompType &val)
    {
        if(idx >= MAX_STACK_COUNT)
            throw JITException("Stack overflow", __FUNCTION__);
        types[idx++] = val;
    }

    JCompType Pop()
    {
        if(idx == 0)
            throw JITException("Stack corrupted: index < 0", __FUNCTION__);
        return types[--idx];
    }

    JCompType Last()
    {
        return types[idx-1];
    }

    JCompType Pop2()
    {
        if(idx < 2)
            throw JITException("Stack corrupted: index < 0", __FUNCTION__);
        auto& t = types[idx-1];
        idx -= 2;
        return t;
    }
};

bool saveReg(uint reg, JITRegStack &reg_stack, vector<byte> &x86code);
void mov(uint reg1, uint reg2, vector<byte> &x86code);
void xchg(uint reg1, uint reg2, vector<byte> &x86code);
void udiv(JITRegStack &reg_stack, vector<byte> &x86code);
void div(JITRegStack &reg_stack, vector<byte> &x86code);

#endif // JIT_H

