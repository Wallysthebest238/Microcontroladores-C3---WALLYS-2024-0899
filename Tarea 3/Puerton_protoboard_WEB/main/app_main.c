//Wallys Josep Hernandez Baez 2024-0899

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"

extern void example_register_wifi_connect_commands(void);

// NVS
#define NVS_NAMESPACE "wifi_cfg"

// WiFi retry
#define WIFI_RETRY_MAX 6

// Pines 
#define MOTOR_ARR GPIO_NUM_5    // Motor Abrir
#define MOTOR_CIE GPIO_NUM_18   // Motor Cerrar
#define BUZZER GPIO_NUM_27
#define LAMP GPIO_NUM_26
#define BTN_ARR GPIO_NUM_33
#define BTN_CIE GPIO_NUM_32
#define BTN_STOP GPIO_NUM_12
#define FIN_ARR GPIO_NUM_17    
#define FIN_CIE GPIO_NUM_14     
#define LDR_PIN GPIO_NUM_13     // Fotoselda Activo en basicamente Oscuridad

typedef enum
{
    INI,    
    CERR,   
    ABR,    
    CIER,   
    ABIE,   
    ERR,    
    STOP,   
    EMERG   
} estado_t;

volatile estado_t estAct = INI, estSig = INI;
volatile estado_t estPrev = INI; // estado previo para invertir
volatile bool init_accion = true;

struct
{
    uint8_t MA : 1, MC : 1, BZ : 1, LAMP : 1;
} io;
static bool lamp_on = false;
static int64_t last_time = 0, blink_ms = 0;

static EventGroupHandle_t wifi_events;
#define WIFI_OK BIT0
#define WIFI_FAIL BIT1

esp_mqtt_client_handle_t mqtt_client = NULL;
static httpd_handle_t web_server = NULL;

static const char *TAG = "porton";

// CONFIGURACIÓN de la NVS valores de base osea mi internet
#define DEF_WIFI_SSID "Willy"
#define DEF_WIFI_PASS "Nancy2012"
#define DEF_MQTT_SERVER "mqtt://test.mosquitto.org:1883"
#define DEF_MQTT_CMD "itla_wally/cmd"
#define DEF_MQTT_STATE "itla_wally/estado"

typedef struct
{
    char ssid[32];
    char pass[64];
    char mqtt_server[128];
    char mqtt_cmd[64];
    char mqtt_state[64];
} app_config_t;

static app_config_t cfg_global;

// DECLARACIONES DE FUNCIONES CLAVE (Prototipos)

void fsm_set_state(estado_t s); 
static void fsm_execute_core(void);
static void actualizar_io(void);


static void save_config(const app_config_t *cfg)
{
    nvs_handle_t nvsh;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvsh) == ESP_OK)
    {
        nvs_set_str(nvsh, "ssid", cfg->ssid);
        nvs_set_str(nvsh, "pass", cfg->pass);
        nvs_set_str(nvsh, "mqtt_server", cfg->mqtt_server);
        nvs_set_str(nvsh, "mqtt_cmd", cfg->mqtt_cmd);
        nvs_set_str(nvsh, "mqtt_state", cfg->mqtt_state);
        nvs_commit(nvsh);
        nvs_close(nvsh);
        ESP_LOGI(TAG, "Configuración guardada en NVS");
    }
    else
    {
        ESP_LOGE(TAG, "Error abriendo NVS para guardar configuración");
    }
}

