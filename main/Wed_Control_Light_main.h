#include "esp_http_server.h"

#define HTTPD_RESP_USE_STRLEN -1

#define PWM_huang   4
#define PWM_bai   14
#define PWM_PERIOD    (500)

esp_err_t configure_server();

/**
 * @brief  Wifi 连接
 *         默认为 AP 模式 SSID:OK PASSWORD:ok201314
 *         设置 nvs 的 wifi_connect 命名空间中的
 *         AP_SSID AP_PASSWORD 自定义 AP
 *         STA_SSID STA_PASSWORD 连接 WIFI
 */
esp_err_t wifi_connect();

/**
 * @brief   API to send a complete string as HTTP response.
 *
 * This API simply calls http_resp_send with buffer length
 * set to string length assuming the buffer contains a null
 * terminated string
 *
 * @param[in] r         The request being responded to
 * @param[in] str       String to be sent as response body
 *
 * @return
 *  - ESP_OK : On successfully sending the response packet
 *  - ESP_ERR_INVALID_ARG : Null request pointer
 *  - ESP_ERR_HTTPD_RESP_HDR    : Essential headers are too large for internal buffer
 *  - ESP_ERR_HTTPD_RESP_SEND   : Error in raw send
 *  - ESP_ERR_HTTPD_INVALID_REQ : Invalid request
 */
static inline esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str) {
    return httpd_resp_send(r, str, (str == NULL) ? 0 : HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief   API to send a string as an HTTP response chunk.
 *
 * This API simply calls http_resp_send_chunk with buffer length
 * set to string length assuming the buffer contains a null
 * terminated string
 *
 * @param[in] r    The request being responded to
 * @param[in] str  String to be sent as response body (NULL to finish response packet)
 *
 * @return
 *  - ESP_OK : On successfully sending the response packet
 *  - ESP_ERR_INVALID_ARG : Null request pointer
 *  - ESP_ERR_HTTPD_RESP_HDR    : Essential headers are too large for internal buffer
 *  - ESP_ERR_HTTPD_RESP_SEND   : Error in raw send
 *  - ESP_ERR_HTTPD_INVALID_REQ : Invalid request
 */
static inline esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *str) {
    return httpd_resp_send_chunk(r, str, (str == NULL) ? 0 : HTTPD_RESP_USE_STRLEN);
}