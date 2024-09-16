#include <unistd.h>
#include <sys/mman.h>
#include <iostream>
#include <cstring>
#include <cmath>
#define SBRK_FAILED (void *) (-1)
#define MAX_SIZE 100000000
#define META_DATA_SIZE sizeof(MallocMetadata)
#define GET_METADATA(p) ((MallocMetadata *) ((p==nullptr)? nullptr:(char *) p - META_DATA_SIZE))
#define GET_USER_PTR(p) ((void*)((char*)(p) + META_DATA_SIZE))
#define MAX_ORDER 10
#define ALIGNMENT 4194304
#define INIT_BLOCK_SIZE 131072
#define NUM_OF_FREE_BLOCKS_AT_INIT 32
#define MINIMAL_BLOCK_SIZE 128
#define MAXIMAL_BUDDY_BLOCK 131072


class MallocMetadata
{
private:
    int cookie;
    size_t total_block_size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;

    MallocMetadata(int cookie, size_t size, bool is_free);
    friend class BuddyAllocator;
    friend class MMapAllocator;
public:
    ~MallocMetadata() = default;
    void setBlockSize(size_t size);
    size_t getBlockSize() const;
    void setNext(MallocMetadata* new_next);
    MallocMetadata* getNext();
    void setPrev(MallocMetadata* new_prev);
    MallocMetadata* getPrev();
    void setIsFree(bool new_is_free);
    bool isFree() const;
};

MallocMetadata::MallocMetadata(int cookie, size_t size, bool is_free):
cookie(cookie), total_block_size(size), is_free(is_free), next(nullptr), prev(nullptr){}


void MallocMetadata::setBlockSize(size_t size)
{
    this->total_block_size = size;
}
size_t MallocMetadata::getBlockSize() const
{
    return this->total_block_size;
}

void MallocMetadata::setNext(MallocMetadata *new_next)
{
    this->next = new_next;
}

MallocMetadata *MallocMetadata::getNext()
{
    return this->next;
}

void MallocMetadata::setPrev(MallocMetadata *new_prev)
{
    this->prev = new_prev;
}

MallocMetadata *MallocMetadata::getPrev()
{
    return this->prev;
}

void MallocMetadata::setIsFree(bool new_is_free)
{
    this->is_free = new_is_free;
}

bool MallocMetadata::isFree() const
{
    return this->is_free;
}


class BuddyAllocator
{
private:
    int cookie;
    MallocMetadata* ordersArray[MAX_ORDER+1];
    bool is_first_allocation;
    intptr_t free_blocks_start_address;
    intptr_t offset;
    size_t num_of_allocated_blocks; // = num_of_meta_data_blocks
    size_t num_of_bytes_in_allocated_blocks;
    size_t num_of_allocated_blocks_that_are_free;
    size_t num_of_bytes_in_allocated_blocks_that_are_free;

    explicit BuddyAllocator(int cookie);
    friend class MemoryManager;
public:
    ~BuddyAllocator() = default; // should we do here sbrk in order to delete all the space we allocated? return the pointer to where it was before? no  - no need to narrow down
                                //might be a problem because someone might allocate after it
    // getters and setters
    bool isFirstAllocation() const;

    //  ~~~~~~~~~~~~~ general static methods ~~~~~~~~~~~~~~
    static unsigned long next_power_of_two(unsigned long num);
    static int convertSizeToOrder(size_t size);
    static size_t convertOrderToSize(int order);

    // ~~~~~~~~~~~~~ methods for malloc ~~~~~~~~~~~~~~
    void initFirstFreeBlocks();
    bool isFreeBlockInOrder(int order);
    MallocMetadata* removeFreeBlockFromStartOfOrder(int order);
    MallocMetadata* splitBlock(MallocMetadata *block_to_split);
    void insertBlockToOrder(MallocMetadata *block, int order);
    MallocMetadata* recFreeBlockLookup(int current_order, int desired_order);

    // ~~~~~~~~~~~~~ methods for free ~~~~~~~~~~~~~~
    MallocMetadata* getBuddyBlock(MallocMetadata *block, int current_order);
    MallocMetadata* mergeBuddies(MallocMetadata *block, MallocMetadata *buddy, int current_order);
    void recMergeBuddyBlocks(MallocMetadata *block, int current_order);

    // ~~~~~~~~~~~~~ methods for realloc ~~~~~~~~~~~~~~
    //MallocMetadata* recMergeBuddyBlocks2(MallocMetadata *block, int block_order, int requested_order);
    bool canReallocByMerging(MallocMetadata *block, int block_order, int requested_order);
    void* reallocByMerging(MallocMetadata *block, int block_order, int requested_order, void *oldp, size_t size_to_copy);

    // ~~~~~~~~~~~~~ statistic related ~~~~~~~~~~~~~~
    size_t getNumOfAllocatedBlocks() const;
    void incNumOfAllocatedBlocksBy(size_t num_of_blocks);
    void decNumOfAllocatedBlocksBy(size_t num_of_blocks);
    size_t getNumOfBytesInAllocatedBlocks() const;
    void incNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes);
    void decNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes);
    size_t getNumOfAllocatedBlocksThatAreFree() const;
    void incNumOfAllocatedBlocksThatAreFreeBy(size_t num_of_blocks);
    void decNumOfAllocatedBlocksThatAreFreeBy(size_t num_of_blocks);
    size_t getNumOfBytesInAllocatedBlocksThatAreFree() const;
    void incNumOfBytesInAllocatedBlocksThatAreFreeBy(size_t num_of_bytes);
    void decNumOfBytesInAllocatedBlocksThatAreFreeBy(size_t num_of_bytes);

    void checkOverFlow(MallocMetadata* md) const;
};

