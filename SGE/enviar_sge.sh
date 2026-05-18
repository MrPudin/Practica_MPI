#!/bin/bash
# ---------------------------------------------------------
# Script para enviar todos los scripts SGE generados
# Se ejecuta desde el directorio SGE/
# ---------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Enviando todos los trabajos SGE ==="

count=0
for script in sge_*.sh; do
    if [ -f "$script" ]; then
        echo "Enviando: $script"
        qsub "$script"
        ((count++))
    fi
done

echo "Se han enviado $count trabajos."