#include "blockallocator.h"

BlockAllocator::BlockAllocator()
{
    setUpBlock(BLOCK_DEFAULT);
}

void BlockAllocator::Clean()
{
    for(Block& b : blocks)
        delete[] b.initial;
}

byte *BlockAllocator::Block::Allocate(uint size)
{
    remaining -= size;
    auto ret = memory;
    memory += size;
    return ret;
}

void BlockAllocator::setUpBlock(uint capacity)
{
    Block b;
    b.remaining = capacity;
    b.memory = b.initial = new byte[capacity];
    blocks.push_back(b);
}
