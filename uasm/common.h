#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <cstdlib>
#include <vector>
#include "../common/opcodes.h"
#include "../common/common.h"

#define DEBUG
#define interface class

using namespace std;

typedef unsigned char byte;
typedef unsigned int addr_t;

class AssemblerException : public Exception {
public:
    AssemblerException(string ex)
    { what = ex; }
};

struct GlobalVar {
    int id;
    string name = "";
    bool isPrivate = true;
    uint type = -1;

    GlobalVar() {
        id = 0;
        name = "";
        isPrivate = true;
    }

    GlobalVar(const GlobalVar &var) {
        id = var.id;
        //if(!var.isPrivate)
        name = var.name;
        isPrivate = var.isPrivate;
        type = var.type;
    }

    ~GlobalVar()
    {
        name.clear();
    }
};

struct LocalVar {
    int id = -1;
    uint type = -1;
};

struct Label {
    string name;
    addr_t addr;
    bool hasAddr;
};

struct Property
{
    PropType proptype;
    uint type;
    string name;
    bool isPrivate = true;
};

typedef vector<uint> farglist;


class Function {
public:
    int file = 0;
    byte flags = 0;

    ifstream::pos_type beg;
    ifstream::pos_type end;

    uint retType = -1;
    string sign = "";
    string module = "";
    //string argStr = "";
    farglist args;

    vector<LocalVar> localVars;
    vector<addr_t> labelAddrTable;
    vector<addr_t> typeAddrTable;
    vector<byte> bytecode;
    vector<Label> labelTable;

    bool isPrivate = true;
    //bool imported = false;

    Function() {
        file = 0;
        args.clear();
    }
    ~Function() {
        args.clear();
        localVars.clear();
        labelAddrTable.clear();
        bytecode.clear();
        labelTable.clear();
    }
    Function(string name) : sign(name) {}
    Function(const Function &f) {
        file = f.file;
        beg = f.beg;
        end = f.end;
        retType = f.retType;
        sign = f.sign;
        args.insert(args.begin(), f.args.begin(), f.args.end());
        //argStr = f.argStr;
        module = f.module;
        isPrivate = f.isPrivate;
        flags = f.flags;
        localVars.clear();
        localVars.insert(localVars.begin(), f.localVars.begin(), f.localVars.end());
        labelAddrTable.clear();
        labelAddrTable.insert(labelAddrTable.begin(), f.labelAddrTable.begin(), f.labelAddrTable.end());
        bytecode.clear();
        bytecode.insert(bytecode.begin(), f.bytecode.begin(), f.bytecode.end());
        labelTable.clear();
        labelTable.insert(labelTable.begin(), f.labelTable.begin(), f.labelTable.end());
    }
    Function(const Function &&f) {
        file = f.file;
        beg = f.beg;
        end = f.end;
        retType = f.retType;
        sign = f.sign;
        args.insert(args.begin(), f.args.begin(), f.args.end());
        //argStr = f.argStr;
        module = f.module;
        isPrivate = f.isPrivate;
        flags = f.flags;
        localVars.clear();
        localVars.insert(localVars.begin(), f.localVars.begin(), f.localVars.end());
        labelAddrTable.clear();
        labelAddrTable.insert(labelAddrTable.begin(), f.labelAddrTable.begin(), f.labelAddrTable.end());
        bytecode.clear();
        bytecode.insert(bytecode.begin(), f.bytecode.begin(), f.bytecode.end());
        labelTable.clear();
        labelTable.insert(labelTable.begin(), f.labelTable.begin(), f.labelTable.end());
    }
    Function& operator=(const Function &f) {
        file = f.file;
        beg = f.beg;
        end = f.end;
        retType = f.retType;
        sign = f.sign;
        args.insert(args.begin(), f.args.begin(), f.args.end());
        //argStr = f.argStr;
        module = f.module;
        isPrivate = f.isPrivate;
        flags = f.flags;
        localVars.clear();
        localVars.insert(localVars.begin(), f.localVars.begin(), f.localVars.end());
        labelAddrTable.clear();
        labelAddrTable.insert(labelAddrTable.begin(), f.labelAddrTable.begin(), f.labelAddrTable.end());
        bytecode.clear();
        bytecode.insert(bytecode.begin(), f.bytecode.begin(), f.bytecode.end());
        labelTable.clear();
        labelTable.insert(labelTable.begin(), f.labelTable.begin(), f.labelTable.end());
        return *this;
    }

    Function(string name, farglist &args) {
        sign = name;
        this->args = move(args);
        //this->argStr = argStr;
        /*for(auto t : this->args) {
            //CompType ct;
            argStr += (char)(byte)t;
        }*/
    }
    Function(string name, farglist &&args) {
        sign = name;
        this->args = move(args);
        //this->argStr = argStr;
        /*for(auto t : this->args) {
            //CompType ct;
            argStr += (char)(byte)t;
        }*/
    }

    /*void operator=(const Function &f) {
        file = f.file;
        beg = f.beg;
        end = f.end;
        retType = f.retType;
        sign = f.sign;
        args.insert(args.begin(), f.args.begin(), f.args.end());
        argStr = f.argStr;
        //addr = f.addr;
        isPrivate = f.isPrivate;
    }*/

    static bool ArgsEquals(farglist &a1, farglist &a2, const vector<CompType> &cts) {
        if(a1.size() != a2.size())
            return false;
        else
            for(int i = 0; i < a1.size(); ++i)
                if(cts[a1[i]] != cts[a2[i]])
                    return false;
        return true;
    }
};

void pushInt(int i, vector<byte> &vec);
void pushAddr(addr_t a, vector<byte> &vec);
addr_t bytesToAddr(vector<byte> &bytes, uint &pos);

void warning(string msg);

inline static uint sizeOf(Type t) {
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
            return 0;
    }
}

#endif // COMMON_H

