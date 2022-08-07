#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/**
 * FreeRTOS
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

/**
 * Drivers;
 */
#include "driver/gpio.h"

/**
 * GPIOs Config;
 */
#include "app.h"

/**
 * Standard configurations loaded
 */
#include "sys_config.h"

/**
 * Logs;
 */
#include "esp_log.h"

/**
 * Rede mesh;
 */
#include "esp_mesh.h"
#include "esp_mesh_internal.h"

/**
 * Lwip
 */
#include "lwip/err.h"
#include "lwip/sys.h"
#include <lwip/sockets.h>

/**
* Json
*/
#include "cJSON.h"

// interaction with public mqtt broker
void mqtt_app_start(void);
void mqtt_app_publish(char* topic, char *publish_string);
/**
 * Gloabal Variables; 
 */
extern EventGroupHandle_t wifi_event_group;
extern const int CONNECTED_BIT;
extern mesh_addr_t route_table[];
extern char mac_address_root_str[];
nodeEsp activeNode[30];
int lengthOfActiveNode = 0;
/**
 * Constants;
 */
static const char *TAG = "app: ";

#define RX_SIZE          (100)
static uint8_t rx_buf[RX_SIZE] = { 0, };

#define TX_SIZE          (100)
static uint8_t tx_buf[TX_SIZE] = { 0, };

bool SignalConnect = 0;
/**
 * Configure the ESP32 gpios (lLED & button );
 */

void gpios_setup( void )
{
    /**
     * Configure the GPIO LED BUILDING
     */
    gpio_config_t io_conf_output;
    io_conf_output.intr_type = GPIO_INTR_DISABLE;
    io_conf_output.mode = GPIO_MODE_OUTPUT;
    io_conf_output.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf_output.pull_down_en = 0;
    io_conf_output.pull_up_en = 1;
    gpio_config(&io_conf_output);

    /**
     * LED Off in the startup
     * Level 0 -> off 
     * Level 1 -> on 
     */
    gpio_set_level( LED_BUILDING, 0 ); 
   
    /**
     * Configure the GPIO BUTTON
    */
    gpio_config_t io_conf_input;
    io_conf_input.intr_type = GPIO_PIN_INTR_DISABLE; 
    io_conf_input.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    io_conf_input.mode = GPIO_MODE_INPUT;
    io_conf_input.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf_input.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf_input);
}
void public_disconnect_msg(char* macID)
{    
    for (int i = 0; i < lengthOfActiveNode; i++){
        if (strcmp(macID, activeNode[i].ssid)==0){
            mqtt_app_publish("ESP-disconnect", activeNode[i].id);
        }
    }
}
void send_connect_msg()
{    
    uint8_t chipid[20];
    char mac_str[30];
    esp_efuse_mac_get_default(chipid);
    snprintf( mac_str, sizeof( mac_str ), ""MACSTR"", MAC2STR( chipid ) );
    
    cJSON *root;
    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "Topic", "Connect-Mesh");
    cJSON_AddStringToObject(root, "ID", NODE_ID);
    cJSON_AddStringToObject(root, "SSID", mac_str);
    char *rendered = cJSON_Print(root);
    snprintf( (char*)tx_buf, TX_SIZE,  rendered ); 
    mesh_data_t data;
    data.data = tx_buf;
    data.size = strlen((char*)tx_buf) + 1;
    esp_err_t err = esp_mesh_send(&mac_address_root_str, &data, MESH_DATA_P2P, NULL, 0);
    if (err) 
    {
        #ifdef DEBUG 
            ESP_LOGI( TAG, "ERROR : Sending Message!\r\n" ); 
        #endif
    } else {
        SignalConnect = 1;
        #ifdef DEBUG 
            ESP_LOGI( TAG, "\r\nNON-ROOT sends (%s) (%s) to ROOT (%s)\r\n", mac_str, tx_buf, mac_address_root_str );                         
        #endif
    }
    

}
/**
 * Button Manipulation Task
 */
