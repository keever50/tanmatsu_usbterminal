/******************************************************************************
 * MIT License
 * 
 * Copyright (c) 2025 Kevin Witteveen (MartiniMarter)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *****************************************************************************/


/******************************************************************************
 * Includes
 *****************************************************************************/

#include <stdio.h>
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "console.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "hal/lcd_types.h"
#include "hal/usb_serial_jtag_hal.h"
#include "nvs_flash.h"
#include "pax_fonts.h"
#include "pax_gfx.h"
#include "pax_text.h"
#include "console.h"
#include "driver/usb_serial_jtag.h"
#include "ezcmd2.h"

/******************************************************************************
 * Preprocessors
 *****************************************************************************/

#define ERRC ESP_ERROR_CHECK
#define BUF_SIZE (1024*16)

/******************************************************************************
 * Globals
 *****************************************************************************/

static esp_lcd_panel_handle_t g_disp_lcd_panel = NULL;
static size_t g_disp_h = 0;
static size_t g_disp_v = 0;
static lcd_color_rgb_pixel_format_t g_disp_color_format;
static pax_buf_t g_pax_buf = {0};
struct cons_insts_s g_con_insts;
static FILE *g_stdout_f;
char g_isb_buff[BUF_SIZE];
static QueueHandle_t g_input_event_queue = NULL;


/******************************************************************************
 * Private Functions
 *****************************************************************************/

void cons_output(char *str, size_t len)
{
  usb_serial_jtag_write_bytes(str, len, 1000);
}

esp_err_t main_init_nvs()
{
  esp_err_t res = nvs_flash_init();
  if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ERRC(nvs_flash_erase());
    res = nvs_flash_init();
  }

  return res;
}

void main_pax_init()
{
  pax_buf_init(&g_pax_buf, NULL,
               g_disp_h, g_disp_v,
               PAX_BUF_16_565RGB);
  pax_buf_reversed(&g_pax_buf, false);
  pax_buf_set_orientation(&g_pax_buf, PAX_O_ROT_CW);
}

void main_draw()
{
  const void *pixels = pax_buf_get_pixels(&g_pax_buf);

  esp_lcd_panel_draw_bitmap(g_disp_lcd_panel,
                            0, 0,
                            g_disp_h,
                            g_disp_v,
                            pixels);
}



/* Entry point */

void app_main( void )
{
  /* Initialization */

  gpio_install_isr_service(0);
  ERRC(main_init_nvs());         /* NVS */
  ERRC(bsp_device_initialize()); /* BSP */

  /* Get display device */

  ERRC(bsp_display_get_panel(&g_disp_lcd_panel));

  ERRC(bsp_display_get_parameters(&g_disp_h,
                                  &g_disp_v,
                                  &g_disp_color_format));

  /* Graphics init */

  main_pax_init();

  /* Console init */

  struct cons_config_s con_conf =
  {
    .font = pax_font_sky_mono,
    .font_size_mult = 1,
    .paxbuf = &g_pax_buf,
    .output_cb = cons_output
  };

  console_init(&g_con_insts, &con_conf);
  console_printf(&g_con_insts, "Init\n");
  main_draw();

  /* Input init */

  ERRC(bsp_input_get_queue(&g_input_event_queue));
  ERRC(bsp_input_set_backlight_brightness(100));

  /* Input loop */

  for (; ; )
  {
    bsp_input_event_t event;
    int ret = xQueueReceive(g_input_event_queue, &event, pdMS_TO_TICKS(10));
    if (ret == pdFALSE)
    {
      continue;
    }

    switch (event.type)
    {
      case INPUT_EVENT_TYPE_KEYBOARD:
      {
        char c = event.args_keyboard.ascii;
        console_put(&g_con_insts, c);
        main_draw();
        break;
      }

      case INPUT_EVENT_TYPE_NAVIGATION:
      {
        break;
      }

      case INPUT_EVENT_TYPE_ACTION:
      {
        break;
      }

      default:
      {
        break;
      }
    }
  }
}