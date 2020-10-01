#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

#include "driver/pwm.h"

#include "Wed_Control_Light_main.h"

#define TAG "configure_server"
#define WIFI_TAG "wifi_connect"

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[]   asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

static esp_err_t http_resp_dir_html(httpd_req_t *req)
{
    extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t huang_handler(httpd_req_t *req)
{
    char buf[16] = {0};

    httpd_req_recv(req, buf, req->content_len);

    int duty_len = strlen(buf) - 5;
    char duty[duty_len + 1];
    memset(duty, 0, duty_len + 1);
    memcpy(&duty, buf + 5, duty_len);

    pwm_set_duty(0,atoi(duty));
    pwm_start();

    ESP_LOGI(TAG, duty);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t bai_handler(httpd_req_t *req)
{
    char buf[16] = {0};

    httpd_req_recv(req, buf, req->content_len);

    int duty_len = strlen(buf) - 5;
    char duty[duty_len + 1];
    memset(duty, 0, duty_len + 1);
    memcpy(&duty, buf + 5, duty_len);

    pwm_set_duty(1,atoi(duty));
    pwm_start();

    ESP_LOGI(TAG, duty);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t wifi_handler(httpd_req_t *req)
{
    char buf[128] = {0};
    char *passwed_p = "&PASSWORD=";

    httpd_req_recv(req, buf, req->content_len);
    passwed_p = strstr(buf, passwed_p);

    int ssid_len = passwed_p - buf - 5;
    char ssid[ssid_len + 1];
    memset(ssid, 0, ssid_len + 1);
    memcpy(&ssid, buf + 5, ssid_len);

    int passwed_len = strlen(buf) - ssid_len - 11;
    char passwed[passwed_len + 1];
    memset(passwed, 0, passwed_len + 1);
    memcpy(&passwed, buf + 5 + ssid_len + 10, passwed_len);

    nvs_handle handle;
    ESP_ERROR_CHECK(nvs_open(WIFI_TAG, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_str(handle, "STA_SSID", ssid));
    ESP_ERROR_CHECK(nvs_set_str(handle, "STA_PASSWORD", passwed));

    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);

    ESP_LOGI(TAG, ssid);
    ESP_LOGI(TAG, passwed);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

/* Function to start the file server */
esp_err_t configure_server()
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }

    /* URI handler for getting uploaded files */
    httpd_uri_t file_download = {
        .uri = "/",  // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = http_resp_dir_html,
        .user_ctx = NULL    // Pass server data as context
    };
    httpd_register_uri_handler(server, &file_download);

    httpd_uri_t favicon_get = {
        .uri = "/favicon.ico",  // Match all URIs of type /path/to/file
        .method = HTTP_GET,
        .handler = favicon_get_handler,
        .user_ctx = NULL    // Pass server data as context
    };
    httpd_register_uri_handler(server, &favicon_get);

    httpd_uri_t wifi = {
        .uri = "/wifi",
        .method = HTTP_POST,
        .handler = wifi_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &wifi);

    httpd_uri_t huang = {
        .uri = "/huang",
        .method = HTTP_POST,
        .handler = huang_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &huang);

    httpd_uri_t bai = {
        .uri = "/bai",
        .method = HTTP_POST,
        .handler = bai_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &bai);

    return ESP_OK;
}