BuddyAllocator::BuddyAllocator(int cookie) : cookie(cookie),ordersArray{}, is_first_allocation(true), free_blocks_start_address(0), offset(0),
                                   num_of_allocated_blocks(0),
                                   num_of_bytes_in_allocated_blocks(0), num_of_allocated_blocks_that_are_free(0),
                                   num_of_bytes_in_allocated_blocks_that_are_free(0){
}

// ~~~~~~~~~~~ getters and setters ~~~~~~~~~~~~~~

bool BuddyAllocator::isFirstAllocation() const
{
    return this->is_first_allocation;
}

//  ~~~~~~~~~~~~~ general static methods ~~~~~~~~~~~~~~

unsigned long BuddyAllocator::next_power_of_two(unsigned long num)
{
    /*unsigned long closest_power = 1;
    while (num > closest_power)
    {
        closest_power *= 2;
    }
    return closest_power;
    */
    auto x = static_cast<unsigned long>(pow(2,ceil(std::log2(num))));
    return (x > MINIMAL_BLOCK_SIZE) ? x : MINIMAL_BLOCK_SIZE;

}

int BuddyAllocator::convertSizeToOrder(size_t size)
{
    return static_cast<int>(std::log2(size/MINIMAL_BLOCK_SIZE));
}

size_t BuddyAllocator::convertOrderToSize(int order)
{
    return MINIMAL_BLOCK_SIZE*std::pow(2,order);
}

// ~~~~~~~~~~~~~ methods for malloc ~~~~~~~~~~~~~~

void BuddyAllocator::initFirstFreeBlocks()
{
    void* current_brk = sbrk(0);
    auto current_address = reinterpret_cast<intptr_t>(current_brk);
    this->free_blocks_start_address = current_address + (ALIGNMENT - (current_address % ALIGNMENT)) % ALIGNMENT;
    this->offset = this->free_blocks_start_address - current_address;
    void* return_value = sbrk(offset + INIT_BLOCK_SIZE * NUM_OF_FREE_BLOCKS_AT_INIT);
    if(return_value == SBRK_FAILED)
    {
        exit(1); //TODO: what to do in this case?
    }
    MallocMetadata* prev = nullptr;
    for (int i = 0; i < NUM_OF_FREE_BLOCKS_AT_INIT ; i++)
    {
        auto* MD = reinterpret_cast<MallocMetadata*>(reinterpret_cast<char*>(this->free_blocks_start_address) + i*INIT_BLOCK_SIZE);
        *MD = MallocMetadata(this->cookie,INIT_BLOCK_SIZE, true);
        //this->checkOverFlow(MD);
        //this->checkOverFlow(prev);
        MD->setPrev(prev);
        if (prev != nullptr)
        {
            //this->checkOverFlow(MD);
            //this->checkOverFlow(prev);
            prev->setNext(MD);
        }
        //this->checkOverFlow(MD);
        //this->checkOverFlow(prev);
        prev = MD;
        if (i == 0)
        {
            //this->checkOverFlow(MD);
            this->ordersArray[MAX_ORDER] = MD;
        }
    }
    this->is_first_allocation = false;
    incNumOfAllocatedBlocksBy(NUM_OF_FREE_BLOCKS_AT_INIT);
    incNumOfBytesInAllocatedBlocksBy(NUM_OF_FREE_BLOCKS_AT_INIT* (INIT_BLOCK_SIZE - META_DATA_SIZE));
    incNumOfAllocatedBlocksThatAreFreeBy(NUM_OF_FREE_BLOCKS_AT_INIT);
    incNumOfBytesInAllocatedBlocksThatAreFreeBy(NUM_OF_FREE_BLOCKS_AT_INIT* (INIT_BLOCK_SIZE - META_DATA_SIZE));
}

bool BuddyAllocator::isFreeBlockInOrder(int order)
{
    //this->checkOverFlow(this->ordersArray[order]);
    return (this->ordersArray[order] != nullptr);
}

MallocMetadata* BuddyAllocator::removeFreeBlockFromStartOfOrder(int order)
{
    //this->checkOverFlow(this->ordersArray[order]);
    MallocMetadata* block_to_return = this->ordersArray[order];
    if (block_to_return == nullptr)
    {
        return nullptr; // there are not any free blocks in this order
    }
    this->checkOverFlow(block_to_return);
    this->checkOverFlow(block_to_return->getNext());
    this->ordersArray[order] = block_to_return->getNext();
    //this->checkOverFlow(this->ordersArray[order]);
    if (this->ordersArray[order] != nullptr)
    {
        //this->checkOverFlow(this->ordersArray[order]);
        (this->ordersArray[order])->setPrev(nullptr);
    }
    //this->checkOverFlow(block_to_return);
    block_to_return->setNext(nullptr);

    //update stats: act as the block is not allocated at all
    this->decNumOfAllocatedBlocksBy(1);
    //this->checkOverFlow(block_to_return);
    this->decNumOfBytesInAllocatedBlocksBy(block_to_return->getBlockSize()-META_DATA_SIZE);
    this->decNumOfAllocatedBlocksThatAreFreeBy(1);
    //this->checkOverFlow(block_to_return);
    this->decNumOfBytesInAllocatedBlocksThatAreFreeBy(block_to_return->getBlockSize()-META_DATA_SIZE);
    //this->checkOverFlow(block_to_return);
    return block_to_return;
}

