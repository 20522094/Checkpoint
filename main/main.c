#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/**
 * FreeRTOS
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

/**
 * WiFi
 */
#include "esp_wifi.h"

/**
 * Logs
 */
#include "esp_log.h"

/**
 * Callback
 */
#include "esp_event_loop.h"

/**
 * Drivers
 */
#include "nvs_flash.h"

/**
 * Aplications (App);
 */
#include "app.h"

/**
 * Mesh APP
 */
 #include "mesh.h"

/**
 * PINOUT; 
 */
#include "sys_config.h"

/**
 * Constants;
 */
static const char *TAG = "main: ";

/**
 * Prototypes Functions;
 */
void app_main( void );

/**
 * Program begins here:)
 */


void app_main( void )
{
     
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /**
     * Inicializa GPIOs;
     */
	gpios_setup();

    /**
     * Inicializa o stack mesh;
     */
	mesh_app_start();
}
