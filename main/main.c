#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "esp_netif_sntp.h"
#include "wifi.h"
#include "appointments.h"
#include "time_utils.h"

#define UART_PORT UART_NUM_0
#define UART_BAUD_RATE 115200
#define LED_GPIO GPIO_NUM_2

typedef struct {
	uint32_t rate_ms;
	uint32_t duration_ms;
	uint8_t appointment_id;
} blink_request_t;

struct tm timeinfo = {0};

static SemaphoreHandle_t appointments_mutex;
static QueueHandle_t ntp_done_queue;
static QueueHandle_t blink_queue;
static bool ntp_sync_in_progress = false;

void uart_printf(const char *format, ...) {
	char buffer[256];
	va_list args;
	va_start(args, format);
	int len = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (len <= 0) {
		return;
	}

	if (len >= sizeof(buffer)) {
		len = sizeof(buffer) - 1;
	}
	uart_write_bytes(UART_PORT, buffer, len);
}

static void lock_appointments(void) {
	if (appointments_mutex != NULL) {
		xSemaphoreTake(appointments_mutex, portMAX_DELAY);
	}
}

static void unlock_appointments(bool save) {
	if (appointments_mutex != NULL) {
		xSemaphoreGive(appointments_mutex);
	}

	if (save) {
		appointments_save();
	}
}

static void ntp_sync_task(void *arg) {
	bool success = false;
	if (!wifi_is_connected()) {
		xQueueSend(ntp_done_queue, &success, portMAX_DELAY);
		ntp_sync_in_progress = false;
		vTaskDelete(NULL);
		return;
	}

	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
	if (esp_netif_sntp_init(&config) == ESP_OK) {
		esp_netif_sntp_start();
		success = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK;
		esp_netif_sntp_deinit();
	}

	xQueueSend(ntp_done_queue, &success, portMAX_DELAY);
	ntp_sync_in_progress = false;
	vTaskDelete(NULL);
}

static bool parse_appointment_command(const char *buffer, int *year, int *month, int *day, int *hours, int *minutes, int *seconds, char *action, int *param1, int *param2) {
	if (buffer == NULL || year == NULL || month == NULL || day == NULL || hours == NULL || minutes == NULL || seconds == NULL || action == NULL || param1 == NULL || param2 == NULL) {
		return false;
	}

	*year = 0;
	*month = 0;
	*day = 0;
	*param1 = 0;
	*param2 = 0;

	// full date: sched add YYYY-MM-DD HH:MM:SS action [params]
	int count = sscanf(buffer, "sched add %d-%d-%d %d:%d:%d %31s %d %d", year, month, day, hours, minutes, seconds, action, param1, param2);
	if (count >= 7) {
		if (!is_valid_date(*year, *month, *day)) {
			return false;
		}

		if (*hours < 0 || *hours > 23 ||
			*minutes < 0 || *minutes > 59 ||
			*seconds < 0 || *seconds > 59) {
			return false;
		}

		if (strcmp(action, "liga") == 0 || strcmp(action, "desliga") == 0) {
			return count == 7;
		}

		if (strcmp(action, "pisca") == 0) {
			return count == 9 && *param1 > 0 && *param2 > 0;
		}

		return false;
	}

	*year = 0;
	*month = 0;
	*day = 0;
	*param1 = 0;
	*param2 = 0;
	// daily: sched add HH:MM:SS action [params]
	count = sscanf(buffer, "sched add %d:%d:%d %31s %d %d", hours, minutes, seconds, action, param1, param2);
	if (count < 4) {
		return false;
	}

	if (*hours < 0 || *hours > 23 || *minutes < 0 || *minutes > 59 || *seconds < 0 || *seconds > 59) {
		return false;
	}

	if (strcmp(action, "liga") == 0 || strcmp(action, "desliga") == 0) {
		return count == 4;
	}

	if (strcmp(action, "pisca") == 0) {
		return count == 6 && *param1 > 0 && *param2 > 0;
	}

	return false;
}

