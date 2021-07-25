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
		if (access(LOCATION_DATA_PATH, R_OK)) {
			sprintf(cmdline, "fetch -o \"" LOCATION_DATA_PATH "\" \"http://ip-api.com/json/?fields=countryCode,regionName,city\"");
			system(cmdline);
		}
		Value * locationData = json_parse_file(LOCATION_DATA_PATH);
		if (!locationData) {
			fprintf(stderr, "%s: city field was set to 'guess' but failed to acquire data from IP geolocation service\n", argv[0]);
			return 1;
		}

		char * cityName = JSON_KEY(locationData, "city")->string;
		char * regionName = JSON_KEY(locationData, "regionName")->string;
		char * countryCode = JSON_KEY(locationData, "countryCode")->string;

		city = malloc(strlen(cityName) + strlen(regionName) + strlen(countryCode) + 10);
		sprintf(city, "%s, %s, %s", cityName, regionName, countryCode);
	}

	sprintf(cmdline, "fetch -o \"" WEATHER_DATA_PATH "\" \"http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s\"", city, key, units);
	system(cmdline);

	Value * result = json_parse_file(WEATHER_DATA_PATH);
	assert(result && result->type == JSON_TYPE_OBJECT);

	Value * _main = JSON_KEY(result,"main");
	Value * conditions = (JSON_KEY(result,"weather") && JSON_KEY(result,"weather")->array->length > 0) ?
		JSON_IND(JSON_KEY(result,"weather"),0) : NULL;

	FILE * out = fopen(WEATHER_OUT_PATH, "w");
	fprintf(out, "%.2lf\n", JSON_KEY(_main,"temp")->number);
	fprintf(out, "%d\n", (int)JSON_KEY(_main,"temp")->number);
	fprintf(out, "%s\n", conditions ? JSON_KEY(conditions,"main")->string : "");
	fprintf(out, "%s\n", conditions ? JSON_KEY(conditions,"icon")->string : "");
	fprintf(out, "%d\n", (int)JSON_KEY(_main,"humidity")->number);
	fprintf(out, "%d\n", JSON_KEY(JSON_KEY(result,"clouds"),"all") ? (int)JSON_KEY(JSON_KEY(result,"clouds"),"all")->number : 0);
	fprintf(out, "%s\n", city);
	char * format = "%a, %d %b %Y %H:%M:%S";
	struct tm * timeinfo;
	struct timeval now;
	char buf[BUFSIZ] = {0};
	gettimeofday(&now, NULL);
	timeinfo = localtime((time_t *)&now.tv_sec);
	strftime(buf,BUFSIZ,format,timeinfo);
	fprintf(out, buf);
	fclose(out);

	return 0;
}

