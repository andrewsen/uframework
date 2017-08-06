#ifndef BLOCKALLOCATOR_H
#define BLOCKALLOCATOR_H

#include <unordered_map>
#include <stack>
#include <vector>
#include "../common/common.h"

using namespace std;

template <class T>
class Allocated
{
    T* ptr;
    uint size;
public:
};

class BlockAllocator
{
    static const uint BLOCK_DEFAULT = 1024*1024;
    class Block
    {
        friend class BlockAllocator;
        byte* memory;
        byte* initial;
    public:
        uint remaining;

        byte* Allocate(uint size);
    private:
    };

    unordered_map<uint, stack<byte*>> free_mem_map;
    vector<Block> blocks;

    void setUpBlock(uint capacity);

public:
    BlockAllocator();

    template <class T> T* Allocate(uint count)
    {
        auto size = count * sizeof(T);
        auto found = free_mem_map[size];
        if(!found.empty())
        {
            byte* ret = found.top();
            found.pop();
            return new (ret) T[count];
        }
        for(Block& b : blocks)
        {
            if(b.remaining >= size)
            {
                return new (b.Allocate(size)) T[count];
            }
        }
        setUpBlock(size > BLOCK_DEFAULT ? size : BLOCK_DEFAULT);
        return new (blocks.back().Allocate(size)) T[count];
    }

    template <class T> T* Allocate()
    {
        auto size = sizeof(T);
        auto found = free_mem_map[size];
        if(!found.empty())
        {
            byte* ret = found.top();
            found.pop();
            return new (ret)T;
        }
        for(Block& b : blocks)
        {
            if(b.remaining >= size)
            {
                return new (b.Allocate(size)) T;
            }
        }
        setUpBlock(size > BLOCK_DEFAULT ? size : BLOCK_DEFAULT);
        return new (blocks.back().Allocate(size)) T;
    }

    byte* RawAllocate(uint size)
    {
        auto found = free_mem_map[size];
        if(!found.empty())
        {
            byte* ret = found.top();
            found.pop();
            return ret;
        }
        for(Block& b : blocks)
        {
            if(b.remaining >= size)
            {
                return b.Allocate(size);
            }
        }
        setUpBlock(size > BLOCK_DEFAULT ? size : BLOCK_DEFAULT);
        return blocks.back().Allocate(size);
    }

    template <class T> void Free(T* chunk, uint count)
    {

        this->free_mem_map[sizeof(T)*count].push((byte*)chunk);
    }

    template <class T> void Free(T* chunk)
    {
        Free(chunk, 1);
    }

    void Clean();
};

#endif // BLOCKALLOCATOR_H
