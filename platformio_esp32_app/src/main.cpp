#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

extern "C" void app_main(void)
{
    lv_init();
    printf("LVGL inicializado no ESP32\n");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
