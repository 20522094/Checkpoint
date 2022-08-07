#ifndef __APPS_H__
#define __APPS_H__
typedef struct 
{
    char* id;
    char* ssid
} nodeEsp;

void mqtt_start();
void public_disconnect_msg(char* );
void send_connect_msg();

void gpios_setup( void );
void task_mesh_tx( void *pvParameter );
void task_mesh_rx ( void *pvParameter );
void task_app_create( void );

#endif