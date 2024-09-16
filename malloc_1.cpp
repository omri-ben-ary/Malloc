#include <unistd.h>
#define SBRK_FAILED (void *) (-1)
#define MAX_SIZE 100000000
void* smalloc (size_t size)
{
    if (size == 0 || size > MAX_SIZE)
    {
        return nullptr; //need to return NULL or nullptr?
    }
    void* return_value = sbrk(long (size));
    if(return_value == SBRK_FAILED)
    {
        return nullptr; //need to return NULL or nullptr?
    }
    return return_value;
}
