//Wallys Josep 2024-0899
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "protocol_examples_common.h"
#include "mqtt_client.h"
#include "driver/gpio.h" // <-- agregado

// Defines Estados de la maquina de estado
#define ESTADO_INIT_CONFIG 0
#define ESTADO_CERRANDO    1
#define ESTADO_ABRIENDO    2
#define ESTADO_ABIERTO     3
#define ESTADO_CERRADO     4
#define ESTADO_ERROR       5
#define ESTADO_PARADO      6

#define LAMPARA_OFF        0
#define LAMPARA_MOV        1
#define LAMPARA_ERR        2
#define LAMPARA_PARADA     3

#define GPIO_LED 2 // LED integrado ESP32

#define TAG "PORTON_MQTT"

// Variables globales del proyecto
typedef struct {
    uint8_t lsa;
    uint8_t lsc;
    uint8_t ma;
    uint8_t mc;
    uint8_t lamp;
} IO_t;

static IO_t io;
static int EstadoActual = ESTADO_INIT_CONFIG;
static int EstadoSiguiente = ESTADO_INIT_CONFIG;
static int EstadoAnterior = ESTADO_INIT_CONFIG;
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Algunos prototipos
static void Timer50msCallback(TimerHandle_t xTimer);
static void LamparaTask(void *pvParameters);
static void fsm_update(void);
static void mqtt_app_start(void);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void enviar_estado_mqtt(const char *estado);

//  MQTT EVENT HANDLER
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Conectado al broker MQTT!");
            esp_mqtt_client_subscribe(mqtt_client, "easy-learning/puerta/cmd", 0);
            break;

        case MQTT_EVENT_DATA:
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);

            if (strncmp(event->data, "abrir el puerton del itla", event->data_len) == 0) {
                EstadoSiguiente = ESTADO_ABRIENDO;
            } else if (strncmp(event->data, "cerrar el puerton del itla", event->data_len) == 0) {
                EstadoSiguiente = ESTADO_CERRANDO;
            } else if (strncmp(event->data, "parar la puerta esa", event->data_len) == 0) {
                EstadoSiguiente = ESTADO_PARADO;
            }
            break;

        default:
            break;
    }
}

// MQTT INIT 
static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "ws://broker.emqx.io:8083/mqtt",
        .credentials.username = "easy-learning",
        .credentials.authentication.password = "demo-para-el-canal"
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// FSM LOGIC 
static void fsm_update(void)
{
    // Guardar el estado anterior antes de cambiar
    EstadoAnterior = EstadoActual;

    switch (EstadoSiguiente)
    {
        case ESTADO_INIT_CONFIG:
            ESP_LOGI(TAG, "Estado: INIT_CONFIG");
            EstadoActual = ESTADO_INIT_CONFIG;
            io.lamp = LAMPARA_OFF;
            EstadoSiguiente = ESTADO_CERRADO;
            enviar_estado_mqtt("init");
            break;

        case ESTADO_ABRIENDO:
            ESP_LOGI(TAG, "Estado: ABRIENDO (Anterior: %d)", EstadoAnterior);
            io.ma = 1; io.mc = 0;
            io.lamp = LAMPARA_MOV;
            gpio_set_level(GPIO_LED, 1);
            EstadoActual = ESTADO_ABRIENDO;
            enviar_estado_mqtt("abriendo");
            vTaskDelay(pdMS_TO_TICKS(3000));
            EstadoSiguiente = ESTADO_ABIERTO;
            break;

        case ESTADO_ABIERTO:
            ESP_LOGI(TAG, "Estado: ABIERTO (Anterior: %d)", EstadoAnterior);
            io.ma = io.mc = 0;
            io.lamp = LAMPARA_OFF;
            gpio_set_level(GPIO_LED, 0);
            enviar_estado_mqtt("abierto");
            break;

        case ESTADO_CERRANDO:
            ESP_LOGI(TAG, "Estado: CERRANDO (Anterior: %d)", EstadoAnterior);
            io.ma = 0; io.mc = 1;
            io.lamp = LAMPARA_MOV;
            gpio_set_level(GPIO_LED, 1);
            EstadoActual = ESTADO_CERRANDO;
            enviar_estado_mqtt("cerrando");
            vTaskDelay(pdMS_TO_TICKS(3000));
            EstadoSiguiente = ESTADO_CERRADO;
            break;

        case ESTADO_CERRADO:
            ESP_LOGI(TAG, "Estado: CERRADO (Anterior: %d)", EstadoAnterior);
            io.ma = io.mc = 0;
            io.lamp = LAMPARA_OFF;
            gpio_set_level(GPIO_LED, 0);
            enviar_estado_mqtt("cerrado");
            break;

        case ESTADO_PARADO:
            ESP_LOGI(TAG, "Estado: PARADO (Anterior: %d)", EstadoAnterior);
            io.ma = io.mc = 0;
            io.lamp = LAMPARA_PARADA;
            enviar_estado_mqtt("parado");
            break;

        default:
            ESP_LOGE(TAG, "Estado desconocido");
            break;
    }
}

// TIMER CALLBACK 
static void Timer50msCallback(TimerHandle_t xTimer)
{
    // Ejemplo de tarea periódica: monitorear o imprimir estado
    ESP_LOGD(TAG, "Timer 50ms activo. Estado actual: %d", EstadoActual);
}

// LÁMPARA TASK 
static void LamparaTask(void *pvParameters)
{
    while (1)
    {
        switch (io.lamp)
        {
            case LAMPARA_OFF:
                break;
            case LAMPARA_MOV:
                // LED ya manejado en FSM
                break;
            case LAMPARA_PARADA:
                gpio_set_level(GPIO_LED, 1);
                vTaskDelay(pdMS_TO_TICKS(300));
                gpio_set_level(GPIO_LED, 0);
                vTaskDelay(pdMS_TO_TICKS(300));
                continue;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// MQTT PUBLISH
static void enviar_estado_mqtt(const char *estado)
{
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"estado\":\"%s\"}", estado);
    esp_mqtt_client_publish(mqtt_client, "easy-learning/puerta/status", payload, 0, 0, 0);
}

// APP MAIN 
void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando sistema...");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    // Configurar LED GPIO2
    gpio_reset_pin(GPIO_LED);
    gpio_set_direction(GPIO_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_LED, 0);

    mqtt_app_start();

    // Crear e iniciar el timer de 50 ms
    TimerHandle_t timer50ms = xTimerCreate("Timer50ms", pdMS_TO_TICKS(50), pdTRUE, NULL, Timer50msCallback);
    xTimerStart(timer50ms, 0);

    // Crear tarea de lámpara
    xTaskCreate(LamparaTask, "LamparaTask", 2048, NULL, 5, NULL);

    // Bucle principal de la FSM
    while (1)
    {
        fsm_update();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Yts renovable resolviendo como se puede
