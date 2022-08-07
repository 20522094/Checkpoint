#ifndef _SYSCONFIG__H
#define _SYSCONFIG__H

/**
 * NODE_ID
 */
#define NODE_ID "1"

/**
 * GPIOs defs
 */
#define LED_BUILDING         ( 2 ) 
#define GPIO_OUTPUT_PIN_SEL  ( 1ULL<<LED_BUILDING )

#define BUTTON               ( 0 )
#define GPIO_INPUT_PIN_SEL   ( 1ULL<<BUTTON )

/**
 * Info wifi your ssid & passwd
 */
#define WIFI_SSID       "1111"
#define WIFI_PASSWORD   "01245678"

/**
 * Net config
 */
#define FIXED_IP 0
#define IP_ADDRESS 		"192.168.0.80"
#define GATEWAY_ADDRESS "192.168.0.1"
#define NETMASK_ADDRESS "255.255.255.0"

/**
 * Globals defs
 */
#define TRUE  1
#define FALSE 0

/**
 * Debugger?
 */
#define DEBUG 1

#endif 