static void load_config(app_config_t *cfg)
{
    nvs_handle_t nvsh;
    bool found = true;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsh) == ESP_OK)
    {
        size_t len;
        len = sizeof(cfg->ssid);
        if (nvs_get_str(nvsh, "ssid", cfg->ssid, &len) != ESP_OK)
            found = false;
        len = sizeof(cfg->pass);
        if (nvs_get_str(nvsh, "pass", cfg->pass, &len) != ESP_OK)
            found = false;
        len = sizeof(cfg->mqtt_server);
        if (nvs_get_str(nvsh, "mqtt_server", cfg->mqtt_server, &len) != ESP_OK)
            found = false;
        len = sizeof(cfg->mqtt_cmd);
        if (nvs_get_str(nvsh, "mqtt_cmd", cfg->mqtt_cmd, &len) != ESP_OK)
            found = false;
        len = sizeof(cfg->mqtt_state);
        if (nvs_get_str(nvsh, "mqtt_state", cfg->mqtt_state, &len) != ESP_OK)
            found = false;
        nvs_close(nvsh);
    }
    else
    {
        found = false;
    }

    if (!found)
    {
        ESP_LOGW(TAG, "No se encontró configuración en NVS. Usando valores por defecto.");
        strncpy(cfg->ssid, DEF_WIFI_SSID, sizeof(cfg->ssid) - 1);
        cfg->ssid[sizeof(cfg->ssid)-1] = '\0';
        strncpy(cfg->pass, DEF_WIFI_PASS, sizeof(cfg->pass) - 1);
        cfg->pass[sizeof(cfg->pass)-1] = '\0';
        strncpy(cfg->mqtt_server, DEF_MQTT_SERVER, sizeof(cfg->mqtt_server) - 1);
        cfg->mqtt_server[sizeof(cfg->mqtt_server)-1] = '\0';
        strncpy(cfg->mqtt_cmd, DEF_MQTT_CMD, sizeof(cfg->mqtt_cmd) - 1);
        cfg->mqtt_cmd[sizeof(cfg->mqtt_cmd)-1] = '\0';
        strncpy(cfg->mqtt_state, DEF_MQTT_STATE, sizeof(cfg->mqtt_state) - 1);
        cfg->mqtt_state[sizeof(cfg->mqtt_state)-1] = '\0';
        save_config(cfg);
    }
    else
    {
        ESP_LOGI(TAG, "Configuración cargada desde NVS");
    }

    ESP_LOGI(TAG, "SSID: %s", cfg->ssid);
    ESP_LOGI(TAG, "MQTT server: %s", cfg->mqtt_server);
    ESP_LOGI(TAG, "MQTT cmd: %s", cfg->mqtt_cmd);
    ESP_LOGI(TAG, "MQTT state: %s", cfg->mqtt_state);
}

// Claves: estado - string 

static const char *estado_to_str(estado_t e)
{
    switch (e)
    {
    case INI:
        return "inicial";
    case CERR:
        return "cerrando";
    case ABR:
        return "abriendo";
    case CIER:
        return "cerrado";
    case ABIE:
        return "abierto";
    case ERR:
        return "error";
    case STOP:
        return "detenido";
    case EMERG:
        return "emergencia";
    default:
        return "desconocido";
    }
}

// MQTT: publicar estado y manejo del mismo oh oh

static void mqtt_publicar_estado(estado_t e)
{
    if (mqtt_client)
    {
        const char *s = estado_to_str(e);
        esp_mqtt_client_publish(mqtt_client, cfg_global.mqtt_state, s, 0, 1, 0);
        ESP_LOGI(TAG, "Publicado estado MQTT: %s -> %s", cfg_global.mqtt_state, s);
    }
}

// forward
static void mqtt_cb(void *handler_args, esp_event_base_t base, int32_t id, void *d);

// iniciar mqtt usando cfg_global
static void start_mqtt(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = cfg_global.mqtt_server,
        .session.keepalive = 60,
        .network.disable_auto_reconnect = false,
    };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_cb, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// callback MQTT 
