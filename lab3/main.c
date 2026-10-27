#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define SAMPLE_RATE 1e6

typedef struct {
    int  node_id;       // 0 - Node 0, 1 - Node 1
    char uri[128];
    double tx_freq;
    double rx_freq;
    double tx_gain;
    double rx_gain;
    char out_filename[64];
} NodeConfig;

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Использование: %s <node_id: 0 | 1> [usb:X.Y.Z]\n", argv[0]);
        printf("  Узел 0: TX 800 MHz, RX 890 MHz\n");
        printf("  Узел 1: TX 890 MHz, RX 800 MHz\n");
        return EXIT_FAILURE;
    }

    NodeConfig cfg;
    cfg.node_id = atoi(argv[1]);

    if (cfg.node_id == 0) {
        cfg.tx_freq = 800e6;
        cfg.rx_freq = 890e6;
        cfg.tx_gain = 0.0;
        cfg.rx_gain = 35.0;
        snprintf(cfg.out_filename, sizeof(cfg.out_filename), "rx_node0.pcm");
        const char *uri = (argc > 2) ? argv[2] : "usb:1.6.5";
        strncpy(cfg.uri, uri, sizeof(cfg.uri));
    } else {
        cfg.tx_freq = 890e6;
        cfg.rx_freq = 800e6;
        cfg.tx_gain = 0.0;
        cfg.rx_gain = 35.0;
        snprintf(cfg.out_filename, sizeof(cfg.out_filename), "rx_node1.pcm");
        const char *uri = (argc > 2) ? argv[2] : "usb:3.4.5";
        strncpy(cfg.uri, uri, sizeof(cfg.uri));
    }

    printf("[Node %d] Подключение: %s\n", cfg.node_id, cfg.uri);
    printf("[Node %d] Настройки: TX=%.1f MHz, RX=%.1f MHz\n", 
           cfg.node_id, cfg.tx_freq / 1e6, cfg.rx_freq / 1e6);

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", cfg.uri);
    SoapySDRKwargs_set(&args, "direct", "1");
    // Фиксируем аппаратный размер блока PlutoSDR на 1920 сэмплов
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");
    SoapySDRKwargs_set(&args, "loopback", "0");

    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (!sdr) {
        printf("Ошибка инициализации устройства: %s\n", SoapySDRDevice_lastError());
        return EXIT_FAILURE;
    }

    // Настройка частот дискретизации и гетеродинов
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, SAMPLE_RATE);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, cfg.rx_freq, NULL);
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, SAMPLE_RATE);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, cfg.tx_freq, NULL);

    size_t channel = 0;
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, channel, cfg.rx_gain);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, channel, cfg.tx_gain);

    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, &channel, 1, NULL);
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, &channel, 1, NULL);

    if (!rxStream || !txStream) {
        printf("Ошибка создания потоков: %s\n", SoapySDRDevice_lastError());
        SoapySDRDevice_unmake(sdr);
        return EXIT_FAILURE;
    }

    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
    printf("MTU - TX: %zu, RX: %zu\n", tx_mtu, rx_mtu);

    int16_t *rx_buff = (int16_t *)malloc(2 * rx_mtu * sizeof(int16_t));
    int16_t *tx_buff = (int16_t *)malloc(2 * tx_mtu * sizeof(int16_t));
    int16_t *tx_zero = (int16_t *)calloc(2 * tx_mtu, sizeof(int16_t));

    // Наполнение: поднесущая 50 кГц через каждый второй отсчет (сэмпл - 0 - сэмпл - 0)
    double f_sub = 50e3;
    for (size_t s = 0; s < tx_mtu; s++) {
        if (s % 10 == 0) {
            double phase = 2.0 * 3.1415926535 * f_sub * s / SAMPLE_RATE;
            tx_buff[2 * s]     = (int16_t)((int16_t)(2000.0 * cos(phase)) << 4);
            tx_buff[2 * s + 1] = (int16_t)((int16_t)(2000.0 * sin(phase)) << 4);
        } else {
            tx_buff[2 * s]     = 0;
            tx_buff[2 * s + 1] = 0;
        }
    }

    // Маркерные заголовки 0xffff
    for (size_t i = 0; i < 2; i++) {
        tx_buff[0 + i]  = (int16_t)0xffff;
        tx_buff[10 + i] = (int16_t)0xffff;
    }

    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0);
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0);

    const long timeoutUs = 500000;

    // Сброс первых 128 буферов для очистки переходных процессов
    for (size_t b = 0; b < 128; b++) {
        void *buffs[] = {rx_buff};
        int flags = 0;
        long long timeNs = 0;
        SoapySDRDevice_readStream(sdr, rxStream, buffs, rx_mtu, &flags, &timeNs, timeoutUs);
    }

    FILE *file = fopen(cfg.out_filename, "wb");
    size_t total_iterations = 500; 
    printf("[Node %d] Старт потокового обмена (%zu буферов)...\n", cfg.node_id, total_iterations);

    for (size_t iter = 0; iter < total_iterations; iter++) {
        void *rx_buffs[] = {rx_buff};
        int rx_flags = 0;
        long long rx_time = 0;

        // 1. Приём
        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &rx_flags, &rx_time, timeoutUs);
        if (sr > 0 && file) {
            fwrite(rx_buff, sizeof(int16_t), 2 * sr, file);
        }

        // 2. Непрерывная передача: чередование полезного пакета и буфера нулей
        void *tx_buffs[1];
        if (iter % 2 == 0) {
            tx_buffs[0] = tx_buff;
        } else {
            tx_buffs[0] = tx_zero;
        }

        int tx_flags = 0; // Передача непрерывным потоком без флага SOAPY_SDR_HAS_TIME
        SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &tx_flags, 0, timeoutUs);
    }

    if (file) fclose(file);
    free(tx_buff);
    free(tx_zero);
    free(rx_buff);

    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_closeStream(sdr, txStream);
    SoapySDRDevice_unmake(sdr);

    printf("[Node %d] Готово. Сохранено в '%s'\n", cfg.node_id, cfg.out_filename);
    return EXIT_SUCCESS;
}