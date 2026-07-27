idf_component_register(
    SRCS
        "main.c"
        "camera_service.c"
        "command_parser.c"
        "d23_sensor.c"
        "fault_detector.c"
        "ina219_sensor.c"
        "mqtt_service.c"
        "streetlight_control.c"
        "streetlight_policy.c"
        "vision_parser.c"
        "wifi_manager.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp32-camera-master esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_event esp_http_server esp_netif esp_psram esp_timer esp_wifi nvs_flash espressif__mqtt
    EMBED_TXTFILES "emqxsl-ca.crt"
)
