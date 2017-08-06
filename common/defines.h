#ifndef FW_DEFINES_H
#define FW_DEFINES_H

#define FRAMEWORK_VERSION 0.06
#define MINIMAL_VERSION 0.06

#ifdef DEBUG
    #define FW_DEBUG
    #define GC_DEBUG
#endif

#define TOOLCHAIN_VERSION "0.4-indev"
#define UI32_NOT_FOUND 0xFFFFFFFF

#ifdef FW_DEBUG
    #warning Using debug build
    #define LOG(m) std::clog << "[" << __FILE__ << ":" << __FUNCTION__ << "] " << m << std::endl
#else
    #define LOG(m)
#endif

#ifdef __GNUC__
    #define ATTR_USED __attribute__((used))
    #define ATTR_UNUSED __attribute__((unused))
    #ifndef __cdecl
        #define __cdecl __attribute__((__cdecl__))
    #endif
#else
    #define ATTR_USED
    #define ATTR_UNUSED
#endif

#define ARRAY_SIZE 9
#define ARRAY_COUNT_OFFSET 1
#define ARRAY_ELEM_SIZE_OFFSET ARRAY_COUNT_OFFSET + 4

#define ARRAY_TYPE(ptr) ((Type)(ptr))
#define ARRAY_COUNT(ptr) (*(uint*)((ptr)+(ARRAY_COUNT_OFFSET)))
#define ARRAY_ELEM_SIZE(ptr) (*(uint*)((ptr)+(ARRAY_ELEM_SIZE_OFFSET)))

#endif
