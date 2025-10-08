//Wallys Josep 2024-0899
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdint.h> // necesario para int32_t

static const char *TAG = "NVS_NUMBER";  // etiqueta para logs
nvs_handle_t app_nvs_handle;            // manejador de la NVS
int32_t stored_number = 0;              // variable donde guardaremos el número

// Inicializa la memoria NVS
static esp_err_t init_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    error = nvs_open("storage", NVS_READWRITE, &app_nvs_handle);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Error abriendo NVS: %s", esp_err_to_name(error));
    } else {
        ESP_LOGI(TAG, "NVS inicializada correctamente");
    }
    return error;
}

// Leer un número guardado en NVS
static void read_number(const char *key, int32_t *value)
{
    esp_err_t error = nvs_get_i32(app_nvs_handle, key, value);
    if (error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "No se encontró el número, usando valor por defecto 0");
        *value = 0; // valor inicial
    } else if (error == ESP_OK) {
        ESP_LOGI(TAG, "Número leído de NVS: %d", *value);
    } else {
        ESP_LOGE(TAG, "Error leyendo de NVS: %s", esp_err_to_name(error));
    }
}

// Guardar un número en NVS
static void write_number(const char *key, int32_t value)
{
    esp_err_t error = nvs_set_i32(app_nvs_handle, key, value);
    if (error == ESP_OK) {
        nvs_commit(app_nvs_handle); // confirmar escritura
        ESP_LOGI(TAG, "Número guardado en NVS: %d", value);
    } else {
        ESP_LOGE(TAG, "Error escribiendo en NVS: %s", esp_err_to_name(error));
    }
}

void app_main(void)
{
    const char *key = "my_number";      // clave para almacenar el número
    init_nvs();                          // inicializar NVS
    read_number(key, &stored_number);    // leer último valor

    while (true) {
        stored_number++;                  // incrementar número
        if (stored_number > 10) {
            stored_number = 0;           // reinicia cuando pasa de 10
        }

        write_number(key, stored_number); // guardar en la flash
        ESP_LOGI(TAG, "Número actual: %d", stored_number); // mostrar en terminal
        vTaskDelay(pdMS_TO_TICKS(2000));  // espera 2 segundos
    }
}
