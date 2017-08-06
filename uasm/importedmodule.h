#ifndef IMPORTEDMODULE_H
#define IMPORTEDMODULE_H
#include "common.h"

class ImportedModule
{
public:
    string name;
    vector<addr_t> funcPtrs;

    ImportedModule();
};

struct ImportedFunction {
    Function func;
    vector<addr_t> addrs;

    ImportedFunction()
    {}

    ImportedFunction(Function f) {
        func = f;
    }
};
#endif // IMPORTEDMODULE_H
