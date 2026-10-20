import numpy as np
import matplotlib.pyplot as plt

raw = np.fromfile("rx_node1.pcm", dtype=np.int16)
real = raw[0::2]
imag = raw[1::2]

# Смотрим первые 10 буферов (19 200 отсчётов)
limit = 1920 * 10

plt.figure(figsize=(13, 6))
plt.plot(imag[:limit], color='red', linewidth=0.8, alpha=0.7, label='Q')
plt.plot(real[:limit], color='blue', linewidth=0.8, alpha=0.7, label='I')
plt.title("rx_node1.pcm (Приём пакетов 1920 отсчётов)")
plt.xlabel("Сэмплы")
plt.ylabel("Амплитуда")
plt.legend(loc='upper right')
plt.tight_layout()
plt.show()