void task_mesh_tx( void *pvParameter )
{   
    int counter = 0;
    char mac_str[30];
    int route_table_size = 0;
    
    esp_err_t err;

    mesh_data_t data;
    data.data = tx_buf;
    data.size = TX_SIZE;

    /**
     *  Defines the format of the message to be sent;
     * 'MESH_PROTO_BIN' is the standard binary format, but there are others:
     * 'MESH_PROTO_JSON'
     * 'MESH_PROTO_MQTT'
     * 'MESH_PROTO_HTTP'
     */
    data.proto = MESH_PROTO_JSON;
    
    for( ;; ) 
    {
        /**
         * If this device is the root, then create the socket server connection;
         */
        if( esp_mesh_is_root() )
        {
            
            //Is it root node? Then turn on the led building

            gpio_set_level( LED_BUILDING, 1 );

            /**
             * The button was pressed?
             */
            if( gpio_get_level( BUTTON ) == 0 ) 
            {       
                #ifdef DEBUG 
                    ESP_LOGI( TAG, "Button %d Pressed.\r\n", BUTTON );
                #endif
                
                counter++;
                snprintf( (char*)tx_buf, TX_SIZE, "%d",  counter ); 
                /**
                 * Calculating the size of the data type buffer
                 * pointed by esp_mesh_send() method
                 */
                data.size = strlen((char*)tx_buf) + 1;
                        
                /**
                * Updates the reading of mac addresses of devices on the mesh network
                 */
                esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                               CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

                for( int i = 0; i < route_table_size; i++ ) 
                {
                    /**
                     * Convert address binary to string;
                     */
                    sprintf(mac_str, MACSTR, MAC2STR(route_table[i].addr));
                    
                    /**
                      * Routing routine for sending the message quoted by the button. 
                      * Here the ROOT sends a message to all node but himself:)
                      */
                    if( strcmp( mac_address_root_str, mac_str) != 0 )
                    {   
                        /**
                         * Actual sending of datatype already loaded
                         */
                        err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);
                        if (err) 
                        {
                            /**
                             * Error child node message
                             */
                            #ifdef DEBUG 
                                ESP_LOGI( TAG, "ERROR : Sending Message!\r\n" ); 
                            #endif
                        } else {
                    
                            /**
                             * Mensagem Enviada com sucesso pelo ROOT;
                             */
                            #ifdef DEBUG 
                                ESP_LOGI( TAG, "ROOT sends (%s) (%s) to NON-ROOT (%s)\r\n", mac_address_root_str, tx_buf, mac_str );                         
                            #endif
                        }
                    }                    

                }                     
            }
            vTaskDelay( 300/portTICK_PERIOD_MS );   
        } 

        /**
         * The device is NON-ROOT = CHILD NODE
         */
         else
        {   
            if (!SignalConnect){
                send_connect_msg();
                vTaskDelay( 500/portTICK_PERIOD_MS);
            }
            /**
             * The button was pressed?
             */
            if( gpio_get_level( BUTTON ) == 0 ) 
            {   
                #ifdef DEBUG 
                    ESP_LOGI( TAG, "Child Button %d Pressed.\r\n", BUTTON );
                #endif
                //Send data
                cJSON *root;
                root=cJSON_CreateObject();
	            cJSON_AddStringToObject(root, "Topic", "Send-Data");
	            cJSON_AddNumberToObject(root, "Data", 156);
                char *rendered=cJSON_Print(root);       
                snprintf( (char*)tx_buf, TX_SIZE,  rendered ); 

                /**
                 * Calculating the size of the data type buffer
                 * pointed by esp_mesh_send() method
                 */
                data.size = strlen((char*)tx_buf) + 1;
                err = esp_mesh_send(&mac_address_root_str, &data, MESH_DATA_P2P, NULL, 0);
                if (err) 
                {
                    #ifdef DEBUG 
                        ESP_LOGI( TAG, "ERROR : Sending Message!\r\n" ); 
                    #endif
                } else {
                    #ifdef DEBUG 
                        uint8_t chipid[20];
                        esp_efuse_mac_get_default(chipid);
                        snprintf( mac_str, sizeof( mac_str ), ""MACSTR"", MAC2STR( chipid ) );
                        ESP_LOGI( TAG, "\r\nNON-ROOT sends (%s) (%s) to ROOT (%s)\r\n", mac_str, tx_buf, mac_address_root_str );                         
                    #endif
                }
            }

            vTaskDelay( 300/portTICK_PERIOD_MS );      
        }
    }
}

