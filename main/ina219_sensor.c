#include "ina219_sensor.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hardware_config.h"

static const char *TAG = "ina219";

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_ina219_dev;
static bool s_ina219_ready;

static esp_err_t ina219_register_write(uint8_t reg_addr, uint16_t value)
{
    uint8_t write_buf[3] = {
        reg_addr,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF),
    };
    return i2c_master_transmit(s_ina219_dev, write_buf, sizeof(write_buf), INA219_I2C_TIMEOUT_MS);
}

static esp_err_t ina219_register_read(uint8_t reg_addr, uint16_t *value)
{
    uint8_t data[2] = {0};
    esp_err_t ret = i2c_master_transmit_receive(
        s_ina219_dev, &reg_addr, 1, data, sizeof(data), INA219_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }

    *value = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

void ina219_sensor_init(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = INA219_I2C_PORT,
        .sda_io_num = INA219_SDA_GPIO,
        .scl_io_num = INA219_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(ret));
        return;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA219_I2C_ADDR,
        .scl_speed_hz = INA219_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(s_i2c_bus, &dev_config, &s_ina219_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "device add failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = ina219_register_write(INA219_REG_CONFIG, INA219_CONFIG_CONTINUOUS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "config failed: %s", esp_err_to_name(ret));
        return;
    }

    s_ina219_ready = true;
    ESP_LOGI(TAG, "initialized: addr=0x%02x SDA=%d SCL=%d shunt=%.3f ohm",
             INA219_I2C_ADDR,
             INA219_SDA_GPIO,
             INA219_SCL_GPIO,
             INA219_SHUNT_RESISTOR_OHMS);
}

void ina219_sensor_update(streetlight_state_t *state)
{
    if (!s_ina219_ready) {
        state->ina219_ok = false;
        return;
    }

    uint16_t bus_raw = 0;
    uint16_t shunt_raw = 0;
    esp_err_t ret = ina219_register_read(INA219_REG_BUS_VOLTAGE, &bus_raw);
    if (ret == ESP_OK) {
        ret = ina219_register_read(INA219_REG_SHUNT_VOLTAGE, &shunt_raw);
    }
    if (ret != ESP_OK) {
        state->ina219_ok = false;
        ESP_LOGW(TAG, "read failed: %s", esp_err_to_name(ret));
        return;
    }

    int16_t shunt_signed = (int16_t)shunt_raw;
    state->bus_voltage_v = (float)((bus_raw >> 3) * 4) / 1000.0f;
    state->shunt_voltage_mv = (float)shunt_signed * 0.01f;
    state->current_ma = state->shunt_voltage_mv / INA219_SHUNT_RESISTOR_OHMS;
    state->ina219_ok = true;
}
