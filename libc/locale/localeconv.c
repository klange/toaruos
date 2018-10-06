#include <locale.h>

static struct lconv _en_US = {
    .decimal_point = ".",
    .thousands_sep = ",",
    .grouping = "\x03\x03",
    .int_curr_symbol = "USD ",
    .currency_symbol = "$",
    .mon_decimal_point = ".",
    .mon_thousands_sep = ",",
    .mon_grouping = "\x03\x03",
    .positive_sign = "+",
    .negative_sign = "-",
    .int_frac_digits = 2,
    .frac_digits = 2,
    .p_cs_precedes = 1,
    .p_sep_by_space = 0,
    .n_cs_precedes = 1,
    .n_sep_by_space = 0,
    .p_sign_posn = 1,
    .n_sign_posn = 1,
};

struct lconv * localeconv(void) {
    return &_en_US;
}