static void mqtt_cb(void *handler_args, esp_event_base_t base, int32_t id, void *d)
{
    esp_mqtt_event_handle_t e = d;
    mqtt_client = e->client;

    switch ((esp_mqtt_event_id_t)id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado");
        esp_mqtt_client_subscribe(mqtt_client, cfg_global.mqtt_cmd, 0);
        break;

    case MQTT_EVENT_DATA:
    {
        char msg[128] = {0};
        int l = (e->data_len < (int)sizeof(msg) - 1) ? e->data_len : (int)sizeof(msg) - 1;
        strncpy(msg, e->data, l);
        msg[l] = '\0';

        // limpiar y pasar a minúsculas
        char *p = msg;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')
            p++;
        if (p != msg)
            memmove(msg, p, strlen(p) + 1);
        for (int i = 0; msg[i]; i++)
            if (msg[i] >= 'A' && msg[i] <= 'Z')
                msg[i] += 'a' - 'A';

        ESP_LOGI(TAG, "MQTT CMD: %s", msg);

        if (strcmp(msg, "abrir") == 0)
        {
            if (estSig == ABR || estSig == ABIE)
            {
                ESP_LOGI(TAG, "Comando 'abrir' ignorado (ya abierto o abriendo)");
            }
            else
            {
                mqtt_publicar_estado(ABR);
                estPrev = estSig;
                fsm_set_state(ABR); 
            }
        }
        else if (strcmp(msg, "cerrar") == 0)
        {
            if (estSig == CERR || estSig == CIER)
            {
                ESP_LOGI(TAG, "Comando 'cerrar' ignorado (ya cerrado o cerrando)");
            }
            else
            {
                mqtt_publicar_estado(CERR);
                estPrev = estSig;
                fsm_set_state(CERR); 
            }
        }
        else if (strcmp(msg, "stop") == 0)
        {
            if (estSig == STOP)
            {
                if (estPrev == ABR)
                {
                    ESP_LOGI(TAG, "MQTT STOP doble: reanudando -> cerrando");
                    fsm_set_state(CERR); 
                }
                else if (estPrev == CERR)
                {
                    ESP_LOGI(TAG, "MQTT STOP doble: reanudando -> abriendo");
                    fsm_set_state(ABR); 
                }
                else
                {
                    ESP_LOGI(TAG, "MQTT STOP doble: no hay direccion previa");
                }
            }
            else if (estSig == ABR || estSig == CERR)
            {
                estPrev = estSig;
                ESP_LOGI(TAG, "MQTT STOP: pausando, estPrev=%s", estado_to_str(estPrev));
                fsm_set_state(STOP); 
            }
            else
            {
                ESP_LOGI(TAG, "MQTT STOP: ignorado (estado actual %s)", estado_to_str(estSig));
            }
        }
        else if (strcmp(msg, "reset") == 0)
        {
            fsm_set_state(INI); 
        }
        else if (strcmp(msg, "emergencia") == 0)
        {
            if (estSig == EMERG)
            {
                ESP_LOGI(TAG, "Comando 'emergencia' ignorado (ya en emergencia)");
            }
            else
            {
                mqtt_publicar_estado(EMERG);
                fsm_set_state(EMERG); 
            }
        }
        else
        {
            ESP_LOGW(TAG, "Comando desconocido: '%s'", msg);
        }
    }
    break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error de conexion MQTT");
        break;

    default:
        break;
    }
}

void fsm_set_state(estado_t s)
{
    if (estSig != s)
    {
        printf("Cambio de estado: %s -> %s\n", estado_to_str(estSig), estado_to_str(s));
        if (mqtt_client)
            esp_mqtt_client_publish(mqtt_client, cfg_global.mqtt_state, estado_to_str(s), 0, 1, 0);
    }
    estSig = s;
    init_accion = true;
    // Forzamos la ejecución para encender/apagar el motor de una ve mio
    fsm_execute_core();
}

// I/O helpers

static void actualizar_io(void)
{
    gpio_set_level(MOTOR_ARR, io.MA);
    gpio_set_level(MOTOR_CIE, io.MC);
    gpio_set_level(BUZZER, io.BZ);
    gpio_set_level(LAMP, io.LAMP);
}

static bool leer_btn(gpio_num_t p) { return gpio_get_level(p) == 0; }
static bool leer_fin(gpio_num_t p) { return gpio_get_level(p) == 0; } 

// GPIO setup

static void gpio_setup(void)
{
    gpio_config_t cfg = {0};
    cfg.intr_type = GPIO_INTR_DISABLE;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pin_bit_mask = ((uint64_t)1 << MOTOR_ARR) | ((uint64_t)1 << MOTOR_CIE) | ((uint64_t)1 << BUZZER) | ((uint64_t)1 << LAMP);
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&cfg);

    cfg.mode = GPIO_MODE_INPUT;
    // BTN y LDR con PULLUP (activo en LOW)
    cfg.pin_bit_mask = ((uint64_t)1 << BTN_ARR) | ((uint64_t)1 << BTN_CIE) | ((uint64_t)1 << BTN_STOP) | ((uint64_t)1 << LDR_PIN);
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&cfg);

    // Finales de carrera con PULLUP para que sean activos en LOW E MA SEGURO
    cfg.pin_bit_mask = ((uint64_t)1 << FIN_ARR) | ((uint64_t)1 << FIN_CIE);
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE; //Pull-up activado
    gpio_config(&cfg);

    // inicial IO
    io.MA = io.MC = io.BZ = io.LAMP = 0;
    actualizar_io();
}