MallocMetadata* BuddyAllocator::splitBlock(MallocMetadata* block_to_split)
{
    //this->checkOverFlow(block_to_split);
    if (block_to_split == nullptr)
    {
        exit(1);
    }
    //this->checkOverFlow(block_to_split);
    size_t new_size = block_to_split->getBlockSize()/2;
    //this->checkOverFlow(block_to_split);
    block_to_split->setBlockSize(new_size);
    auto* second_block = reinterpret_cast<MallocMetadata*>(reinterpret_cast<char*>(block_to_split) + new_size); // don't sure that it's ok
    *second_block = MallocMetadata(this->cookie,new_size, true);
    //second_block->setBlockSize(new_size);

    //update stats
    this->incNumOfAllocatedBlocksBy(2);
    this->incNumOfBytesInAllocatedBlocksBy(2*(new_size-META_DATA_SIZE));
    //this->checkOverFlow(second_block);
    return second_block;
}
void BuddyAllocator::insertBlockToOrder(MallocMetadata* block, int order)
{
    //this->checkOverFlow(this->ordersArray[order]);
    //this->checkOverFlow(block);
    MallocMetadata* curr = this->ordersArray[order];
    this->checkOverFlow(curr);
    if (curr == nullptr)
    {
        //this->checkOverFlow(this->ordersArray[order]);
        this->ordersArray[order] = block;
        //this->checkOverFlow(block);
        block->setNext(nullptr);
        //this->checkOverFlow(block);
        block->setPrev(nullptr);
    }
    else
    {
        if (block < curr) // not sure about the syntax
        {
            //this->checkOverFlow(this->ordersArray[order]);
            //this->checkOverFlow(block);
            this->ordersArray[order] = block;
            //this->checkOverFlow(curr);
            //this->checkOverFlow(block);
            block->setNext(curr);
            //this->checkOverFlow(block);
            block->setPrev(nullptr);
            //this->checkOverFlow(curr);
            //this->checkOverFlow(block);
            curr->setPrev(block);
        }
        else
        {
            bool block_is_entered = false;
            //this->checkOverFlow(curr);
            //this->checkOverFlow(curr->getNext());
            while (curr->getNext() != nullptr)
            {
                //this->checkOverFlow(curr);
                this->checkOverFlow(curr->getNext());
                if (block < curr->getNext())
                {
                    //this->checkOverFlow(curr);
                    //this->checkOverFlow(curr->getNext());
                    //this->checkOverFlow(block);
                    block->setNext(curr->getNext());
                    //this->checkOverFlow(curr);
                    //this->checkOverFlow(block);
                    block->setPrev(curr);
                    //this->checkOverFlow(curr);
                    //this->checkOverFlow(curr->getNext());
                    //this->checkOverFlow(block);
                    curr->getNext()->setPrev(block);
                    //this->checkOverFlow(curr);
                    //this->checkOverFlow(block);
                    curr->setNext(block);
                    block_is_entered = true;
                    break;
                }
                curr = curr->getNext();
            }
            if (!block_is_entered)
            {
                //this->checkOverFlow(block);
                block->setNext(nullptr);
                //this->checkOverFlow(curr);
                //this->checkOverFlow(block);
                block->setPrev(curr);
                //this->checkOverFlow(curr);
                //this->checkOverFlow(block);
                curr->setNext(block);
            }
        }
    }
    //this->checkOverFlow(block);
    block->setIsFree(true);
    // update stats
    this->incNumOfAllocatedBlocksThatAreFreeBy(1);
    //this->checkOverFlow(block);
    this->incNumOfBytesInAllocatedBlocksThatAreFreeBy(block->getBlockSize()-META_DATA_SIZE);
}

MallocMetadata* BuddyAllocator::recFreeBlockLookup(int current_order, int desired_order)
{
    if (current_order > MAX_ORDER)
    {
        return nullptr;
    }
    if (this->isFreeBlockInOrder(current_order))
    {
        MallocMetadata* block_to_split = this->removeFreeBlockFromStartOfOrder(current_order); // update stats
        if (current_order != desired_order)
        {
            //this->checkOverFlow(block_to_split);
            MallocMetadata* second_block = splitBlock(block_to_split); // block_to_split becomes the first block
            //this->checkOverFlow(second_block);
            insertBlockToOrder(second_block, current_order-1);
        }
        else
        {
            this->incNumOfAllocatedBlocksBy(1);
            //this->checkOverFlow(block_to_split);
            this->incNumOfBytesInAllocatedBlocksBy(block_to_split->getBlockSize() - META_DATA_SIZE);
        }
        //this->checkOverFlow(block_to_split);
        return block_to_split;
    }
    MallocMetadata* splitted_block = recFreeBlockLookup(current_order+1, desired_order);
    //this->checkOverFlow(splitted_block);
    if (splitted_block == nullptr)
    {
        return nullptr;
    }

    if (current_order != desired_order)
    {
        this->decNumOfAllocatedBlocksBy(1); // split will add 2 blocks
        //this->checkOverFlow(splitted_block);
        this->decNumOfBytesInAllocatedBlocksBy(splitted_block->getBlockSize()-META_DATA_SIZE); // split will add bytes for two blocks
        //this->checkOverFlow(splitted_block);
        MallocMetadata* second_block = splitBlock(splitted_block); // block_to_split becomes the first block
        //this->checkOverFlow(second_block);
        insertBlockToOrder(second_block, current_order-1);
    }
    //this->checkOverFlow(splitted_block);
    return splitted_block;

}

