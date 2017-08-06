#include <iostream>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <getopt.h>
#include <cstdio>
#include "../common/opcodes.h"
#include "../uasm/common.h"
#include "../common/common.h"
#include "loadablemodule.h"

using namespace std;

/*struct Header {
    enum Type : byte {
        Bytecode=1, Strings, Imports, Metadata, Functions, Globals, User
    } type;
    string name;
    uint begin;
    uint end;
};

struct LocalVar
{
    uint addr;
    Type type;
};*/

vector<Header> headers;

void readSegmentHeaders(ifstream &ifs, uint hend);
void readSegments(ifstream &ifs);
void viewSegments(string file);
string typeToString(Type t);

FILE * out = stdout;

int main(int argc, char* argv[])
{
    if(argc == 1)
    {
        cout << "Usage: " << argv[0] << " <module.sem>\n";
        return 0;
    }


    const char* short_options = "o:bm:hvs:";

    const struct option long_options[] = {
        {"view-bytecode",no_argument,NULL,'b'},
        {"help",no_argument,NULL,'h'},
        {"version",no_argument,NULL,'v'},
        {"out",required_argument,NULL,'o'},
        {"segment",required_argument,NULL,'s'},
        {"segments",required_argument,NULL,0},
        {NULL,0,NULL,0}
    };

    int res;
    int option_index;

    while ((res = getopt_long(argc, argv, short_options,
        long_options, &option_index)) !=- 1){

        switch (res)
        {
            case 'h': {
                cout << "Ulang dump - compiled (sem) files viewer " << TOOLCHAIN_VERSION << endl << endl;
                cout <<  argv[0] << " [options] <module.sem>" << endl << endl;

                printf("  %-2s, %-10s %s", "-h", "--help", "show this message\n");
                printf("  %-2s, %-10s %s", "-o", "--out", "wite to file instead of stdout\n");
                printf("  %-2s, %-10s %s", "-v", "--version", "show version\n");
                printf("  %-2s, %-10s %s", "-m", "--get-meta", "show metadata entry by key\n");
                printf("  %-2s, %-10s %s", "-b", "--view-bytecode", "view function's bytecode\n");
                return 0;
            }
            case 'o': {
                out = fopen(optarg, "w+");
                break;
            }
            case 'v': {
                cout << TOOLCHAIN_VERSION << endl;
                return 0;
            }
            case 0:
                //viewSegment(argv[argc-1]);
                break;
            case '?':
            default: {
                printf("found unknown option\n");
                return 1;
            }
        }
    }

    LoadableModule lm;
    lm.Open(argv[argc-1]);
    lm.LoadSegments();

    auto&& segs = lm.AllSegments();

    for(Segment* s : segs)
    {
        printf("Segment: %s\n", s->Name());
    }

    /*ifstream ifs(argv[argc-1], ios::in | ios::binary);

    uint mg;

    ifs.read((char*)&mg, 4);
    //if(mg != MOD_MAGIC)
    //    throw InvalidMagicException(mg);

    fprintf(out, "File: \"%s\", magic: 0x%X (%d)\n", argv[argc-1], mg, mg);
    //fprintf(out,  "File: \"" << argv[1] << "\" magic: 0x" << hex << mg << "(" << dec << mg << ")\n";

    uint headers_end;
    ModuleFlags mflags;
    ifs.read((char*)&headers_end, 4);
    ifs.read((char*)&mflags, sizeof(ModuleFlags));

    readSegmentHeaders(ifs, headers_end);
    readSegments(ifs);

    if(out != stdout)
        fclose(out);*/

    return 0;
}
/*
void readSegmentHeaders(ifstream &ifs, uint hend) {
    while(ifs.tellg() != hend) {
        //fprintf(out,  ifs.tellg() << endl;
        Header h;

        char segName[80];
        char* ptr = &segName[0];
        while(true) {
            *ptr = ifs.get();
            if(*ptr == '\0') break;
            ptr++;
        }
        h.type = (Header::Type)(byte)ifs.get();
        ifs.read((char*)&h.begin, 4);
        ifs.read((char*)&h.end, 4);

        h.name = segName;

        fprintf(out, " %s (", h.name.c_str());
        switch (h.type) {
            case Header::Functions:
                fprintf(out, "Functions");
                break;
            case Header::Globals:
                fprintf(out,  "Globals");
                break;
            case Header::Metadata:
                fprintf(out,  "Metadata");
                break;
            case Header::Strings:
                fprintf(out,  "Strings");
                break;
            default:
                fprintf(out,  "User");
                break;
        }
        fprintf(out, ") (0x%X, 0x%X) (%d bytes)\n", h.begin, h.end, (h.end - h.begin));

        headers.push_back(h);
    }
}

void readSegments(ifstream &ifs) {
    for(uint i = 0; i < headers.size(); ++i) {
        Header &h = headers[i];
        //fprintf(out,  "Found header: " << h.name << endl;
        switch (h.type) {
            case Header::Functions:
                {
                    fprintf(out, "Functioin segment:\n");
                    ifs.seekg(h.begin);
                    uint fcount;
                    ifs.read((char*)&fcount, 4);
                    fprintf(out, " Function count: %d\n", fcount);

                    while (ifs.tellg() != h.end) {

                        Type rt = (Type)ifs.get(); /// RETURN
                        LocalVar args[25]; /// ARGS

                        //fprintf(out,  "ifs.tellg() = " << hex << ifs.tellg() << endl;
                        char sign [80]; /// SIGNATURE
                        ifs.get(sign, 80, '\0').ignore();

                        //fprintf(out,  "ifs.tellg() = " << hex << ifs.tellg() << dec << endl;
                        char ch = ifs.get();
                        uint arg_addr = 0, argc = 0;
                        while(ch != '\0') {
                            args[argc].type = (Type)ch;
                            args[argc].addr = arg_addr;
                            ++argc;
                            ch = ifs.get();
                        }

                        bool isPrivate = !(bool)ifs.get(); /// ACCESS
                        FFlags fflags = (FFlags)ifs.get(); /// IMPORTED
                        char mod[128]; /// MODULE

                        if(fflags & FFlags::IMPORTED) {
                            ifs.get(mod, 128, '\0').ignore();
                        }

                        fprintf(out, " %s%s%s %s %s ( ", (isPrivate ? "private " : "public "),
                                (fflags & FFlags::RTINTERNAL ? "rtinternal " : ""),
                                (fflags & FFlags::IMPORTED ? (string("[") + mod + string("]")).c_str() : ""),
                              typeToString(rt).c_str(), sign);

                        for(uint i = 0; i < argc; ++i)
                        {
                            fprintf(out, "%s ", typeToString(args[i].type).c_str());
                        }
                        fprintf(out, ")\n");

                        uint varsz = 0;
                        uint bc_size = 0;

                        if(!(fflags & FFlags::IMPORTED)) {
                            //uint size;
                            ifs.read((char*)&varsz, 4);
                            if(varsz > 0) {
                                fprintf(out, "  locals (%d):\n", varsz);

                                uint local_mem_size;
                                ifs.read((char*)&local_mem_size, 4);
                                fprintf(out, "  mem size: %d\n", local_mem_size);
                                //fun->locals_mem = new byte[local_mem_size];

                                for(uint i = 0; i < varsz; ++i) {
                                    Type type = (Type)ifs.get();
                                    fprintf(out,  "\t%d: %s\n", (i+1), typeToString(type).c_str());
                                }
                            }
                            ifs.read((char*)&bc_size, 4);
                            fprintf(out, "  bytecode size: %d\n", bc_size);
                            //fprintf(out,  hex << "TELLG1: " << ifs.tellg() << endl;
                            ifs.seekg(ifs.tellg() + bc_size);
                            //fprintf(out,  hex << "TELLG2: " << ifs.tellg() << endl;
                            //ifs.read((char*)fun->bytecode, fun->bc_size);
                        }
                        else
                            fprintf(out, "\n");
                    }
                }
                break;
            case Header::Strings:
                {
                    //fprintf(out,  "Strings size: " << h.end - h.begin << endl;
                    fprintf(out, "Strings segment:\n");
                    uint size = h.end - h.begin;
                    fprintf(out, " size: %d\n", size);
                }
                break;
            case Header::Globals:
                {
                    fprintf(out, "Globals segment:\n");
                    ifs.seekg(h.begin);
                    uint gcount;
                    ifs.read((char*)&gcount, 4);
                    uint size = h.end - h.begin;
                    fprintf(out, " size: %d count: %d\n", size, gcount);

                    if(gcount == 0) {
                        continue;
                    }

                    uint c = 0;
                    while(ifs.tellg() != h.end) {
                        fprintf(out, "%d: %s", c, typeToString((Type)ifs.get()).c_str());
                        fprintf(out, " -> %s ", ((bool)ifs.get() ? "private" : "public"));

                        char name [80];
                        ifs.get(name, 80, '\0').ignore();
                        fprintf(out, "%s\n", name);
                        ++c;
                    }
                }
                break;
            default:
                break;
        }
    }
}
*/
string typeToString(Type t)
{
    switch (t) {
        case Type::BOOL:
            return string("bool");
        case Type::DOUBLE:
            return string("double");
        case Type::I16:
            return string("i16");
        case Type::I32:
            return string("i32");
        case Type::I64:
            return string("i64");
        case Type::UI8:
            return string("ui8");
        case Type::UI32:
            return string("ui32");
        case Type::UI64:
            return string("ui64");
        case Type::UTF8:
            return string("utf8");
        case Type::VOID:
            return string("void");
        default:
            return string("unknown");
    }
}

void warning(string msg)
{
    cerr << "warning: " << msg << endl;
}

