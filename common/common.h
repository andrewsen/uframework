#ifndef COMMON_COMMON_H
#define COMMON_COMMON_H

#include <string>
#include "defines.h"

typedef unsigned char byte;

class Exception {
protected:
    std::string what;

public:
    virtual std::string What() {
        return what;
    }

    virtual ~Exception()
    {}
};

#endif // COMMON_H