// ~~~~~~~~~~~~~ methods for free ~~~~~~~~~~~~~~
MallocMetadata* BuddyAllocator::getBuddyBlock(MallocMetadata* block, int current_order)
{
    size_t block_size = this->convertOrderToSize(current_order);
    //size_t block_size = block->getBlockSize();
    auto block_address = reinterpret_cast<uintptr_t>(block);
    uintptr_t buddy_address = block_address ^ block_size;
    //this->checkOverFlow(this->ordersArray[current_order]);
    MallocMetadata *curr = this->ordersArray[current_order];
    //this->checkOverFlow(curr);
    while (curr != nullptr)
    {
        this->checkOverFlow(curr);
        if (buddy_address == reinterpret_cast<uintptr_t>(curr))
        {
            //this->checkOverFlow(curr);
            return curr;
        }
        //this->checkOverFlow(curr);
        //this->checkOverFlow(curr->getNext());
        curr = curr->getNext();
    }
    return nullptr;
}

MallocMetadata* BuddyAllocator::mergeBuddies(MallocMetadata* block, MallocMetadata* buddy, int current_order)
{
    // 1. remove buddy block from list (and update stats)
    //this->checkOverFlow(buddy);
    this->checkOverFlow(buddy->getPrev());
    MallocMetadata* prev = buddy->getPrev();
    //this->checkOverFlow(buddy);
    //this->checkOverFlow(buddy->getPrev());
    this->checkOverFlow(buddy->getNext());
    MallocMetadata* next = buddy->getNext();
    //this->checkOverFlow(buddy);
    buddy->setPrev(nullptr);
    //this->checkOverFlow(buddy);
    buddy->setNext(nullptr);
    //this->checkOverFlow(prev);
    if (prev == nullptr)
    {
        // first in the list
        //this->checkOverFlow(this->ordersArray[current_order]);
        //this->checkOverFlow(next);
        this->ordersArray[current_order] = next;
    }
    else
    {
        // not first in the list
        //this->checkOverFlow(next);
        //this->checkOverFlow(prev);
        prev->setNext(next);
    }
    //this->checkOverFlow(next);
    if (next != nullptr)
    {
        //this->checkOverFlow(next);
        //this->checkOverFlow(prev);
        next->setPrev(prev);
    }

    //2. update stats
    this->decNumOfAllocatedBlocksThatAreFreeBy(1); // in recMerge - buddy and block was free and now merged is free. in relloc - only buddy was free and merged is not free
    //this->checkOverFlow(buddy);
    this->decNumOfBytesInAllocatedBlocksThatAreFreeBy(buddy->getBlockSize()-META_DATA_SIZE); // only the buddy
//    std::cout << buddy->getBlockSize()-META_DATA_SIZE << std::endl;
//    std::cout << this->getNumOfBytesInAllocatedBlocksThatAreFree() << std::endl;
    this->decNumOfAllocatedBlocksBy(1); // two blocks became 1
    // compute: 2*(buddy->getBlockSize()-META_DATA_SIZE) - 2*buddy->getBlockSize()-META_DATA_SIZE = -META_DATA_SIZE
    this->incNumOfBytesInAllocatedBlocksBy(META_DATA_SIZE); // two blocks became 1

    // 3. merge blocks + return new block
    //this->checkOverFlow(block);
    //this->checkOverFlow(buddy);
    MallocMetadata* merged_block = (block < buddy) ? block : buddy;
    //this->checkOverFlow(block);
    //this->checkOverFlow(merged_block);
    merged_block->setBlockSize(2*block->getBlockSize());
    //this->checkOverFlow(merged_block);
    merged_block->setNext(nullptr);
    //this->checkOverFlow(merged_block);
    merged_block->setPrev(nullptr);

    //this->checkOverFlow(merged_block);
    return merged_block;
}

void BuddyAllocator::recMergeBuddyBlocks(MallocMetadata* block, int current_order)
{
    if (current_order == MAX_ORDER)
    {
        //this->checkOverFlow(block);
        insertBlockToOrder(block, current_order);
        return;
    }

    //this->checkOverFlow(block);
    MallocMetadata* buddy_block = getBuddyBlock(block, current_order);
    //this->checkOverFlow(buddy_block);
    if (buddy_block == nullptr)
    {
        // insert block and update stats
        //this->checkOverFlow(block);
        insertBlockToOrder(block, current_order);
        return;
    }
    //buddy_block->printBlock();
    //this->checkOverFlow(block);
    //this->checkOverFlow(buddy_block);
    MallocMetadata* merged_block = mergeBuddies(block, buddy_block, current_order);
    //this->checkOverFlow(merged_block);
    merged_block->setIsFree(true);
    //this->checkOverFlow(merged_block);
    recMergeBuddyBlocks(merged_block, current_order + 1);
}

// ~~~~~~~~~~~~~~~~~~~~~~ methods for realloc ~~~~~~~~~~~~~~~~~~~

