// Copyright 2022-2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb_stream.h"
#include <jpeglib.h>

static const char *TAG = "uvc_mic_spk_demo";
/****************** configure the example working mode *******************************/
#define ENABLE_UVC_CAMERA_FUNCTION        1        /* enable uvc function */
#define ENABLE_UAC_MIC_SPK_FUNCTION       0        /* enable uac mic+spk function */
#if (ENABLE_UVC_CAMERA_FUNCTION)
#define ENABLE_UVC_FRAME_RESOLUTION_ANY   0        /* Using any resolution found from the camera */
#define ENABLE_UVC_WIFI_XFER              1        /* transfer uvc frame to wifi http */
#endif
#if (ENABLE_UAC_MIC_SPK_FUNCTION)
#define ENABLE_UAC_FEATURE_CONTROL        0        /* enable feature control(volume, mute) if the module support*/
#define ENABLE_UAC_MIC_SPK_LOOPBACK       0        /* transfer mic data to speaker */
#endif

#if (ENABLE_UVC_CAMERA_FUNCTION)
#if (ENABLE_UVC_FRAME_RESOLUTION_ANY)
#define DEMO_UVC_FRAME_WIDTH        FRAME_RESOLUTION_ANY
#define DEMO_UVC_FRAME_HEIGHT       FRAME_RESOLUTION_ANY
#else
#define DEMO_UVC_FRAME_WIDTH        960
#define DEMO_UVC_FRAME_HEIGHT       544
#endif
#define DEMO_UVC_XFER_BUFFER_SIZE   (35 * 1024) //Double buffer
#if (ENABLE_UVC_WIFI_XFER)
#include "app_wifi.h"
#include "app_httpd.h"
#include "esp_camera.h"

#define BIT1_NEW_FRAME_START (0x01 << 1)
#define BIT2_NEW_FRAME_END (0x01 << 2)
static EventGroupHandle_t s_evt_handle;
static camera_fb_t s_fb = {0};

camera_fb_t* esp_camera_fb_get()
{
    xEventGroupWaitBits(s_evt_handle, BIT1_NEW_FRAME_START, true, true, portMAX_DELAY);
    return &s_fb;
}

void esp_camera_fb_return(camera_fb_t * fb)
{
    xEventGroupSetBits(s_evt_handle, BIT2_NEW_FRAME_END);
    return;
}
#define OUTPUT_BUF_SIZE  4096
typedef struct {
    struct jpeg_destination_mgr pub; /* public fields */

    JOCTET * buffer;    /* start of buffer */

    unsigned char *outbuffer;
    int outbuffer_size;
    unsigned char *outbuffer_cursor;
    int *written;

} mjpg_destination_mgr;

typedef mjpg_destination_mgr * mjpg_dest_ptr;
METHODDEF(void) init_destination(j_compress_ptr cinfo)
{
    mjpg_dest_ptr dest = (mjpg_dest_ptr) cinfo->dest;

    /* Allocate the output buffer --- it will be released when done with image */
    dest->buffer = (JOCTET *)(*cinfo->mem->alloc_small)((j_common_ptr) cinfo, JPOOL_IMAGE, OUTPUT_BUF_SIZE * sizeof(JOCTET));

    *(dest->written) = 0;

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;
}

/******************************************************************************
Description.: called whenever local jpeg buffer fills up
Input Value.:
Return Value:
******************************************************************************/
METHODDEF(boolean) empty_output_buffer(j_compress_ptr cinfo)
{
    mjpg_dest_ptr dest = (mjpg_dest_ptr) cinfo->dest;

    memcpy(dest->outbuffer_cursor, dest->buffer, OUTPUT_BUF_SIZE);
    dest->outbuffer_cursor += OUTPUT_BUF_SIZE;
    *(dest->written) += OUTPUT_BUF_SIZE;

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;

    return TRUE;
}

