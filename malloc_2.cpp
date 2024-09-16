#include <unistd.h>
#include <cstring>
#define SBRK_FAILED (void *) (-1)
#define MAX_SIZE 100000000
#define META_DATA_SIZE sizeof(MallocMetadata)
#define GET_METADATA(p) ((MallocMetadata *) ((char *) p - META_DATA_SIZE))
#define GET_USER_PTR(p) ((void*)((char*)(p) + META_DATA_SIZE))
#define ALLOC_SIZE(s) long(s + META_DATA_SIZE)

class MallocMetadata
{
private:
    size_t user_size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
public:
    explicit MallocMetadata(size_t user_size);
    ~MallocMetadata() = default;
    size_t getUserSize() const;
    void setNext(MallocMetadata* new_next);
    MallocMetadata* getNext();
    void setPrev(MallocMetadata* new_prev);
    void setIsFree(bool new_is_free);
    bool isFree() const;
};

MallocMetadata::MallocMetadata(size_t user_size): user_size(user_size), is_free(false),
                                                  next(nullptr), prev(nullptr) {}

size_t MallocMetadata::getUserSize() const {
    return this->user_size;
}

void MallocMetadata::setNext(MallocMetadata *new_next) {
    this->next = new_next;
}

MallocMetadata *MallocMetadata::getNext() {
    return this->next;
}

void MallocMetadata::setPrev(MallocMetadata *new_prev) {
    this->prev = new_prev;
}

void MallocMetadata::setIsFree(bool new_is_free) {
    this->is_free = new_is_free;
}

bool MallocMetadata::isFree() const {
    return this->is_free;
}

class MemoryManager
{
private:
    MallocMetadata* head;
    MallocMetadata* tail;
    size_t num_of_allocated_blocks; // = num_of_meta_data_blocks
    size_t num_of_bytes_in_allocated_blocks;
    size_t num_of_allocated_blocks_that_are_free;
    size_t num_of_bytes_in_allocated_blocks_that_are_free;

public:
    MemoryManager();
    ~MemoryManager() =default;
    void setHead(MallocMetadata* new_head);
    MallocMetadata* getHead();
    void setTail(MallocMetadata* new_tail);
    MallocMetadata* getTail();
    size_t getNumOfAllocatedBlocks() const;
    void incNumOfAllocatedBlocks();
    size_t getNumOfBytesInAllocatedBlocks() const;
    void incNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes);
    size_t getNumOfAllocatedBlocksThatAreFree() const;
    void incNumOfAllocatedBlocksThatAreFree();
    void decNumOfAllocatedBlocksThatAreFree();
    size_t getNumOfBytesInAllocatedBlocksThatAreFree() const;
    void incNumOfBytesInAllocatedBlocksThatAreFreeBy(size_t num_of_bytes);
    void decNumOfBytesInAllocatedBlocksThatAreFreeBy(size_t num_of_bytes);
};

MemoryManager::MemoryManager(): head(nullptr), tail(nullptr), num_of_allocated_blocks(0),
        num_of_bytes_in_allocated_blocks(0), num_of_allocated_blocks_that_are_free(0),
                                num_of_bytes_in_allocated_blocks_that_are_free(0) {}

MallocMetadata *MemoryManager::getHead() {
    return this->head;
}

void MemoryManager::setTail(MallocMetadata *new_tail) {
    this->tail = new_tail;
}

MallocMetadata *MemoryManager::getTail() {
    return this->tail;
}

void MemoryManager::setHead(MallocMetadata *new_head) {
    this->head = new_head;
}

size_t MemoryManager::getNumOfAllocatedBlocks() const {
    return this->num_of_allocated_blocks;
}

void MemoryManager::incNumOfAllocatedBlocks()
{
    this->num_of_allocated_blocks++;
}

size_t MemoryManager::getNumOfBytesInAllocatedBlocks() const {
    return num_of_bytes_in_allocated_blocks;
}

void MemoryManager::incNumOfBytesInAllocatedBlocksBy(size_t num_of_bytes) {
    this->num_of_bytes_in_allocated_blocks += num_of_bytes;
}

size_t MemoryManager::getNumOfAllocatedBlocksThatAreFree() const {
    return this->num_of_allocated_blocks_that_are_free;
}

void MemoryManager::incNumOfAllocatedBlocksThatAreFree() {
    this->num_of_allocated_blocks_that_are_free++;
}

void MemoryManager::decNumOfAllocatedBlocksThatAreFree() {
    this->num_of_allocated_blocks_that_are_free--;
}

