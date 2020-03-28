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

int main(int argc, char * argv[]) {

	Value * config = json_parse_file("/etc/weather.json");
	if (!config) {
		fprintf(stderr, "No weather config data\n");
		return 1;
	}

	char * city = JSON_KEY(config, "city")->string;
	char * key = JSON_KEY(config, "key")->string;
	char * units = JSON_KEY(config, "units")->string;

	char cmdline[1024];
	sprintf(cmdline, "fetch -o /tmp/weather-data.json \"http://api.openweathermap.org/data/2.5/weather?q=%s&appid=%s&units=%s\"", city, key, units);
	system(cmdline);

	Value * result = json_parse_file("/tmp/weather-data.json");
	assert(result && result->type == JSON_TYPE_OBJECT);

	Value * _main = JSON_KEY(result,"main");
	Value * conditions = (JSON_KEY(result,"weather") && JSON_KEY(result,"weather")->array->length > 0) ?
		JSON_IND(JSON_KEY(result,"weather"),0) : NULL;

	FILE * out = fopen("/tmp/weather-parsed.conf", "w");
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

