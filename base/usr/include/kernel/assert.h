#pragma once

extern void __assert_failed(const char * file, int line, const char * func, const char * cond);
#define assert(condition) do { if (!(condition)) __assert_failed(__FILE__,__LINE__,__func__,#condition); } while (0)
