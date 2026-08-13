#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_event.h"
#include "esp_err.h"

#define WIFI_NVS_NAMESPACE "wifi"
#define WIFI_SSID_KEY "ssid"
#define WIFI_PASSWORD_KEY "password"
#define WIFI_DEFAULT_SSID "YOUR_WIFI_SSID"
#define WIFI_DEFAULT_PASSWORD "YOUR_WIFI_PASSWORD"
#define WIFI_SSID_LEN 32
#define WIFI_PASSWORD_LEN 64
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY 5

extern EventGroupHandle_t wifi_event_group;
extern int retry_count;
extern char wifi_ssid[WIFI_SSID_LEN];
extern char wifi_password[WIFI_PASSWORD_LEN];

esp_err_t save_wifi_credentials(const char *ssid, const char *password);
esp_err_t wifi_init(char *wifi_ssid, size_t wifi_ssid_size, char *wifi_password, size_t wifi_password_size);
esp_err_t apply_wifi_config(const char *ssid, const char *password);
esp_err_t wifi_apply(void);

EventGroupHandle_t wifi_get_event_group(void);
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

bool load_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);
bool wifi_is_connected(void);

#endif
