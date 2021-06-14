#pragma once

#include <stdint.h>

typedef struct {
	char year[4];
	char month[2];
	char day[2];
	char hour[2];
	char minute[2];
	char second[2];
	char hundredths[2];
	int8_t timezone;
} __attribute__((packed)) iso_9660_datetime_t;

typedef struct {
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	int8_t timezone;
} __attribute__((packed)) iso_9660_rec_date_t;

typedef struct {
	uint8_t length;
	uint8_t ext_length;

	uint32_t extent_start_LSB;
	uint32_t extent_start_MSB;

	uint32_t extent_length_LSB;
	uint32_t extent_length_MSB;

	iso_9660_rec_date_t record_date;

	uint8_t flags;
	uint8_t interleave_units;
	uint8_t interleave_gap;

	uint16_t volume_seq_LSB;
	uint16_t volume_seq_MSB;

	uint8_t name_len;
	char name[];
} __attribute__((packed)) iso_9660_directory_entry_t;

typedef struct {
	uint8_t type; /* 0x01 */
	char id[5]; /* CD001 */

	uint8_t version;
	uint8_t _unused0;

	char system_id[32];
	char volume_id[32];

	uint8_t _unused1[8];

	uint32_t volume_space_LSB;
	uint32_t volume_space_MSB;

	uint8_t _unused2[32];

	uint16_t volume_set_LSB;
	uint16_t volume_set_MSB;

	uint16_t volume_seq_LSB;
	uint16_t volume_seq_MSB;

	uint16_t logical_block_size_LSB;
	uint16_t logical_block_size_MSB;

	uint32_t path_table_size_LSB;
	uint32_t path_table_size_MSB;

	uint32_t path_table_LSB;
	uint32_t optional_path_table_LSB;

	uint32_t path_table_MSB;
	uint32_t optional_path_table_MSB;

	/* iso_9660_directory_entry_t */
	char root[34];

	char volume_set_id[128];
	char volume_publisher[128];
	char data_preparer[128];
	char application_id[128];

	char copyright_file[38];
	char abstract_file[36];
	char bibliographic_file[37];

	iso_9660_datetime_t creation;
	iso_9660_datetime_t modification;
	iso_9660_datetime_t expiration;
	iso_9660_datetime_t effective;

	uint8_t file_structure_version;
	uint8_t _unused_3;

	char application_use[];
} __attribute__((packed)) iso_9660_volume_descriptor_t;

#define ISO_SECTOR_SIZE 2048

#define FLAG_HIDDEN      0x01
#define FLAG_DIRECTORY   0x02
#define FLAG_ASSOCIATED  0x04
#define FLAG_EXTENDED    0x08
#define FLAG_PERMISSIONS 0x10
#define FLAG_CONTINUES   0x80

extern char root_data[ISO_SECTOR_SIZE];
extern iso_9660_volume_descriptor_t * root;
extern iso_9660_directory_entry_t * dir_entry;

int navigate(char * name);