// BOTONES Y FSM

static void handle_buttons(void)
{
    static bool a = 0, c = 0, s = 0; 
    bool btn_a = leer_btn(BTN_ARR);
    bool btn_c = leer_btn(BTN_CIE);
    bool btn_s = leer_btn(BTN_STOP);

    if (btn_a && !a) // Botón Abrir
    {
        if (estSig == ABR || estSig == ABIE)
        {
            printf("Boton 'abrir' ignorado (ya abierto o abriendo)\n");
        }
        else
        {
            estPrev = estSig; // Guarda estado previo para la inversion de STOP
            fsm_set_state(ABR);
        }
    }
    if (btn_c && !c) // Botón Cerrar (flanco de subida)
    {
        if (estSig == CERR || estSig == CIER)
        {
            printf("Boton 'cerrar' ignorado (ya cerrado o cerrando)\n");
        }
        else
        {
            estPrev = estSig; // Guarda estado previo para la inversion de STOP
            fsm_set_state(CERR);
        }
    }

    if (btn_s && !s) // Botón STOP/Invertir (flanco de subida)
    {
        if (estSig == STOP)
        {
            // STOP Doble: Reanudar en sentido inverso
            if (estPrev == ABR)
            {
                printf("Boton STOP doble: reanudando en sentido inverso -> cerrando\n");
                fsm_set_state(CERR);
            }
            else if (estPrev == CERR)
            {
                printf("Boton STOP doble: reanudando en sentido inverso -> abriendo\n");
                fsm_set_state(ABR);
            }
            else
            {
                printf("Boton STOP doble: no hay direccion previa para invertir (estPrev=%s).\n", estado_to_str(estPrev));
            }
        }
        else if (estSig == ABR || estSig == CERR)
        {
            // STOP Simple: Pausa el movimiento activo
            estPrev = estSig;
            printf("Boton STOP: pausando, estPrev=%s\n", estado_to_str(estPrev));
            fsm_set_state(STOP);
        }
        else
        {
            printf("Boton STOP: ignorado (estado actual %s)\n", estado_to_str(estSig));
        }
    }

    a = btn_a;
    c = btn_c;
    s = btn_s;
}

// FSM PRINCIPAL llamado periódicamente o forzado por fsm_set_state