static bool parse_appointment_in_command(const char *buffer, int *delay_s, char *action, size_t action_size, int *rate_ms, int *duration_s) {
	if (buffer == NULL || delay_s == NULL || action == NULL || rate_ms == NULL || duration_s == NULL) {
		return false;
	}

	*rate_ms = 0;
	*duration_s = 0;
	int count = sscanf(buffer, "sched in %d %31s %d %d", delay_s, action, rate_ms, duration_s);
	if (count < 2) {
		return false;
	}

	if (*delay_s <= 0) {
		return false;
	}

	if (strcmp(action, "liga") == 0 || strcmp(action, "desliga") == 0) {
		return count == 2;
	}

	if (strcmp(action, "pisca") == 0) {
		if (count != 4) {
			return false;
		}

		if (*rate_ms <= 0 || *duration_s <= 0) {
			return false;
		}
		return true;
	}
	return false;
}

static void execute_appointment_action(const appointment_t *appointment) {
	if (strcmp(appointment->action, "liga") == 0) {
		gpio_set_level(LED_GPIO, 1);
		uart_printf("Executando agendamento ID %d: LED ON\r\n", appointment->id);
	} else if (strcmp(appointment->action, "desliga") == 0) {
		gpio_set_level(LED_GPIO, 0);
		uart_printf("Executando agendamento %d: LED OFF\r\n", appointment->id);
	} else if (strcmp(appointment->action, "pisca") == 0) {
		blink_request_t request;
		if (sscanf(appointment->params, "%lu %lu", &request.rate_ms, &request.duration_ms) != 2 || request.rate_ms <= 0 || request.duration_ms <= 0) {
			uart_printf("Parametros invalidos para agendamento ID %d\r\n", appointment->id);
			return;
		}
		request.appointment_id = appointment->id;
		if (blink_queue != NULL) {
			xQueueSend(blink_queue, &request, portMAX_DELAY);
		}
	}
}

static void led_blink_task(void *pvParameters) {
	blink_request_t request;
	while (1) {
		if (xQueueReceive(blink_queue, &request, portMAX_DELAY) == pdTRUE) {
			uart_printf("Piscando LED para agendamento ID %d\r\n", request.appointment_id);
			uint32_t elapsed_ms = 0;
			uint32_t total_ms = request.duration_ms * 1000UL;
			bool led_state = false;
			while (elapsed_ms < total_ms) {
				gpio_set_level(LED_GPIO, led_state ? 1 : 0);
				vTaskDelay(pdMS_TO_TICKS(request.rate_ms));
				elapsed_ms += request.rate_ms;
				led_state = !led_state;
			}
			gpio_set_level(LED_GPIO, 0);
		}
	}
}

