#include "camera_service.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_camera.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hardware_config.h"
#include "img_converters.h"

#define PART_BOUNDARY "123456789000000000000987654321"

static const char *TAG = "camera_service";
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

static httpd_handle_t s_httpd;

typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *chunk = (jpg_chunking_t *)arg;
    if (index == 0) {
        chunk->len = 0;
    }
    if (httpd_resp_send_chunk(chunk->req, (const char *)data, len) != ESP_OK) {
        return 0;
    }
    chunk->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGE(TAG, "capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_err_t res = httpd_resp_set_type(req, "image/jpeg");
    if (res == ESP_OK) {
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }

    size_t jpg_len = 0;
    int64_t start_us = esp_timer_get_time();
    if (res == ESP_OK) {
        if (fb->format == PIXFORMAT_JPEG) {
            jpg_len = fb->len;
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        } else {
            jpg_chunking_t chunk = {
                .req = req,
                .len = 0,
            };
            bool converted = frame2jpg_cb(fb, 80, jpg_encode_stream, &chunk);
            if (converted) {
                res = httpd_resp_send_chunk(req, NULL, 0);
                jpg_len = chunk.len;
            } else {
                res = ESP_FAIL;
            }
        }
    }

    esp_camera_fb_return(fb);
    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGI(TAG, "capture: %u KB, %" PRIi64 " ms",
             (unsigned)(jpg_len / 1024), elapsed_ms);
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    char part_buf[80];
    int64_t last_frame_us = esp_timer_get_time();

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            ESP_LOGE(TAG, "stream capture failed");
            res = ESP_FAIL;
            break;
        }

        uint8_t *jpg_buf = NULL;
        size_t jpg_len = 0;
        bool converted = false;

        if (fb->format == PIXFORMAT_JPEG) {
            jpg_buf = fb->buf;
            jpg_len = fb->len;
        } else {
            converted = frame2jpg(fb, 80, &jpg_buf, &jpg_len);
            if (!converted) {
                ESP_LOGE(TAG, "jpeg conversion failed");
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
                break;
            }
        }

        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        }
        if (res == ESP_OK) {
            int header_len = snprintf(part_buf, sizeof(part_buf), STREAM_PART, jpg_len);
            if (header_len < 0 || header_len >= (int)sizeof(part_buf)) {
                ESP_LOGE(TAG, "stream part header truncated");
                res = ESP_FAIL;
            } else {
                res = httpd_resp_send_chunk(req, part_buf, (size_t)header_len);
            }
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len);
        }

        if (converted) {
            free(jpg_buf);
        }
        esp_camera_fb_return(fb);

        if (res != ESP_OK) {
            break;
        }

        int64_t now_us = esp_timer_get_time();
        int64_t frame_ms = (now_us - last_frame_us) / 1000;
        last_frame_us = now_us;
        float fps = frame_ms > 0 ? 1000.0f / (float)frame_ms : 0.0f;
        ESP_LOGI(TAG, "stream: %u KB, %" PRIi64 " ms, %.1f fps",
                 (unsigned)(jpg_len / 1024), frame_ms, fps);
    }

    return res;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    const char *html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<title>Smart Streetlight Camera</title></head>"
        "<body><h1>Smart Streetlight Camera</h1>"
        "<p><a href=\"/capture\">Capture</a></p>"
        "<p><img src=\"/stream\" style=\"max-width:100%;height:auto\"></p>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t camera_init(void)
{
    camera_config_t config = {
        .pin_pwdn = CAMERA_PIN_PWDN,
        .pin_reset = CAMERA_PIN_RESET,
        .pin_xclk = CAMERA_PIN_XCLK,
        .pin_sccb_sda = CAMERA_PIN_SIOD,
        .pin_sccb_scl = CAMERA_PIN_SIOC,
        .pin_d7 = CAMERA_PIN_D7,
        .pin_d6 = CAMERA_PIN_D6,
        .pin_d5 = CAMERA_PIN_D5,
        .pin_d4 = CAMERA_PIN_D4,
        .pin_d3 = CAMERA_PIN_D3,
        .pin_d2 = CAMERA_PIN_D2,
        .pin_d1 = CAMERA_PIN_D1,
        .pin_d0 = CAMERA_PIN_D0,
        .pin_vsync = CAMERA_PIN_VSYNC,
        .pin_href = CAMERA_PIN_HREF,
        .pin_pclk = CAMERA_PIN_PCLK,
        .xclk_freq_hz = CAMERA_XCLK_FREQ_HZ,
        .ledc_timer = LEDC_TIMER_1,
        .ledc_channel = LEDC_CHANNEL_1,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = FRAMESIZE_SVGA,
        .jpeg_quality = CAMERA_JPEG_QUALITY,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t ret = esp_camera_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "camera init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor != NULL) {
        ESP_LOGI(TAG, "camera sensor pid=0x%04x", sensor->id.PID);
    }

    ESP_LOGI(TAG, "camera initialized: /capture and /stream enabled");
    return ESP_OK;
}

static esp_err_t http_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CAMERA_HTTP_PORT;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&s_httpd, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "http server start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    const httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
    };
    const httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &index_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &capture_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &stream_uri));

    ESP_LOGI(TAG, "camera HTTP server started on port %d", CAMERA_HTTP_PORT);
    return ESP_OK;
}

esp_err_t camera_service_start(void)
{
#if ESP_CAMERA_SUPPORTED
    esp_err_t ret = camera_init();
    if (ret != ESP_OK) {
        return ret;
    }
    return http_server_start();
#else
    ESP_LOGE(TAG, "camera is not supported on this target");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
