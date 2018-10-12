#ifndef NDEBUG
extern void __assert_func(const char * file, int line, const char * func, const char * failedexpr);
#define assert(statement) ((statement) ? (void)0 : __assert_func(__FILE__, __LINE__, __FUNCTION__, #statement))
#else
#define assert(statement) ((void)0)
#endif