static void fsm_execute_core(void)
{
    estado_t current_state = estSig; 
    int64_t now = esp_timer_get_time() / 1000;
    
    // Solo manejar botones si no estamos en un estado de error/emergencia que bloquee la entrada
    if (current_state != EMERG && current_state != ERR)
    {
        handle_buttons();
        current_state = estSig; // Refrescar por si handle_buttons cambió el estado
    }

    // Comprobación de emergencia de Fin de Carrera simultáneos
    if (leer_fin(FIN_ARR) && leer_fin(FIN_CIE) && current_state != ERR)
    {
        fsm_set_state(ERR);
        current_state = estSig; 
    }
    
    // LDR: Apertura automática por oscuridad (solo si está completamente cerrado)
    if (current_state == CIER && !init_accion && leer_btn(LDR_PIN))
    {
        printf("LDR detecta oscuridad. Abriendo porton automáticamente.\n");
        estPrev = CIER;
        fsm_set_state(ABR); 
        current_state = estSig; 
    }

    if (current_state == ABR && leer_fin(FIN_ARR))
    {
        // Motor abriendo Y Fin de carrera de APERTURA presionado.
        printf("FIN DE CARRERA ABIERTO (FIN_ARR): Transicionando a ABIE (Abierto)\n");
        fsm_set_state(ABIE);
        current_state = estSig; // Asegurar que el switch use ABIE y salga de ABR
    }
    else if (current_state == CERR && leer_fin(FIN_CIE))
    {
        // Motor cerrando Y Fin de carrera de CIERRE presionado.
        printf("FIN DE CARRERA CERRADO (FIN_CIE): Transicionando a CIER (Cerrado)\n");
        fsm_set_state(CIER);
        current_state = estSig; // Asegurar que el switch use CIER y salga de CERR
    }

    switch (current_state)
    {
    case INI:
        if (init_accion)
        {
            io.MA = io.MC = io.BZ = io.LAMP = 0;
            blink_ms = 0;
            actualizar_io();
            init_accion = false; 
        }
        break;

    case ABR:
    {
        static int64_t start_abr = 0;
        static bool abr_timer = false;

        if (init_accion)
        {
            // ACTIVACIÓN DE MOTOR ARRIBA
            io.MA = 1;
            io.MC = 0;
            io.BZ = 0;
            blink_ms = 500;
            lamp_on = false;
            last_time = now;
            actualizar_io(); 
            init_accion = false;
            abr_timer = false;
        }

        // Lógica de parpadeo (continúa ejecutándose)
        if (blink_ms > 0 && now - last_time >= blink_ms)
        {
            lamp_on = !lamp_on;
            io.LAMP = lamp_on;
            last_time = now;
            actualizar_io();
        }

        if (!abr_timer)
        {
            start_abr = now;
            abr_timer = true;
        }

        // Timer de 5 segundos (5000ms)
        if (now - start_abr >= 5000)
        {
            printf("TIMEOUT ABR: Transicionando a ABIE (Abierto)\n");
            fsm_set_state(ABIE);
            abr_timer = false;
        }
        break;
    }

    case CERR:
    {
        static int64_t start_cerr = 0;
        static bool cerr_timer = false;

        if (init_accion)
        {
            // ACTIVACIÓN DE MOTOR CERRAR
            io.MA = 0;
            io.MC = 1;
            io.BZ = 0;
            blink_ms = 250;
            lamp_on = false;
            last_time = now;
            actualizar_io(); 
            init_accion = false;
            cerr_timer = false;
        }

        // Lógica de parpadeo (continúa ejecutándose)
        if (blink_ms > 0 && now - last_time >= blink_ms)
        {
            lamp_on = !lamp_on;
            io.LAMP = lamp_on;
            last_time = now;
            actualizar_io();
        }

        if (!cerr_timer)
        {
            start_cerr = now;
            cerr_timer = true;
        }

        // Timer de 5 segundos (5000ms)
        if (now - start_cerr >= 5000)
        {
            printf("TIMEOUT CERR: Transicionando a CIER (Cerrado)\n");
            fsm_set_state(CIER);
            cerr_timer = false;
        }
        break;
    }

    case ABIE:
        if (init_accion)
        {
            io.MA = 0;
            io.MC = 0; 
            io.BZ = 0;
            io.LAMP = 1; // Lámpara encendida en estado abierto 
            actualizar_io();
            init_accion = false;
        }
        break;

    case CIER:
        if (init_accion)
        {
            io.MA = 0;
            io.MC = 0; 
            io.BZ = 0;
            io.LAMP = 0; // Lámpara apagada en estado cerrado
            actualizar_io();
            init_accion = false;
        }
        break;

    case STOP:
        if (init_accion)
        {
            io.MA = io.MC = io.BZ = io.LAMP = 0;
            actualizar_io();
            init_accion = false;
        }
        break;

    case EMERG:
        if (init_accion)
        {
            io.MA = io.MC = 0;
            io.BZ = 1;
            io.LAMP = 1;
            actualizar_io();
            init_accion = false;
        }
        break;

    case ERR:
        if (init_accion)
        {
            io.MA = io.MC = 0;
            io.BZ = 1;
            blink_ms = 300;
            lamp_on = false;
            last_time = now;
            actualizar_io();
            init_accion = false;
        }

        if (blink_ms > 0 && now - last_time >= blink_ms)
        {
            lamp_on = !lamp_on;
            io.LAMP = lamp_on;
            last_time = now;
            actualizar_io();
        }
        break;

    default:
        break;
    }

    estAct = estSig;
}

static void fsm_timer_cb(void *arg)
{
    (void)arg;
    fsm_execute_core();
}

// HTTP server: HTML + handlers (hecho con bakaneria)

