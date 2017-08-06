#ifndef MODULE_H
#define MODULE_H
#include "../uasm/common.h"
#include "importedmodule.h"
#include <algorithm>

interface Segment {
public:
    enum Type : byte {
        Bytecode=1, Strings, Imports, Metadata, Functions, Globals, Types, Properties, User
    };

protected:
    string name;
    Segment::Type type;

    virtual void generateBytes() {    }
    virtual void restore() {    }
public:    
    vector<byte> bytes;
    virtual void Restore() = 0;

    virtual string Name() { return name; }
    virtual void Name(string n) { name = n; }
    virtual Segment::Type GetType() { return type; }
    virtual vector<byte> GetBytes() {
        if(bytes.empty())
            generateBytes();
        return bytes;
    }
    virtual uint Size() {
        if(bytes.empty())
            generateBytes();
        return bytes.size();
    }

    virtual void AppendTo(vector<byte> &bytes) {
        throw "Unimplemented";
    }
};

class TypeSegment : public Segment
{
public:
    vector<CompType> types;
    //vector<byte> bytes;

    TypeSegment()
    {
        type = Segment::Types;
        name = "types";
    }

    uint AddType(const CompType& type)
    {
        for(int i = 0; i < types.size(); ++i)
        {
            if(types[i] == type)
                return i;
        }
        types.push_back(type);
        return types.size()-1;
    }

    virtual void AppendTo(vector<byte> &b) {
        b.insert(b.end(), bytes.begin(), bytes.end());
    }

    virtual void Restore() //TEST
    {
        types.clear();
        for(uint i = 0; i < bytes.size(); ++i)
        {
            CompType t;
            ::Type plain = (::Type)bytes[i];
            if(plain == ::Type::ARRAY)
            {
                uint dimens = bytesToAddr(bytes, ++i);
                t.dimens = dimens;
                t.plain = (::Type)bytes[++i];
            }
            else
                t.plain = plain;
            types.push_back(t);
        }
    }

protected:
    virtual void generateBytes() {
        uint size;
        //vector<byte> result;
        for(auto& t : types)
        {
            cout << "Type: ";
            t.addr = bytes.size();
            if(t.dimens != 0)
            {
                cout << t.dimens << " array of ";
                bytes.push_back((byte)::Type::ARRAY);
                pushAddr(t.dimens, bytes);
            }
            else if(t.plain == ::Type::ARRAY)
            {
                bytes.push_back((byte)::Type::ARRAY);
                pushAddr(0, bytes);
            }

            cout << (uint)t.plain << endl;
            bytes.push_back((byte)t.plain);
        }
        //size = result.size() + 4;
        //pushAddr(size, bytes);
        //bytes.insert(bytes.end(), result.begin(), result.end());
    }
};

class GlobalsSegment : public Segment {
public:
    TypeSegment* typeSeg;
    vector<GlobalVar> vars;
    //vector<byte> bytes;

    GlobalsSegment() {
        type = Segment::Globals;
        name = "globals";
    }

    virtual void Restore() //TEST
    {
        if(bytes.size() == 0)
        {
            return;
        }
        vars.clear();
        for(uint i = 4, c = 0; i < bytes.size(); ++i, ++c)
        {
            GlobalVar gv;
            gv.type = bytesToAddr(bytes, i);
            gv.isPrivate = (bool)bytes[++i];
            while(bytes[++i] != 0)
            {
                gv.name += (char)bytes[i];
            }
            gv.id = c;
            vars.push_back(gv);
        }
    }

protected:
    virtual void  generateBytes() {
        if(vars.size() == 0)
        {
            bytes.clear();
            return;
        }
        sort(vars.begin(), vars.end(), [&](const GlobalVar &v1, const GlobalVar &v2) {return v1.id < v2.id;});
        pushAddr(vars.size(), bytes);

#ifdef DEBUG
        for(int i = 0; i < vars.size(); ++i) {
            if(vars[i].id != i) cout << "Wrong id: " << vars[i].id << " at: " << i << endl;
            else cout << "Right id:" << i << endl;
            //cout << "Var name: " << vars[i].name << endl;
        }
#endif

        for(GlobalVar var : vars) {
            uint type = typeSeg->types[var.type].addr;
            //cout << "addr of var " << var.name << " is " << type << endl;
            //bytes.push_back((byte)var.type);
            pushAddr(type, bytes);
            //pushInt(var.id, bytes);

            if(var.isPrivate)
                bytes.push_back(1);
            else
                bytes.push_back(0);
            for(char ch : var.name) bytes.push_back(ch);
            bytes.push_back('\0');
        }
    }
};

