#if defined(_WIN32)
#define WZ_TEST_EXPORT __declspec(dllexport)
#else
#define WZ_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" WZ_TEST_EXPORT int wz_not_the_behavior_symbol()
{
    return 7;
}
