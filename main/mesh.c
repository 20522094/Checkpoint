/**
 * Lib C
 */
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
 * ESP hall;
 */

#include "esp_wifi.h"
#include "esp_system.h"

/**
 * Callback do WiFi e MQTT;
 */
#include "esp_event_loop.h"
#include "esp_event.h"

/**
 * Logs;
 */
#include "esp_log.h"

/**
 * Mesh Net;
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
 * Drivers
 */
#include "nvs_flash.h"
#include "driver/gpio.h"

/**
 * PINOUT; 
 */
#include "sys_config.h"

/**
 * App;
 */
#include "app.h"

/**
* Json
*/
#include "cJSON.h"

/**
 * Overloads sdkconfig file;
 * What Crypto algorithm to use in Mesh Network?
 */
#define CONFIG_MESH_IE_CRYPTO_FUNCS  1
#define CONFIG_MESH_IE_CRYPTO_KEY    "chave de criptografia"

/**
 * Global defs
 */
char mac_address_root_str[50];
mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];

static const char *TAG = "mesh";
static const uint8_t MESH_ID[6] = { 0x77, 0x77, 0x77, 0x77, 0x77, 0x77 };

static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;

EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

#define TX_SIZE          (100)
static uint8_t tx_buf[TX_SIZE] = { 0, };
/**
 * Function prototypes
 */
static void esp_mesh_rx_start( void );
void mesh_event_handler( void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data );
void mesh_app_start( void );

/**
 * Calls reception thread to change messages
 */
static void esp_mesh_rx_start( void )
{
    static bool is_esp_mesh_rx_started = false;
    if( !is_esp_mesh_rx_started )
    {
        is_esp_mesh_rx_started = true;
        task_app_create();
    }
}

/**
 * Callback function
 */
void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};
    static uint8_t last_layer = 0;
    ESP_LOGD(TAG, "esp_event_handler:%d", event_id);

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGI(TAG, "<MESH_EVENT_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_STOPPED: {
        ESP_LOGI(TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_CHILD_CONNECTED: {
        mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                 child_connected->aid,
                 MAC2STR(child_connected->mac));
    }
    break;
    case MESH_EVENT_CHILD_DISCONNECTED: {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 child_disconnected->aid,
                 MAC2STR(child_disconnected->mac));
        char* mac_id[20];
        snprintf( mac_id, sizeof( mac_id ), ""MACSTR"", MAC2STR( child_disconnected->mac ) );
        public_disconnect_msg(mac_id);
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new);
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new);
    }
    break;
    case MESH_EVENT_NO_PARENT_FOUND: {
        mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 no_parent->scan_times);
    }
    /* TODO handler for the failure */
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        ESP_LOGI(TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR"",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr));
        last_layer = mesh_layer;
        if (esp_mesh_is_root()) 
        {
            /**
             * FIXED IP?
             */
            #if !FIXED_IP
                tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
            #endif
        }

        /**
         * Initialize the message reception thread 
         */
        esp_mesh_rx_start();
    }
    break;
    
    /**
     * Parent desconnection event
     */
    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGI(TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);
        mesh_layer = esp_mesh_get_layer();
    
    }
    break;

    /**
     * Layer change event
     */
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
        mesh_layer = layer_change->new_layer;
        ESP_LOGI(TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");
        last_layer = mesh_layer;
    }
    break;
    /**
     * Root address event
     */
    case MESH_EVENT_ROOT_ADDRESS: {
        mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(root_addr->addr));
        /**
         * Storage ROOT Address event
         */
        if(esp_mesh_is_root()) 
        {   
            uint8_t chipid[20];
            esp_efuse_mac_get_default( chipid );
            snprintf( mac_address_root_str, sizeof( mac_address_root_str ), ""MACSTR"", MAC2STR( chipid ) );
            mqtt_start();
        }
    }
    break;
    /**
     * Init vote routine event
     */
    case MESH_EVENT_VOTE_STARTED: {
        mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
        ESP_LOGI(TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
                 vote_started->attempts,
                 vote_started->reason,
                 MAC2STR(vote_started->rc_addr.addr));
    }
    break;
    case MESH_EVENT_VOTE_STOPPED: {
        ESP_LOGI(TAG, "<MESH_EVENT_VOTE_STOPPED>");
    break;
    }
    /**
     * Software forced request for root exchange 
     */
    case MESH_EVENT_ROOT_SWITCH_REQ: {
        mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
        ESP_LOGI(TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
                 switch_req->reason,
                 MAC2STR( switch_req->rc_addr.addr));
    }
    break;
    /**
     * Callback acknowledgemnts
     */
    case MESH_EVENT_ROOT_SWITCH_ACK: {
        /* new root */
        mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        ESP_LOGI(TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer, MAC2STR(mesh_parent_addr.addr));
    }
    break;
    /**
     * Messages sent by root can be addressed to an external IP.
     * When we use this mesh stack feature, this event will be used
     * in notification of states (toDS - for DS (distribute system))
     */
    case MESH_EVENT_TODS_STATE: {
        mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    }
    break;
    /**
     * MESH_EVENT_ROOT_FIXED forces the child device to maintain the same settings as the
     * parent device on the mesh network; 
     */
    case MESH_EVENT_ROOT_FIXED: {
        mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 root_fixed->is_fixed ? "fixed" : "not fixed");
    }
    break;
    /**
     * Event called when there is another and best possible candidate to be root of the network; 
     * The current root passes control to the new root to take over the network;
     */
    case MESH_EVENT_ROOT_ASKED_YIELD: {
        mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
        ESP_LOGI(TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
                 MAC2STR(root_conflict->addr),
                 root_conflict->rssi,
                 root_conflict->capacity);
    }
    break;
    /**
     * Channel switch event
     */
    case MESH_EVENT_CHANNEL_SWITCH: {
        mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
    }
    break;
    case MESH_EVENT_SCAN_DONE: {
        mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 scan_done->number);
    }
    break;
    case MESH_EVENT_NETWORK_STATE: {
        mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 network_state->is_rootless);
    }
    break;
    /**
     * Event called when the root device stops connecting to the router and
     * the child device stops connecting to the parent device;
     */
    case MESH_EVENT_STOP_RECONNECTION: {
        ESP_LOGI(TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    }
    break;
    /**
     * Event called when the device encounters a mesh network to be paired
     */
    case MESH_EVENT_FIND_NETWORK: {
        mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
                 find_network->channel, MAC2STR(find_network->router_bssid));
    }
    break;
    /**
     * Event called when the device finds and exchanges for another router 
     * (linksys, dlink ...) with the same SSID;
     */
    case MESH_EVENT_ROUTER_SWITCH: {
        mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
        ESP_LOGI(TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
                 router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
    }
    break;
    default:
        ESP_LOGI(TAG, "unknown id:%d", event_id);
        break;
    }
}


