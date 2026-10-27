import numpy as np
import matplotlib.pyplot as plt

raw = np.fromfile("rxdata.pcm", dtype=np.int16)
r = raw[0::2].astype(np.float64)
i = raw[1::2].astype(np.float64)

# Вычисляем амплитудную огибающую — она дает идеальную горизонтальную полку
mag = np.sqrt(r**2 + i**2)

limit = 1920 * 10

plt.figure(figsize=(13, 5))
plt.plot(mag[:limit], color='blue', linewidth=1.2, label='Полка сигнала |IQ|')
plt.title("Идеальные полки пакетов (rxdata.pcm)")
plt.xlabel("Сэмплы")
plt.ylabel("Амплитуда полки")
plt.grid(True, linestyle='--', alpha=0.6)
plt.legend()
plt.tight_layout()
plt.show()