/******************************************************************************
Description.: called by jpeg_finish_compress after all data has been written.
              Usually needs to flush buffer.
Input Value.:
Return Value:
******************************************************************************/
METHODDEF(void) term_destination(j_compress_ptr cinfo)
{
    mjpg_dest_ptr dest = (mjpg_dest_ptr) cinfo->dest;
    size_t datacount = OUTPUT_BUF_SIZE - dest->pub.free_in_buffer;

    /* Write any data remaining in the buffer */
    memcpy(dest->outbuffer_cursor, dest->buffer, datacount);
    dest->outbuffer_cursor += datacount;
    *(dest->written) += datacount;
}
GLOBAL(void) dest_buffer(j_compress_ptr cinfo, unsigned char *buffer, int size, int *written)
{
    mjpg_dest_ptr dest;

    if(cinfo->dest == NULL) {
        cinfo->dest = (struct jpeg_destination_mgr *)(*cinfo->mem->alloc_small)((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(mjpg_destination_mgr));
    }

    dest = (mjpg_dest_ptr) cinfo->dest;
    dest->pub.init_destination = init_destination;
    dest->pub.empty_output_buffer = empty_output_buffer;
    dest->pub.term_destination = term_destination;
    dest->outbuffer = buffer;
    dest->outbuffer_size = size;
    dest->outbuffer_cursor = buffer;
    dest->written = written;
}
static void camera_frame_cb(uvc_frame_t *frame, void *ptr)
{
    ESP_LOGI(TAG, "uvc callback! frame_format = %d, seq = %"PRIu32", width = %"PRIu32", height = %"PRIu32", length = %u, ptr = %d",
            frame->frame_format, frame->sequence, frame->width, frame->height, frame->data_bytes, (int) ptr);

    switch (frame->frame_format) {
        case UVC_FRAME_FORMAT_MJPEG:
        ESP_LOGI(TAG, "size of DATA = %u ptr = %u",
            sizeof(frame->data), frame->data_bytes);
            struct jpeg_compress_struct cinfo;
            struct jpeg_error_mgr jerr;
            JSAMPROW row_pointer[1];
            unsigned char *line_buffer, *yuyv;
            int z;
            static int written;
            line_buffer = calloc(frame->width * 3, 1);
            yuyv = frame->data;
            cinfo.err = jpeg_std_error(&jerr);
            jpeg_create_compress(&cinfo);
            dest_buffer(&cinfo, frame->data, frame->data_bytes, &written);
            ESP_LOGI(TAG, "starting compression--->%u",written);

            cinfo.image_width = frame->width;
            cinfo.image_height = frame->height;
            cinfo.input_components = 3;
            cinfo.in_color_space = JCS_RGB;

            jpeg_set_defaults(&cinfo);
            jpeg_set_quality(&cinfo, 80, TRUE);//80 means 80% of quality for the JPG image to produce
            cinfo.comp_info[0].h_samp_factor = 2;
            cinfo.comp_info[0].v_samp_factor = 2;
            jpeg_start_compress(&cinfo, TRUE);
            z = 0;
            unsigned char* yplane = yuyv;
            int uvdiv = 2;
            int uvheight = frame->height;
            int uvwidth = frame->width;
            // ------>startn nv12
            int i=0, j=0;
            int idx=0;
        
            unsigned char* ybase = yuyv;
            unsigned char *ubase = NULL;
            ubase=yuyv+uvwidth*uvheight;
            ESP_LOGI(TAG, "looping--->");
            while (cinfo.next_scanline < cinfo.image_height)
            {
                unsigned char *ptr = line_buffer;
                idx=0;
                for(i=0;i<uvwidth;i++)
                {   
                    *(ptr++)=ybase[i + j * uvwidth];
                    *(ptr++)=ubase[j/2 * uvwidth+(i/2)*2];
                    *(ptr++)=ubase[j/2 * uvwidth+(i/2)*2+1];
                }
                ESP_LOGI(TAG, "before_rec--->");
                row_pointer[0] = (char*)line_buffer;//recasting the line_buffer due to incompatibility with the compiler
                ESP_LOGI(TAG, "after_rec--->");
                jpeg_write_scanlines(&cinfo, row_pointer, 1);
                j++;
            }
            ESP_LOGI(TAG, "end lool--->");
            jpeg_finish_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);

            free(line_buffer);

            s_fb.buf = frame->data;
            s_fb.len = frame->data_bytes;
            s_fb.width = frame->width;
            s_fb.height = frame->height;
            s_fb.buf = frame->data;
            s_fb.format = PIXFORMAT_JPEG;
            s_fb.timestamp.tv_sec = frame->sequence;
            xEventGroupSetBits(s_evt_handle, BIT1_NEW_FRAME_START);
            ESP_LOGI(TAG, "send frame = %"PRIu32"",frame->sequence);
            xEventGroupWaitBits(s_evt_handle, BIT2_NEW_FRAME_END, true, true, pdTICKS_TO_MS(1000));
            ESP_LOGI(TAG, "send frame done = %"PRIu32"",frame->sequence);
            break;
        default:
            ESP_LOGW(TAG, "========Format not supported=======");
            assert(0);
            break;
    }
}
#else
static void camera_frame_cb(uvc_frame_t *frame, void *ptr)
{
    ESP_LOGI(TAG, "uvc callback! frame_format = %d, seq = %"PRIu32", width = %"PRIu32", height = %"PRIu32", length = %u, ptr = %d",
            frame->frame_format, frame->sequence, frame->width, frame->height, frame->data_bytes, (int) ptr);
}
#endif //ENABLE_UVC_WIFI_XFER
#endif //ENABLE_UVC_CAMERA_FUNCTION

#if (ENABLE_UAC_MIC_SPK_FUNCTION)
static void mic_frame_cb(mic_frame_t *frame, void *ptr)
{
    // We should using higher baudrate here, to reduce the blocking time here
    ESP_LOGD(TAG, "mic callback! bit_resolution = %u, samples_frequence = %"PRIu32", data_bytes = %"PRIu32,
            frame->bit_resolution, frame->samples_frequence, frame->data_bytes);
    // We should never block in mic callback!
#if (ENABLE_UAC_MIC_SPK_LOOPBACK)
    uac_spk_streaming_write(frame->data, frame->data_bytes, 0);
#endif //ENABLE_UAC_MIC_SPK_LOOPBACK
}
#endif //ENABLE_UAC_MIC_SPK_FUNCTION

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_err_t ret = ESP_FAIL;

