#include <stdio.h>
#include "bsp/device.h"
#include "bsp/display.h"
#include "bsp/input.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "hal/lcd_types.h"
#include "nvs_flash.h"
#include "pax_fonts.h"
#include "pax_gfx.h"
#include "pax_text.h"

// Constants
static char const TAG[] = "main";

// Global variables
static esp_lcd_panel_handle_t       display_lcd_panel    = NULL;
static esp_lcd_panel_io_handle_t    display_lcd_panel_io = NULL;
static size_t                       display_h_res        = 0;
static size_t                       display_v_res        = 0;
static lcd_color_rgb_pixel_format_t display_color_format;
static pax_buf_t                    fb                = {0};
static QueueHandle_t                input_event_queue = NULL;

void blit(void) {
    esp_lcd_panel_draw_bitmap(display_lcd_panel, 0, 0, display_h_res, display_v_res, pax_buf_get_pixels(&fb));
}

void app_main(void) {
    // Start the GPIO interrupt service
    gpio_install_isr_service(0);

    // Initialize the Non Volatile Storage service
    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        res = nvs_flash_init();
    }
    ESP_ERROR_CHECK(res);

    // Initialize the Board Support Package
    ESP_ERROR_CHECK(bsp_device_initialize());

    // Fetch the handle for using the screen, this works even when
    res = bsp_display_get_panel(&display_lcd_panel);
    ESP_ERROR_CHECK(res);                             // Check that the display handle has been initialized
    bsp_display_get_panel_io(&display_lcd_panel_io);  // Do not check result of panel IO handle: not all types of
                                                      // display expose a panel IO handle
    res = bsp_display_get_parameters(&display_h_res, &display_v_res, &display_color_format);
    ESP_ERROR_CHECK(res);  // Check that the display parameters have been initialized

    ESP_ERROR_CHECK(bsp_input_get_queue(&input_event_queue));

    // Initialize the graphics stack
    pax_buf_init(&fb, NULL, display_h_res, display_v_res, PAX_BUF_16_565RGB);
    pax_buf_reversed(&fb, false);
    pax_buf_set_orientation(&fb, PAX_O_ROT_CW);

    ESP_LOGW(TAG, "Hello world!");

    pax_background(&fb, 0xFFFFFFFF);
    pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 0, "Hello world!");
    blit();

    while (1) {
        bsp_input_event_t event;
        if (xQueueReceive(input_event_queue, &event, pdMS_TO_TICKS(10)) == pdTRUE) {
            switch (event.type) {
                case INPUT_EVENT_TYPE_KEYBOARD: {
                    if (event.args_keyboard.ascii != '\b' ||
                        event.args_keyboard.ascii != '\t') {  // Ignore backspace & tab keyboard events
                        ESP_LOGI(TAG, "Keyboard event %c (%02x) %s", event.args_keyboard.ascii,
                                 (uint8_t)event.args_keyboard.ascii, event.args_keyboard.utf8);
                        pax_simple_rect(&fb, 0xFFFFFFFF, 0, 0, pax_buf_get_width(&fb), 72);
                        pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 0, "Keyboard event");
                        char text[64];
                        snprintf(text, sizeof(text), "ASCII:     %c (0x%02x)", event.args_keyboard.ascii,
                                 (uint8_t)event.args_keyboard.ascii);
                        pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 18, text);
                        snprintf(text, sizeof(text), "UTF-8:     %s", event.args_keyboard.utf8);
                        pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 36, text);
                        snprintf(text, sizeof(text), "Modifiers: 0x%0" PRIX32, event.args_keyboard.modifiers);
                        pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 54, text);
                        blit();
                    }
                    break;
                }
                case INPUT_EVENT_TYPE_NAVIGATION: {
                    ESP_LOGI(TAG, "Navigation event %0" PRIX32 ": %s", (uint32_t)event.args_navigation.key,
                             event.args_navigation.state ? "pressed" : "released");

                    if (event.args_navigation.key == BSP_INPUT_NAVIGATION_KEY_F1) {
                        bsp_input_set_backlight_brightness(0);
                    }
                    if (event.args_navigation.key == BSP_INPUT_NAVIGATION_KEY_F2) {
                        bsp_input_set_backlight_brightness(100);
                    }

                    pax_simple_rect(&fb, 0xFFFFFFFF, 0, 0, pax_buf_get_width(&fb), 72);
                    pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 0, "Navigation event");
                    char text[64];
                    snprintf(text, sizeof(text), "Key:       0x%0" PRIX32, (uint32_t)event.args_navigation.key);
                    pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 18, text);
                    snprintf(text, sizeof(text), "State:     %s", event.args_navigation.state ? "pressed" : "released");
                    pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 36, text);
                    snprintf(text, sizeof(text), "Modifiers: 0x%0" PRIX32, event.args_navigation.modifiers);
                    pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 54, text);
                    blit();
                    break;
                }
                case INPUT_EVENT_TYPE_ACTION: {
                    ESP_LOGI(TAG, "Action event %0" PRIX32 ": %s", (uint32_t)event.args_action.type,
                             event.args_action.state ? "yes" : "no");
                    pax_simple_rect(&fb, 0xFFFFFFFF, 0, 0, pax_buf_get_width(&fb), 72);
                    pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 0, "Action event");
                    char text[64];
                    snprintf(text, sizeof(text), "Type:      0x%0" PRIX32, (uint32_t)event.args_action.type);
                    pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 36, text);
                    snprintf(text, sizeof(text), "State:     %s", event.args_action.state ? "yes" : "no");
                    pax_draw_text(&fb, 0xFF000000, pax_font_sky_mono, 16, 0, 54, text);
                    blit();
                    break;
                }
                default:
                    break;
            }
        }
    }
}
