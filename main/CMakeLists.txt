file(GLOB_RECURSE app_sources
    "app/*.cpp"
    "data/*.cpp"
    "display/*.cpp"
    "input/*.cpp"
    "net/*.cpp"
    "screens/*.cpp"
    "assets/*.c")

idf_component_register(
    SRCS "main.cpp" ${app_sources}
    INCLUDE_DIRS "." "app" "data" "display" "input" "net" "screens" "assets" "www"
    REQUIRES
        app_update
        driver
        esp_adc
        esp_app_format
        esp_event
        esp_http_client
        esp_http_server
        esp_netif
        esp_timer
        esp_wifi
        json
        log
        lwip
        mbedtls
        nvs_flash
        epdiy
        lvgl
    EMBED_TXTFILES
        "www/index.html"
        "www/app.js"
        "www/style.css")
