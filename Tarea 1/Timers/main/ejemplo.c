//wallys THE BEST 2024-0899

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/timers.h"

#define led1 2 // Aquí decimos que el LED está conectado al pin GPIO 2

uint8_t led_level = 0; // Esta variable guarda el estado del LED (encendido o apagado)
static const char *tag = "Main"; // Etiqueta que usamos para los mensajes del log
TimerHandle_t xTimers; // Variable para manejar el temporizador
int interval = 100; // Tiempo del timer en milisegundos (100 ms)
int timerId = 1; // ID del timer, por si luego se usan varios

// Declaramos las funciones que usaremos más abajo
esp_err_t init_led(void);
esp_err_t blink_led(void);
esp_err_t set_timer();

// Esta función se ejecuta automáticamente cada vez que el timer "dispara"
void vTimerCallback(TimerHandle_t pxTimer)
{
    ESP_LOGI(tag, "Event was called from timer"); // Solo muestra un mensaje en el monitor
    blink_led(); // Cambia el estado del LED (lo prende o lo apaga)
}

void app_main(void)
{
    // Esta es la función principal que corre al iniciar el ESP32
    init_led();  // Configura el pin del LED como salida
    set_timer(); // Configura y arranca el temporizador
}

// Esta función configura el LED (pin GPIO) para que funcione como salida
esp_err_t init_led(void)
{
    gpio_reset_pin(led1); // Resetea la configuración del pin por si estaba usado antes
    gpio_set_direction(led1, GPIO_MODE_OUTPUT); // Lo pone como pin de salida
    return ESP_OK; // Devuelve que todo salió bien
}

// Esta función cambia el estado del LED: si está encendido lo apaga, y viceversa
esp_err_t blink_led(void)
{
    led_level = !led_level; // Invierte el valor (0 -> 1 o 1 -> 0)
    gpio_set_level(led1, led_level); // Cambia el nivel del pin para prender o apagar el LED
    return ESP_OK;
}

// Esta función configura el timer para que se ejecute cada cierto tiempo
esp_err_t set_timer(void)
{
    ESP_LOGI(tag, "Timer init configuration"); // Mensaje para saber que se está configurando

    // Creamos el timer con los siguientes parámetros:
    xTimers = xTimerCreate("Timer",                   // Nombre del timer (solo para referencia)
                           (pdMS_TO_TICKS(interval)), // Cada cuánto se ejecuta (intervalo)
                           pdTRUE,                    // pdTRUE = el timer se repite automáticamente
                           (void *)timerId,           // ID del timer
                           vTimerCallback             // Función que se ejecuta cada vez que el timer se activa
    );

    // Verifica si el timer se creó correctamente
    if (xTimers == NULL)
    {
        // Si llega aquí, el timer no se creó
        ESP_LOGI(tag, "The timer was not created");
    }
    else
    {
        // Si el timer se creó, intentamos arrancarlo
        if (xTimerStart(xTimers, 0) != pdPASS)
        {
            // Si no se puede activar el timer, muestra un mensaje de error
            ESP_LOGI(tag, "The timer could not be set into the Active state");
        }
    }

    return ESP_OK; // Todo salió bien gracia a papa dio
}