static esp_err_t send_html(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static const char *html_page =
"<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>Control Portón</title>"
"<style>"
"body{background:#000;color:#fff;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial;margin:0;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;min-height:100vh;padding:20px;}"
".card{background:#111;width:100%;max-width:540px;border-radius:18px;padding:22px;box-shadow:0 6px 20px rgba(255,255,255,0.05);}"
"h1{text-align:center;font-weight:600;margin:0 0 16px 0;font-size:22px;}"
".row{display:flex;flex-wrap:wrap;justify-content:center;gap:12px;margin:16px 0;}"
".btn{padding:12px 18px;border:none;border-radius:12px;font-size:16px;cursor:pointer;min-width:120px;color:#fff;transition:transform 0.15s, box-shadow 0.2s;}"
".btn:active{transform:scale(0.95);box-shadow:0 0 10px rgba(255,255,255,0.3);}"
".abrir{background:#34c759;} .cerrar{background:#ff3b30;} .stop{background:#ffd60a;color:#111;} .emerg{background:#ff9500;}"
".status{margin-top:14px;font-size:16px;padding:12px;border-radius:10px;background:#1a1a1a;display:flex;flex-direction:column;gap:8px;box-shadow:inset 0 0 6px rgba(255,255,255,0.05);}"
".line{display:flex;justify-content:space-between;}"
".meta{font-size:13px;color:#aaa;margin-top:8px;text-align:center}"
".console{background:#0d0d0d;border-radius:10px;padding:10px;margin-top:12px;font-size:13px;max-height:100px;overflow-y:auto;color:#0f0;box-shadow:inset 0 0 6px rgba(0,255,0,0.2);white-space:pre-wrap;}"
"</style></head><body>"
"<div class='card'>"
"<h1>Control del Portón</h1>"
"<div class='row'>"
"<button class='btn abrir' onclick=\"enviar('abrir')\">Abrir</button>"
"<button class='btn cerrar' onclick=\"enviar('cerrar')\">Cerrar</button>"
"<button class='btn stop' onclick=\"enviar('stop')\">Stop</button>"
"<button class='btn emerg' onclick=\"enviar('emergencia')\">Emergencia</button>"
"</div>"
"<div class='status'>"
"<div class='line'><span>Estado:</span> <strong id='estado'>--</strong></div>"
"<div class='line'><span>Comando activo:</span> <span id='cmd_act'>--</span></div>"
"<div class='line'><span>Lámpara:</span> <span id='lamp'>--</span></div>"
"<div class='line'><span>Buzzer:</span> <span id='buzzer'>--</span></div>"
"<div class='line'><span>WiFi:</span> <span id='wifi'>--</span></div>"
"<div class='line'><span>IP:</span> <span id='ip'>--</span></div>"
"</div>"
"<div class='console' id='console'>Esperando acciones...</div>"
"</div>"
"<script>"
"let lastEstado=''; let currentCmd=''; let logs=[];"
"async function refresh(){"
"  try{"
"    let r=await fetch('/estado'); if(r.ok){let e=await r.text(); document.getElementById('estado').innerText=e;}"
"    let w=await fetch('/wifi'); if(w.ok){let t=await w.text(); let online=(t!='desconectado'); document.getElementById('wifi').innerText=online?'Conectado':'Desconectado'; document.getElementById('wifi').style.color=online?'#34c759':'#ff3b30'; document.getElementById('ip').innerText=online?t:'--'; }"
"    let bz = await fetch('/buzzer'); if(bz.ok){let b=await bz.text(); document.getElementById('buzzer').innerText=(b==='on')?'Encendido':'Apagado'; document.getElementById('buzzer').style.color=(b==='on')?'#ff3b30':'#888'; }"
"    let estado = document.getElementById('estado').innerText;"
"    let lampOn = (estado==='abierto' || estado==='emergencia' || estado==='abriendo' || estado==='cerrando' || estado==='error');" // Lógica de la lámpara en UI
"    document.getElementById('lamp').innerText = lampOn?'Encendida':'Apagada';"
"    document.getElementById('lamp').style.color = lampOn?'#ffd60a':'#888';"
"    if(currentCmd){"
"      if( (currentCmd==='abrir' && estado==='abierto') || (currentCmd==='cerrar' && estado==='cerrado') || estado==='error' || estado==='emergencia' || estado==='detenido' ){"
"        let msg = '< Terminando: '+currentCmd+' -> '+estado+' '; logs.push(msg); currentCmd=''; document.getElementById('cmd_act').innerText='--';"
"        if(logs.length>2) logs = logs.slice(-2);"
"        document.getElementById('console').innerText = logs.join('\\n');"
"      } else {"
"        document.getElementById('cmd_act').innerText = 'En proceso: '+currentCmd;"
"      }"
"    } else {"
"      document.getElementById('cmd_act').innerText='--';"
"    }"
"    lastEstado=estado;"
"  }catch(e){ document.getElementById('wifi').innerText='Desconectado'; document.getElementById('estado').innerText='--'; }"
"}"
"function addLog(s){ logs.push(s); if(logs.length>2) logs = logs.slice(-2); document.getElementById('console').innerText = logs.join('\\n'); }"
"async function enviar(cmd){"
"  addLog('> Enviando: '+cmd); currentCmd = cmd; document.getElementById('cmd_act').innerText = 'En proceso: '+cmd;"
"  try{ let r = await fetch('/'+cmd); if(r.ok){ addLog('< Ejecutado: '+cmd); } else { addLog('! Error enviando: '+cmd); currentCmd=''; } } catch(e){ addLog('! Error enviando: '+cmd); currentCmd=''; }"
"}"
"setInterval(refresh,1500); refresh();"
"</script></body></html>";

static esp_err_t root_get(httpd_req_t *req) { return send_html(req, html_page); }

static esp_err_t cmd_get(httpd_req_t *req)
{
    // La lógica de la FSM y los estados previos se manejan en handle_buttons/mqtt_cb
    // Aquí solo se llama a fsm_set_state/estPrev 
    
    if (strcmp(req->uri, "/abrir") == 0)
    {
        if (estSig == ABR || estSig == ABIE)
        {
            // ignorar
        }
        else
        {
            estPrev = estSig; 
            fsm_set_state(ABR);
        }
    }
    else if (strcmp(req->uri, "/cerrar") == 0)
    {
        if (estSig == CERR || estSig == CIER)
        {
            // ignorar
        }
        else
        {
            estPrev = estSig; 
            fsm_set_state(CERR);
        }
    }
    else if (strcmp(req->uri, "/stop") == 0)
    {
        if (estSig == STOP)
        {
            // Doble-PUSH: reanudar en sentido inverso
            if (estPrev == ABR)
            {
                fsm_set_state(CERR);
            }
            else if (estPrev == CERR)
            {
                fsm_set_state(ABR);
            }
        }
        else if (estSig == ABR || estSig == CERR)
        {
            // Stop simple
            estPrev = estSig;
            fsm_set_state(STOP);
        }
    }
    else if (strcmp(req->uri, "/emergencia") == 0)
    {
        if (estSig != EMERG)
            fsm_set_state(EMERG);
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t estado_get(httpd_req_t *req)
{
    httpd_resp_sendstr(req, estado_to_str(estSig));
    return ESP_OK;
}

static esp_err_t wifi_get(httpd_req_t *req)
{
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
    {
        if (ip_info.ip.addr != 0)
        {
            char ipstr[32];
            snprintf(ipstr, sizeof(ipstr), "%u.%u.%u.%u",
                     (uint8_t)((ip_info.ip.addr) & 0xFF),
                     (uint8_t)((ip_info.ip.addr >> 8) & 0xFF),
                     (uint8_t)((ip_info.ip.addr >> 16) & 0xFF),
                     (uint8_t)((ip_info.ip.addr >> 24) & 0xFF));
            httpd_resp_sendstr(req, ipstr);
            return ESP_OK;
        }
    }
    httpd_resp_sendstr(req, "desconectado");
    return ESP_OK;
}

// buzzer (on/off)
static esp_err_t buzzer_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    if (io.BZ)
        httpd_resp_sendstr(req, "on");
    else
        httpd_resp_sendstr(req, "off");
    return ESP_OK;
}

static esp_err_t favicon_get(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get, .user_ctx = NULL};
        httpd_uri_t abrir = {.uri = "/abrir", .method = HTTP_GET, .handler = cmd_get, .user_ctx = NULL};
        httpd_uri_t cerrar = {.uri = "/cerrar", .method = HTTP_GET, .handler = cmd_get, .user_ctx = NULL};
        httpd_uri_t stop = {.uri = "/stop", .method = HTTP_GET, .handler = cmd_get, .user_ctx = NULL};
        httpd_uri_t emerg = {.uri = "/emergencia", .method = HTTP_GET, .handler = cmd_get, .user_ctx = NULL};
        httpd_uri_t estado = {.uri = "/estado", .method = HTTP_GET, .handler = estado_get, .user_ctx = NULL};
        httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_GET, .handler = wifi_get, .user_ctx = NULL};
        httpd_uri_t buz = {.uri = "/buzzer", .method = HTTP_GET, .handler = buzzer_get, .user_ctx = NULL};
        httpd_uri_t fav = {.uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get, .user_ctx = NULL};

        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &abrir);
        httpd_register_uri_handler(server, &cerrar);
        httpd_register_uri_handler(server, &stop);
        httpd_register_uri_handler(server, &emerg);
        httpd_register_uri_handler(server, &estado);
        httpd_register_uri_handler(server, &wifi);
        httpd_register_uri_handler(server, &buz);
        httpd_register_uri_handler(server, &fav);
        ESP_LOGI(TAG, "Servidor web iniciado");
    }
    else
    {
        ESP_LOGE(TAG, "Error iniciando servidor web");
    }
    return server;
}