void task_mesh_rx ( void *pvParameter )
{
    esp_err_t err;
    mesh_addr_t from;

    mesh_data_t data;
    data.data = rx_buf;
    data.size = RX_SIZE;

    char mac_address_str[30];
    int flag = 0;
    
    static uint8_t buffer_s[10];
    mesh_data_t data_s;
    data_s.data = buffer_s;
    data_s.size = sizeof(buffer_s);    

    for( ;; )
    {
        data.size = RX_SIZE;

       /**
        * Waits for message reception
        */
        err = esp_mesh_recv( &from, &data, portMAX_DELAY, &flag, NULL, 0 );
        if( err != ESP_OK || !data.size ) 
        {
            #ifdef DEBUG 
                ESP_LOGI( TAG, "err:0x%x, size:%d", err, data.size );
            #endif
            continue;
        }

        /**
         * Is it routed for ROOT Node?
         */
        if( esp_mesh_is_root() ) 
        {
            //**ROOT handle message
            
            char myJson[100];
            snprintf(myJson, 100, (char*) data.data);
            
            cJSON *root = cJSON_Parse(myJson);
            char* topic = cJSON_GetObjectItem(root,"Topic")->valuestring;
            if (strcmp(topic,"Connect-Mesh")==0){
                char* id = cJSON_GetObjectItem(root,"ID")->valuestring;
                char* ssid = cJSON_GetObjectItem(root,"SSID")->valuestring;
                for (int i = 0; i <= lengthOfActiveNode; i++){
                    if (i == lengthOfActiveNode){
                        activeNode[lengthOfActiveNode].id = id;
                        activeNode[lengthOfActiveNode++].ssid = ssid;
                        break;}
                    if (lengthOfActiveNode != 0)
                        if (strcmp(ssid,activeNode[i].ssid)==0) {
                            activeNode[i].id = id;
                            break;}
                            ESP_LOGI(TAG, "Active node HERE");   
                }
                ESP_LOGI(TAG, "Active node%s",activeNode[0].ssid);
                snprintf( mac_address_str, sizeof(mac_address_str), ""MACSTR"", MAC2STR(from.addr) );
                #ifdef DEBUG
                ESP_LOGI(TAG, "NON-ROOT(MAC:%s)- Node %s: %s, ", mac_address_str, topic, id);  
                ESP_LOGI(TAG, "Tried to publish %s", id);  
                #endif
                mqtt_app_publish("ESP-connect", id);                
            }
            if (strcmp(topic,"Send-Data")==0){
                int nodeData = cJSON_GetObjectItem(root,"Data")->valueint;
                #ifdef DEBUG
                ESP_LOGI(TAG, "NON-ROOT(MAC:%s)- Node %s: %d, ", mac_address_str, topic, nodeData);  
                ESP_LOGI(TAG, "Tried to publish %d", nodeData);  
                #endif
                char nodeDt[20];
                snprintf(nodeDt,sizeof(nodeDt),"%d",nodeData);
                mqtt_app_publish("ESP-send", nodeDt); 
            }
            
            #ifdef DEBUG 
                ESP_LOGI( TAG,"ROOT(MAC:%s) - Msg: %s, ", mac_address_root_str, data.data );
                /**
                 * Log message to console
                 */
                ESP_LOGI( TAG, "send by NON-ROOT: %s\r\n", mac_address_str );
            #endif

        } 

        else 
        {   //**NON-ROOT handle message
            /**
             * Finds out which MAC Address NON-ROOT node gets the message 
             */
            uint8_t mac_address[20];
            esp_efuse_mac_get_default( mac_address );
            snprintf( mac_address_str, sizeof( mac_address_str ), ""MACSTR"", MAC2STR( mac_address ) );

            #ifdef DEBUG 
                ESP_LOGI( TAG, "NON-ROOT(MAC:%s)- Msg: %s, ", mac_address_str, (char*)data.data );  
                snprintf( mac_address_str, sizeof(mac_address_str), ""MACSTR"", MAC2STR(from.addr) );
                ESP_LOGI( TAG, "send by ROOT: %s\r\n", mac_address_str );
            #endif

            /**
             * Toggle the LED_BUILDING at each button increment
             */
            if( data.size > 0 )
            {
                gpio_set_level( LED_BUILDING, atoi((char*)data.data) % 2 );
            }
                           
        }

    }

    vTaskDelete(NULL);
}

void mqtt_start(){
    mqtt_app_start();
}

void task_app_create( void )
{   
    #ifdef DEBUG
    ESP_LOGI( TAG, "task_app_create() called" );
    if( esp_mesh_is_root() )
    {     
        ESP_LOGI( TAG, "ROOT NODE\r\n");     
    }
    else
    {   
        ESP_LOGI( TAG, "CHILD NODE\r\n");         
    }
    #endif
    /**
     * Creates a Task to receive message;
     */
    if( xTaskCreate( task_mesh_rx, "task_mesh_rx", 1024 * 5, NULL, 2, NULL) != pdPASS )
    {
        #ifdef DEBUG
        ESP_LOGI( TAG, "ERROR - task_mesh_rx NOT ALLOCATED :/\r\n" );  
        #endif
        return;   
    }

    /**
     *  Creates a Task to transfer message;
     */
    if( xTaskCreate( task_mesh_tx, "task_mesh_tx", 1024 * 8, NULL, 1, NULL ) != pdPASS )
    {
        #ifdef DEBUG
        ESP_LOGI( TAG, "ERROR - task_mesh_tx NOT ALLOCATED :/\r\n" );  
        #endif
        return;   
    }     
}
