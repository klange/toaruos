#ifndef __ARCH_PERF_H__
#define __ARCH_PERF_H__

#ifdef PERF
#define PERF_START
#define PERF_STOP(x)
#else /* PERF */
#define PERF_START    /* null definition */
#define PERF_STOP(x)  /* null definition */
#endif /* PERF */

#endif /* __ARCH_PERF_H__ */

