#include "webserver.h"

static const char *TAG = "webserver";

extern joined_nodes_id_t joined_nodes_id[10];
extern uint8_t nodes_index;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

extern const uint8_t script_js_start[] asm("_binary_script_js_start");
extern const uint8_t script_js_end[]   asm("_binary_script_js_end");

extern const uint8_t style_css_start[] asm("_binary_style_css_start");
extern const uint8_t style_css_end[]   asm("_binary_style_css_end");

static esp_err_t root_get_handler(
    httpd_req_t *req)
{
   size_t html_size = index_html_end - index_html_start;

    httpd_resp_set_type(req, "text/html");

    return httpd_resp_send(
        req,
        (const char *)index_html_start,
        html_size
    );
}

static const char *get_sensor_name(uint16_t sensor_id)
{
    if (sensor_id == 0x0001) {
        return "Schlafzimmer";
    }
    else if (sensor_id == 0x0002) {
        return "Wohnzimmer";
    }
    else if (sensor_id == 0x0003) {
        return "Kueche";
    }
    else if (sensor_id == 0x0004) {
        return "Bad";
    }
    else {
        return "Unbekannt";
    }
}

static esp_err_t sensors_get_handler(httpd_req_t *req)
{
    char response[1024];
    int offset = 0;

    offset += snprintf(
        response + offset,
        sizeof(response) - offset,
        "{\"sensors\":["
    );

    for (int i = 0; i < nodes_index; i++) {

        const char *name =
            get_sensor_name(joined_nodes_id[i].sensor_id);

        offset += snprintf(
        response + offset,
        sizeof(response) - offset,
        "%s"
        "{\"name\":\"%s\","
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"moisture\":%.2f}",

        (i > 0) ? "," : "",


        name,

        joined_nodes_id[i].data.temp,

        joined_nodes_id[i].data.humidity,

        joined_nodes_id[i].data.moisture
        );
    }

    offset += snprintf(
        response + offset,
        sizeof(response) - offset,
        "]}"
    );

    httpd_resp_set_type(req, "application/json");

    return httpd_resp_send(
        req,
        response,
        HTTPD_RESP_USE_STRLEN
    );
}

// ============================================================
// GET /script.js
// ============================================================

static esp_err_t script_get_handler(httpd_req_t *req)
{
    size_t script_size = script_js_end - script_js_start;

    httpd_resp_set_type(req, "application/javascript");

    return httpd_resp_send(
        req,
        (const char *)script_js_start,
        script_size
    );
}


// ============================================================
// GET /style.css
// ============================================================

static esp_err_t style_get_handler(httpd_req_t *req)
{
    size_t style_size = style_css_end - style_css_start;

    httpd_resp_set_type(req, "text/css");

    return httpd_resp_send(
        req,
        (const char *)style_css_start,
        style_size
    );
}

httpd_uri_t root = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = root_get_handler,
            .user_ctx  = NULL
        };

httpd_uri_t sensors = {
    .uri      = "/api/sensors",
    .method   = HTTP_GET,
    .handler  = sensors_get_handler,
    .user_ctx = NULL
};

// --------------------------------------------------------
// /script.js
// --------------------------------------------------------

httpd_uri_t script_uri = {
    .uri      = "/script.js",
    .method   = HTTP_GET,
    .handler  = script_get_handler,
    .user_ctx = NULL
};


// --------------------------------------------------------
// /style.css
// --------------------------------------------------------

httpd_uri_t style_uri = {
    .uri      = "/style.css",
    .method   = HTTP_GET,
    .handler  = style_get_handler,
    .user_ctx = NULL
};

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &sensors);
        httpd_register_uri_handler(server, &script_uri);
        httpd_register_uri_handler(server, &style_uri);
#if CONFIG_EXAMPLE_ENABLE_SSE_HANDLER
        httpd_register_uri_handler(server, &sse); // Register SSE handler
#endif
#if CONFIG_EXAMPLE_BASIC_AUTH
        httpd_register_basic_auth(server);
#endif
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_stop(server);
}