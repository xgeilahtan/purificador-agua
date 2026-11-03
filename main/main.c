#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "placa.h"

void system_task(void *pvParameter) {
    int last_printed_flow = -1;

    while (1) {
        // 1. Update status LEDs immediately when state changes
        update_leds();

        // 2. Handle Beep if requested by ISR
        if (system_state.beep_requested) {
            system_state.beep_requested = false;
            buzzer_beep();
        }

        // 3. Read Potentiometer and print on >= 2% change or limits (0%, 100%)
        int current_flow = read_potentiometer_percentage();
        system_state.flow_percentage = current_flow;

        if (abs(current_flow - last_printed_flow) >= 2 || current_flow == 0 || current_flow == 100) {
            if (current_flow != last_printed_flow) {
                printf("Water Flow: %d%%\n", current_flow);
                last_printed_flow = current_flow;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void led_blink_task(void *pvParameter) {
    bool led3_state = false;

    while (1) {
        bool is_flowing = system_state.natural_active || system_state.chilled_active;
        int flow = system_state.flow_percentage;

        if (is_flowing && flow > 0) {
            led3_state = !led3_state;
            gpio_set_level(PIN_LED_FLOW, led3_state ? 0 : 1); // Active LOW

            // 100% flow = fast blink (50ms), 1% flow = slow blink (500ms)
            int blink_delay = 500 - (flow * 450 / 100);
            vTaskDelay(pdMS_TO_TICKS(blink_delay));
        } else {
            gpio_set_level(PIN_LED_FLOW, 1); // OFF
            led3_state = false;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void app_main(void) {
    hardware_init();

    xTaskCreate(system_task, "system_task", 4096, NULL, 5, NULL);
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 4, NULL);
}