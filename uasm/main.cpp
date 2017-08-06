#include <iostream>
#include <cassert>
#include "common.h"
#include "assembler.h"
#include <cstdlib>
#include <stdarg.h>

using namespace std;

int main(int argc, char* argv[])
{    
    //if(argc == 1)
    //{
    //    cout << "Usage: " << argv[0] << " <module.vas>\n";
    //}

    try {
        Assembler a;
        a << argv[1];
        //a << "/home/senko/qt/ualang/framework_v2/bin/a.vas";
        //for(int i = 1; i < argc; i++)
        //    a << argv[i];
        a.Compile();
        a.Write();
    }
    catch (AssemblerException ae) {
        cerr << ae.What() << endl;
    }

    return 0;
}

void warning(string msg)
{
    cerr << "warning: " << msg << endl;
}
