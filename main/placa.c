#include "placa.h"

#include <stdlib.h>
#include <string.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define DEBOUNCE_MS           200
#define FILTRO_ADC_JANELA     8U

static const char *TAG = "PLACA";

static adc_oneshot_unit_handle_t adc1_handle = NULL;

// Estrutura da media movel de 8 amostras para estabilizar o potenciometro
typedef struct {
    uint16_t amostras[FILTRO_ADC_JANELA];
    uint32_t soma;
    uint8_t proximo_indice;
    uint8_t quantidade;
} filtro_media_movel_t;

static filtro_media_movel_t filtro_pot;
static int ultimo_adc_filtrado = 0;

// Flags de eventos alimentadas pelas ISRs com debounce (nunca perde um clique)
static volatile bool evento_natural_pendente = false;
static volatile bool evento_gelada_pendente = false;
static TickType_t ultimo_tick_natural = 0;
static TickType_t ultimo_tick_gelada = 0;
static bool ja_apertou_natural = false;
static bool ja_apertou_gelada = false;

static int processar_media_movel(filtro_media_movel_t *f, int leitura_bruta)
{
    if (leitura_bruta < 0) leitura_bruta = 0;
    if (leitura_bruta > 4095) leitura_bruta = 4095;

    if (f->quantidade == FILTRO_ADC_JANELA) {
        f->soma -= f->amostras[f->proximo_indice];
    } else {
        f->quantidade++;
    }

    f->amostras[f->proximo_indice] = (uint16_t)leitura_bruta;
    f->soma += (uint32_t)leitura_bruta;
    f->proximo_indice = (uint8_t)((f->proximo_indice + 1U) % FILTRO_ADC_JANELA);

    return (int)((f->soma + f->quantidade / 2U) / f->quantidade);
}

// ISR do Botao 1 (GPIO34 - Agua Natural) na borda de subida
static void isr_handler_btn_natural(void *arg)
{
    (void)arg;
    TickType_t agora = xTaskGetTickCountFromISR();
    if (!ja_apertou_natural ||
        (TickType_t)(agora - ultimo_tick_natural) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
        ja_apertou_natural = true;
        ultimo_tick_natural = agora;
        evento_natural_pendente = true;
    }
}

// ISR do Botao 2 (GPIO35 - Agua Gelada) na borda de subida
static void isr_handler_btn_gelada(void *arg)
{
    (void)arg;
    TickType_t agora = xTaskGetTickCountFromISR();
    if (!ja_apertou_gelada ||
        (TickType_t)(agora - ultimo_tick_gelada) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
        ja_apertou_gelada = true;
        ultimo_tick_gelada = agora;
        evento_gelada_pendente = true;
    }
}

uint64_t millis(void)
{
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void placa_set_leds(bool led_natural, bool led_gelada, bool led_fluxo)
{
    ESP_ERROR_CHECK(gpio_set_level(PINO_LED1, led_natural ? LED_LIGADO : LED_DESLIGADO));
    ESP_ERROR_CHECK(gpio_set_level(PINO_LED2, led_gelada ? LED_LIGADO : LED_DESLIGADO));
    ESP_ERROR_CHECK(gpio_set_level(PINO_LED3, led_fluxo ? LED_LIGADO : LED_DESLIGADO));
}

void buzzer_pwm(int duty)
{
    if (duty < 0) duty = 0;
    if (duty > 255) duty = 255;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

void bip_buzzer(uint32_t tempo_ligado_ms, uint32_t tempo_desligado_ms)
{
    buzzer_pwm(VOLUME_BUZZER);
    delay_ms(tempo_ligado_ms);
    buzzer_pwm(0);
    if (tempo_desligado_ms > 0) {
        delay_ms(tempo_desligado_ms);
    }
}

void placa_bip(uint32_t duracao_ms)
{
    bip_buzzer(duracao_ms, 0);
}

int placa_ler_vazao(void)
{
    int leitura_bruta = 0;
    if (adc_oneshot_read(adc1_handle, CANAL_ADC_POT, &leitura_bruta) == ESP_OK) {
        ultimo_adc_filtrado = processar_media_movel(&filtro_pot, leitura_bruta);
    }
    int vazao = (ultimo_adc_filtrado * 100) / 4095;
    if (vazao < 0) vazao = 0;
    if (vazao > 100) vazao = 100;
    return vazao;
}

bool placa_clicou_natural(void)
{
    if (evento_natural_pendente) {
        evento_natural_pendente = false;
        return true;
    }
    return false;
}

bool placa_clicou_gelada(void)
{
    if (evento_gelada_pendente) {
        evento_gelada_pendente = false;
        return true;
    }
    return false;
}

void placa_inicializar(void)
{
    memset(&filtro_pot, 0, sizeof(filtro_pot));
    ultimo_adc_filtrado = 0;

    // 1) GPIO23: mantido em alta impedancia para nao interferir na trilha da PCB
    const gpio_config_t conf_trilha = {
        .pin_bit_mask = (1ULL << PINO_POT_TRILHA),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&conf_trilha));

    // 2) LEDs: saidas digitais com logica active-low
    const gpio_config_t conf_leds = {
        .pin_bit_mask = (1ULL << PINO_LED1) | (1ULL << PINO_LED2) | (1ULL << PINO_LED3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&conf_leds));
    placa_set_leds(false, false, false);

    // 3) ADC1 Canal 4 (GPIO32), 12 bits, atenuacao de 12 dB
    const adc_oneshot_unit_init_cfg_t adc_unit = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_unit, &adc1_handle));

    const adc_oneshot_chan_cfg_t adc_canal = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, CANAL_ADC_POT, &adc_canal));

    // 4) Buzzer em GPIO2: configurado EXCLUSIVAMENTE via PWM LEDC (2 kHz / 8 bits)
    const ledc_timer_config_t pwm_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BUZZER_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&pwm_timer));

    const ledc_channel_config_t pwm_channel = {
        .gpio_num = PINO_BUZZER,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&pwm_channel));
    buzzer_pwm(0);

    // 5) Botoes em GPIO34 e GPIO35 com pull-down externo e interrupcao de borda de subida
    const gpio_config_t conf_botoes = {
        .pin_bit_mask = (1ULL << PINO_BTN_NATURAL) | (1ULL << PINO_BTN_GELADA),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&conf_botoes));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PINO_BTN_NATURAL, isr_handler_btn_natural, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PINO_BTN_GELADA, isr_handler_btn_gelada, NULL));

    ESP_LOGI(TAG, "Hardware do purificador inicializado com sucesso!");
}