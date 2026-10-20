#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

int main(int argc, char *argv[]) {
    const char *uri = (argc > 1) ? argv[1] : "usb:";

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", uri);
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920"); // Фиксируем размер пакета в 1920

    printf("[TX] Подключение к Pluto (%s)...\n", uri);
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (!sdr) {
        fprintf(stderr, "[TX] Ошибка инициализации!\n");
        return -1;
    }

    const double sample_rate = 1e6;
    const double carrier_freq = 800e6;

    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_TX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_TX, 0, carrier_freq, NULL);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_TX, 0, 0.0); // 0 dB усиление

    size_t channel = 0;
    SoapySDRStream *txStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_TX, SOAPY_SDR_CS16, &channel, 1, NULL);
    SoapySDRDevice_activateStream(sdr, txStream, 0, 0, 0);

    size_t tx_mtu = SoapySDRDevice_getStreamMTU(sdr, txStream);
    printf("[TX] MTU: %zu\n", tx_mtu);

    int16_t *tx_buff = (int16_t *)malloc(2 * tx_mtu * sizeof(int16_t));

    // Треугольный радиоимпульс с поднесущей 50 кГц
    double f_sub = 50e3;
    for (size_t s = 0; s < tx_mtu; s++) {
        double env = (s < tx_mtu / 2) ? ((double)s / (tx_mtu / 2.0)) 
                                      : ((double)(tx_mtu - s) / (tx_mtu / 2.0));
        double phase = 2.0 * 3.1415926535 * f_sub * s / sample_rate;
        int16_t amp = (int16_t)(env * 2000.0);

        tx_buff[2 * s]     = (int16_t)((int16_t)(amp * cos(phase)) << 4);
        tx_buff[2 * s + 1] = (int16_t)((int16_t)(amp * sin(phase)) << 4);
    }

    printf("[TX] Передача сигнала (нажмите Ctrl+C для остановки)...\n");
    void *tx_buffs[] = {tx_buff};
    const long timeoutUs = 500000;

    // Передаем 3000 пакетов (~6 секунд непрерывного вещания)
    for (int i = 0; i < 3000; i++) {
        int flags = 0;
        int st = SoapySDRDevice_writeStream(sdr, txStream, (const void * const*)tx_buffs, tx_mtu, &flags, 0, timeoutUs);
        if (st < 0) {
            fprintf(stderr, "[TX] Ошибка передачи: %d\n", st);
            break;
        }
    }

    printf("[TX] Передача завершена.\n");
    free(tx_buff);
    SoapySDRDevice_deactivateStream(sdr, txStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, txStream);
    SoapySDRDevice_unmake(sdr);
    return 0;
}