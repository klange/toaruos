static int sel_max = 0;
static int sel = 0;

void toggle(int ndx, int value, char *str) {
	set_attr(sel == ndx ? 0x70 : 0x07);
	if (value) {
		print_(" [X] ");
	} else {
		print_(" [ ] ");
	}
	print_(str);
	if (x < 40) {
		while (x < 39) {
			print_(" ");
		}
		x = 40;
	} else {
		print_("\n");
	}
}

struct option {
	int * value;
	char * title;
	char * description_1;
	char * description_2;
} boot_options[20] = {{0}}; /* can't really hold more than that */

static int _boot_offset = 0;
#define BOOT_OPTION(_value, default_val, option, d1, d2) \
	int _value = default_val;\
	boot_options[_boot_offset].value = &_value; \
	boot_options[_boot_offset].title = option; \
	boot_options[_boot_offset].description_1 = d1; \
	boot_options[_boot_offset].description_2 = d2; \
	_boot_offset++

struct bootmode {
	int index;
	char * key;
	char * title;
};

#define BASE_SEL ((sizeof(boot_mode_names)/sizeof(*boot_mode_names))-1)

