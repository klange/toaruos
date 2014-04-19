#ifndef __ARCH_CC_H__
#define __ARCH_CC_H__

#include <system.h>
#include <logging.h>
#include <stdint.h>

#define LWIP_TIMEVAL_PRIVATE 0
#define BYTE_ORDER LITTLE_ENDIAN

typedef uint8_t u8_t;
typedef  int8_t s8_t;
typedef uint16_t u16_t;
typedef  int16_t s16_t;
typedef uint32_t u32_t;
typedef  int32_t s32_t;
typedef uintptr_t mem_ptr_t;

#define X8_F  "2x"
#define U16_F "d"
#define S16_F "d"
#define X16_F "4x"
#define U32_F "d"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "d"

#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#define FOOBAR(...) do {debug_print (NOTICE, "", __VA_ARGS__);} while(0)
#define LWIP_PLATFORM_DIAG(...)  FOOBAR __VA_ARGS__
#define LWIP_PLATFORM_ASSERT(x) do {debug_print(ERROR, "Assertion \"%s\" failed at line %d in %s\n", \
                                     x, __LINE__, __FILE__); kexit(1); } while(0)

#define LWIP_PLATFORM_BYTESWAP 1
#define LWIP_PLATFORM_HTONS(x) ( (((u16_t)(x))>>8) | (((x)&0xFF)<<8) )
#define LWIP_PLATFORM_HTONL(x) ( (((u32_t)(x))>>24) | (((x)&0xFF0000)>>8) \
                              | (((x)&0xFF00)<<8) | (((x)&0xFF)<<24) )

#define LWIP_RAND() ((u32_t)krand())

extern void * heap_external;


#endif /* __ARCH_CC_H__ */
