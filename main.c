#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event_loop.h"

#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include <driver/gpio.h>
#include <driver/adc.h>

#include <bmp280.h>

#include "mqtt_client.h"

#if defined(CONFIG_IDF_TARGET_ESP8266)
#define SDA_GPIO 4
#define SCL_GPIO 5
#else
#define SDA_GPIO 16
#define SCL_GPIO 17
#endif

xSemaphoreHandle wifi_on;
xSemaphoreHandle mqtt;

#define WIFI_SSID ""
#define WIFI_PASS ""

char buff[50] = {0};

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event){
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            esp_mqtt_client_subscribe(event->client, "embebidos", 1);
            break;

        case MQTT_EVENT_SUBSCRIBED:
        	xSemaphoreGive(mqtt);
            break;

        case MQTT_EVENT_DATA:
        	memcpy(buff, event->data, event->data_len);
        	printf("%s\r\n", buff);

            break;

        default:
			break;
    }
    return ESP_OK;
}

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event){
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
        	xSemaphoreGive(wifi_on);
            break;

        default:
            break;
    }
    return ESP_OK;
}

void delay_ms(int ms){
	vTaskDelay(ms/portTICK_PERIOD_MS);
}

void bmp280(void *pvParamters){
    bmp280_params_t params;
    bmp280_init_default_params(&params);
    bmp280_t dev;
    memset(&dev, 0, sizeof(bmp280_t));

    ESP_ERROR_CHECK(bmp280_init_desc(&dev, BMP280_I2C_ADDRESS_0, 0, SDA_GPIO, SCL_GPIO));
    ESP_ERROR_CHECK(bmp280_init(&dev, &params));

    bool bme280p = dev.id == BME280_CHIP_ID;
    printf("BMP280: found %s\n", bme280p ? "BME280" : "BMP280");

    float pressure, temperature, humidity;

    xSemaphoreTake(wifi_on, portMAX_DELAY);
    printf("Conectando a wifi\r\n");

    esp_mqtt_client_config_t mqtt_cfg = {
          .host = "192.168.1.57",
          .event_handle = mqtt_event_handler,
  		    .lwt_topic = "testament",
  		    .lwt_msg = "electricaribe_HP",
  		    .disable_clean_session = 1
          };

      esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
      esp_mqtt_client_start(client);

      xSemaphoreTake(mqtt, portMAX_DELAY);
      printf("Subscrito a Broker\r\n");
      float temp;
      char c[50];
    while (1){
      delay_ms(15000);
        if (bmp280_read_float(&dev, &temperature, &pressure, &humidity) != ESP_OK)
        {
            printf("Temperature/pressure reading failed\n");
            continue;
        }

        /* float is used in printf(). you need non-default configuration in
         * sdkconfig for ESP8266, which is enabled by default for this
         * example. see sdkconfig.defaults.esp8266
         */
        printf("Pressure: %.2f Pa, Temperature: %.2f C", pressure, temperature);
        sprintf(c, "Temperature: %g", temperature);
        esp_mqtt_client_publish(client, "BME280", c, 0, 1, 0);
        sprintf(c, "Pressure: %g", pressure);
        esp_mqtt_client_publish(client, "BME280", c, 0, 1, 0);
        if (bme280p)
            printf(", Humidity: %.2f\n", humidity);
        else
            printf("\n");
    }

}

void main_task(void){

  esp_mqtt_client_config_t mqtt_cfg = {
        .host = "192.168.1.57",
        .event_handle = mqtt_event_handler,
        .lwt_topic = "testament",
        .lwt_msg = "electricaribe_HP",
        .disable_clean_session = 1
        };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
    xSemaphoreTake(mqtt, portMAX_DELAY);

    float temp;
    char c[50];
    while(1){
      int val = adc1_get_raw(ADC1_CHANNEL_0);
  		temp = (100 *  val / 1023) * 1.3;
      sprintf(c, "ADC Temp: %g", temp);
    	esp_mqtt_client_publish(client, "temperatura", c, 0, 1, 0);
    	delay_ms(15000);
    }
}

void app_main()
{
    ESP_ERROR_CHECK(i2cdev_init());
  	nvs_flash_init();
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());


    adc1_config_width(ADC_WIDTH_BIT_10);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_0);

    wifi_on = xSemaphoreCreateBinary();
    mqtt = xSemaphoreCreateBinary();

    xTaskCreate(&main_task, "mqtt_task", 20000, NULL, 2, NULL);
    xTaskCreatePinnedToCore(bmp280, "bmp280_test", configMINIMAL_STACK_SIZE * 8, NULL, 2, NULL, APP_CPU_NUM);
}
