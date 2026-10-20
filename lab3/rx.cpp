#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
    const char *uri = (argc > 1) ? argv[1] : "usb:";

    FILE *outfile = fopen("rxdata.pcm", "wb");
    if (!outfile) {
        perror("[RX] Ошибка создания файла rxdata.pcm");
        return -1;
    }

    SoapySDRKwargs args = {};
    SoapySDRKwargs_set(&args, "driver", "plutosdr");
    SoapySDRKwargs_set(&args, "uri", uri);
    SoapySDRKwargs_set(&args, "direct", "1");
    SoapySDRKwargs_set(&args, "timestamp_every", "1920");

    printf("[RX] Подключение к Pluto (%s)...\n", uri);
    SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
    SoapySDRKwargs_clear(&args);

    if (!sdr) {
        fprintf(stderr, "[RX] Ошибка инициализации!\n");
        fclose(outfile);
        return -1;
    }

    const double sample_rate = 1e6;
    const double carrier_freq = 650e6;

    SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, sample_rate);
    SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, carrier_freq, NULL);
    SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, 30.0); // Высокая чувствительность

    size_t channel = 0;
    SoapySDRStream *rxStream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CS16, &channel, 1, NULL);
    SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0);

    size_t rx_mtu = SoapySDRDevice_getStreamMTU(sdr, rxStream);
    int16_t *rx_buffer = (int16_t *)malloc(2 * rx_mtu * sizeof(int16_t));

    const long timeoutUs = 500000;
    size_t total_buffers = 1000; // Читаем 100 буферов

    printf("[RX] Прием сэмплов в файл rxdata.pcm...\n");
    for (size_t i = 0; i < total_buffers; i++) {
        void *rx_buffs[] = {rx_buffer};
        int flags = 0;
        long long timeNs = 0;

        int sr = SoapySDRDevice_readStream(sdr, rxStream, rx_buffs, rx_mtu, &flags, &timeNs, timeoutUs);
        if (sr > 0) {
            fwrite(rx_buffer, sizeof(int16_t), 2 * sr, outfile);
        }
    }

    printf("[RX] Данные записаны.\n");
    fclose(outfile);
    free(rx_buffer);
    SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
    SoapySDRDevice_closeStream(sdr, rxStream);
    SoapySDRDevice_unmake(sdr);
    return 0;
}