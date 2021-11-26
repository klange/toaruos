/**
 * @brief Query a remote API to get timezone information based geoip lookup.
 *
 * We ask @see ip-api.com for geo-ip data, which includes a timezone offset.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <toaru/json.h>
#include <toaru/hashmap.h>
#include <toaru/list.h>

typedef struct JSON_Value Value;

#define LOCATION_DATA_PATH "/tmp/location-data.json"

int main(int argc, char * argv[]) {
	/* See if the location data already exists... */
	char cmdline[1024];
	Value * locationData = json_parse_file(LOCATION_DATA_PATH);
	if (!locationData) {
		sprintf(cmdline, "fetch -o \"" LOCATION_DATA_PATH "\" \"http://ip-api.com/json/?fields=lat,lon,city,offset\"");
		system(cmdline);
		locationData = json_parse_file(LOCATION_DATA_PATH);
	}
	/* If we still failed to load it, then bail. */
	if (!locationData) {
		return 1;
	}
	double offset = JSON_KEY(locationData, "offset")->number;

	printf("%d\n", (int)offset);
	return 0;
}


