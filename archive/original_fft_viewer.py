import serial
import numpy as np
import matplotlib.pyplot as plt
import re
import time

port = 'COM5'
baud = 115200
ser = serial.Serial(port, baud)

plt.ion()
fig, ax = plt.subplots()

FFT_SIZE = 128
Fs = 8000.0
x = np.arange(FFT_SIZE//2) * (Fs / FFT_SIZE)  # 0~4000 Hz
line, = ax.plot(x, np.zeros_like(x))
ax.set_xlim(0, Fs/2)
ax.set_ylim(0, 3000)
ax.set_xlabel("Frequency (Hz)")
ax.grid(True)   

pattern = re.compile(r"FFT:(.*)")
5
last_update = time.time()
update_interval = 5

while True:
    line_raw = ser.readline().decode(errors='ignore').strip()
    match = pattern.search(line_raw)
    if match:
        parts = [p.strip() for p in match.group(1).split(',') if p.strip() != '']
        nums = list(map(int, parts))
        if len(nums) == FFT_SIZE//2:
            if time.time() - last_update > update_interval:
                line.set_ydata(nums)
                plt.pause(0.5)
                last_update = time.time()



