#include "placa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_rom_sys.h"

PurifierState_t system_state = {
    .natural_active = false,
    .chilled_active = false,
    .flow_percentage = 0,
    .beep_requested = false
};

static volatile uint64_t last_isr_time_natural = 0;
static volatile uint64_t last_isr_time_chilled = 0;

static adc_oneshot_unit_handle_t adc1_handle = NULL;

static void buzzer_pwm(int duty) {
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

static void IRAM_ATTR button_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    uint64_t current_time = (uint64_t)esp_timer_get_time() / 1000ULL;
    int level = gpio_get_level(gpio_num);

    if (gpio_num == PIN_BTN_NATURAL) {
        if (level == 1) {
            if ((current_time - last_isr_time_natural) >= DEBOUNCE_TIME_MS) {
                system_state.natural_active = !system_state.natural_active;
                if (system_state.natural_active) {
                    system_state.chilled_active = false;
                }
                system_state.beep_requested = true;
                last_isr_time_natural = current_time;
            }
        } else {
            // Lock timer on falling edge (1 -> 0) to filter release spring bounce
            last_isr_time_natural = current_time;
        }
    } else if (gpio_num == PIN_BTN_CHILLED) {
        if (level == 1) {
            if ((current_time - last_isr_time_chilled) >= DEBOUNCE_TIME_MS) {
                system_state.chilled_active = !system_state.chilled_active;
                if (system_state.chilled_active) {
                    system_state.natural_active = false;
                }
                system_state.beep_requested = true;
                last_isr_time_chilled = current_time;
            }
        } else {
            // Lock timer on falling edge (1 -> 0) to filter release spring bounce
            last_isr_time_chilled = current_time;
        }
    }
}

void hardware_init(void) {
    // 1) Potentiometer VCC Pin (GPIO 23 set HIGH)
    const gpio_config_t vcc_conf = {
        .pin_bit_mask = (1ULL << PIN_POT_VCC),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&vcc_conf));
    ESP_ERROR_CHECK(gpio_set_level(PIN_POT_VCC, 1));

    // 2) LED Config (Active LOW)
    const gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_LED_NATURAL) | (1ULL << PIN_LED_CHILLED) | (1ULL << PIN_LED_FLOW),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&led_conf));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_NATURAL, 1));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_CHILLED, 1));
    ESP_ERROR_CHECK(gpio_set_level(PIN_LED_FLOW, 1));

    // 3) ADC1 Config (GPIO 32 / ADC_CHANNEL_4)
    const adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    const adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_4, &chan_config));

    // 4) Buzzer Config (PWM LEDC 8-bit, 2500 Hz on GPIO 2)
    const ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_8_BIT, // Matches VOLUME_BUZZER = 128 (50% duty)
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 2500,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    const ledc_channel_config_t ledc_channel = {
        .gpio_num       = PIN_BUZZER,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    buzzer_pwm(0);

    // 5) Button Config (GPIO 34 & 35, external pull-down, ANYEDGE)
    const gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << PIN_BTN_NATURAL) | (1ULL << PIN_BTN_CHILLED),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&btn_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BTN_NATURAL, button_isr_handler, (void*) PIN_BTN_NATURAL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BTN_CHILLED, button_isr_handler, (void*) PIN_BTN_CHILLED));
}

void buzzer_beep(void) {
    buzzer_pwm(VOLUME_BUZZER);
    vTaskDelay(pdMS_TO_TICKS(100));
    buzzer_pwm(0);
}

long map_val(long x, long in_min, long in_max, long out_min, long out_max) {
    if (x < in_min) return out_min;
    if (x > in_max) return out_max;
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int read_potentiometer_percentage(void) {
    int adc_sum = 0;
    int valid_samples = 0;

    for (int i = 0; i < 10; i++) {
        int adc_val = 0;
        if (adc_oneshot_read(adc1_handle, ADC_CHANNEL_4, &adc_val) == ESP_OK) {
            adc_sum += adc_val;
            valid_samples++;
        }
        esp_rom_delay_us(200);
    }

    int adc_avg = (valid_samples > 0) ? (adc_sum / valid_samples) : 0;
    return (int)map_val(adc_avg, ADC_DEAD_ZONE_LOW, ADC_DEAD_ZONE_HIGH, 0, 100);
}

void update_leds(void) {
    gpio_set_level(PIN_LED_NATURAL, system_state.natural_active ? 0 : 1);
    gpio_set_level(PIN_LED_CHILLED, system_state.chilled_active ? 0 : 1);
}