import pandas as pd
import matplotlib.pyplot as plt

# caminho com raw string (pra evitar erro com "\")
path = r"Z:\\walking_01.csv"

# lê o CSV
df = pd.read_csv(path, sep=";", decimal=".", engine="python").dropna()
print(df.head())

# === intervalo entre amostras em ms (use o valor de EI_CLASSIFIER_INTERVAL_MS) ===
sample_interval_ms = 50  # ajuste para o valor real
t = (df.index.to_numpy() * sample_interval_ms) / 1000.0  # tempo em segundos

plt.figure(figsize=(12, 8))
plt.subplot(2, 1, 1)
plt.plot(t, df["ax"], label="ax")
plt.plot(t, df["ay"], label="ay")
plt.plot(t, df["az"], label="az")
plt.title("Acelerômetro")
plt.xlabel("Tempo (s)")
plt.ylabel("g")
plt.legend()
plt.grid(True)

plt.subplot(2, 1, 2)
plt.plot(t, df["gx"], label="gx")
plt.plot(t, df["gy"], label="gy")
plt.plot(t, df["gz"], label="gz")
plt.title("Giroscópio")
plt.xlabel("Tempo (s)")
plt.ylabel("°/s")
plt.legend()
plt.grid(True)

plt.tight_layout()
plt.show()