bool BuddyAllocator::canReallocByMerging(MallocMetadata* block, int block_order, int requested_order)
{
    if (block_order == requested_order)
    {
        return true;
    }
    //this->checkOverFlow(block);
    MallocMetadata* buddy_block = getBuddyBlock(block, block_order); // need to generalize getBuddyBlock
    //this->checkOverFlow(buddy_block);
    if (buddy_block == nullptr)
    {
        return false;
    }
    //this->checkOverFlow(block);
    //this->checkOverFlow(buddy_block);
    MallocMetadata* merged_block_md = (block < buddy_block) ? block : buddy_block;
    //this->checkOverFlow(merged_block_md);
    return canReallocByMerging(merged_block_md, block_order+1, requested_order);

}
void* BuddyAllocator::reallocByMerging(MallocMetadata* block, int block_order, int requested_order, void* oldp, size_t size_to_copy)
{
    if (block_order == requested_order)
    {
        // need to understand if we might override something?
        // need to understand if we might copy something that it's too big (because we give sizes that are power of 2)
        //this->checkOverFlow(block);
        //this->checkOverFlow(GET_METADATA(oldp));
        std::memmove(GET_USER_PTR(block), oldp, size_to_copy);
        //this->checkOverFlow(block);
        return GET_USER_PTR(block);
    }
    //this->checkOverFlow(block);
    MallocMetadata* buddy_block = getBuddyBlock(block, block_order); // need to generalize getBuddyBlock
    // this function is called after BuddyAllocator::canReallocByMerging, therefore we know that getBuddyBlock must succeed
    //this->checkOverFlow(block);
    //this->checkOverFlow(buddy_block);
    MallocMetadata* merged_block = mergeBuddies(block, buddy_block, block_order);
    //this->checkOverFlow(merged_block);
    merged_block->setIsFree(false);
    //this->checkOverFlow(merged_block);
    //this->checkOverFlow(GET_METADATA(oldp));
    return reallocByMerging(merged_block, block_order + 1, requested_order, oldp, size_to_copy);
}

// ~~~~~~~~~~~~~ statistic related ~~~~~~~~~~~~~~

size_t BuddyAllocator::getNumOfAllocatedBlocks() const
{
    return this->num_of_allocated_blocks;
}

void BuddyAllocator::incNumOfAllocatedBlocksBy(size_t num_of_blocks)
{
    this->num_of_allocated_blocks += num_of_blocks;
}

void BuddyAllocator::decNumOfAllocatedBlocksBy(size_t num_of_blocks)
{
    this->num_of_allocated_blocks -= num_of_blocks;
}

size_t BuddyAllocator::getNumOfBytesInAllocatedBlocks() const
{
    return num_of_bytes_in_allocated_blocks;
}

void BuddyAllocator::incNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes)
{
    this->num_of_bytes_in_allocated_blocks += num_of_bytes;
}

void BuddyAllocator::decNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes)
{
    this->num_of_bytes_in_allocated_blocks -= num_of_bytes;
}

size_t BuddyAllocator::getNumOfAllocatedBlocksThatAreFree() const
{
    return this->num_of_allocated_blocks_that_are_free;
}
void BuddyAllocator::incNumOfAllocatedBlocksThatAreFreeBy(size_t num_of_blocks)
{
    this->num_of_allocated_blocks_that_are_free += num_of_blocks;
}

void BuddyAllocator::decNumOfAllocatedBlocksThatAreFreeBy(size_t num_of_blocks)
{
    this->num_of_allocated_blocks_that_are_free -= num_of_blocks;
}

size_t BuddyAllocator::getNumOfBytesInAllocatedBlocksThatAreFree() const
{
    return num_of_bytes_in_allocated_blocks_that_are_free;
}

void BuddyAllocator::incNumOfBytesInAllocatedBlocksThatAreFreeBy(size_t num_of_bytes)
{
    this->num_of_bytes_in_allocated_blocks_that_are_free += num_of_bytes;
}

void BuddyAllocator::decNumOfBytesInAllocatedBlocksThatAreFreeBy(size_t num_of_bytes)
{
    this->num_of_bytes_in_allocated_blocks_that_are_free -= num_of_bytes;
}

void BuddyAllocator::checkOverFlow(MallocMetadata *md) const {
    if (md != nullptr && this->cookie != md->cookie)
    {
        exit(0xdeadbeef);
    }
}

class MMapAllocator
{
private:
    int cookie{};
    MallocMetadata* head{};
    MallocMetadata* tail{};
    size_t num_of_allocated_blocks{}; // = num_of_meta_data_blocks
    size_t num_of_bytes_in_allocated_blocks{};

    explicit MMapAllocator(int cookie);
    friend class MemoryManager;
public:
    ~MMapAllocator() = default;
    void setHead(MallocMetadata* new_head);
    MallocMetadata* getHead();
    void setTail(MallocMetadata* new_tail);
    MallocMetadata* getTail();
    MallocMetadata CreateMallocMetaData(size_t user_size, bool is_free) const;
    void RemoveFromList(MallocMetadata* md);
    // ~~~~~~~~~~~~~ statistic related ~~~~~~~~~~~~~~
    size_t getNumOfAllocatedBlocks() const;
    void incNumOfAllocatedBlocksBy(size_t num_of_blocks);
    void decNumOfAllocatedBlocksBy(size_t num_of_blocks);
    size_t getNumOfBytesInAllocatedBlocks() const;
    void incNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes);
    void decNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes);

    void checkOverFlow(MallocMetadata* md) const;
};

