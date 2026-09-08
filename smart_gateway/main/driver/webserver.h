#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <esp_log.h>
#include <sys/param.h>
#include "esp_netif.h"
#include <esp_http_server.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_check.h"
#include <time.h>
#include <sys/time.h>
#include <esp_wifi.h>
#include <esp_system.h>

typedef struct {
    float moisture;
    float soc;
    float humidity;
    float temp;
} sensor_data_t;

typedef struct {
    uint16_t sensor_id;
    uint16_t network_addr;
    uint64_t network_addr_ieee;
    sensor_data_t data;
} joined_nodes_id_t;


httpd_handle_t start_webserver(void);