/**
 * Mesh stack init
 */
void mesh_app_start( void )
{
    /*  tcpip stack init */
    tcpip_adapter_init();
    /* for mesh
     * stop DHCP server on softAP interface by default
     * stop DHCP client on station interface by default
     * */
    ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
    ESP_ERROR_CHECK(tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA));

#if FIXED_IP
    /**
     * The ESP32 ROOT of the Mesh network is one that receives the IP address of the Router;
     * Do you want to work with the fixed IP address on the network? 
     * That is, you want to configure; ROOT with Static IP?
     */
    tcpip_adapter_ip_info_t sta_ip;
    sta_ip.ip.addr = ipaddr_addr( IP_ADDRESS );
    sta_ip.gw.addr = ipaddr_addr( GATEWAY_ADDRESS );
    sta_ip.netmask.addr = ipaddr_addr( NETMASK_ADDRESS );
    tcpip_adapter_set_ip_info(WIFI_IF_STA, &sta_ip);
#endif

    /**
     * WiFi Init
     */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init( &config ) );
    ESP_ERROR_CHECK( esp_wifi_set_storage( WIFI_STORAGE_FLASH ) );
    ESP_ERROR_CHECK( esp_wifi_start() );

    /**
     * Mesh init
     */
    ESP_ERROR_CHECK( esp_mesh_init() );
    ESP_ERROR_CHECK( esp_mesh_set_max_layer( CONFIG_MESH_MAX_LAYER ));
    ESP_ERROR_CHECK( esp_mesh_set_vote_percentage(1) );
    ESP_ERROR_CHECK( esp_mesh_set_ap_assoc_expire(10) );

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    
    /**
     * Register the Mesh network ID. All non-root who wish to participate 
     * in this mesh network must have the same ID and the login and password  
     * to access the network (informed further down in the code);
     */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    
    /**
     * Registers the callback function of the Mesh network;
     * The callback function is responsible for signaling to you, the user
     * the states of the internal operations of the Mesh network;
     */
    /**
     * Define channel frequency
     */
    cfg.channel = CONFIG_MESH_CHANNEL;

    /**
     * Defines the ssid and password that will be used for communication between nodes
     * Mesh network; This SSID and PASSWORD is that of YOUR ROUTER FROM YOUR HOME or COMPANY;
     */
    cfg.router.ssid_len = strlen(WIFI_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, WIFI_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));

    /**
     * The Mesh network requires authentication and will be configured as an access point;
     */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));

    /**
     * Defines the maximum number of non-root (node) in each node of the network;
     * If 'max_connection' is equal to 1, then only one node per layer will be allowed;
     * Example: 3x ESP32 would be: A (root) -> B (non-root) -> C (non-root); So there would be
     * 3 layers the Mesh network;
     */
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;

    /**
     * SSID and Password for network access BETWEEN Mesh network nodes;
     * This SSID and PASSWORD is used only by devices on the network;
     */
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD, strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    /**
     * Mesh start;
     */
    ESP_ERROR_CHECK(esp_mesh_start());

}
