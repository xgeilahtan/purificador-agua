#ifndef PLACA_H
#define PLACA_H

#include <stdint.h>
#include <stdbool.h>

// GPIO Definitions
#define PIN_LED_NATURAL    12
#define PIN_LED_CHILLED    14
#define PIN_LED_FLOW       27
#define PIN_BTN_NATURAL    34
#define PIN_BTN_CHILLED    35
#define PIN_BUZZER         2
#define PIN_POT_VCC        23
#define PIN_POT_ADC        32 // ADC1_CHANNEL_4

// Constants (VOLUME_BUZZER = 128 is 50% duty cycle in 8-bit LEDC resolution)
#define VOLUME_BUZZER      128
#define DEBOUNCE_TIME_MS   200

// ADC Dead Zones
#define ADC_DEAD_ZONE_LOW  150
#define ADC_DEAD_ZONE_HIGH 3950

// Global State Structure
typedef struct {
    volatile bool natural_active;
    volatile bool chilled_active;
    volatile int flow_percentage;
    volatile bool beep_requested;
} PurifierState_t;

extern PurifierState_t system_state;

// Function Prototypes
void hardware_init(void);
void buzzer_beep(void);
int read_potentiometer_percentage(void);
void update_leds(void);

#endif // PLACA_H