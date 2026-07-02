#define CONTAINER_ASSERT(cond) \
    do                       \
    {                        \
        if (!(cond))         \
            std::abort();    \
    } while (0)