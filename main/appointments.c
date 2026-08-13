#include "appointments.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

uint8_t appointments_count = 0;
appointment_t appointments[MAX_APPOINTMENTS];

esp_err_t appointments_save(void)
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open(APPOINTMENTS_NVS_NAMESPACE, NVS_READWRITE, &handle);

	if (err != ESP_OK) {
		return err;
	}

	err = nvs_set_blob(handle, APPOINTMENTS_KEY, appointments, sizeof(appointments));
	if (err != ESP_OK) {
		nvs_close(handle);
		return err;
	}

	err = nvs_set_u8(handle, COUNT_KEY, appointments_count);
	if (err != ESP_OK) {
		nvs_close(handle);
		return err;
	}

	err = nvs_commit(handle);
	nvs_close(handle);

	return err;
}

esp_err_t appointments_load(void)
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open(APPOINTMENTS_NVS_NAMESPACE, NVS_READONLY, &handle);

	if (err == ESP_ERR_NVS_NOT_FOUND) {
		appointments_count = 0;
		memset(appointments, 0, sizeof(appointments));
		return ESP_OK;
	}

	if (err != ESP_OK) {
		return err;
	}

	size_t size = sizeof(appointments);

	err = nvs_get_blob(handle, APPOINTMENTS_KEY, appointments, &size);
	if (err != ESP_OK) {
		nvs_close(handle);
		appointments_count = 0;
		memset(appointments, 0, sizeof(appointments));
		return err;
	}

	uint8_t count;

	err = nvs_get_u8(handle, COUNT_KEY, &count);
	nvs_close(handle);

	if (err != ESP_OK) {
		appointments_count = 0;
		memset(appointments, 0, sizeof(appointments));
		return err;
	}

	if (count > MAX_APPOINTMENTS) {
		appointments_count = 0;
		memset(appointments, 0, sizeof(appointments));
		return ESP_ERR_INVALID_SIZE;
	}

	appointments_count = count;

	return ESP_OK;
}

esp_err_t appointments_clear(void)
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open(APPOINTMENTS_NVS_NAMESPACE, NVS_READWRITE, &handle);

	if (err != ESP_OK) {
		return err;
	}

	err = nvs_erase_key(handle, APPOINTMENTS_KEY);
	if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
		nvs_close(handle);
		return err;
	}

	err = nvs_erase_key(handle, COUNT_KEY);
	if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
		nvs_close(handle);
		return err;
	}

	err = nvs_commit(handle);
	nvs_close(handle);

	if (err != ESP_OK) {
		return err;
	}

	appointments_count = 0;
	memset(appointments, 0, sizeof(appointments));

	return ESP_OK;
}

uint8_t get_available_appointment_id(void) {
	for (uint8_t id = 1; id <= MAX_APPOINTMENTS; id++) {
		bool used = false;
		for (uint8_t i = 0; i < appointments_count; i++) {
			if (appointments[i].id == id) {
				used = true;
				break;
			}
		}

		if (!used) {
			return id;
		}
	}
	return -1;
}
