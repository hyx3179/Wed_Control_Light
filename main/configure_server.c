#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

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
    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

    /* Get handle to embedded file upload script */
    extern const unsigned char upload_script_start[] asm("_binary_upload_script_html_start");
    extern const unsigned char upload_script_end[]   asm("_binary_upload_script_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

    /* Send remaining chunk of HTML file to complete it */
    httpd_resp_sendstr_chunk(req, "</body></html>");

    /* Send empty chunk to signal HTTP response completion */
    httpd_resp_sendstr_chunk(req, NULL);
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

    // /* URI handler for uploading files to server */
    // httpd_uri_t file_upload = {
    //     .uri = "/upload/*",   // Match all URIs of type /upload/path/to/file
    //     .method = HTTP_POST,
    //     .handler = upload_post_handler,
    //     .user_ctx = NULL    // Pass server data as context
    // };
    // httpd_register_uri_handler(server, &file_upload);

    // /* URI handler for deleting files from server */
    // httpd_uri_t file_delete = {
    //     .uri = "/delete/*",   // Match all URIs of type /delete/path/to/file
    //     .method = HTTP_POST,
    //     .handler = delete_post_handler,
    //     .user_ctx = NULL    // Pass server data as context
    // };
    // httpd_register_uri_handler(server, &file_delete);

    // httpd_uri_t enable_macro = {
    //     .uri = "/enable/*",
    //     .method = HTTP_POST,
    //     .handler = enable_macro_handler,
    //     .user_ctx = server_data
    // };
    // httpd_register_uri_handler(server, &enable_macro);

    // httpd_uri_t send_macro = {
    //     .uri = "/send/*",
    //     .method = HTTP_POST,
    //     .handler = send_macro_handler,
    //     .user_ctx = server_data
    // };
    // httpd_register_uri_handler(server, &send_macro);

    return ESP_OK;
}