class PropSegment : public Segment
{
public:
    vector<Property> props;
    PropSegment()
    {
        type = Segment::Properties;
        name = "props";
    }

    virtual void Restore()
    {
        if(bytes.size() == 0)
        {
            return;
        }
        props.clear();
        for(uint i = 4; i < bytes.size(); ++i)
        {
            Property prop;
            while(bytes[i] != 0)
            {
                prop.name += (char)bytes[i];
                ++i;
            }
            prop.proptype = (PropType)bytes[++i];
            prop.isPrivate = (bool)bytes[++i];
            prop.type = bytesToAddr(bytes, ++i);

            props.push_back(prop);
        }
    }

protected:
    virtual void generateBytes()
    {
        if(props.size() == 0)
        {
            bytes.clear();
            return;
        }
        pushAddr(props.size(), bytes);

        for(const Property& prop: props)
        {
            bytes.insert(bytes.end(), prop.name.begin(), prop.name.end());
            bytes.push_back(0);
            bytes.push_back((byte)prop.proptype);
            bytes.push_back((byte)prop.isPrivate);
            pushAddr(prop.type, bytes);
        }
    }
};

class MetaSegment : public Segment {
public:
    vector<::Metadata> elems;
    //vector<byte> bytes;

    MetaSegment() {
        type = Segment::Metadata;
        name = "meta";
    }

    ::Metadata operator [](string key)
    {
        for(auto& m : elems)
            if(m.key == key)
                return m;
        ::Metadata none;
        none.sval = "";
        none.type = ::Metadata::None;
        return none;
    }

    virtual void Restore()
    {
        if(bytes.size() == 0)
        {
            return;
        }
        elems.clear();
        for(uint i = 4; i < bytes.size(); ++i)
        {
            ::Metadata m;
            while(bytes[i] != 0)
            {
                m.key += (char)bytes[i];
                ++i;
            }
            m.type = (::Metadata::Type)bytes[++i];
            switch (m.type) {
                case ::Metadata::Boolean:
                    m.bval = (bool)bytes[++i];
                    break;
                case ::Metadata::Integer:
                    m.i32val = (int)bytesToAddr(bytes, ++i);
                    break;
                case ::Metadata::Float:
                    m.dconv[0] = bytesToAddr(bytes, ++i);
                    m.dconv[0] = bytesToAddr(bytes, ++i);
                    break;
                case ::Metadata::String:
                {
                    uint size = bytesToAddr(bytes, ++i);
                    for(int t = 0; t < size; ++t)
                    {
                        ++i;
                        m.sval += (char)bytes[i];
                    }
                    ++i;
                }
                    break;
                case ::Metadata::Raw:
                 {
                    uint size = bytesToAddr(bytes, ++i);
                    for(int t = 0; t < size; ++t)
                    {
                        ++i;
                        m.rval.push_back(bytes[i]);
                    }
                    ++i;
                }
                    break;
                default:
                    break;
            }

            elems.push_back(m);
        }
    }

protected:
    virtual void generateBytes() {
        if(elems.size() == 0)
        {
            bytes.clear();
            return;
        }
        pushAddr(elems.size(), bytes);

        /*uint offset = 4 + elems.size() * 4;

        for(::Metadata m : elems)
        {
            if(!m.compilable)
                continue;
            pushAddr(offset, bytes);
            //offset += m.key.size() + 2;
            switch (m.type) {
                case ::Metadata::Boolean:
                    ++offset;
                    break;
                case ::Metadata::Integer:
                    offset += 4;
                    break;
                case ::Metadata::Float:
                    offset += 8;vers
                    break;
                case ::Metadata::String:
                    offset += m.sval.size() + 5;
                    break;
                case ::Metadata::Raw:
                    offset += m.rval.size() + 5;
                    break;
                default:
                    break;
            }
        }*/

        for(::Metadata m : elems)
        {
            if(!m.compilable)
                continue;
            bytes.insert(bytes.end(), m.key.begin(), m.key.end());
            bytes.push_back(0);
            bytes.push_back((byte)m.type);
            switch (m.type) {
                case ::Metadata::Boolean:
                    bytes.push_back((byte)m.bval);
                    break;
                case ::Metadata::Integer:
                    pushInt(m.i32val, bytes);
                    break;
                case ::Metadata::Float:
                    pushAddr(m.dconv[0], bytes);
                    pushAddr(m.dconv[1], bytes);
                    break;
                case ::Metadata::String:
                    pushAddr(m.sval.size(), bytes);
                    bytes.insert(bytes.end(), m.sval.begin(), m.sval.end());
                    break;
                case ::Metadata::Raw:
                    pushAddr(m.rval.size(), bytes);
                    bytes.insert(bytes.end(), m.rval.begin(), m.rval.end());
                    break;
                default:
                    break;
            }
        }
    }
};

