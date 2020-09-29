#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"

#define TAG "wifi_connect"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY_NUM 3

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_netif_initialized = false;

static void sta_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 0) {
            ESP_LOGE(TAG, "Disconnected from AP, Retry in %d second", (-s_retry_num) * 2);
            vTaskDelay((-s_retry_num) * 1000 / portTICK_PERIOD_MS);
            esp_wifi_connect();
            s_retry_num *= 2;
        } else if (s_retry_num < MAX_RETRY_NUM) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        if (s_retry_num >= 0) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        s_retry_num = -1;
    }
}

static esp_err_t fast_connect(char *ssid, char *password)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event_handler, NULL));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    memcpy(&wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    memcpy(&wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());


    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        vEventGroupDelete(s_wifi_event_group);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_ERROR_CHECK(esp_wifi_stop());
        vEventGroupDelete(s_wifi_event_group);
        return ESP_ERR_INVALID_ARG;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t start_ap(char *ssid, char *password)
{
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    memcpy(&wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));
    memcpy(&wifi_config.ap.password, password, sizeof(wifi_config.ap.password));
    if (password == NULL) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    }
    wifi_config.ap.max_connection = 1;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

esp_err_t wifi_connect()
{
    esp_err_t ret;
    nvs_handle connect_handle;

    if (!s_netif_initialized) {
        s_netif_initialized = true;
        tcpip_adapter_init();
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    }

    ESP_ERROR_CHECK(nvs_open(TAG, NVS_READWRITE, &connect_handle));
    size_t ssid_len, password_len;

    if ((ret = nvs_get_str(connect_handle, "STA_SSID", NULL, &ssid_len)) == ESP_OK) {
        char str_ssid[ssid_len];
        ESP_ERROR_CHECK(nvs_get_str(connect_handle, "STA_SSID", &str_ssid[0], &ssid_len));
        if ((ret = nvs_get_str(connect_handle, "STA_PASSWORD", NULL, &password_len)) == ESP_OK) {
            char str_password[password_len];
            ESP_ERROR_CHECK(nvs_get_str(connect_handle, "STA_PASSWORD", &str_password[0], &password_len));
            ret = fast_connect(str_ssid, str_password);
        } else {
            ret = fast_connect(str_ssid, NULL);
        }
    }
    if (ret != ESP_OK) {
        if ((ret = nvs_get_str(connect_handle, "AP_SSID", NULL, &ssid_len)) == ESP_OK) {
            char ap_ssid[ssid_len];
            ESP_ERROR_CHECK(nvs_get_str(connect_handle, "AP_SSID", &ap_ssid[0], &ssid_len));
            if ((ret = nvs_get_str(connect_handle, "AP_PASSWORD", NULL, &password_len)) == ESP_OK) {
                char ap_password[password_len];
                ESP_ERROR_CHECK(nvs_get_str(connect_handle, "AP_PASSWORD", &ap_password[0], &password_len));
                start_ap(ap_ssid, ap_password);
            } else {
                start_ap(ap_ssid, NULL);
            }
        } else {
            char ap_ssid[] = "OK", ap_password[] = "ok201314";
            start_ap(ap_ssid, ap_password);
        }
    }

    nvs_close(connect_handle);
    return ret;
}
