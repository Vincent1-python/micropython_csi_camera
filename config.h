#define _CONFIG_H_

// 摄像头配置
#define CSI_USED_LDO_CHAN_ID               3
#define CSI_USED_LDO_VOLTAGE_MV            2500
#define CSI_RGB565_BITS_PER_PIXEL          16
#define CSI_MIPI_CSI_LANE_BITRATE_MBPS     200
#define CSI_MIPI_CSI_CAM_SCCB_SCL_IO       8
#define CSI_MIPI_CSI_CAM_SCCB_SDA_IO       7
#define CSI_MIPI_CSI_DISP_HRES             800
#define CSI_MIPI_CSI_DISP_VRES             640
#define CSI_CAM_FORMAT                     "MIPI_2lane_24Minput_RAW8_800x640_50fps"

// 显示屏配置 (ST7789)
#define LCD_HOST  SPI2_HOST
#define LCD_PIN_NUM_SCLK            4
#define LCD_PIN_NUM_MOSI            5
#define LCD_PIN_NUM_MISO            -1
#define LCD_PIN_NUM_LCD_DC          21
#define LCD_PIN_NUM_LCD_RST         20
#define LCD_PIN_NUM_LCD_CS          22
#define LCD_PIN_NUM_BK_LIGHT        23
#define LCD_DISP_H_RES              240    // 显示屏水平分辨率
#define LCD_DISP_V_RES              320    // 显示屏垂直分辨率
#define LCD_DISP_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)
#define LCD_DISP_BK_LIGHT_ON_LEVEL  1
#define LCD_DISP_BK_LIGHT_OFF_LEVEL !LCD_DISP_BK_LIGHT_ON_LEVEL

#define LCD_DISP_ROTATE             0       // 0, 90, 180, 270
#define CAMERA_SELFIE_MODE          true    // true-同向(自拍模式)  false-反向(观察模式)

// 双缓冲
#define NUM_CAM_BUFFERS 2
