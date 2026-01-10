// app_httpd.cpp
// Added check_for_human_offline() function for AI face detection without web stream on top of the example code provided by Espressif.
#include "Arduino.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "driver/ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
    #include "esp32-hal-log.h"
    #define TAG ""
#else
    #include "esp_log.h"
    static const char *TAG = "camera_httpd";
#endif

#define CONFIG_ESP_FACE_DETECT_ENABLED 1
#define CONFIG_ESP_FACE_RECOGNITION_ENABLED 0

#if CONFIG_ESP_FACE_DETECT_ENABLED
    #include "fd_forward.h"
#endif

typedef struct {
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

#if CONFIG_ESP_FACE_DETECT_ENABLED
    static int8_t detection_enabled = 1;
    static mtmn_config_t mtmn_config = {0};
#endif

typedef struct {
    size_t size;
    size_t index;
    size_t count;
    int sum;
    int *values;
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
    memset(filter, 0, sizeof(ra_filter_t));
    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values) return NULL;
    memset(filter->values, 0, sample_size * sizeof(int));
    filter->size = sample_size;
    return filter;
}

static void draw_face_boxes(dl_matrix3du_t *image_matrix, box_array_t *boxes) {
    int x, y, w, h, i;
    uint32_t color = 0x0000FF00;
    fb_data_t fb;
    fb.width = image_matrix->w;
    fb.height = image_matrix->h;
    fb.data = image_matrix->item;
    fb.bytes_per_pixel = 3;
    fb.format = FB_BGR888;
    for (i = 0; i < boxes->len; i++) {
        x = (int)boxes->box[i].box_p[0];
        y = (int)boxes->box[i].box_p[1];
        w = (int)boxes->box[i].box_p[2] - x + 1;
        h = (int)boxes->box[i].box_p[3] - y + 1;
        fb_gfx_drawFastHLine(&fb, x, y, w, color);
        fb_gfx_drawFastHLine(&fb, x, y + h - 1, w, color);
        fb_gfx_drawFastVLine(&fb, x, y, h, color);
        fb_gfx_drawFastVLine(&fb, x + w - 1, y, h, color);
    }
}

static esp_err_t stream_handler(httpd_req_t *req) {
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[128];
    #if CONFIG_ESP_FACE_DETECT_ENABLED
        dl_matrix3du_t *image_matrix = NULL;
        bool detected = false;
    #endif
    static int64_t last_frame = 0;
    if (!last_frame) last_frame = esp_timer_get_time();

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        #if CONFIG_ESP_FACE_DETECT_ENABLED
            detected = false;
        #endif
        fb = esp_camera_fb_get();
        if (!fb) {
            res = ESP_FAIL;
        } else {
            #if CONFIG_ESP_FACE_DETECT_ENABLED
                if (!detection_enabled || fb->width > 400) {
            #endif
                    if (fb->format != PIXFORMAT_JPEG) {
                        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                        esp_camera_fb_return(fb);
                        fb = NULL;
                        if (!jpeg_converted) res = ESP_FAIL;
                    } else {
                        _jpg_buf_len = fb->len;
                        _jpg_buf = fb->buf;
                    }
            #if CONFIG_ESP_FACE_DETECT_ENABLED
                } else {
                    image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
                    if (!image_matrix) {
                        res = ESP_FAIL;
                    } else {
                        if (!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)) {
                            res = ESP_FAIL;
                        } else {
                            box_array_t *net_boxes = NULL;
                            if (detection_enabled) {
                                net_boxes = face_detect(image_matrix, &mtmn_config);
                            }
                            if (net_boxes) {
                                detected = true;
                                draw_face_boxes(image_matrix, net_boxes);
                                dl_lib_free(net_boxes->score);
                                dl_lib_free(net_boxes->box);
                                if (net_boxes->landmark != NULL) dl_lib_free(net_boxes->landmark);
                                dl_lib_free(net_boxes);
                            }
                            if (!fmt2jpg(image_matrix->item, fb->width * fb->height * 3, fb->width, fb->height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len)) {
                            }
                            esp_camera_fb_return(fb);
                            fb = NULL;
                        }
                        dl_matrix3du_free(image_matrix);
                    }
                }
            #endif
        }
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res == ESP_OK) {
            size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, 0, 0);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        if (fb) {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if (_jpg_buf) {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK) break;
    }
    return res;
}

static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    
    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
    httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };

    ra_filter_init(&ra_filter, 20);

    #if CONFIG_ESP_FACE_DETECT_ENABLED
        mtmn_config.type = FAST;
        mtmn_config.min_face = 40; 
        mtmn_config.pyramid = 0.707;
        mtmn_config.pyramid_times = 4;
        mtmn_config.p_threshold.score = 0.5;
        mtmn_config.p_threshold.nms = 0.7;
        mtmn_config.p_threshold.candidate_number = 20;
        mtmn_config.r_threshold.score = 0.7;
        mtmn_config.r_threshold.nms = 0.7;
        mtmn_config.r_threshold.candidate_number = 10;
        mtmn_config.o_threshold.score = 0.6;
        mtmn_config.o_threshold.nms = 0.7;
        mtmn_config.o_threshold.candidate_number = 1;
    #endif

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
    }
    
    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

// --- HUMAN DETECTION ---
void check_for_human_offline() {
    camera_fb_t *fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb) return;

    #if CONFIG_ESP_FACE_DETECT_ENABLED
    dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
    
    if (image_matrix) {
        if (fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item)) {
            
            // Run face detection algorithm
            // mtmn_config contains detection thresholds (set in startCameraServer)
            box_array_t *net_boxes = face_detect(image_matrix, &mtmn_config);
            
            if (net_boxes) {
                // If Face detected, Notify NodeMCU to arm the fence
                Serial.println("###HUMAN_DETECTED###");
                
                // Free detection result memory
                dl_lib_free(net_boxes->score);
                dl_lib_free(net_boxes->box);
                if (net_boxes->landmark != NULL) dl_lib_free(net_boxes->landmark);
                dl_lib_free(net_boxes);
            }
            //If net_boxes is NULL, no face was detected
        }
        dl_matrix3du_free(image_matrix);
    }
    #endif
    esp_camera_fb_return(fb);
}