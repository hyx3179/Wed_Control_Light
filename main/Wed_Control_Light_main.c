#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "driver/pwm.h"

#include "Wed_Control_Light_main.h"

#define TAG "main"

const uint32_t pin_num[2] = {
    PWM_huang,
    PWM_bai
};

uint32_t duties[2] = {
    PWM_PERIOD/2, PWM_PERIOD/2
};

float phase[2] = {
    0, 0
};

void app_main(void)
{

    pwm_init(PWM_PERIOD, duties, 2, pin_num);
    pwm_set_phases(phase);
    pwm_start();
    
    esp_err_t ret;

    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_connect();

    /* Start the file server */
    configure_server();

    // start_ble_hid_server();

    // ESP_ERROR_CHECK(configure_key_scan_array());

    // xTaskCreatePinnedToCore(keyboard_scan, "keyboard_scan", 2048, NULL, 0, NULL, tskNO_AFFINITY);

}