size_t MemoryManager::getNumOfBytesInAllocatedBlocksThatAreFree() const {
    return num_of_bytes_in_allocated_blocks_that_are_free;
}

void MemoryManager::incNumOfBytesInAllocatedBlocksThatAreFreeBy(size_t num_of_bytes) {
    this->num_of_bytes_in_allocated_blocks_that_are_free += num_of_bytes;
}

void MemoryManager::decNumOfBytesInAllocatedBlocksThatAreFreeBy(size_t num_of_bytes) {
    this->num_of_bytes_in_allocated_blocks_that_are_free -= num_of_bytes;
}


MemoryManager manager = MemoryManager();


MallocMetadata* lookForAvailableBlock(size_t user_size)
{
    MallocMetadata* itr = manager.getHead();
    while(itr != nullptr)
    {
        if(itr->getUserSize() >= user_size && itr->isFree())
        {
            return itr;
        }
        itr = itr->getNext();
    }
    return itr;
}

void* smalloc(size_t size)
{
    if (size == 0 || size > MAX_SIZE)
    {
        return NULL; //need to return NULL or nullptr?
    }

    MallocMetadata* block_to_use = lookForAvailableBlock(size);
    if(block_to_use == nullptr)
    {
        block_to_use = (MallocMetadata*) (sbrk(ALLOC_SIZE(size)));
        if(block_to_use == SBRK_FAILED)
        {
            return NULL;
        }
        *block_to_use = MallocMetadata(size);
        if(manager.getHead() == nullptr)
        {
            manager.setHead(block_to_use);
            manager.setTail(block_to_use);
        }
        else
        {
            block_to_use->setPrev(manager.getTail());
            manager.getTail()->setNext(block_to_use);
            manager.setTail(block_to_use);
        }

        // add to stats - new block was allocated successfully
        manager.incNumOfAllocatedBlocks();
        manager.incNumOfBytesInAllocatedBlocksBy(size);
    }
    else
    {
        block_to_use->setIsFree(false);
        // add to stats - free block was retaken
        manager.decNumOfAllocatedBlocksThatAreFree();
        manager.decNumOfBytesInAllocatedBlocksThatAreFreeBy(block_to_use->getUserSize());
    }
    return GET_USER_PTR(block_to_use);
}

void sfree(void* p)
{
    if(p == NULL)
    {
        return;
    }
    MallocMetadata* metadata = GET_METADATA(p);
    if (metadata->isFree()) return;
    metadata->setIsFree(true);

    // add stats - block was successfully freed
    manager.incNumOfAllocatedBlocksThatAreFree();
    manager.incNumOfBytesInAllocatedBlocksThatAreFreeBy(metadata->getUserSize());
}

void* scalloc(size_t num, size_t size)
{
    void *free_block = smalloc(num * size);
    if (free_block == nullptr) {
        return NULL;
    }
    std::memset(free_block, 0, num * size);
    return free_block;
    // relevant stats are added inside smalloc
}

void* srealloc(void* oldp, size_t size)
{
    if (size == 0 || size > MAX_SIZE)
    {
        return NULL; //need to return NULL or nullptr?
    }
    if(oldp == NULL)
    {
        return smalloc(size); // stats were added inside smalloc for this case
    }

    MallocMetadata* oldp_md = GET_METADATA(oldp);
    if (oldp_md->isFree())
    {
        return NULL; // TODO: check if needed in tests and delete after
    }
    if(oldp_md->getUserSize() >= size)
    {
        // stats hasn't changed in this case
        return oldp;
    }
    void* newp = smalloc(size); // changed stats according to the block that was created
    if(newp == nullptr)
    {
        return NULL;
    }
    std::memmove(newp, oldp, oldp_md->getUserSize());
    sfree(oldp); // stats has changed as part of free
    return newp;
}

size_t _num_free_blocks()
{
    return manager.getNumOfAllocatedBlocksThatAreFree();
}

size_t _num_free_bytes()
{
    return manager.getNumOfBytesInAllocatedBlocksThatAreFree();
}

size_t _num_allocated_blocks()
{
    return manager.getNumOfAllocatedBlocks();
}

size_t _num_allocated_bytes()
{
    return manager.getNumOfBytesInAllocatedBlocks();
}

size_t _num_meta_data_bytes()
{
    return manager.getNumOfAllocatedBlocks() * META_DATA_SIZE;
}

size_t _size_meta_data()
{
    return META_DATA_SIZE;
}