// WIFI event handler y init (usa cfg_global)

static void wifi_evt(void *a, esp_event_base_t b, int32_t e, void *d)
{
    (void)a; (void)d;
    if (b == WIFI_EVENT && e == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (b == WIFI_EVENT && e == WIFI_EVENT_STA_DISCONNECTED)
    {
        static int r = 0;
        if (r < WIFI_RETRY_MAX)
        {
            esp_wifi_connect();
            r++;
        }
        else
            xEventGroupSetBits(wifi_events, WIFI_FAIL);
    }
    else if (b == IP_EVENT && e == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "WiFi conectado. Iniciando servicios...");
        xEventGroupSetBits(wifi_events, WIFI_OK);
    }
}


// 🔑 FUNCIÓN MODIFICADA PARA USAR NVS (cfg_global)
static void wifi_init(void)
{
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // ** AQUÍ SE CREA LA INTERFAZ DE RED MUY JEVI **
    esp_netif_create_default_wifi_sta(); 

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // ** CONFIGURACIÓN CLAVE para usar NVS **
    wifi_config_t wifi_config = {
        .sta = {
            .bssid_set = false, 
            .listen_interval = 0,
        },
    };
    
    // Copiamos la configuración de NVS a la estructura de WiFi
    strncpy((char *)wifi_config.sta.ssid, cfg_global.ssid, sizeof(wifi_config.sta.ssid));
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0'; 

    strncpy((char *)wifi_config.sta.password, cfg_global.pass, sizeof(wifi_config.sta.password));
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';


    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_evt, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_evt, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
}