class FunctionSegment : public Segment {
public:
    TypeSegment* typeSeg;
    vector<Function> funcs;
    //vector<byte> bytes;

    FunctionSegment(){
        type = Segment::Functions;
        name = "funcs";
        funcs.reserve(65535);
    }

    virtual void Restore()
    {
        if(bytes.size() == 0)
        {
            return;
        }
        funcs.clear();
        for(uint i = 4; i < bytes.size(); ++i)
        {
            Function f;

            f.retType = bytesToAddr(bytes, i);

            while(bytes[++i] != 0)
            {
                f.sign += (char)bytes[i];
            }

            uint arg = bytesToAddr(bytes, ++i);
            while(arg != 0xFFFFFFFF)
            {
                f.args.push_back(arg);
                arg = bytesToAddr(bytes, ++i);
            }

            //bytes.push_back('\0');

            f.isPrivate = (bool)bytes[++i];

            f.flags = bytes[++i];

            if(f.flags & FFlags::IMPORTED) {
                while(bytes[++i] != 0)
                {
                    f.module += (char)bytes[i];
                }
            }
            else if (!(f.flags & FFlags::RTINTERNAL)) {
                uint lv_size = bytesToAddr(bytes, ++i);
                if(lv_size > 0) {
                    i += 4;
                    for(int t = 0; t < lv_size; ++t)
                    {
                        LocalVar lv;
                        lv.type = bytesToAddr(bytes, ++i);
                        f.localVars.push_back(lv);
                    }
                }
                uint bc_size = bytesToAddr(bytes, ++i);
                for(int t = 0; t < bc_size; ++t)
                {
                    f.bytecode.push_back(bytes[++i]);
                }
            }

            funcs.push_back(f);
        }
    }
protected:
    virtual void generateBytes() {
        //sort(funcs.begin(), funcs.end(), [](const Function &v1, const Function &v2) {return v1.isPrivate && !v2.isPrivate;});
        pushAddr(funcs.size(), bytes);
        for(Function& f : funcs) {
            if(f.typeAddrTable.size() != 0)
            {
                union {
                    addr_t a;
                    byte bytes[4];
                } a2b;
                auto& bc = f.bytecode;
                for(uint addr : f.typeAddrTable)
                {
                    a2b.bytes[0] = bc[addr];
                    a2b.bytes[1] = bc[addr+1];
                    a2b.bytes[2] = bc[addr+2];
                    a2b.bytes[3] = bc[addr+3];
                    CompType t = typeSeg->types[a2b.a];
                    //if(!l.hasAddr) throw AssemblerException("Label " + l.name + " wasn't defined");
                    cout << "Type " << t.signatre << " addr replaced: 0x" << hex << a2b.a << " -> 0x" << t.addr << endl << dec;
                    a2b.a = t.addr;
                    bc[addr] = a2b.bytes[0];
                    bc[addr+1] = a2b.bytes[1];
                    bc[addr+2] = a2b.bytes[2];
                    bc[addr+3] = a2b.bytes[3];
                }
            }
            //bytes.push_back(0xCC);
            //pushAddr(f.addr, bytes);
            //bytes.push_back((byte)f.retType);
            uint type = -1;
            if(f.retType != -1)
                type = typeSeg->types[f.retType].addr;
            //bytes.push_back((byte)var.type);
            pushAddr(type, bytes);

            for(char ch : f.sign) bytes.push_back(ch);
            bytes.push_back('\0');
            for(uint t : f.args)
            {
                //bytes.push_back((byte)t);
                uint type = typeSeg->types[t].addr;
                //bytes.push_back((byte)var.type);
                pushAddr(type, bytes);
            }
            pushAddr(0xFFFFFFFF, bytes);
            //bytes.push_back('\0');

            if(f.isPrivate)
                bytes.push_back(1);
            else
                bytes.push_back(0);

            bytes.push_back(f.flags);

            if(f.flags & FFlags::IMPORTED) {
                for(char ch : f.module)
                    bytes.push_back(ch);
                bytes.push_back('\0');
            }
            else if (!(f.flags & FFlags::RTINTERNAL)) {
                pushAddr(f.localVars.size(), bytes);
                if(f.localVars.size() > 0) {
                    uint size = 0;
                    vector<byte> types;
                    for(LocalVar var : f.localVars) {
                        //TEST: I dont push var.id in bytes - this may cause problems in VM!
                        //types.push_back((byte)var.type);
                        uint type = typeSeg->types[var.type].addr;
                        //bytes.push_back((byte)var.type);
                        if(typeSeg->types[var.type].dimens == 0)
                            size += sizeOf(typeSeg->types[var.type].plain);
                        else
                            size += sizeOf(::Type::ARRAY);
                        pushAddr(type, types);
                    }
                    pushAddr(size, bytes);
                    bytes.insert(bytes.end(), types.begin(), types.end());
                }
                pushAddr(f.bytecode.size(), bytes);
                bytes.insert(bytes.end(), f.bytecode.begin(), f.bytecode.end());
                //bytes.push_back(f.bytecode.size());
            }

            //bytes.push_back(0xDD);
        }
    }
};