#if (ENABLE_UVC_CAMERA_FUNCTION)
#if (ENABLE_UVC_WIFI_XFER)
    s_evt_handle = xEventGroupCreate();
    if (s_evt_handle == NULL) {
        ESP_LOGE(TAG, "line-%u event group create failed", __LINE__);
        assert(0);
    }
    app_wifi_main();
    app_httpd_main();
#endif //ENABLE_UVC_WIFI_XFER
    /* malloc double buffer for usb payload, xfer_buffer_size >= frame_buffer_size*/
    uint8_t *xfer_buffer_a = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    assert(xfer_buffer_a != NULL);
    uint8_t *xfer_buffer_b = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    assert(xfer_buffer_b != NULL);

    /* malloc frame buffer for a jpeg frame*/
    uint8_t *frame_buffer = (uint8_t *)malloc(DEMO_UVC_XFER_BUFFER_SIZE);
    assert(frame_buffer != NULL);

    uvc_config_t uvc_config = {
        .frame_width = DEMO_UVC_FRAME_WIDTH,
        .frame_height = DEMO_UVC_FRAME_HEIGHT,
        .frame_interval = FPS2INTERVAL(15),
        .xfer_buffer_size = DEMO_UVC_XFER_BUFFER_SIZE,
        .xfer_buffer_a = xfer_buffer_a,
        .xfer_buffer_b = xfer_buffer_b,
        .frame_buffer_size = DEMO_UVC_XFER_BUFFER_SIZE,
        .frame_buffer = frame_buffer,
        .frame_cb = &camera_frame_cb,
        .frame_cb_arg = NULL,
    };

    ret = uvc_streaming_config(&uvc_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uvc streaming config failed");
    }
#endif

#if (ENABLE_UAC_MIC_SPK_FUNCTION)
    uac_config_t uac_config = {
        .mic_bit_resolution = 16,
        .mic_samples_frequence = 16000,
        .spk_bit_resolution = 16,
        .spk_samples_frequence = 16000,
        .spk_buf_size = 16000,
        .mic_min_bytes = 320,
        .mic_cb = &mic_frame_cb,
        .mic_cb_arg = NULL,
    };
    ret = uac_streaming_config(&uac_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uac streaming config failed");
    }
#endif

    /* Start stream with pre-configs, usb stream driver will create multi-tasks internal
    to handle usb data from different pipes, and user's callback will be called after new frame ready. */
    ret = usb_streaming_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "usb streaming start failed");
    }
    ESP_LOGI(TAG, "usb streaming start succeed");

// IF not enable loopback, play default sound
#if (ENABLE_UAC_MIC_SPK_FUNCTION && !ENABLE_UAC_MIC_SPK_LOOPBACK)
    // set the speaker volume to 10%
#if (ENABLE_UAC_FEATURE_CONTROL)
    usb_streaming_control(STREAM_UAC_SPK, CTRL_UAC_VOLUME, (void *)60);
#endif
    extern const uint8_t wave_array_32000_16_1[];
    extern const uint32_t s_buffer_size;
    int freq_offsite_step = 32000 / uac_config.spk_samples_frequence;
    int downsampling_bits = 16 - uac_config.spk_bit_resolution;
    const int buffer_ms = 400;
    const int buffer_size = buffer_ms * (uac_config.spk_bit_resolution / 8) * (uac_config.spk_samples_frequence / 1000);
    // if 8bit spk, declare uint8_t *d_buffer
    uint16_t *s_buffer = (uint16_t *)wave_array_32000_16_1;
    uint16_t *d_buffer = calloc(1, buffer_size);
    size_t offset_size = buffer_size / (uac_config.spk_bit_resolution / 8);

    while (1) {
        if ((uint32_t)(s_buffer + offset_size) >= (uint32_t)(wave_array_32000_16_1 + s_buffer_size)) {
            s_buffer = (uint16_t *)wave_array_32000_16_1;
            // mute the speaker
#if (ENABLE_UAC_FEATURE_CONTROL)
            usb_streaming_control(STREAM_UAC_SPK, CTRL_UAC_MUTE, (void *)1);
#endif
            vTaskDelay(pdMS_TO_TICKS(1000));
            // un-mute the speaker
#if (ENABLE_UAC_FEATURE_CONTROL)
            usb_streaming_control(STREAM_UAC_SPK, CTRL_UAC_MUTE, (void *)0);
#endif
        } else {
            // fill to usb buffer
            for (size_t i = 0; i < offset_size; i++) {
                d_buffer[i] = *(s_buffer + i * freq_offsite_step) >> downsampling_bits;
            }
            // write to usb speaker
            uac_spk_streaming_write(d_buffer, buffer_size, portMAX_DELAY);
            s_buffer += offset_size * freq_offsite_step;
        }
    }
#endif

    while(1) {
        vTaskDelay(100);
    }
}
