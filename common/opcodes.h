#ifndef OPCODES_H
#define OPCODES_H

//#include "common.h"
#include <vector>
#include <string>
#include "defines.h"

typedef unsigned char byte;

struct ModuleFlags
{
    unsigned executable_bit : 1;
    unsigned no_globals_bit : 1;
    unsigned no_internal_bit : 1;
    unsigned reserved : 5;
};

struct Metadata {
    enum Type : byte {
        Void, Integer, Float, Boolean, String, Raw, None
    };

    std::string key;
    Metadata::Type type;
    std::string sval;
    std::vector<byte> rval;
    union {
        int i32val;
        uint dconv[2];
        double dval;
        bool bval;
    };

    bool compilable = true;
};

enum FFlags
{
    RTINTERNAL = 1,
    IMPORTED = 2,
    PROTOTYPE = 4
};

enum PropType : byte
{
    RO_PROP = 1,
    RW_PROP = 2
};

enum class Type : byte {
    PTR_NULL,
    VOID,
    UI8,
    CHAR,
    I16,
    UI32,
    I32,
    UI64,
    I64,
    BOOL,
    DOUBLE,
    UTF8,
    ARRAY,
    CLASS,
    GC_MOVED,
    REF = 0x10
};

class CompType
{
public:
    std::string signatre = "";
    CompType *base = nullptr, *included = nullptr;
    std::vector<CompType> inner;
    uint dimens = 0;
    uint addr = 0;

    Type plain;

    CompType()
    {}

    CompType(Type p)
        : plain(p)
    {

    }
    CompType(CompType* b)
        : base(b)
    {

    }

};

struct RTType
{
    byte dimens;
    Type plain;
};

inline bool operator ==(const CompType& t1, const CompType& t2)
{
    bool equality = (t1.plain == t2.plain) &&
            (t1.dimens == t2.dimens) && (t1.signatre == t2.signatre);
    /*if(equality)
    {
        if(base != nullptr && type.base != nullptr)
            equality = *base = *type.base;
        if(included != nullptr && type.included != nullptr)
            equality = *included = *type.included;
    }*/
    return equality;
}

inline bool operator !=(const CompType& t1, const CompType& t2)
{
    return !(t1 == t2);
}

extern unsigned int sizes [];

enum class OpCode : byte {
    NOP,                //0x0
    //TOP,                //0x1
    DUP,
    BAND,
    BOR,
    ADD,
    ///ADDL
    ADDF,
    SUB,
    SUBF,
    MUL,
    ///MULL
    MULF,
    DIV,
    ///DIVL
    DIVF,
    REM,
    REMF,
    CONV_UI8,
    CONV_I16,          //15
    CONV_CHR,
    CONV_I32,
    CONV_UI32,
    CONV_I64,
    CONV_UI64,
    CONV_F,
    JMP,
    JZ,
    JT,
    JNZ,
    JF,
    JNULL,
    JNNULL,
    CALL,
    NEWARR,
    //FREELOC,
    LDLOC,              //31
    LDLOC_0,
    LDLOC_1,
    LDLOC_2,
    STLOC,
    STLOC_0,
    STLOC_1,
    STLOC_2,
    LDELEM,
    LDELEM_0,
    LDELEM_1,
    LDELEM_2,
    LD_AREF,
    LD_BYREF,
    STELEM,
    STELEM_0,
    STELEM_1,
    STELEM_2,
    ST_BYREF,
    LDARG,
    LDARG_0,
    LDARG_1,
    LDARG_2,
    STARG,
    STARG_0,
    STARG_1,
    STARG_2,
    LDFLD,
    LDFLD_0,            //48
    LDFLD_1,
    LDFLD_2,
    STFLD,
    STFLD_0,
    STFLD_1,
    STFLD_2,
    LD_0,
    LD_1,
    LD_2,
    LD_0U,
    LD_1U,
    LD_2U,
    LD_STR,
    LD_UI8,
    LD_I16,
    LD_CHR,
    LD_I32,
    LD_UI32,
    LD_I64,             //63
    LD_UI64,
    LD_F,
    LD_TRUE,
    LD_FALSE,
    LD_NULL,
    AND,
    ///ANDL
    OR,
    ///ORL
    EQ,
    ///EQL
    ///EQF
    NEQ,
    ///NEQL
    ///NEQF
    NOT,
    INV,
    ///INVL
    XOR,
    ///XORL
    NEG,
    ///NEGL
    ///NEGF
    POS,
    INC,
    ///INCL
    DEC,
    ///DECL
    SHL,
    ///SHLL
    SHR,
    ///SHRL
    POP,
    GT,
    ///GTL
    ///GTF
    GTE,
    ///GTEL
    ///GTEF
    LT,
    ///LTL
    ///LTF
    LTE,
    ///LTEL
    ///LTEF
    SIZEOF,
    TYPEOF,
    RET, //88

    CALL_INTERNAL,
    BREAK,
    UNDEFINED
};

#endif // OPCODES_H
