file(GLOB_RECURSE srcs "main.c" "driver/*.c")

idf_component_register(SRCS "${srcs}"
		       PRIV_REQUIRES nvs_flash esp_wifi esp_http_server
                       INCLUDE_DIRS ""
                       EMBED_FILES "webserver/index.html" "webserver/script.js" "webserver/style.css")
