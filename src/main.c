
/****************************
 * Author: Matt Riley
 * Created:  4/15/2022
 * 
 * Licensed under CC0-1.0 license
 * 
 * Attribution:
 * Some content here is heavily borrowed/copied from the publicly available (and CC0 licensed) ESP32
 * examples (specifically Wifi and MQTT). I also used examples from the cJSON project's Github.
 * 
*******************************/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "creds.h"

#include "cJSON.h"
//#include "cJSON_Utils.h"

#include "lwip/err.h"


static const char *TAG = "SPRNK_MQTT";



#define SPRINKLER_TOPIC  "home/sprinkler"
#define ACC_NAME "sprinkler"

//Define sprinkler zone pins
#define ZONE1 27
#define ZONE2 26
#define ZONE3 "We don't talk about Bruno"
#define ZONE4 25
#define ZONE5 33
#define ZONE6 32
#define ZONE7 18
#define ZONE8 19
#define ZONE9 21

#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
const static int MAXIMUM_RETRY = 3;
static bool zone_on = false;

//Honestly this should be more simple, but my sprinkler system has a broken zone so this is the way I'm
//keeping everything straight in my head.
struct zone
{
    int zone_id;
    int pin_id;
};

const struct zone my_zones[8] = {
                            {1,ZONE1},
                            {2,ZONE2},
                            {4,ZONE4},
                            {5,ZONE5},
                            {6,ZONE6},
                            {7,ZONE7},
                            {8,ZONE8},
                            {9,ZONE9}
};


static void led_init(void)
{


    for (int i = 0; i < 8; i++)
    {
        gpio_set_direction(my_zones[i].pin_id, GPIO_MODE_OUTPUT);
        gpio_set_level(my_zones[i].pin_id, 1);
    }

} 


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retrying connection to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP failed");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

        wifi_config_t wifi_config = {.sta = {.threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD},};
        strcpy((char*)wifi_config.sta.ssid, HOME_SSID);
        strcpy((char*)wifi_config.sta.password, SSID_PW);


    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    
     if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 HOME_SSID, "junk");
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 HOME_SSID, "junk");
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void mqtt_zone_handler(void *event_data)
{
    // I don't think these MQTT vars are null terminated strings, so I had to do some odd copies to get the comparison to work
    //There's probably a better way to handle this, but my C is rusty and this worked.
     esp_mqtt_event_handle_t event = event_data;
     esp_mqtt_client_handle_t client = event->client;
     int topic_length = event->topic_len;
     char topic_cpy[25] = "";
     strncpy(topic_cpy,event->topic,topic_length);

    cJSON *root = cJSON_Parse(event->data);
    cJSON *operation = cJSON_GetObjectItem(root, "value");
    cJSON *zone = cJSON_GetObjectItem(root, "service_name");
    cJSON *characteristic = cJSON_GetObjectItem(root, "characteristic");

    int current_zone = zone->valueint;
    int zone_pin = -1;

    for (int i = 0; i < 8; i++)
    {
        if (my_zones[i].zone_id == zone->valueint)
            zone_pin = my_zones[i].pin_id;
    }
    
    ESP_LOGI(TAG, "Zone: %i", zone->valueint);
   // ESP_LOGI(TAG, "Zone str: %s", zone->valuestring);
    ESP_LOGI(TAG, "Operation: %s", operation->valuestring);
    ESP_LOGI(TAG, "Characteristic: %s", characteristic->valuestring);
     //If a zone is already on, bail out and update the status in homebridge
     //This assume this happened because someone manually tried to turn on a zone
     if (zone_on && strcmp(operation->valuestring, "true") == 0)
     {
        ESP_LOGE(TAG, "A zone is already on.");

        //Fire off an MQTT message back to homebridge to update the status of the switch
        //This should make it look to the user like a switch they flipped just flipped right back
        char buffer[100];
        int buf_len = snprintf(buffer,100,"{\"name\":\"sprinkler\",\"service_name\":\"Zone%i\",\"characteristic\":\"On\",\"value\":false}",current_zone);
        int off_msg = esp_mqtt_client_publish(client, "homebridge/to/set", buffer, buf_len, 0, 0);

        return;
     }

    // Make sure the topic is right and it's actually the sprinkler accessory

     if (strcmp(topic_cpy, SPRINKLER_TOPIC) + strcmp(ACC_NAME, "sprinkler")== 0)
    {
        if (cJSON_IsString(operation) && (operation->valuestring !=NULL) )
        {
            if(strcmp(operation->valuestring, "true") == 0 && strcmp(characteristic->valuestring, "On") == 0)
            {
                /* TODO */
                /* We should really have some sort of timeout safety catch in here
                   Right now I'm relying on Homebridge/HomeKit and I just don't 
                   trust them enough
                */
                gpio_set_level(zone_pin, 0);
                ESP_LOGI(TAG, "Turned on zone %i", current_zone);
                zone_on = true;
            }
            else
            {
                ESP_LOGI(TAG, "Turned off zone %i", current_zone);
                gpio_set_level(zone_pin, 1);
                zone_on = false;
            }
            
        }
        //we dont' want to be flipping zones on/off too fast
         vTaskDelay(100);
    }
    else
    {
        ESP_LOGE(TAG, "Topic/accessory mismatch!");
    }
    cJSON_Delete(root);

}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    //int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            /* ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

            msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
            ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
            break; */
            esp_mqtt_client_subscribe(client, "home/sprinkler", 0);
            ESP_LOGI(TAG, "Subscribed to SPRINKLER");

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
           // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
            //ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            //printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            //printf("Raw TOPIC=%s", event->topic);
            //printf("DATA=%.*s\r\n", event->data_len, event->data);
            mqtt_zone_handler(event_data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
            {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
        break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void zone_init(esp_mqtt_client_handle_t zone_client)
{
    //turn off all switches in Homebridge upon reboot
    //This is pretty specific to my Homebridge config - should abstract this out
    char buffer[100];
    for (int i = 0; i < 8; i++)
    {
            int buf_len = snprintf(buffer,100,"{\"name\":\"sprinkler\",\"service_name\":\"Zone%i\",\"characteristic\":\"On\",\"value\":false}",my_zones[i].zone_id);
            int off_msg = esp_mqtt_client_publish(zone_client, "homebridge/to/set", buffer, buf_len, 0, 0);
            if (off_msg != 0)
            {
                ESP_LOGE(TAG, "Could not set all switches to off!");
            }

    }

}

static void mqtt_start(void)
{
    /* TODO */
    // Abstract the URI out of the code
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = MQTT_BROKER
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_config);

    //Register the event handler
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    //Start the MQTT client and monitor success/fail
    if (esp_mqtt_client_start(client) == ESP_OK)
        {
            ESP_LOGI(TAG, "MQTT started successfully");
            zone_init(client);        
        }
    else { ESP_LOGE(TAG, "MQTT failed to start");}
}

void app_main(void)
{
    //Initialize NVS - had to do this or it just boot looped
     esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);

    led_init();
    wifi_init_sta();
    mqtt_start();
}
