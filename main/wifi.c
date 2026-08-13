#include "wifi.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs.h"

EventGroupHandle_t wifi_event_group;
int retry_count = 0;
char wifi_ssid[WIFI_SSID_LEN] = {0};
char wifi_password[WIFI_PASSWORD_LEN] = {0};

esp_err_t save_wifi_credentials(const char *ssid, const char *password) {
	if (ssid == NULL || password == NULL || ssid[0] == '\0') {
		return ESP_ERR_INVALID_ARG;
	}

	nvs_handle_t handle;
	esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
	if (err != ESP_OK) {
		return err;
	}

	err = nvs_set_str(handle, WIFI_SSID_KEY, ssid);
	if (err == ESP_OK) {
		err = nvs_set_str(handle, WIFI_PASSWORD_KEY, password);
	}

	if (err == ESP_OK) {
		err = nvs_commit(handle);
	}

	nvs_close(handle);
	return err;
}

bool load_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size) {
	if (ssid == NULL || password == NULL || ssid_size == 0 || password_size == 0) {
		return false;
	}

	nvs_handle_t handle;
	esp_err_t err = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &handle);
	if (err != ESP_OK) {
		return false;
	}

	size_t size = ssid_size;
	err = nvs_get_str(handle, WIFI_SSID_KEY, ssid, &size);
	if (err != ESP_OK) {
		nvs_close(handle);
		return false;
	}

	size = password_size;
	err = nvs_get_str(handle, WIFI_PASSWORD_KEY, password, &size);
	nvs_close(handle);
	return err == ESP_OK;
}

esp_err_t apply_wifi_config(const char *ssid, const char *password) {
	if (ssid == NULL || ssid[0] == '\0') {
		return ESP_ERR_INVALID_ARG;
	}

	wifi_config_t config = {0};
	snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", ssid);
	snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s", password ? password : "");
	config.sta.threshold.authmode = password && password[0] != '\0' ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
	return esp_wifi_set_config(WIFI_IF_STA, &config);
}

esp_err_t wifi_apply(void)
{
	esp_err_t err = apply_wifi_config(wifi_ssid, wifi_password);
	if (err != ESP_OK) {
		return err;
	}

	err = esp_wifi_disconnect();
	if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED) {
		return err;
	}

	return esp_wifi_connect();
}

void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
	if (wifi_event_group == NULL) {
		return;
	}

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
		if (retry_count < MAX_RETRY) {
			esp_wifi_connect();
			retry_count++;
			ESP_LOGI("WIFI", "tentando conexao...");
		} else {
			xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
		}
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		retry_count = 0;
		xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
		xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
		ESP_LOGI("WIFI", "Conectado");
	}
}

EventGroupHandle_t wifi_get_event_group(void) {
	return wifi_event_group;
}

esp_err_t wifi_init(char *wifi_ssid, size_t wifi_ssid_size, char *wifi_password, size_t wifi_password_size) {
	wifi_event_group = xEventGroupCreate();
	if (wifi_event_group == NULL) {
		return ESP_ERR_NO_MEM;
	}

	esp_err_t err = esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT());
	if (err != ESP_OK) {
		ESP_LOGE("WIFI", "Falha ao inicializar driver Wi-Fi: %s", esp_err_to_name(err));
		return err;
	}

	if (esp_netif_create_default_wifi_sta() == NULL) {
		ESP_LOGE("WIFI", "Falha ao criar interface Wi-Fi STA");
		return ESP_FAIL;
	}

	err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
	if (err != ESP_OK) {
		return err;
	}

	err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
	if (err != ESP_OK) {
		return err;
	}

	if (!load_wifi_credentials(wifi_ssid, wifi_ssid_size, wifi_password, wifi_password_size)) {
		ESP_LOGW("WIFI", "Credenciais Wi-Fi nao encontradas");

		snprintf(wifi_ssid, wifi_ssid_size, "%s", WIFI_DEFAULT_SSID);
		snprintf(wifi_password, wifi_password_size, "%s", WIFI_DEFAULT_PASSWORD);

		err = save_wifi_credentials(wifi_ssid, wifi_password);
		if (err != ESP_OK) {
			ESP_LOGE("WIFI", "Falha ao salvar credenciais padrao: %s", esp_err_to_name(err));
			return err;
		}
	}

	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err != ESP_OK) {
		return err;
	}

	err = esp_wifi_start();
	if (err != ESP_OK) {
		ESP_LOGE("WIFI", "Falha ao iniciar Wi-Fi: %s", esp_err_to_name(err));
		return err;
	}

	err = apply_wifi_config(wifi_ssid, wifi_password);
	if (err != ESP_OK) {
		ESP_LOGE("WIFI", "Falha ao aplicar configuracoes Wi-Fi: %s", esp_err_to_name(err));
		return err;
	}

	err = esp_wifi_start();
	if (err != ESP_OK) {
		ESP_LOGE("WIFI", "Falha ao iniciar Wi-Fi: %s", esp_err_to_name(err));
		return err;
	}

	ESP_LOGI("WIFI", "Wi-Fi iniciado");
	return ESP_OK;
}

bool wifi_is_connected(void) {
	if (wifi_event_group == NULL) {
		return false;
	}

	return (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}
