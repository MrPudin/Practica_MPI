#!/bin/bash
# ---------------------------------------------------------
# Script para enviar todos los scripts SGE generados
# Se ejecuta desde el directorio SGE/
# ---------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Enviando todos los trabajos SGE ==="
echo "Directorio: $SCRIPT_DIR"
echo "Fecha: $(date)"
echo ""

count=0
errores=0

for script in sge_*.sh; do
    if [ -f "$script" ]; then
        echo -n "Enviando: $script ... "
        # Capturar salida de qsub para obtener el job ID
        resultado=$(qsub "$script" 2>&1)
        if [ $? -eq 0 ]; then
            job_id=$(echo "$resultado" | grep -oP '(?<=Your job )\d+')
            echo "OK (job $job_id)"
            ((count++))
        else
            echo "ERROR: $resultado"
            ((errores++))
        fi
        # Pequeña pausa para no saturar el scheduler
        sleep 0.5
    fi
done

echo ""
echo "=== Resumen ==="
echo "Enviados correctamente: $count"
echo "Errores: $errores"
echo "Total procesados: $((count + errores))"