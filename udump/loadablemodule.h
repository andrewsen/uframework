#ifndef LOADABLEMODULE
#define LOADABLEMODULE

#include "../uasm/common.h"
#include "../uasm/module.h"

struct Header {
    enum Type : byte {
        Bytecode=1, Strings, Imports, Metadata, Functions, Globals, Types, Properties, User
    } type;
    string name;
    uint begin;
    uint end;
};

class LoadableModule : public Module
{
    ifstream ifs;
public:
    const static uint MOD_MAGIC = 0x4153DEC0;
    vector<Header> headers;

    void Open(string file)
    {
        ifs.open(file, ios::in | ios::binary);
        if(!ifs)
            throw "no such file `" + file + "`";

        uint mg;

        ifs.read((char*)&mg, 4);
        if(mg != MOD_MAGIC)
            throw "file `" + file + "` is not valid ulang module";

        uint headers_end;
        ifs.read((char*)&headers_end, 4);
        ifs.read((char*)&mflags, sizeof(ModuleFlags));

        readSegmentHeaders(headers_end);
    }

    void LoadSegments()
    {
        for(uint i = 0; i < headers.size(); ++i) {
            Header &h = headers[i];

            ifs.seekg(h.begin);

            Segment* s;

            switch (h.type) {
                case Header::Types:
                    s = &typeSeg;
                    //ifs.read((char*)&typeSeg.bytes[0], h.end-h.begin);
                    break;
                case Header::Globals:
                    s = &globalsSeg;
                    //ifs.read((char*)&globalsSeg.bytes[0], h.end-h.begin);
                    break;
                case Header::Properties:
                    s = &propertySeg;
                    //ifs.read((char*)&propertySeg.bytes[0], h.end-h.begin);
                    break;
                case Header::Strings:
                    s = &stringSeg;
                    //ifs.read((char*)&stringSeg.bytes[0], h.end-h.begin);
                    break;
                case Header::Metadata:
                    s = &metaSeg;
                    //ifs.read((char*)&metaSeg.bytes[0], h.end-h.begin);
                    break;
                case Header::Functions:
                    s = &functionSeg;
                    //ifs.read((char*)&functionSeg.bytes[0], h.end-h.begin);
                    break;
                default:
                    break;
            }
            s->bytes.reserve(h.end-h.begin);
            ifs.read((char*)&s->bytes[0], h.end-h.begin);
            s->Name(h.name);
        }

        vector<Segment*>&& segs = this->AllSegments();
        for(Segment* seg : segs)
            seg->Restore();

    }

    ModuleFlags GetFlags()
    {
        return mflags;
    }

private:
    void readSegmentHeaders(uint hend) {
        while(ifs.tellg() != hend) {
            Header h;

            char segName[80];
            char* ptr = &segName[0];
            while(true) {
                *ptr = ifs.get();
                if(*ptr == '\0')
                    break;
                ptr++;
            }
            h.type = (Header::Type)(byte)ifs.get();
            ifs.read((char*)&h.begin, 4);
            ifs.read((char*)&h.end, 4);

            h.name = segName;
            this->headers.push_back(h);
        }
    }
};

#endif // LOADABLEMODULE

