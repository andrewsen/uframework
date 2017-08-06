#include "configreader.h"
#include <cstring>

ConfigReader::ConfigReader(const char* file)
{
    in.open(file, ios::in);
}

bool ConfigReader::Exists()
{
    return in.good();
}

bool ConfigReader::CanRead()
{
    return !in.eof();
}

ConfigLine ConfigReader::Next()
{
    ConfigLine line;
    if(in.bad())
        return line;

    string buf;
    do {
        getline(in, buf);
    } while(!isalpha(buf[0]) && !in.eof());

    if(in.eof())
    {
        return line;
    }

    line.empty = false;

    const char* sep = strchr(buf.c_str(), '=');
    if(sep == nullptr)
        strcpy(line.key, buf.c_str());
    else
    {
        strncpy(line.key, buf.c_str(), (size_t)(sep - buf.c_str()));
        strcpy(line.value, sep+1);
    }
    return line;
}
