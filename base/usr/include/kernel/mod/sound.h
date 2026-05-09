#pragma once

#include <stdint.h>

#define SND_MAX_KNOBS 256
#define SND_KNOB_NAME_SIZE 256
#define SND_KNOB_MAX_VALUE UINT32_MAX

#define SND_KNOB_MASTER 0
#define SND_DEVICE_MAIN 0

#define SND_FORMAT_L16SLE 0  /* Linear 16-bit signed little endian */

typedef struct snd_knob_list {
	uint32_t device;              /* IN */
	uint32_t num;                 /* OUT */
	uint32_t ids[SND_MAX_KNOBS];  /* OUT */
} snd_knob_list_t;

typedef struct snd_knob_info {
	uint32_t device;               /* IN */
	uint32_t id;                   /* IN */
	char name[SND_KNOB_NAME_SIZE]; /* OUT */
} snd_knob_info_t;

typedef struct snd_knob_value {
	uint32_t device; /* IN */
	uint32_t id;     /* IN */
	uint32_t val;    /* OUT for SND_MIXER_READ_KNOB, IN for SND_MIXER_WRITE_KNOB */
} snd_knob_value_t;

typedef struct snd_device_user {
	uint32_t id;
	char name[256];
	uint32_t playback_speed;
	uint32_t playback_format;
	uint32_t num_knobs;
} snd_device_user_t;

typedef struct snd_device_list {
	size_t space;
	size_t count;
	snd_device_user_t devices[];
} snd_device_list_t;


/* IOCTLs */
#define SND_MIXER_GET_KNOBS 0
#define SND_MIXER_GET_KNOB_INFO  1
#define SND_MIXER_READ_KNOB 2
#define SND_MIXER_WRITE_KNOB 3
#define SND_MIXER_GET_DEVICES 4