MMapAllocator::MMapAllocator(int cookie): cookie(cookie), head(nullptr), tail(nullptr),
num_of_allocated_blocks(0), num_of_bytes_in_allocated_blocks(0){}

void MMapAllocator::setHead(MallocMetadata *new_head) {
    this->checkOverFlow(new_head);
    this->checkOverFlow(this->head);
    this->head = new_head;
}

MallocMetadata *MMapAllocator::getHead() {
    this->checkOverFlow(this->head);
    return this->head;
}

void MMapAllocator::setTail(MallocMetadata *new_tail) {
    this->checkOverFlow(new_tail);
    this->checkOverFlow(this->tail);
    this->tail = new_tail;
}

MallocMetadata *MMapAllocator::getTail() {
    this->checkOverFlow(this->tail);
    return this->tail;
}

MallocMetadata MMapAllocator::CreateMallocMetaData(size_t user_size, bool is_free) const {
    return {this->cookie, user_size + META_DATA_SIZE, is_free};
}

void MMapAllocator::RemoveFromList(MallocMetadata *md) {
    this->checkOverFlow(this->head);
    //this->checkOverFlow(md);
    if(this->head == md)
    {
        this->checkOverFlow(this->tail);
        //this->checkOverFlow(md);
        if(this->tail == md)
        {
            //this->checkOverFlow(this->head);
            this->setHead(nullptr);
            //this->checkOverFlow(this->tail);
            this->setTail(nullptr);
        }
        else
        {
            //this->checkOverFlow(md);
            this->checkOverFlow(md->getNext());
            md->getNext()->setPrev(nullptr);

            //this->checkOverFlow(this->head);
            //this->checkOverFlow(md->getNext());
            this->setHead(md->getNext());
        }
    }
    else
    {
        this->checkOverFlow(this->tail);
        //this->checkOverFlow(md);
        if(this->tail == md)
        {
            //this->checkOverFlow(md);
            this->checkOverFlow(md->getPrev());
            md->getPrev()->setNext(nullptr);

            //this->checkOverFlow(this->tail);
            //this->checkOverFlow(md->getPrev());
            this->setTail(md->getPrev());
        }
        else
        {
            //this->checkOverFlow(md);
            this->checkOverFlow(md->getNext());
            this->checkOverFlow(md->getPrev());
            md->getNext()->setPrev(md->getPrev());

            //this->checkOverFlow(md);
            //this->checkOverFlow(md->getNext());
            //this->checkOverFlow(md->getPrev());
            md->getPrev()->setNext(md->getNext());
        }
    }
}

size_t MMapAllocator::getNumOfAllocatedBlocks() const {
    return this->num_of_allocated_blocks;
}

void MMapAllocator::incNumOfAllocatedBlocksBy(size_t num_of_blocks) {
    this->num_of_allocated_blocks += num_of_blocks;
}

void MMapAllocator::decNumOfAllocatedBlocksBy(size_t num_of_blocks) {
    this->num_of_allocated_blocks -= num_of_blocks;
}

size_t MMapAllocator::getNumOfBytesInAllocatedBlocks() const {
    return this->num_of_bytes_in_allocated_blocks;
}

void MMapAllocator::incNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes) {
    this->num_of_bytes_in_allocated_blocks += num_of_bytes;
}

void MMapAllocator::decNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes) {
    this->num_of_bytes_in_allocated_blocks -= num_of_bytes;
}

void MMapAllocator::checkOverFlow(MallocMetadata *md) const {
    if (md != nullptr && this->cookie != md->cookie)
    {
        exit(0xdeadbeef);
    }
}

class MemoryManager
{
private:
    int cookie;
    BuddyAllocator buddy_allocator;
    MMapAllocator mmap_allocator;
public:
    MemoryManager();
    ~MemoryManager() = default;
    BuddyAllocator* getBuddyAllocator();
    MMapAllocator* getMMapAllocator();
    size_t getNumOfAllocatedBlocks() const;
    size_t getNumOfBytesInAllocatedBlocks() const;
    size_t getNumOfAllocatedBlocksThatAreFree() const;
    size_t getNumOfBytesInAllocatedBlocksThatAreFree() const;
};

MemoryManager::MemoryManager(): cookie(rand()), buddy_allocator(cookie), mmap_allocator(cookie){}

BuddyAllocator* MemoryManager::getBuddyAllocator() {
    return &(this->buddy_allocator);
}

MMapAllocator* MemoryManager::getMMapAllocator() {
    return &(this->mmap_allocator);
}

size_t MemoryManager::getNumOfAllocatedBlocks() const
{
    return this->buddy_allocator.getNumOfAllocatedBlocks() + this->mmap_allocator.getNumOfAllocatedBlocks();
}

size_t MemoryManager::getNumOfBytesInAllocatedBlocks() const
{
    return this->buddy_allocator.getNumOfBytesInAllocatedBlocks() + this->mmap_allocator.getNumOfBytesInAllocatedBlocks();
}

size_t MemoryManager::getNumOfAllocatedBlocksThatAreFree() const
{
    return this->buddy_allocator.getNumOfAllocatedBlocksThatAreFree();
}

