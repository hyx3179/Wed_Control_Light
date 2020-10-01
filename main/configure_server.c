#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "driver/pwm.h"

#include "Wed_Control_Light_main.h"

#define TAG "configure_server"

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
    const char* resp_str = (const char*) req->user_ctx;

    pwm_set_duty(0,10);

    ESP_LOGI(TAG, resp_str);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t bai_handler(httpd_req_t *req)
{
    const char* resp_str = (const char*) req->user_ctx;

    pwm_set_duty(1,10);

    ESP_LOGI(TAG, resp_str);
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
    config.max_uri_handlers = 14;

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

    httpd_uri_t huang = {
        .uri = "/huang_0",
        .method = HTTP_POST,
        .handler = huang_handler,
        .user_ctx = "0"
    };
    httpd_register_uri_handler(server, &huang);

    huang.uri = "/huang_20";
    huang.user_ctx = "20";
    httpd_register_uri_handler(server, &huang);

    huang.uri = "/huang_40";
    huang.user_ctx = "40";
    httpd_register_uri_handler(server, &huang);

    huang.uri = "/huang_60";
    huang.user_ctx = "60";
    httpd_register_uri_handler(server, &huang);

    huang.uri = "/huang_80";
    huang.user_ctx = "80";
    httpd_register_uri_handler(server, &huang);

    huang.uri = "/huang_100";
    huang.user_ctx = "100";
    httpd_register_uri_handler(server, &huang);

    httpd_uri_t bai = {
        .uri = "/bai_0",
        .method = HTTP_POST,
        .handler = bai_handler,
        .user_ctx = "0"
    };
    httpd_register_uri_handler(server, &bai);
    
    bai.uri = "/bai_20";
    bai.user_ctx = "20";
    httpd_register_uri_handler(server, &bai);

    bai.uri = "/bai_40";
    bai.user_ctx = "40";
    httpd_register_uri_handler(server, &bai);

    bai.uri = "/bai_60";
    bai.user_ctx = "60";
    httpd_register_uri_handler(server, &bai);

    bai.uri = "/bai_80";
    bai.user_ctx = "80";
    httpd_register_uri_handler(server, &bai);

    bai.uri = "/bai_100";
    bai.user_ctx = "100";
    httpd_register_uri_handler(server, &bai);

    return ESP_OK;
}