class StringSegment : public Segment {
public:

    StringSegment() {
        type = Segment::Strings;
        name = "strings";
    }

    virtual uint Size() {
        return bytes.size(); //FIXME!!!!!
    }

    virtual void AppendTo(vector<byte> &bytes) {
        bytes.insert(bytes.end(), bytes.begin(), bytes.end());
    }

    virtual vector<byte> GetBytes() {
#ifdef DEBUG
        cout << "Strings size: " << bytes.size() << endl;
#endif
        return bytes;
    }

    virtual void Restore()
    {

    }
};

/*class OffsetSegment : public Segment {
public:
    vector<addr_t> strings;

    OffsetSegment() {
        type = Segment::Offsets;
        name = "offsets";
    }

    virtual uint size() {
        return strings.size()*4; //FIXME!!!!!
    }

    virtual void appendTo(vector<byte> &bytes) {
        //bytes.insert(bytes.end(), strings.begin(), strings.end());
    }

    virtual vector<byte> getBytes() {
        vector<byte> bytes;
        bytes.reserve(strings.size()*4);
        for(addr_t a: strings) {
            pushAddr(a, bytes);
        }
        return bytes;
    }
};

class ImportSegment : public Segment {
public:
    vector<ImportedModule> imports;
    //vector<ImportedFunction> importedFuncs;
    vector<byte> bytes;

    ImportSegment() {
        type = Segment::Imports;
        name = "imports";
    }

    virtual uint size() {
        if(bytes.empty())
            generateBytes();
        return bytes.size();
    }

    virtual void appendTo(vector<byte> &bytes) {
        //FIXME!!!!!
    }

    virtual vector<byte> getBytes() {
        if(bytes.empty())
            generateBytes();
        return bytes;
    }
private:
    void generateBytes() {
        for(ImportedModule im : imports) {
            for(char ch : im.name)
                bytes.push_back((byte)ch);
            bytes.push_back('\0');
            pushInt(im.funcPtrs.size()*4, bytes);
            for(addr_t a : im.funcPtrs)
                pushAddr(a, bytes);
        }
        union {
            addr_t a;
            byte bytes[4];
        } a2b;
        a2b.a = bytes.size() + 4;
        bytes.insert(bytes.begin(), a2b.bytes[3]);
        bytes.insert(bytes.begin(), a2b.bytes[2]);
        bytes.insert(bytes.begin(), a2b.bytes[1]);
        bytes.insert(bytes.begin(), a2b.bytes[0]);
        for(ImportedFunction ifunc : importedFuncs) {
            for(char ch: ifunc.func.sign)
                bytes.push_back((byte)ch);
            bytes.push_back('\0');
            for(char ch : ifunc.func.argStr) bytes.push_back(ch);
                bytes.push_back('\0');
            pushAddr(ifunc.addrs.size()*4, bytes);
            for(addr_t a : ifunc.addrs)
                pushAddr(a, bytes);
        }
    }
};*/

class Module
{
protected:
    string name = "";
    MetaSegment metaSeg;
    GlobalsSegment globalsSeg;
    PropSegment propertySeg;
    FunctionSegment functionSeg;
    TypeSegment typeSeg;
    StringSegment stringSeg;
    //OffsetSegment offsetSeg;
    vector<Segment*> userSegments;
    ModuleFlags mflags;

    friend class Assembler;
public:
    Module();

    void AddGlobal(GlobalVar var);
    void AddFunction(const Function &fun);
    vector<Segment *> AllSegments();
    void ImportIfNew(string module, string sign, vector<Type> &args);

    void Import(string mod);

    string GetName();
    void SetName(string name);
};

#endif // MODULE_H





