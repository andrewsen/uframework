#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <fstream>
#include <typeinfo>
#include <cstring>

using namespace std;


class Log
{
    char out[128];
    bool enable_file_out = false;
    ofstream out_stream;
public:
    enum LogType
    {
        Error, Warning, Info
    };
    Log();

    void SetType(LogType t)
    {
        ltype = t;
    }

    void StoreToFile(const char* path)
    {
        if(path == nullptr && enable_file_out)
        {
            out_stream.flush();
            out_stream.close();
            enable_file_out = false;
        }
        else if(path != nullptr)
        {
            if(strcmp(path, out))
            {
                strcpy(out, path);
                out_stream.open(out, ios_base::out | ios_base::trunc);
            }
            enable_file_out = true;
        }
    }

    void Close()
    {
        if(out_stream.is_open())
        {
            out_stream.flush();
            out_stream.close();
        }
    }

    template <typename T> Log &operator<<(T arg)
    {
        if(ltype == Info)
            clog << arg;
        else
            cerr << arg;

        if(enable_file_out)
        {
            out_stream << arg;
        }

        return *this;
    }
private:

    LogType ltype;
};

#endif // LOG_H