void mainTask(void *parameters) {
	char buffer[128];
	while (1) {
		bool ntp_result = false;
		if (xQueueReceive(ntp_done_queue, &ntp_result, 0) == pdTRUE) {
			uart_printf(ntp_result ? "Hora sincronizada\r\n" : "Falha na hora de sincronizar!\r\n");
		}

		int len = uart_read_bytes(UART_PORT, buffer, sizeof(buffer) - 1, pdMS_TO_TICKS(100));
		if (len > 0) {
			buffer[len] = '\0';
			if (strncmp(buffer, "time set ", 9) == 0) {
				int year, month, day;
				int hours, minutes, seconds;

				bool has_date = false;
				if (sscanf(buffer, "time set \"%d-%d-%d %d:%d:%d\"", &year, &month, &day, &hours, &minutes, &seconds) == 6) {
					has_date = true;
				} else if (sscanf(buffer, "time set \"%d:%d:%d\"", &hours, &minutes, &seconds) == 3) {
					time_t now;
					time(&now);
					struct tm current_time;
					if (localtime_r(&now, &current_time) == NULL) {
						uart_printf("ERRO GET HORA\r\n");
						continue;
					}

					year = current_time.tm_year + 1900;
					month = current_time.tm_mon + 1;
					day = current_time.tm_mday;
				} else {
					uart_printf("ERRO FORMATO INVALIDO\r\n");
					continue;
				}

				if (!is_valid_date(year, month, day)) {
					uart_printf("ERRO DATA INVALIDA\r\n");
					continue;
				}

				if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
					uart_printf("ERRO HORA INVALIDA\r\n");
					continue;
				}

				struct tm new_time = {
					.tm_sec = seconds,
					.tm_min = minutes,
					.tm_hour = hours,
					.tm_mday = day,
					.tm_mon = month - 1,
					.tm_year = year - 1900,
					.tm_isdst = -1
				};

				time_t new_timestamp = mktime(&new_time);
				if (new_timestamp == (time_t)-1) {
					uart_printf("ERRO SET_TIME\r\n");
					continue;
				}

				struct timeval tv = {
					.tv_sec = new_timestamp,
					.tv_usec = 0
				};

				if (settimeofday(&tv, NULL) != 0) {
					uart_printf("ERRO SET_TIME\r\n");
					continue;
				}

				if (has_date) {
					uart_printf("{\"event\":\"time_set\",\"message\":\"data e hora definidas para %04d-%02d-%02d %02d:%02d:%02d\"}\r\n", year, month, day, hours, minutes, seconds);
				} else {
					uart_printf("{\"event\":\"time_set\",\"message\":\"hora definida para %02d:%02d:%02d\"}\r\n", hours, minutes, seconds);
				}

			} else if (strcmp(buffer, "time get") == 0) {
				time_t now;
				time(&now);
				struct tm local_time;
				if (localtime_r(&now, &local_time) == NULL) {
					uart_printf("ERRO GET HORA\r\n");
					continue;
				}
				uart_printf("Data atual: %04d-%02d-%02d %02d:%02d:%02d\r\n", local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday, local_time.tm_hour, local_time.tm_min, local_time.tm_sec);

			} else if (strcmp(buffer, "ntp sync") == 0) {
				if (ntp_sync_in_progress) {
					uart_printf("Sincronizacao NTP em andamento...\r\n");
					continue;
				}

				ntp_sync_in_progress = true;
				if (xTaskCreate(ntp_sync_task, "ntp_sync_task", 4096, NULL, 4, NULL) != pdPASS) {
					ntp_sync_in_progress = false;
					uart_printf("ERRO NTP_TASK_CREATE\r\n");
					continue;
				}

				uart_printf("Sincronizacao NTP iniciada em segundo plano...\r\n");

			} else if (strcmp(buffer, "help") == 0) {
				uart_printf("\r\n");
				uart_printf("========== COMANDOS DISPONIVEIS ==========\r\n");
				uart_printf("\r\n");
				uart_printf("COMANDOS SCHEDULE:\r\n");
				uart_printf("  sched add HH:MM:SS <action> [params]  - Adicionar agendamento\r\n");
				uart_printf("                                         acoes: liga, desliga, pisca <rate_ms> <duration_s>\r\n");
				uart_printf("  sched list                            - Listar todos agendamentos\r\n");
				uart_printf("  sched del <id>                        - Deletar apontamentos por ID\r\n");
				uart_printf("  sched clear                           - Limpar agendamentos\r\n");
				uart_printf("\r\n");
				uart_printf("TIME COMMANDS:\r\n");
				uart_printf("  time set \"HH:MM:SS\"                   - Definir hora do sistema manualmente\r\n");
				uart_printf("  time get                              - Mostrar hora do sistema atual\r\n");
				uart_printf("  ntp sync                              - Sincronizar hora via NTP\r\n");
				uart_printf("\r\n");
				uart_printf("WiFi COMMANDS:\r\n");
				uart_printf("  wifi set ssid <name>                  - Definir nome do WiFi\r\n");
				uart_printf("  wifi set password <pass>              - Definir senha do WiFi\r\n");
				uart_printf("  wifi apply                            - Aplicar configuracoes WiFi\r\n");
				uart_printf("\r\n");
				uart_printf("========================================\r\n");
				uart_printf("\r\n");

			} else if (strcmp(buffer, "sched list") == 0) {
				lock_appointments();
				if (appointments_count == 0) {
					uart_printf("Nenhum agendamento encontrado.\r\n");
					unlock_appointments(false);
					continue;
				}

				uart_printf("\r\n%-3s %-12s %-9s %-9s %-12s %s\r\n", "ID", "DATA", "HORA", "ACAO", "PARAMETROS", "ESTADO");
				uart_printf("%-3s %-12s %-9s %-9s %-12s %s\r\n", "--", "------------", "--------", "--------", "------------", "-------");
				for (int i = 0; i < appointments_count; i++) {
					char time_buffer[9];
					seconds_to_time_str(appointments[i].time, time_buffer, sizeof(time_buffer));
					char date_buffer[16];
					if (appointments[i].date == 0) {
						snprintf(date_buffer, sizeof(date_buffer), "%s", "DIARIO");
					} else {
						uint32_t date = appointments[i].date;
						int year = date / 10000;
						int month = (date / 100) % 100;
						int day = date % 100;
						snprintf(date_buffer, sizeof(date_buffer), "%04d-%02d-%02d", year, month, day);
					}

					uart_printf("%-3u %-12s %-9s %-9s %-12s %s\r\n", appointments[i].id, date_buffer, time_buffer, appointments[i].action, appointments[i].params, appointments[i].state ? "ativo" : "inativo");
				}
				unlock_appointments(false);

			} else if (strncmp(buffer, "sched add ", 10) == 0) {
				int year, month, day;
				int hours, minutes, seconds;
				int rate_ms, duration_s;
				char action[32];

				if (!parse_appointment_command(buffer, &year, &month, &day, &hours, &minutes, &seconds,action, &rate_ms, &duration_s)) {
					uart_printf("ERRO FORMATO INVALIDO\r\n");
					continue;
				}

				if (strcmp(action, "pisca") == 0 && (rate_ms <= 0 || duration_s <= 0)) {
					uart_printf("ERRO PARAMETROS INVALIDOS\r\n");
					continue;
				}

				uint32_t appointment_time = (uint32_t)hours * 3600U + (uint32_t)minutes * 60U + (uint32_t)seconds;
				uint32_t appointment_date = 0;
				if (year != 0) {
					appointment_date = make_date(year, month, day);
				}

				lock_appointments();
				if (appointments_count >= MAX_APPOINTMENTS) {
					unlock_appointments(false);
					uart_printf("ERRO LISTA DE AGENDAMENTOS CHEIA\r\n");
					continue;
				}

				bool duplicate = false;

				for (int i = 0; i < appointments_count; i++) {
					if (appointments[i].date == appointment_date && appointments[i].time == appointment_time) {
						duplicate = true;
						break;
					}
				}

				if (duplicate) {
					unlock_appointments(false);
					uart_printf("ERRO AGENDAMENTO DUPLICADO\r\n");
					continue;
				}

				uint8_t appointment_id = get_available_appointment_id();
				if (appointment_id == 0xFF) {
					unlock_appointments(false);
					uart_printf("ERRO SEM IDS DISPONIVEIS\r\n");
					continue;
				}

				appointment_t *appointment = &appointments[appointments_count];
				memset(appointment, 0, sizeof(appointment_t));
				appointment->id = appointment_id;
				appointment->date = appointment_date;
				appointment->time = appointment_time;
				appointment->state = true;
				snprintf(appointment->action, sizeof(appointment->action), "%s", action);
				if (strcmp(action, "pisca") == 0) {
					snprintf(appointment->params, sizeof(appointment->params), "%d %d", rate_ms,duration_s);
				}

				appointments_count++;
				unlock_appointments(true);
				if (appointment_date == 0) {
					uart_printf("{\"event\":\"sched_add\",\"id\":%u,\"date\":\"daily\",\"time\":\"%02d:%02d:%02d\",\"action\":\"%s\"}\r\n", appointment->id, hours, minutes, seconds, appointment->action);
				} else {
					uart_printf("{\"event\":\"sched_add\",\"id\":%u,\"date\":\"%04d-%02d-%02d\",\"time\":\"%02d:%02d:%02d\",\"action\":\"%s\"}\r\n", appointment->id, year, month, day, hours, minutes, seconds, appointment->action);
				}

			} else if (strcmp(buffer, "sched clear") == 0) {
				esp_err_t err = appointments_clear();
				if (err != ESP_OK) {
					uart_printf("ERRO AO LIMPAR AGENDAMENTOS\r\n");
					continue;
				}

				uart_printf("{\"event\":\"sched_clear\",\"message\":\"agendamentos removidos\"}\r\n");

			} else if (strncmp(buffer, "sched del ", 10) == 0) {
				int id;
				if (sscanf(buffer, "sched del %d", &id) != 1) {
					uart_printf("ERRO FORMATO INVALIDO\r\n");
					continue;
				}

				lock_appointments();
				int index = -1;
				for (int i = 0; i < appointments_count; i++) {
					if (appointments[i].id == id) {
						index = i;
						break;
					}
				}

				if (index == -1) {
					unlock_appointments(false);
					uart_printf("ERRO ID INVALIDO\r\n");
					continue;
				}

				for (int i = index; i < appointments_count - 1; i++) {
					appointments[i] = appointments[i + 1];
				}

				appointments_count--;
				memset(&appointments[appointments_count], 0, sizeof(appointment_t));
				unlock_appointments(true);
				uart_printf("{\"event\":\"sched_del\",\"id\":%u\"}\r\n", id);

			} else if (strncmp(buffer, "sched in ", 9) == 0) {
				int delay_s;
				int rate_ms;
				int duration_s;
				char action[32];

				if (!parse_appointment_in_command(buffer, &delay_s, action, sizeof(action), &rate_ms, &duration_s)) {
					uart_printf("ERRO FORMATO INVALIDO\r\n");
					continue;
				}

				lock_appointments();
				if (appointments_count >= MAX_APPOINTMENTS) {
					unlock_appointments(false);
					uart_printf("ERRO LISTA DE AGENDAMENTOS CHEIA\r\n");
					continue;
				}

				int appointment_id = get_available_appointment_id();
				if (appointment_id == -1) {
					unlock_appointments(false);
					uart_printf("ERRO SEM IDS DISPONIVEIS\r\n");
					continue;
				}

				time_t now;
				time(&now);
				struct tm local_time;
				if (localtime_r(&now, &local_time) == NULL) {
					unlock_appointments(false);
					uart_printf("ERRO GET HORA\r\n");
					continue;
				}

				uint32_t now_seconds = local_time.tm_hour * 3600U + local_time.tm_min * 60U + local_time.tm_sec;
				uint32_t appointment_time = (now_seconds + delay_s) % (24U * 3600U);
				appointment_t new_appointment;
				new_appointment.id = appointment_id;
				new_appointment.date = 0;
				new_appointment.time = appointment_time;
				new_appointment.state = true;

				snprintf(new_appointment.action, sizeof(new_appointment.action), "%s", action);
				if (strcmp(action, "pisca") == 0) {
					snprintf(new_appointment.params, sizeof(new_appointment.params), "%d %d", rate_ms, duration_s);
				} else {
					new_appointment.params[0] = '\0';
				}

				appointments[appointments_count] = new_appointment;
				appointments_count++;
				unlock_appointments(true);
				char time_buffer[9];
				seconds_to_time_str(new_appointment.time, time_buffer, sizeof(time_buffer));
				uart_printf("{\"event\":\"sched_in\",\"id\":%u,\"timestamp\":%lu,\"time\":\"%s\",\"action\":\"%s\"}\r\n", new_appointment.id, (unsigned long) new_appointment.time, time_buffer, new_appointment.action);

			} else if (strncmp(buffer, "wifi set ssid ", 14) == 0) {
				const char *value = buffer + 14;
				if (*value == '\0') {
					uart_printf("ERRO NOME DO WIFI INVALIDO\r\n");
					continue;
				}
				
				if (strlen(value) >= sizeof(wifi_ssid)) {
					uart_printf("ERRO NOME DO WIFI LONGO\r\n");
					continue;
				}

				strcpy(wifi_ssid, value);
				if (save_wifi_credentials(wifi_ssid, wifi_password) != ESP_OK) {
					uart_printf("ERRO AO SALVAR CREDENCIAIS WIFI\r\n");
					continue;
				}

				uart_printf("Nome do Wi-Fi salvo na flash.\r\n");

			} else if (strncmp(buffer, "wifi set password ", 18) == 0) {
				const char *value = buffer + 18;
				if (strlen(value) >= sizeof(wifi_password)) {
					uart_printf("ERRO SENHA LONGA\r\n");
					continue;
				}

				strcpy(wifi_password, value);
				if (save_wifi_credentials(wifi_ssid, wifi_password) != ESP_OK) {
					uart_printf("ERRO AO SALVAR CREDENCIAIS WIFI\r\n");
					continue;
				}

				uart_printf("Senha do Wi-Fi salva na flash.\r\n");

			} else if (strcmp(buffer, "wifi apply") == 0) {
				if (wifi_ssid[0] == '\0') {
					uart_printf("ERRO WIFI NAO CONFIGURADO\r\n");
					continue;
				}

				if (wifi_apply() != ESP_OK) {
					uart_printf("Configuracoes Wi-Fi aplicadas.\r\n");
				} else  {
					uart_printf("Erro ao aplicar configuracoes WiFi.\r\n");
				}
			}
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void appointmentTask(void *parameters) {
	while (1) {
		time_t now;
		time(&now);
		struct tm local_time;
		if (localtime_r(&now, &local_time) == NULL) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		uint32_t now_seconds = (uint32_t)local_time.tm_hour * 3600U + (uint32_t)local_time.tm_min * 60U + (uint32_t)local_time.tm_sec;
		uint32_t today = (uint32_t)(local_time.tm_year + 1900) * 10000U + (uint32_t)(local_time.tm_mon + 1) * 100U + (uint32_t)local_time.tm_mday;

		bool changed = false;
		lock_appointments();
		for (int i = 0; i < appointments_count; i++) {
			appointment_t *appointment = &appointments[i];

			if (!appointment->state) {
				continue;
			}

			if (appointment->time != now_seconds) {
				continue;
			}

			if (appointment->date != 0 && appointment->date != today) {
				continue;
			}

			execute_appointment_action(appointment);
			if (appointment->date != 0) {
				changed = true;
			}
		}

		unlock_appointments(changed);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

void app_main(void) {
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
	}

	uart_config_t config = {
		.baud_rate = UART_BAUD_RATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT
	};

	ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 1024, 1024, 0, NULL, 0));
	ESP_ERROR_CHECK(uart_param_config(UART_PORT, &config));

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	appointments_mutex = xSemaphoreCreateMutex();
	ntp_done_queue = xQueueCreate(4, sizeof(bool));
	blink_queue = xQueueCreate(4, sizeof(blink_request_t));
	if (appointments_mutex == NULL || ntp_done_queue == NULL || blink_queue == NULL) {
		ESP_LOGE("ESP32", "Falha ao criar objetos de sincronizacao RTOS");
		return;
	}

	setenv("TZ", "UTC+3", 1); // UTC-3 = UTC+3: POSIX convention
	tzset();

	gpio_config_t led_conf = {
		.pin_bit_mask = (1ULL << LED_GPIO),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};

	ESP_ERROR_CHECK(gpio_config(&led_conf));
	gpio_set_level(LED_GPIO, 0);

	if (appointments_load() != ESP_OK) {
		ESP_LOGW("ESP32", "Falha ao carregar agendamentos");
	}

	esp_err_t wifi_err = wifi_init(wifi_ssid, sizeof(wifi_ssid), wifi_password, sizeof(wifi_password));
	if (wifi_err != ESP_OK) {
		ESP_LOGW("WIFI", "Wi-Fi indisponivel: %s", esp_err_to_name(wifi_err));
	}

	if (xTaskCreatePinnedToCore(mainTask, "mainTask", 4096, NULL, 1, NULL, 0) != pdPASS) {
		ESP_LOGE("ESP32", "Falha ao criar mainTask");
		return;
	}

	if (xTaskCreatePinnedToCore( appointmentTask, "appointmentTask", 4096, NULL, 1, NULL, 1) != pdPASS) {
		ESP_LOGE("ESP32", "Falha ao criar appointmentTask");
		return;
	}

	if (xTaskCreatePinnedToCore(led_blink_task, "led_blink_task", 4096, NULL, 2, NULL, 1) != pdPASS) {
		ESP_LOGE("ESP32", "Falha ao criar led_blink_task");
		return;
	}

	uart_printf("ESP32 PRONTO\r\n");
}
