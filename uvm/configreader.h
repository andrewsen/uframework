#ifndef CONFIGREADER_H
#define CONFIGREADER_H

#include <fstream>

using namespace std;

typedef unsigned int uint;

struct ConfigLine
{
    char key[80];
    char value[128];
    bool empty = true;
};

class ConfigReader
{
    ifstream in;
public:
    ConfigReader(const char *file);
    bool Exists();
    bool CanRead();
    ConfigLine Next();
};

#endif // CONFIGREADER_H
