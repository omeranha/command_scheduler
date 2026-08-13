#include "time_utils.h"

time_t time_str_to_time_t(const char *time_str) {
	int hours;
	int minutes;
	int seconds;
	if (time_str == NULL) {
		return (time_t)-1;
	}

	if (sscanf(time_str, "%2d:%2d:%2d", &hours, &minutes, &seconds) != 3) {
		return (time_t)-1;
	}

	if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
		return (time_t)-1;
	}
	return hours * 3600 + minutes * 60 + seconds;
}

bool set_system_time(int hours, int minutes, int seconds) {
	time_t now;
	struct tm current_time;
	if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
		return false;
	}

	if (time(&now) == (time_t)-1) {
		return false;
	}

	if (localtime_r(&now, &current_time) == NULL) {
		return false;
	}

	current_time.tm_hour = hours;
	current_time.tm_min = minutes;
	current_time.tm_sec = seconds;
	current_time.tm_isdst = -1;
	time_t new_time = mktime(&current_time);
	if (new_time == (time_t)-1) {
		return false;
	}

	struct timeval tv = {
		.tv_sec = new_time,
		.tv_usec = 0
	};
	return settimeofday(&tv, NULL) == 0;
}

int time_t_to_time_str(time_t time, char *buffer, size_t buffer_size) {
	struct tm timeinfo;
	if (buffer == NULL || buffer_size == 0) {
		return -1;
	}

	if (localtime_r(&time, &timeinfo) == NULL) {
		return -1;
	}
	return snprintf(buffer, buffer_size, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

int seconds_to_time_str(uint32_t seconds, char *buffer, size_t buffer_size) {
	uint32_t hours = seconds / 3600U;
	uint32_t minutes = (seconds % 3600U) / 60U;
	uint32_t secs = seconds % 60U;
	if (buffer == NULL || buffer_size == 0) {
		return -1;
	}

	return snprintf(buffer, buffer_size, "%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes, (unsigned long)secs);
}

bool is_leap_year(int year) {
	return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month) {
	const uint8_t days[] = {
		31, 28, 31, 30, 31, 30,
		31, 31, 30, 31, 30, 31
	};

	if (month < 1 || month > 12) {
		return 0;
	}

	if (month == 2 && is_leap_year(year)) {
		return 29;
	}
	return days[month - 1];
}

bool is_valid_date(int year, int month, int day) {
	if (year < 1970 || year > 9999) {
		return false;
	}

	int max_day = days_in_month(year, month);
	return max_day > 0 && day >= 1 && day <= max_day;
}

uint32_t make_date(int year, int month, int day) {
	return (uint32_t)(year * 10000 + month * 100 + day);
}
