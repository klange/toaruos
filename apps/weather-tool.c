/**
 * @brief Ask OpenWeather for forecast data.
 *
 * Fetches weather forecast data from OpenWeather and converts the JSON
 * output to a simpler parsed format we read in the panel.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018-2021 K. Lange
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

#define WEATHER_CONF_PATH  "/etc/weather.json"
#define WEATHER_DATA_PATH  "/tmp/weather-data.json"
#define WEATHER_OUT_PATH   "/tmp/weather-parsed.conf"
#define LOCATION_DATA_PATH "/tmp/location-data.json"

int main(int argc, char * argv[]) {
	Value * config = json_parse_file(WEATHER_CONF_PATH);
	if (!config) {
		fprintf(stderr, "No weather config data\n");
		return 1;
	}

	char * city = JSON_KEY(config, "city")->string;
	char * key = JSON_KEY(config, "key")->string;
	char * units = JSON_KEY(config, "units")->string;
	char cmdline[1024];

	/* If the city is 'guess', we'll make a single query to a separate service to
	 * get a location from the user's external IP... */
	if (!strcmp(city, "guess")) {
		/* See if the location data already exists... */
		Value * locationData = json_parse_file(LOCATION_DATA_PATH);
		if (!locationData) {
			sprintf(cmdline, "fetch -o \"" LOCATION_DATA_PATH "\" \"http://ip-api.com/json/?fields=lat,lon,city,offset\"");
			system(cmdline);
			locationData = json_parse_file(LOCATION_DATA_PATH);
		}
		/* If we still failed to load it, then bail. */
		if (!locationData) {
			fprintf(stderr, "%s: city field was set to 'guess' but failed to acquire data from IP geolocation service\n", argv[0]);
			return 1;
		}

		city = JSON_KEY(locationData, "city")->string;
		double lat = JSON_KEY(locationData, "lat")->number;
		double lon = JSON_KEY(locationData, "lon")->number;

		sprintf(cmdline, "fetch -o \"" WEATHER_DATA_PATH "\" \"http://api.openweathermap.org/data/2.5/weather?lat=%.5lf&lon=%.5lf&appid=%s&units=%s\"", lat, lon, key, units);
		system(cmdline);
	} else {
		sprintf(cmdline, "fetch -o \"" WEATHER_DATA_PATH "\" \"http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s\"", city, key, units);
		system(cmdline);
	}

	Value * result = json_parse_file(WEATHER_DATA_PATH);
	assert(result && result->type == JSON_TYPE_OBJECT);

	Value * _main = JSON_KEY(result,"main");
	Value * conditions = (JSON_KEY(result,"weather") && JSON_KEY(result,"weather")->array->length > 0) ?
		JSON_IND(JSON_KEY(result,"weather"),0) : NULL;

	FILE * out = fopen(WEATHER_OUT_PATH, "w");

	/**
	 * The format for a parsed weather payload is a series of line-separated entries:
	 * - Formatted temperature, with decimal.
	 * - Integral temperature, eg. for the panel widget.
	 * - Main weather conditions string, eg. "Clouds"
	 * - Icon identifier, eg. 02d is "cloudy, daytime".
	 * - Humidity (integer, percentage)
	 * - Cloud coverage (integer, percentage)
	 * - City name (we're using the guessed location, not the one from the weather provider...)
	 * - Date string of last update
	 */
	fprintf(out, "%.2lf\n", JSON_KEY(_main,"temp")->number);
	fprintf(out, "%d\n", (int)JSON_KEY(_main,"temp")->number);
	fprintf(out, "%s\n", conditions ? JSON_KEY(conditions,"main")->string : "");
	fprintf(out, "%s\n", conditions ? JSON_KEY(conditions,"icon")->string : "");
	fprintf(out, "%d\n", (int)JSON_KEY(_main,"humidity")->number);
	fprintf(out, "%d\n", JSON_KEY(JSON_KEY(result,"clouds"),"all") ? (int)JSON_KEY(JSON_KEY(result,"clouds"),"all")->number : 0);
	fprintf(out, "%s\n", city);
	char * format = "%a, %d %b %Y %H:%M:%S\n";
	struct tm * timeinfo;
	struct timeval now;
	char buf[BUFSIZ] = {0};
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	strftime(buf,BUFSIZ,format,timeinfo);
	fprintf(out, buf);

	fprintf(out, "%d\n", (int)JSON_KEY(_main,"pressure")->number);

	fclose(out);

	return 0;
}

