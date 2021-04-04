#include <efi.h>
#include <efilib.h>
#include <stdio.h>
#include <kuroko/vm.h>
#include <kuroko/object.h>
#include <kuroko/util.h>

uint32_t secs_of_years(int years) {
	uint32_t days = 0;
	while (years > 1969) {
		days += 365;
		if (years % 4 == 0) {
			if (years % 100 == 0) {
				if (years % 400 == 0) {
					days++;
				}
			} else {
				days++;
			}
		}
		years--;
	}
	return days * 86400;
}

uint32_t secs_of_month(int months, int year) {
	uint32_t days = 0;
	switch(months) {
		case 11:
			days += 30;
		case 10:
			days += 31;
		case 9:
			days += 30;
		case 8:
			days += 31;
		case 7:
			days += 31;
		case 6:
			days += 30;
		case 5:
			days += 31;
		case 4:
			days += 30;
		case 3:
			days += 31;
		case 2:
			days += 28;
			if ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) {
				days++;
			}
		case 1:
			days += 31;
		default:
			break;
	}
	return days * 86400;
}

KRK_FUNC(time,{
	EFI_TIME now_efi;
	uefi_call_wrapper(ST->RuntimeServices->GetTime, 2, &now_efi, NULL);
	double now =
		(double)secs_of_years(now_efi.Year-1) +
		(double)secs_of_month(now_efi.Month-1,now_efi.Year) +
		(now_efi.Day -1) * 86400.0 +
		(now_efi.Hour) * 3600.0 +
		(now_efi.Minute) * 60.0 +
		(double)(now_efi.Second) +
		((double)now_efi.Nanosecond / 100000000.0);

	return FLOATING_VAL(now);
})

KRK_FUNC(sleep,{
	FUNCTION_TAKES_EXACTLY(1);
	if (!IS_INTEGER(argv[0]) && !IS_FLOATING(argv[0])) {
		return TYPE_ERROR(int or float,argv[0]);
	}

	uint64_t usecs = (IS_INTEGER(argv[0]) ? AS_INTEGER(argv[0]) :
	                 (IS_FLOATING(argv[0]) ? AS_FLOATING(argv[0]) : 0)) *
	                 1000000;

	uefi_call_wrapper(ST->BootServices->Stall, 1, usecs);

})

void _createAndBind_timeMod(void) {
	KrkInstance * module = krk_newInstance(vm.baseClasses->moduleClass);
	krk_attachNamedObject(&vm.modules, "time", (KrkObj*)module);
	krk_attachNamedObject(&module->fields, "__name__", (KrkObj*)S("time"));
	krk_attachNamedValue(&module->fields, "__file__", NONE_VAL());
	BIND_FUNC(module,time);
	BIND_FUNC(module,sleep);
}