// MAIN

void app_main(void)
{
    // Carga la configuración de NVS (incluye WiFi/MQTT)
    load_config(&cfg_global);

    // Configuración de GPIOs (motores, botones, fines de carrera, ETC TU SABE
    gpio_setup();

    // Temporizador para el FSM core (ejecución periódica de 100ms)
    const esp_timer_create_args_t timer_args = {
        .callback = &fsm_timer_cb,
        .name = "fsm_timer"
    };
    esp_timer_handle_t fsm_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &fsm_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(fsm_timer, 100000)); // 100ms

    // Inicialización y conexión WiFi
    wifi_init();

    // Esperar a que se conecte WiFi o falle
    EventBits_t bits = xEventGroupWaitBits(wifi_events, WIFI_OK | WIFI_FAIL, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_OK) 
    {
        // WiFi OK: Iniciar MQTT y WebServer
        start_mqtt();
        web_server = start_webserver();
    } 
    else 
    {
        ESP_LOGE(TAG, "FALLO FATAL: No se pudo conectar a WiFi");
    }

    // Inicializar la consola para comandos WiFi (solo si se conecta)
    const esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    
    // Poner el FSM en estado inicial
    fsm_set_state(INI); 

    // Bucle principal (la lógica se maneja en el timer FSM y callbacks)
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}