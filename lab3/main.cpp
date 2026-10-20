#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <signal.h>

static volatile int keep_running = 1;
void sig_handler(int) {
    keep_running = 0;
}

// -------------------------------------------------------------
// Таблица частот
// -------------------------------------------------------------
const double FREQ_TX_SDR1 = 800e6; // SDR1 TX  -> 800 MHz
const double FREQ_RX_SDR2 = 800e6; // SDR2 RX  -> 800 MHz

const double FREQ_TX_SDR2 = 890e6; // SDR2 TX  -> 890 MHz
const double FREQ_RX_SDR1 = 890e6; // SDR1 RX  -> 890 MHz

const double SAMPLE_RATE  = 1e6;   // 1 Msps

// -------------------------------------------------------------
// Функция выполнения режима RX
// -------------------------------------------------------------
int run_rx(SoapySDRDevice *sdr, double freq, const char *out_filename) {
    FILE *outfile = fopen(out_filename, "wb");
    if (!outfile) {
        perror("[RX] Ошибка создания файла");
        return -1;
    }

    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, SAMPLE_RATE);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, freq, NULL);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, 90.0); // Усиление приемника

    size_t channel = 0;
    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, &channel, 1, NULL);
    if (!rxStream) {
        fprintf(stderr, "[RX] Ошибка создания потока!\n");
        fclose(outfile);
        return -1;
    }

    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0);
    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    int16_t *rx_buffer = (int16_t *)malloc(2 * rx_mtu * sizeof(int16_t));

    const long timeoutUs = 500000;
    size_t total_buffers = 1000; // ~1.92 секунды непрерывного приёма

    printf("[RX] Прием на %.1f МГц в файл '%s'...\n", freq / 1e6, out_filename);

    for (size_t i = 0; i < total_buffers && keep_running; i++) {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;
        long long timeNs = 0;

        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        if (sr > 0) {
            fwrite(rx_buffer, sizeof(int16_t), 2 * sr, outfile);
        }
    }

    printf("[RX] Запись завершена.\n");
    fclose(outfile);
    free(rx_buffer);
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, rxStream);
    return 0;
}

// -------------------------------------------------------------
// Функция выполнения режима TX
// -------------------------------------------------------------
int run_tx(SoapySDRDevice *sdr, double freq) {
    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, SAMPLE_RATE);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, freq, NULL);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, 0.0); // 0 dB = максимальная мощность

    size_t channel = 0;
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, &channel, 1, NULL);
    if (!txStream) {
        fprintf(stderr, "[TX] Ошибка создания потока!\n");
        return -1;
    }

    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0);
    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
    int16_t *tx_buff = (int16_t *)malloc(2 * tx_mtu * sizeof(int16_t));

    // Треугольная огибающая на поднесущей 50 кГц (прохождение через DC-фильтр)
    double f_sub = 50e3;
    for (size_t s = 0; s < tx_mtu; s++) {
        double env = (s < tx_mtu / 2) ? ((double)s / (tx_mtu / 2.0)) 
                                      : ((double)(tx_mtu - s) / (tx_mtu / 2.0));
        double phase = 2.0 * M_PI * f_sub * s / SAMPLE_RATE;
        int16_t amp = (int16_t)(env * 1800.0);

        tx_buff[2 * s]     = (int16_t)((int16_t)(amp * cos(phase)) << 4);
        tx_buff[2 * s + 1] = (int16_t)((int16_t)(amp * sin(phase)) << 4);
    }

    printf("[TX] Непрерывная передача на %.1f МГц. Для остановки нажмите Ctrl+C...\n", freq / 1e6);
    void *tx_buffs[] = {tx_buff};
    const long timeoutUs = 500000;

    while (keep_running) {
        int flags = 0;
        int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, 0, timeoutUs);
        if (st < 0) {
            fprintf(stderr, "[TX] Ошибка передачи: %d\n", st);
            break;
        }
    }

    printf("[TX] Передача остановлена.\n");
    free(tx_buff);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, txStream);
    return 0;
}

// -------------------------------------------------------------
// Точка входа
// -------------------------------------------------------------
int main(int argc, char *argv[]) {
    signal(SIGINT, sig_handler);

    if (argc < 4) {
        printf("Использование: %s <uri> <sdr_num: 1|2> <mode: rx|tx>\n", argv[0]);
        printf("Пример 1: %s usb:1.7.5 1 tx\n", argv[0]);
        printf("Пример 2: %s usb:3.6.5 2 rx\n", argv[0]);
        return -1;
    }

    const char *uri  = argv[1];
    int sdr_num      = atoi(argv[2]);
    const char *mode = argv[3];

    // Выбор рабочей частоты в зависимости от номера SDR и режима
    double target_freq = 0.0;
    char out_filename[64] = "rxdata.pcm";

    if (sdr_num == 1) {
        if (strcmp(mode, "tx") == 0) {
            target_freq = FREQ_TX_SDR1; // 800 МГц
        } else if (strcmp(mode, "rx") == 0) {
            target_freq = FREQ_RX_SDR1; // 890 МГц
            snprintf(out_filename, sizeof(out_filename), "rx_sdr1.pcm");
        }
    } else if (sdr_num == 2) {
        if (strcmp(mode, "tx") == 0) {
            target_freq = FREQ_TX_SDR2; // 890 МГц
        } else if (strcmp(mode, "rx") == 0) {
            target_freq = FREQ_RX_SDR2; // 800 МГц
            snprintf(out_filename, sizeof(out_filename), "rx_sdr2.pcm");
        }
    }

    if (target_freq == 0.0) {
        fprintf(stderr, "Ошибка параметров: sdr_num должен быть 1 или 2, mode — rx или tx.\n");
        return -1;
    }

    printf("=== Старт: Pluto #%d | Режим: %s | Частота: %.1f МГц | URI: %s ===\n",
           sdr_num, mode, target_freq / 1e6, uri);

    // Инициализация устройства SoapySDR
    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", uri);
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");

    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (!sdr) {
        fprintf(stderr, "Не удалось открыть устройство по адресу %s\n", uri);
        return -1;
    }

    // Запуск соответствующей ветки
    if (strcmp(mode, "rx") == 0) {
        run_rx(sdr, target_freq, out_filename);
    } else {
        run_tx(sdr, target_freq);
    }

    SoapySDRDevice_unmake(sdr);
    return 0;
}