size_t MemoryManager::getNumOfBytesInAllocatedBlocksThatAreFree() const
{
    return this->buddy_allocator.getNumOfBytesInAllocatedBlocksThatAreFree();
}

MemoryManager mem_man;
//BuddyAllocator buddy_allocator = mem_man.getBuddyAllocator();
// ~~~~~~~~~~~~~~ IMPLEMENT MALLOC, FREE, CALLOC, REALLOC ~~~~~~~~~~~~~~~~~~~~~~

void* smalloc(size_t size)
{
    BuddyAllocator* buddy_allocator = mem_man.getBuddyAllocator();
    if (buddy_allocator->isFirstAllocation())
    {
        buddy_allocator->initFirstFreeBlocks();
        //buddy_allocator->printInfo();
    }

    if (size == 0 || size > MAX_SIZE)
    {
        return NULL; //need to return NULL or nullptr?
    }
    MallocMetadata* block_to_use = nullptr;
    if(size + META_DATA_SIZE > MAXIMAL_BUDDY_BLOCK)
    {
        //mmap
        MMapAllocator* mmap_allocator = mem_man.getMMapAllocator();
        block_to_use = (MallocMetadata*) (mmap(NULL, size + META_DATA_SIZE,
                                               PROT_READ | PROT_WRITE,
                                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if(block_to_use == MAP_FAILED)
        {
            return NULL;
        }
        *block_to_use = mmap_allocator->CreateMallocMetaData(size,false);

        if(mmap_allocator->getHead() == nullptr)
        {
            //mmap_allocator->checkOverFlow(block_to_use);
            //mmap_allocator->checkOverFlow(mmap_allocator->getHead());
            mmap_allocator->setHead(block_to_use);

            //mmap_allocator->checkOverFlow(block_to_use);
            //mmap_allocator->checkOverFlow(mmap_allocator->getTail());
            mmap_allocator->setTail(block_to_use);
        }
        else
        {
            mmap_allocator->checkOverFlow(mmap_allocator->getHead());
            mmap_allocator->checkOverFlow(mmap_allocator->getTail());
            //mmap_allocator->checkOverFlow(block_to_use);
            block_to_use->setPrev(mmap_allocator->getTail());

            //mmap_allocator->checkOverFlow(block_to_use);
            //mmap_allocator->checkOverFlow(mmap_allocator->getTail());
            mmap_allocator->getTail()->setNext(block_to_use);

            //mmap_allocator->checkOverFlow(block_to_use);
            //mmap_allocator->checkOverFlow(mmap_allocator->getTail());
            mmap_allocator->setTail(block_to_use);
        }

        // add to stats - new block was allocated successfully
        mmap_allocator->incNumOfAllocatedBlocksBy(1);
        mmap_allocator->incNumOfBytesInAllocatedBlocksBy(size);
    }
    else
    {
        //buddy_allocator
        size_t block_size = buddy_allocator->next_power_of_two(size + META_DATA_SIZE);
        int order = buddy_allocator->convertSizeToOrder(block_size);

        // need to check what to do if there is no available size
        block_to_use = buddy_allocator->recFreeBlockLookup(order, order);
        //buddy_allocator->checkOverFlow(block_to_use);
        if (block_to_use == nullptr)
        {
            return NULL;
        }
        // update fields of block_to_use - size should already be updated
        //buddy_allocator->checkOverFlow(block_to_use);
        block_to_use->setIsFree(false);
        //buddy_allocator->checkOverFlow(block_to_use);
        block_to_use->setNext(nullptr);
        //buddy_allocator->checkOverFlow(block_to_use);
        block_to_use->setPrev(nullptr);
    }
    //buddy_allocator->checkOverFlow(block_to_use);
    return (block_to_use == nullptr)? NULL:GET_USER_PTR(block_to_use);
}


void* scalloc(size_t num, size_t size)
{
    void* allocated_block = smalloc(num * size);
    //buddy_allocator->checkOverFlow(GET_METADATA(allocated_block));
    if (allocated_block == nullptr)
    {
        return NULL;
    }
    //buddy_allocator->checkOverFlow(GET_METADATA(allocated_block));
    std::memset(allocated_block, 0, num * size); // TODO: should we set all bytes in the block to 0 or just the amount the user asked for
    //buddy_allocator->checkOverFlow(GET_METADATA(allocated_block));
    return allocated_block;
    // relevant stats are added inside smalloc
}

void sfree(void* p)
{
    BuddyAllocator* buddy_allocator = mem_man.getBuddyAllocator();
    //buddy_allocator->checkOverFlow(GET_METADATA(p));
    if(p == NULL)
    {
        return;
    }
    MallocMetadata* metadata = GET_METADATA(p);
    buddy_allocator->checkOverFlow(metadata);
    //buddy_allocator->checkOverFlow(metadata);
    if (metadata->isFree()) return;
    //buddy_allocator->checkOverFlow(metadata);
    if(metadata->getBlockSize() > MAXIMAL_BUDDY_BLOCK)
    {
        //mmap
        MMapAllocator* mmap_allocator = mem_man.getMMapAllocator();
        //mmap_allocator->checkOverFlow(metadata);
        mmap_allocator->RemoveFromList(metadata);

        //mmap_allocator->checkOverFlow(metadata);
        size_t block_size = metadata->getBlockSize();

        mmap_allocator->decNumOfAllocatedBlocksBy(1);
        mmap_allocator->decNumOfBytesInAllocatedBlocksBy(block_size - META_DATA_SIZE);

        //mmap_allocator->checkOverFlow(metadata);
        void* block_to_munmap = static_cast<void*>(metadata);
        //mmap_allocator->checkOverFlow(static_cast<MallocMetadata*> (block_to_munmap));
        if(munmap(block_to_munmap, block_size) != 0)
        {
            exit(1);
        }
    }
    else
    {
        //buddy_allocator
        //buddy_allocator->checkOverFlow(metadata);
        int order = buddy_allocator->convertSizeToOrder(metadata->getBlockSize());
        //buddy_allocator->checkOverFlow(metadata);
        buddy_allocator->recMergeBuddyBlocks(metadata, order);
    }

}

void* srealloc(void* oldp, size_t size)
{
    BuddyAllocator* buddy_allocator = mem_man.getBuddyAllocator();
    //buddy_allocator->checkOverFlow(GET_METADATA(oldp));
    if (oldp == NULL)
    {
        return smalloc(size); // stats were added inside smalloc for this case
    }
    // realloc
    if (size == 0 || size > MAX_SIZE)
    {
        return NULL; //need to return NULL or nullptr?
    }

    //buddy_allocator->checkOverFlow(GET_METADATA(oldp));
    MallocMetadata* oldp_md = GET_METADATA(oldp);
    if (oldp_md == NULL)
    {
        return NULL;
    }
    buddy_allocator->checkOverFlow(oldp_md);
    if (oldp_md->isFree())
    {
        return NULL; // TODO: check if needed in tests and delete after
    }
    //buddy_allocator->checkOverFlow(oldp_md);
    if(oldp_md->getBlockSize() > MAXIMAL_BUDDY_BLOCK)
    {
        //mmap
        //mmap_allocator->checkOverFlow(oldp_md);
        if(oldp_md->getBlockSize() == size + META_DATA_SIZE)
        {
            //mmap_allocator->checkOverFlow(GET_METADATA(oldp));
            return oldp;
        }
        //mmap_allocator->checkOverFlow(oldp_md);
        size_t bytes_to_copy = oldp_md->getBlockSize()-META_DATA_SIZE;
        void* newp = smalloc(size);
        //mmap_allocator->checkOverFlow(GET_METADATA(oldp));
        //mmap_allocator->checkOverFlow(GET_METADATA(newp));
        std::memmove(newp, oldp, bytes_to_copy);
        //mmap_allocator->checkOverFlow(GET_METADATA(oldp));
        sfree(oldp);
        //mmap_allocator->checkOverFlow(GET_METADATA(newp));
        return newp;
    }
    else
    {
        //buddy_allocator
        //buddy_allocator->checkOverFlow(oldp_md);
        if (oldp_md->getBlockSize() >= size+META_DATA_SIZE)
        {
            // stats hasn't changed in this case
            //buddy_allocator->checkOverFlow(GET_METADATA(oldp));
            return oldp;
        }
        //buddy_allocator->checkOverFlow(oldp_md);
        int current_order = buddy_allocator->convertSizeToOrder(oldp_md->getBlockSize());
        size_t requested_block_size = buddy_allocator->next_power_of_two(size+META_DATA_SIZE);
        int requested_order = buddy_allocator->convertSizeToOrder(requested_block_size);
        //buddy_allocator->checkOverFlow(oldp_md);
        if (buddy_allocator->canReallocByMerging(oldp_md,current_order,requested_order))
        {
            //buddy_allocator->checkOverFlow(oldp_md);
            //buddy_allocator->checkOverFlow(GET_METADATA(oldp));
            return buddy_allocator->reallocByMerging(oldp_md, current_order, requested_order, oldp, oldp_md->getBlockSize()-META_DATA_SIZE);
        }
        else
        {

            // TODO: can we assume that there is must be a different block in the required size
            //TODO: what if first realloc before malloc, is it considered as malloc for init requirement
            //buddy_allocator->checkOverFlow(oldp_md);
            size_t bytes_to_copy = oldp_md->getBlockSize()-META_DATA_SIZE;
            void* newp = smalloc(size);
            //buddy_allocator->checkOverFlow(GET_METADATA(oldp));
            //buddy_allocator->checkOverFlow(GET_METADATA(newp));
            std::memmove(newp, oldp, bytes_to_copy);
            //buddy_allocator->checkOverFlow(GET_METADATA(oldp));
            sfree(oldp);
            //buddy_allocator->checkOverFlow(GET_METADATA(newp));
            return newp;
        }
    }
}
// ~~~~~~~~~~~~~~~~~~~~~~~ IMPLEMENT STATISTICS ~~~~~~~~~~~~~~~~~~~
size_t _num_free_blocks()
{
    return mem_man.getNumOfAllocatedBlocksThatAreFree();
}

size_t _num_free_bytes()
{
    return mem_man.getNumOfBytesInAllocatedBlocksThatAreFree();
}

size_t _num_allocated_blocks()
{
    return mem_man.getNumOfAllocatedBlocks();
}

size_t _num_allocated_bytes()
{
    return mem_man.getNumOfBytesInAllocatedBlocks();
}

size_t _num_meta_data_bytes()
{
    return mem_man.getNumOfAllocatedBlocks() * META_DATA_SIZE;
}

size_t _size_meta_data()
{
    return META_DATA_SIZE;
}
