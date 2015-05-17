#ifndef USERSPACE_LIB_SND_H
#define USERSPACE_LIB_SND_H

#include <types.h>

#define SND_MAX_KNOBS 256
#define SND_KNOB_NAME_SIZE 256

#define SND_KNOB_MASTER 0
#define SND_DEVICE_MAIN 0

typedef struct snd_knob_list {
	uint32_t device;
	uint32_t num;
	uint32_t ids[SND_MAX_KNOBS];
} snd_knob_list_t;

typedef struct snd_knob_info {
	uint32_t device;
	uint32_t id;
	char name[SND_KNOB_NAME_SIZE];
} snd_knob_info_t;

typedef struct snd_knob_value {
	uint32_t device;
	uint32_t id;
	uint32_t val;
} snd_knob_value_t;


/* IOCTLs */
#define SND_MIXER_GET_KNOBS 0
#define SND_MIXER_GET_KNOB_INFO  1
#define SND_MIXER_READ_KNOB 2
#define SND_MIXER_WRITE_KNOB 3

#endif  /* USERSPACE_LIB_SND_H */
