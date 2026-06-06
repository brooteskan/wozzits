#define WZ_PLATFORM_ASSERT(cond, msg)                                                            \
    do                                                                                           \
    {                                                                                            \
        if (!(cond))                                                                             \
        {                                                                                        \
            WZ::log_error(std::string("Assertion failed: ") + #cond + " - " + (msg ? msg : "")); \
            assert(cond);                                                                        \
        }                                                                                        \
    } while (0)

#define CONTAINER_ASSERT(cond) \
    do                       \
    {                        \
        if (!(cond))         \
            std::abort();    \
    } while (0)