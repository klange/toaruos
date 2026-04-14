#pragma once

#ifdef __cplusplus
#   define _Begin_C_Header extern "C" {
#   define _End_C_Header }
#   define __restrict
#else
#   define _Begin_C_Header
#   define _End_C_Header
#   define __restrict restrict
#endif
