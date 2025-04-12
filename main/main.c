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

#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"
#include "bsp/power.h"

/******************************************************************************
 * Preprocessors
 *****************************************************************************/

#define ERRC ESP_ERROR_CHECK
#define BUF_SIZE (1024*16)
#define LINE_BUF_SIZE 128

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
char g_linebuffer[LINE_BUF_SIZE];

unsigned long g_baudrate = 115200;
bool g_usb_connected = false;

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

/* ACM-ADC  ******************************************************************/

static void usb_lib_task(void *arg)
{
    while (1) {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            //ESP_LOGI(TAG, "USB: All devices freed");
            // Continue handling USB events to allow device reconnection
        }
    }
}

static bool handle_acmrx(const uint8_t *data, size_t data_len, void *arg)
{
    //ESP_LOGI(TAG, "Data received");
    //ESP_LOG_BUFFER_HEXDUMP(TAG, data, data_len, ESP_LOG_INFO);
    for (size_t i = 0; i < data_len; i++)
    {
      console_put(&g_con_insts, data[i]);
      //ESP_LOGW(TAG, "%c", data[i]);
    }
    main_draw();
    return true;
}

static void handle_acmevent(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        console_printf(&g_con_insts, "CDC-ACM error has occurred, err_no = %i\n", event->data.error); main_draw();
        g_usb_connected = false;
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        console_printf(&g_con_insts, "Device suddenly disconnected\n"); main_draw();
        ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
        g_usb_connected = false;
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        console_printf(&g_con_insts, "Serial state notif 0x%04X\n", event->data.serial_state.val); main_draw();
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
        console_printf(&g_con_insts, "Connected\n"); main_draw();
        g_usb_connected = true;
        break;
    default:
        console_printf(&g_con_insts, "Unsupported CDC event: %i\n", event->type); main_draw();
        break;
    }
}


void main_acmcdc()
{
  const cdc_acm_host_device_config_t dev_config = {
    .connection_timeout_ms = 100,
    .out_buffer_size = 512,
    .in_buffer_size = 512,
    .user_arg = NULL,
    .event_cb = handle_acmevent,
    .data_cb = handle_acmrx
  };

  cdc_acm_dev_hdl_t cdc_dev = NULL;

  /* Turn on USB power */

  bsp_power_set_usb_host_boost_enabled(true);
  //vTaskDelay(100);
  //bsp_power_set_radio_state(BSP_POWER_RADIO_STATE_APPLICATION);  

  /* Automatically connect to USB dev by polling... */

  console_printf(&g_con_insts, "Scanning for ACM-CDC interface..."); main_draw();

  int interface = 0;
  for (;;)
  {
    vTaskDelay(1);
    interface++;
    interface = interface % 10;

    esp_err_t err = cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, interface, &dev_config, &cdc_dev);
    if (err != ESP_OK) continue;
    cdc_acm_line_coding_t line_coding;
    line_coding.dwDTERate = g_baudrate;
    line_coding.bDataBits = 8;
    line_coding.bParityType = 0;
    line_coding.bCharFormat = 0;      
    err = cdc_acm_host_line_coding_set(cdc_dev, &line_coding);
    if (err != ESP_OK) continue;
    break;
  }

  console_printf(&g_con_insts, "\nConnected\n"); main_draw();

  g_usb_connected = 1;
  while (g_usb_connected)
  {
    vTaskDelay(100);
  }

  console_printf(&g_con_insts, "\nStopped\n"); main_draw();

  cdc_acm_host_close(cdc_dev);

  bsp_power_set_usb_host_boost_enabled(false);
}

/* Commands ******************************************************************/

void main_cmd_help(struct ezcmd_inst_s *ez)
{
  console_printf(&g_con_insts, "help.           Shows this\n");
  console_printf(&g_con_insts, "baud {x}.       Sets the baudrate (default 115200)\n");
  console_printf(&g_con_insts, "start.          Opens the ACM-CDC terminal\n");
}

void main_cmd_baud(struct ezcmd_inst_s *ez)
{
  char *arg = ezcmd_iterate_arguments(ez);
  if (arg == NULL) return;

  g_baudrate = strtoul(arg, NULL, 0);
  console_printf(&g_con_insts, "Baudrate set to %lu\n", g_baudrate);
}

void main_cmd_start(struct ezcmd_inst_s *ez)
{
  main_acmcdc();
}

void main_parse_cmd(struct ezcmd_inst_s *ez)
{
  /* Get command */

  char *cmd = ezcmd_iterate_arguments(ez);
  if (cmd == NULL) return;

  if (!strcmp(cmd, "help"))
  {
    main_cmd_help(ez);
  }
  else if (!strcmp(cmd, "baud"))
  {
    main_cmd_baud(ez);
  }
  else if (!strcmp(cmd, "start"))
  {
    main_cmd_start(ez);
  }  
}

void main_onkey(struct ezcmd_inst_s *ez, char c)
{
  console_put(&g_con_insts, c);

  int ret = ezcmd_put(ez, c);
  if (ret == 0) return;

  /* When ezcmd returns 1, we got a command ready */

  main_parse_cmd(ez);

  /* Reset and continue */

  ezcmd_reset(ez);

  console_printf(&g_con_insts, ">");
}

void main_inputlogic()
{
  struct ezcmd_inst_s ez;
  ezcmd_init(&ez, g_linebuffer, sizeof(g_linebuffer));

  /* Input loop */

  console_printf(&g_con_insts, ">");
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
        if (c == '\b')
        {
          /* Replace character with space */

          console_printf(&g_con_insts, "\b ");
        }
        //main_draw();
        main_onkey(&ez, c);
        break;
      }

      case INPUT_EVENT_TYPE_NAVIGATION:
      {
        bsp_input_navigation_key_t k = event.args_navigation.key;
        bool s = event.args_navigation.state;
        if (!s) break;

        /* Handle return */

        if (k == BSP_INPUT_NAVIGATION_KEY_RETURN)
        {
          //main_draw();
          console_printf(&g_con_insts, "\n");
          main_onkey(&ez, '\r');
          
          break;
        }

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

    main_draw();
  }

}

/* Entry point */

void app_main( void )
{
  /* Initialization */

  bsp_power_initialize();

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

  /* USB Host */

  console_printf(&g_con_insts, "Installing USB Host\n"); main_draw();
  const usb_host_config_t host_config = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  ESP_ERROR_CHECK(usb_host_install(&host_config));
  BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096, xTaskGetCurrentTaskHandle(), 1, NULL);
  assert(task_created == pdTRUE);

  /* CDC ACM driver */

  console_printf(&g_con_insts, "Installing CDC-ACM driver\n"); main_draw();
  ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

  
  for(;;)
  {
    main_inputlogic();
  }
}