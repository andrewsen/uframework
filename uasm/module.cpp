#include "module.h"

Module::Module()
{
    mflags.executable_bit = 1;
    mflags.no_globals_bit = 0;
    mflags.no_internal_bit = 0;
    mflags.reserved = 0;

    functionSeg.typeSeg = &typeSeg;
    globalsSeg.typeSeg = &typeSeg;
}

void Module::AddGlobal(GlobalVar var)
{
    globalsSeg.vars.push_back(var);
}

void Module::AddFunction(const Function &fun)
{
    //cout << functionSeg.funcs.size() << endl;
    functionSeg.funcs.push_back(fun);
}

vector<Segment *> Module::AllSegments()
{
    vector<Segment*> segs;
    //segs.push_back(&this->bytecodeSeg);
    if(typeSeg.Size() != 0)
        segs.push_back(&this->typeSeg);
    if(globalsSeg.Size() != 0)
        segs.push_back(&this->globalsSeg);
    if(functionSeg.Size() != 0)
        segs.push_back(&this->functionSeg);
    if(propertySeg.Size() != 0)
        segs.push_back(&this->propertySeg);
    if(stringSeg.Size() != 0)
        segs.push_back(&this->stringSeg);
    if(metaSeg.Size() != 0)
        segs.push_back(&this->metaSeg);

    segs.insert(segs.end(), this->userSegments.begin(), this->userSegments.end());

    return segs;
}

void Module::ImportIfNew(string module, string sign, vector<Type> &args)
{
#if 0

    string argsStr = "";
    for(Type arg : args)
        argsStr += (char)(byte)(arg);
    int mid = -1;
    for(int i = 0; i < importSeg.imports.size(); ++i) {
        if(importSeg.imports[i].name == module) {
            mid = i;
            break;
        }
    }

    ImportedModule& mod = importSeg.imports[mid];
    bool found = false;
    for(addr_t addr : mod.funcPtrs) {
        auto& f = importSeg.importedFuncs[addr];
        if(f.func.sign == sign && f.func.argStr == argsStr) {
            f.addrs.push_back(bytecodeSeg.bytecode.size());
            found = true;
        }
    }

    if(!found) {
        ImportedFunction f;
        f.func = Function(sign, args);
        f.addrs.push_back(bytecodeSeg.bytecode.size());
        mod.funcPtrs.push_back(importSeg.importedFuncs.size());
        importSeg.importedFuncs.push_back(f);
    }
#endif
}

void Module::Import(string mod)
{
    //importSeg.imports.push_back(mod);
}

string Module::GetName()
{
    return name;
}

void Module::SetName(string name)
{
    if(this->name != "")
        warning("Module name has been already defined with " + this->name);
    this->name = name;
}
