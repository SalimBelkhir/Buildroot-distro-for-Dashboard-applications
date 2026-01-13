#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "addr_from_stdin.h"
#include "lwip/err.h"
#include "lwip/sockets.h"

#include "dht.h"  

#define DHT_GPIO GPIO_NUM_4  
#define SEND_INTERVAL_MS 5000 

#if defined(CONFIG_EXAMPLE_IPV4)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#elif defined(CONFIG_EXAMPLE_IPV6)
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV6_ADDR
#else
#define HOST_IP_ADDR ""
#endif

#define PORT CONFIG_EXAMPLE_PORT

static const char* TAG = "example" ;
static const char* payload = "Message from ESP32" ;

static void tcp_client_task(void *pvParameters)
{
    char host_ip[] = HOST_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;
    
    while (1) {
#if defined(CONFIG_EXAMPLE_IPV4)
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(host_ip);
        if (dest_addr.sin_addr.s_addr == INADDR_NONE) {
            ESP_LOGE(TAG, "Invalid IPv4 address: %s", host_ip);
            break;
        }
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
#elif defined(CONFIG_EXAMPLE_IPV6)
        struct sockaddr_in6 dest_addr = {0};
        inet6_aton(host_ip, &dest_addr.sin6_addr);
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        dest_addr.sin6_scope_id = esp_netif_get_netif_impl_index(EXAMPLE_INTERFACE);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
#endif
        
        int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        
        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);
        
        int conn = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (conn != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            close(sock);
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }
        
        ESP_LOGI(TAG, "Successfully connected to Pi");
        
        while (1) {
            float temperature = 0.0;
            float humidity = 0.0;
            
            esp_err_t result = dht_read_float_data(
                DHT_TYPE_AM2301,  
                DHT_GPIO,
                &humidity,
                &temperature
            );
            
            if (result == ESP_OK) {
                char payload[64];
                snprintf(payload, sizeof(payload), "%.1f,%.1f\n", 
                        temperature, humidity);
                
                int err = send(sock, payload, strlen(payload), 0);
                if (err < 0) {
                    ESP_LOGE(TAG, "Error sending data: errno %d", errno);
                    break;
                }
                
                ESP_LOGI(TAG, "Sent: Temp=%.1f°C, Humidity=%.1f%%", 
                        temperature, humidity);
            } else {
                ESP_LOGE(TAG, "Failed to read DHT22 sensor: %d", result);
            }
            
            vTaskDelay(SEND_INTERVAL_MS / portTICK_PERIOD_MS);
        }
        
        if (sock != -1) {
            ESP_LOGI(TAG, "Shutting down socket and reconnecting...");
            shutdown(sock, 0);
            close(sock);
        }
        
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    
    vTaskDelete(NULL);
}


void app_main(void){
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(example_connect());
	xTaskCreate(tcp_client_task,"tcp_client",4096,NULL,5,NULL);
}