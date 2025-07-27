/*
 * 仅支持：init() -> capture() -> deinit()
 * 分辨率由关键字参数 w / h 传入（暂未实现），其余在 config.h
 */
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/binary.h"
#include "config.h"                 // 用户硬件配置
#include "unity.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "esp_ldo_regulator.h"
#include "driver/isp.h"
#include "example_sensor_init.h"
#include "esp_private/esp_cache_private.h"
#include "esp_heap_caps.h"

/* ---------------- 内部状态 ---------------- */
#define NUM_CAM_BUFFERS   2

static void *cam_buffers[NUM_CAM_BUFFERS] = {NULL};
static size_t cam_buffer_size = 0;

static esp_cam_ctlr_handle_t cam_handle = NULL;
static isp_proc_handle_t     isp_proc   = NULL;
static esp_ldo_channel_handle_t ldo_mipi_phy = NULL;

static bool frame_ready = false;

/* ---------------- 回调 ---------------- */
static bool camera_get_new_buffer(esp_cam_ctlr_handle_t handle,
                                  esp_cam_ctlr_trans_t *trans,
                                  void *user_data)
{
    trans->buffer = cam_buffers[0];          // 只用第一块 buffer
    trans->buflen = cam_buffer_size;
    return false;
}

static bool camera_trans_finished(esp_cam_ctlr_handle_t handle,
                                  esp_cam_ctlr_trans_t *trans,
                                  void *user_data)
{
    frame_ready = true;
    return false;
}

/* -------------- init -------------- */
static mp_obj_t camera_init(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args)
{
    enum { ARG_w, ARG_h };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_w, MP_ARG_INT, {.u_int = CSI_MIPI_CSI_DISP_HRES} },
        { MP_QSTR_h, MP_ARG_INT, {.u_int = CSI_MIPI_CSI_DISP_VRES} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args,
                     MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const uint16_t w = args[ARG_w].u_int;
    const uint16_t h = args[ARG_h].u_int;

    cam_buffer_size = w * h * 3;          // RGB565

    /* 1. LDO */
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = CSI_USED_LDO_CHAN_ID,
        .voltage_mv = CSI_USED_LDO_VOLTAGE_MV,
    };
    if (esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy) != ESP_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("LDO acquire failed"));
    }
    mp_printf(&mp_plat_print, "LDO initialized\n");
    /* 2. 帧缓冲 */
    size_t align = 0;
    esp_cache_get_alignment(0, &align);
    for (int i = 0; i < NUM_CAM_BUFFERS; ++i) {
        cam_buffers[i] = heap_caps_aligned_calloc(align, 1, cam_buffer_size,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
        if (!cam_buffers[i]) {
            mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("Buffer alloc failed"));
        }
    	mp_printf(&mp_plat_print, "Camera buffer %d allocated: %p\n", i, cam_buffers[i]);
    }
    mp_printf(&mp_plat_print, "%d camera buffers allocated\n", NUM_CAM_BUFFERS);
    /* 3. 传感器 */
    i2c_master_bus_handle_t sensor_handle;
    example_sensor_config_t sensor_cfg = {
        .i2c_port_num   = I2C_NUM_0,
        .i2c_sda_io_num = CSI_MIPI_CSI_CAM_SCCB_SDA_IO,
        .i2c_scl_io_num = CSI_MIPI_CSI_CAM_SCCB_SCL_IO,
        .port           = ESP_CAM_SENSOR_MIPI_CSI,
        .format_name    = CSI_CAM_FORMAT,
    };
    example_sensor_init(&sensor_cfg, &sensor_handle);
    mp_printf(&mp_plat_print, "Camera sensor initialized\n");

    /* 4. CSI 控制器 */
    esp_cam_ctlr_csi_config_t csi_cfg = {
        .ctlr_id                = 0,
        .h_res                  = w,
        .v_res                  = h,
        .lane_bit_rate_mbps     = CSI_MIPI_CSI_LANE_BITRATE_MBPS,
        .input_data_color_type  = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB888,
        .data_lane_num          = 2,
        .byte_swap_en           = false,
        .queue_items            = 1,
    };
    if (esp_cam_new_csi_ctlr(&csi_cfg, &cam_handle) != ESP_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("CSI init failed"));
    }
    mp_printf(&mp_plat_print, "CSI controller initialized\n");
   

    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans  = camera_get_new_buffer,
        .on_trans_finished = camera_trans_finished,
    };
    if (esp_cam_ctlr_register_event_callbacks(cam_handle, &cbs, NULL) != ESP_OK ||
        esp_cam_ctlr_enable(cam_handle) != ESP_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("CSI enable failed"));
    }
    mp_printf(&mp_plat_print, "Camera event callbacks registered\n");
    /* 5. ISP */
    esp_isp_processor_cfg_t isp_cfg = {
        .clk_hz                 = 80 * 1000 * 1000,
        .input_data_source      = ISP_INPUT_DATA_SOURCE_CSI,
        .input_data_color_type  = ISP_COLOR_RAW8,
        .output_data_color_type = ISP_COLOR_RGB888,
        .has_line_start_packet  = true,
        .has_line_end_packet    = true,
        .h_res                  = w,
        .v_res                  = h,
    };
    if (esp_isp_new_processor(&isp_cfg, &isp_proc) != ESP_OK ||
        esp_isp_enable(isp_proc) != ESP_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("ISP init failed"));
    }
    mp_printf(&mp_plat_print, "ISP processor initialized\n");

    /* 6. 启动 */
    if (esp_cam_ctlr_start(cam_handle) != ESP_OK) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Camera start failed"));
    }
    mp_printf(&mp_plat_print, "Camera capture started\n");

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(camera_init_obj, 0, camera_init);

/* -------------- deinit -------------- */
static mp_obj_t camera_deinit(void)
{
    if (cam_handle) {
        esp_cam_ctlr_stop(cam_handle);
        esp_cam_ctlr_disable(cam_handle);
        esp_cam_ctlr_del(cam_handle);
        cam_handle = NULL;
    }
    if (isp_proc) {
        esp_isp_disable(isp_proc);
        esp_isp_del_processor(isp_proc);
        isp_proc = NULL;
    }
    if (ldo_mipi_phy) {
        esp_ldo_release_channel(ldo_mipi_phy);
        ldo_mipi_phy = NULL;
    }
    for (int i = 0; i < NUM_CAM_BUFFERS; ++i) {
        if (cam_buffers[i]) {
            heap_caps_free(cam_buffers[i]);
            cam_buffers[i] = NULL;
        }
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(camera_deinit_obj, camera_deinit);

/* -------------- capture -------------- */
static mp_obj_t camera_capture(void)
{
    if (!cam_buffers[0]) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Camera not initialized"));
    }
    /* 阻塞等待一帧（这里简化：直接返回第一块 buffer 内容） */
    // TODO: 实际应用需要等待 frame_ready 或超时
    return mp_obj_new_bytes(cam_buffers[0], cam_buffer_size);
}
MP_DEFINE_CONST_FUN_OBJ_0(camera_capture_obj, camera_capture);

/* -------------- 模块表 -------------- */
static const mp_rom_map_elem_t camera_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_camera) },
    { MP_ROM_QSTR(MP_QSTR_init),    MP_ROM_PTR(&camera_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),  MP_ROM_PTR(&camera_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_capture), MP_ROM_PTR(&camera_capture_obj) },
};
static MP_DEFINE_CONST_DICT(camera_globals, camera_globals_table);

const mp_obj_module_t camera_user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&camera_globals,
};

MP_REGISTER_MODULE(MP_QSTR_camera, camera_user_cmodule);
