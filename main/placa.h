#ifndef PLACA_H
#define PLACA_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"

// LEDs com logica active-low (0 = aceso, 1 = apagado)
#define PINO_LED1           GPIO_NUM_12 // LED Agua Natural
#define PINO_LED2           GPIO_NUM_14 // LED Agua Gelada
#define PINO_LED3           GPIO_NUM_27 // LED Fluxo / Vazao
#define LED_LIGADO          0
#define LED_DESLIGADO       1

// Botoes em GPIO34 e GPIO35: somente entrada, com pull-down externo (0 -> 1 ao apertar)
#define PINO_BTN_NATURAL    GPIO_NUM_34
#define PINO_BTN_GELADA     GPIO_NUM_35

// Buzzer passivo no GPIO2 acionado via LEDC PWM
#define PINO_BUZZER         GPIO_NUM_2
#define BUZZER_PWM_HZ       2000
#define VOLUME_BUZZER       128 // 50% de duty cycle em 8 bits (0..255)

// Potenciometro no GPIO32 (ADC1 Canal 4) e trilha no GPIO23 em alta impedancia
#define PINO_POT_TRILHA     GPIO_NUM_23
#define CANAL_ADC_POT       ADC_CHANNEL_4

// Inicializa GPIOs, ADC, PWM LEDC do buzzer e interrupcoes dos botoes
void placa_inicializar(void);

// Controle de tempo e utilitarios
uint64_t millis(void);
void delay_ms(uint32_t ms);

// Controle dos 3 LEDs (true = aceso, false = apagado)
void placa_set_leds(bool led_natural, bool led_gelada, bool led_fluxo);

// Controle direto do PWM do buzzer (0 = desligado, 128 = 50% duty) e bipe temporizado
void buzzer_pwm(int duty);
void bip_buzzer(uint32_t tempo_ligado_ms, uint32_t tempo_desligado_ms);
void placa_bip(uint32_t duracao_ms);

// Leitura da vazao no potenciometro (0 a 100%) com media movel de 8 amostras
int placa_ler_vazao(void);

// Verifica se houve clique nos botoes (capturado via ISR + fila FreeRTOS com debounce)
bool placa_clicou_natural(void);
bool placa_clicou_gelada(void);

#endif // PLACA_H