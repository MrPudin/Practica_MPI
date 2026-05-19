#!/usr/bin/env python3
import os
import re
import csv
from pathlib import Path

# Directorio donde están los .out
RES_DIR = Path(Path.home() / "Practica_MPI" / "Resultados")

def extraer_tiempo_real(linea):
    """
    Extrae tiempo en formato: real  57m52.119s
    o: real	24m37.618s
    """
    m = re.search(r'real\s+(\d+)m(\d+\.?\d*)s', linea)
    if not m:
        return None
    minutos = int(m.group(1))
    segundos = float(m.group(2))
    return minutos * 60 + segundos

def extraer_n_ciudades_y_p(filename, tipo):
    """
    filename ejemplo:
      tspsec_20.out   -> n_ciudades=20, p=1
      tspmpi_20_4.out -> n_ciudades=20, p=4
    """
    if tipo == "serial":
        m = re.search(r'tspsec_(\d+)\.out', filename)
        if m:
            return int(m.group(1)), 1
    else:
        m = re.search(r'tspmpi_(\d+)_(\d+)\.out', filename)
        if m:
            return int(m.group(1)), int(m.group(2))
    return None, None

def main():
    resultados = []

    # Recorrer archivos .out
    for f in RES_DIR.glob("*.out"):
        filename = f.name

        if filename.startswith("tspsec_"):
            tipo = "serial"
        elif filename.startswith("tspmpi_"):
            tipo = "parallel"
        else:
            continue

        n_ciudades, p = extraer_n_ciudades_y_p(filename, tipo)
        if n_ciudades is None:
            print(f"Saltando archivo (no se puede parsear): {filename}")
            continue

        tiempo = None
        with open(f, "r", encoding="utf8", errors="ignore") as file:
            for linea in file:
                if "real" in linea.lower():
                    t = extraer_tiempo_real(linea)
                    if t is not None:
                        tiempo = t
                        break

        if tiempo is None:
            print(f"Advertencia: no se encontró tiempo 'real' en {filename}")
            continue

        resultados.append({
            "n_ciudades": n_ciudades,
            "tipo": tipo,
            "p": p,
            "tiempo_seg": tiempo
        })

    # CSV crudo (incluye secuenciales y paralelos)
    csv_raw = RES_DIR / "tiempos_raw.csv"
    with open(csv_raw, "w", newline="", encoding="utf8") as f:
        writer = csv.DictWriter(f, fieldnames=["n_ciudades", "tipo", "p", "tiempo_seg"])
        writer.writeheader()
        writer.writerows(resultados)

    print(f"Tiempos crudos guardados en: {csv_raw}")

    # Imprimir todos los tiempos crudos
    print("\n=== Tiempos crudos ===")
    print("n_ciudades | tipo     |   p | tiempo_seg")
    print("-" * 50)
    for r in resultados:
        print(f"{r['n_ciudades']:10} | {r['tipo']:8} | {r['p']:3} | {r['tiempo_seg']:10.3f}")

    # Construir diccionario: (n_ciudades, p) -> tiempo
    tiempos = {}
    for r in resultados:
        n = r["n_ciudades"]
        p = r["p"]
        t = r["tiempo_seg"]
        tiempos[(n, p)] = t

    analisis = []

    for n_ciudades in sorted(set(r["n_ciudades"] for r in resultados)):
        t_serial = tiempos.get((n_ciudades, 1), None)
        if t_serial is None or t_serial <= 0:
            print(f"Saltando n_ciudades={n_ciudades} (no hay tiempo serial)")
            continue

        # Incluir TODOS los p (incluido p = 1)
        for p in sorted(set(p for (n, p) in tiempos if n == n_ciudades)):
            t_p = tiempos.get((n_ciudades, p), None)
            if t_p is None or t_p <= 0:
                continue

            if p == 1:
                speedup = 1.0
                eficiencia = 1.0
            else:
                speedup = t_serial / t_p
                eficiencia = speedup / p

            analisis.append({
                "n_ciudades": n_ciudades,
                "p": p,
                "T_p": t_p,
                "Speedup": speedup,
                "Eficiencia": eficiencia
            })

    # Escribir CSV de análisis (INCLUYE p=1, pero sin T_serial como columna)
    csv_analisis = RES_DIR / "analisis.csv"
    with open(csv_analisis, "w", newline="", encoding="utf8") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "n_ciudades", "p", "T_p", "Speedup", "Eficiencia"
        ])
        writer.writeheader()
        writer.writerows(analisis)

    print(f"\nAnálisis de speed-up y eficiencia guardado en: {csv_analisis}")

    # Imprimir resumen en pantalla (INCLUYE p=1, sin T_serial)
    print("\n=== Resumen de análisis (incluye p=1, sin T_serial) ===")
    print("n_ciudades |   p |   T_p(s) |  Speedup | Eficiencia")
    print("-" * 60)
    for a in analisis:
        print(f"{a['n_ciudades']:10} | {a['p']:3} | {a['T_p']:8.3f} | {a['Speedup']:8.3f} | {a['Eficiencia']:8.3f}")

if __name__ == "__main__":
    main()