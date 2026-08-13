#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <time.h>

#include "esp_sntp.h"

time_t time_str_to_time_t(const char *time_str);

bool set_system_time(int hours, int minutes, int seconds);
bool is_leap_year(int year);
bool is_valid_date(int year, int month, int day);

int time_t_to_time_str(time_t time, char *buffer, size_t buffer_size);
int seconds_to_time_str(uint32_t seconds, char *buffer, size_t buffer_size);
int days_in_month(int year, int month);

uint32_t make_date(int year, int month, int day);

#endif
