#!/usr/bin/env python3
import csv
import matplotlib.pyplot as plt
from pathlib import Path

# Directorio de resultados
RES_DIR = Path(Path.home() / "Practica_MPI" / "Resultados")
csv_file = RES_DIR / "analisis.csv"

# Leer datos
data = []
with open(csv_file, "r", encoding="utf8") as f:
    reader = csv.DictReader(f)
    for row in reader:
        data.append({
            "n_ciudades": int(row["n_ciudades"]),
            "p": int(row["p"]),
            "T_p": float(row["T_p"]),
            "Speedup": float(row["Speedup"]),
            "Eficiencia": float(row["Eficiencia"])
        })

# Agrupar por n_ciudades
from collections import defaultdict
por_tamaño = defaultdict(list)
for d in data:
    por_tamaño[d["n_ciudades"]].append(d)

# Ordenar cada grupo por p
for n in por_tamaño:
    por_tamaño[n].sort(key=lambda x: x["p"])

# Colores y estilos
colores = {
    4:  "#1f77b4",  # azul
    5:  "#ff7f0e",  # naranja
    10: "#2ca02c",  # verde
    20: "#d62728",  # rojo
    40: "#9467bd",  # morado
    50: "#8c564b",  # marrón
}

# 1) Gráfica de Speed-up
plt.figure(figsize=(10, 6))
for n_ciudades in sorted(por_tamaño.keys()):
    group = por_tamaño[n_ciudades]
    ps = [d["p"] for d in group]
    speedups = [d["Speedup"] for d in group]
    color = colores.get(n_ciudades, "black")
    plt.plot(ps, speedups, marker="o", label=f"{n_ciudades} ciudades", color=color)

plt.xlabel("Número de procesos (p)")
plt.ylabel("Speed-up")
plt.title("Speed-up vs. número de procesos (TSP MPI)")
plt.legend()
plt.grid(True, linestyle="--", alpha=0.4)
plt.xticks([1, 2, 4, 8, 16])
plt.tight_layout()
plt.savefig(RES_DIR / "speedup.png", dpi=300)
print(f"Gráfica de speed-up guardada en: {RES_DIR / 'speedup.png'}")
plt.close()

# 2) Gráfica de Eficiencia
plt.figure(figsize=(10, 6))
for n_ciudades in sorted(por_tamaño.keys()):
    group = por_tamaño[n_ciudades]
    ps = [d["p"] for d in group]
    eficiencias = [d["Eficiencia"] for d in group]
    color = colores.get(n_ciudades, "black")
    plt.plot(ps, eficiencias, marker="o", label=f"{n_ciudades} ciudades", color=color)

plt.xlabel("Número de procesos (p)")
plt.ylabel("Eficiencia")
plt.title("Eficiencia vs. número de procesos (TSP MPI)")
plt.legend()
plt.grid(True, linestyle="--", alpha=0.4)
plt.xticks([1, 2, 4, 8, 16])
plt.tight_layout()
plt.savefig(RES_DIR / "eficiencia.png", dpi=300)
print(f"Gráfica de eficiencia guardada en: {RES_DIR / 'eficiencia.png'}")
plt.close()

# 3) Gráfica de tiempo de ejecución (T_p) vs p
plt.figure(figsize=(10, 6))
for n_ciudades in sorted(por_tamaño.keys()):
    group = por_tamaño[n_ciudades]
    ps = [d["p"] for d in group]
    tp = [d["T_p"] for d in group]
    color = colores.get(n_ciudades, "black")
    plt.plot(ps, tp, marker="o", label=f"{n_ciudades} ciudades", color=color)

plt.xlabel("Número de procesos (p)")
plt.ylabel("Tiempo de ejecución T_p (s)")
plt.title("Tiempo de ejecución vs. número de procesos (TSP MPI)")
plt.legend()
plt.grid(True, linestyle="--", alpha=0.4)
plt.xticks([1, 2, 4, 8, 16])
plt.yscale("log")  # escala logarítmica para ver mejor las diferencias
plt.tight_layout()
plt.savefig(RES_DIR / "tiempo_vs_p.png", dpi=300)
print(f"Gráfica de tiempo vs. p guardada en: {RES_DIR / 'tiempo_vs_p.png'}")
plt.close()

print("\nTodas las gráficas se han guardado en:")
print(f"  {RES_DIR / 'speedup.png'}")
print(f"  {RES_DIR / 'eficiencia.png'}")
print(f"  {RES_DIR / 'tiempo_vs_p.png'}")