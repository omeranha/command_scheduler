#ifndef APPOINTMENTS_H
#define APPOINTMENTS_H

#include "esp_err.h"

#define MAX_APPOINTMENTS 5
#define APPOINTMENTS_NVS_NAMESPACE "appointments"
#define APPOINTMENTS_KEY "list"
#define COUNT_KEY "count"

typedef struct {
	uint8_t id;
	uint32_t date; // 0 = every day, YYYYMMDD = execute only on this date
	uint32_t time; // seconds since midnight
	char action[32];
	char params[32];
	bool state;
} appointment_t;

typedef struct {
	uint8_t count;
	appointment_t appointments[MAX_APPOINTMENTS];
} appointment_storage_t;

extern uint8_t appointments_count;
extern appointment_t appointments[MAX_APPOINTMENTS];

esp_err_t appointments_save(void);
esp_err_t appointments_load(void);
esp_err_t appointments_clear(void);
uint8_t get_available_appointment_id(void);

#endif
