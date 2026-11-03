#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "placa.h"

typedef enum {
    ESTADO_PARADO = 0,
    ESTADO_AGUA_NATURAL,
    ESTADO_AGUA_GELADA
} estado_purificador_t;

void app_main(void)
{
    placa_inicializar();

    // Bipe curto ao ligar a placa para confirmar imediatamente que o buzzer esta funcionando
    placa_bip(100);

    estado_purificador_t estado = ESTADO_PARADO;
    uint64_t ultimo_toggle_fluxo_ms = millis();
    uint64_t ultimo_log_ms = millis();
    bool led_fluxo_aceso = false;

    printf("\n=== PURIFICADOR DE AGUA INICIADO ===\n");
    printf("Botao 1 (GPIO34): Agua Natural | Botao 2 (GPIO35): Agua Gelada\n");
    printf("Potenciometro (GPIO32): Regula a vazao (0 a 100%%)\n\n");

    while (1) {
        const uint64_t agora = millis();
        const int vazao = placa_ler_vazao();

        // Verifica clique no Botao 1 (Agua Natural)
        if (placa_clicou_natural()) {
            placa_bip(100); // Toca o buzzer por 100 ms
            if (estado == ESTADO_AGUA_NATURAL) {
                estado = ESTADO_PARADO;
                printf(">>> Fluxo DESLIGADO.\n");
            } else {
                estado = ESTADO_AGUA_NATURAL;
                ultimo_toggle_fluxo_ms = agora;
                led_fluxo_aceso = true;
                printf(">>> Agua NATURAL selecionada!\n");
            }
        }

        // Verifica clique no Botao 2 (Agua Gelada)
        if (placa_clicou_gelada()) {
            placa_bip(100); // Toca o buzzer por 100 ms
            if (estado == ESTADO_AGUA_GELADA) {
                estado = ESTADO_PARADO;
                printf(">>> Fluxo DESLIGADO.\n");
            } else {
                estado = ESTADO_AGUA_GELADA;
                ultimo_toggle_fluxo_ms = agora;
                led_fluxo_aceso = true;
                printf(">>> Agua GELADA selecionada!\n");
            }
        }

        // Controle visual dos LEDs (nao bloqueante)
        if (estado == ESTADO_PARADO || vazao == 0) {
            led_fluxo_aceso = false;
            placa_set_leds(estado == ESTADO_AGUA_NATURAL,
                           estado == ESTADO_AGUA_GELADA,
                           false);
        } else {
            // Quanto maior a vazao (1..100%), menor o meio-periodo da piscada (500 ms ate 80 ms)
            uint32_t meio_periodo_ms = (uint32_t)(500 - ((vazao * 420) / 100));
            if (meio_periodo_ms < 80U) meio_periodo_ms = 80U;

            if (agora - ultimo_toggle_fluxo_ms >= meio_periodo_ms) {
                ultimo_toggle_fluxo_ms = agora;
                led_fluxo_aceso = !led_fluxo_aceso;
            }

            placa_set_leds(estado == ESTADO_AGUA_NATURAL,
                           estado == ESTADO_AGUA_GELADA,
                           led_fluxo_aceso);
        }

        // Log periodico no monitor serial a cada 500 ms
        if (agora - ultimo_log_ms >= 500) {
            ultimo_log_ms = agora;
            const char *nome_estado = (estado == ESTADO_AGUA_NATURAL) ? "NATURAL" :
                                      (estado == ESTADO_AGUA_GELADA)  ? "GELADA"  : "PARADO";
            printf("Estado: %-7s | Vazao: %3d%%\n", nome_estado, vazao);
        }

        // Pausa curta de 15 ms para liberar a CPU sem bloquear a leitura dos botoes
        delay_ms(15);
    }
}