#!/bin/bash
# ---------------------------------------------------------
# Script para generar múltiples scripts SGE para el estudio
# Se ejecuta desde el directorio SGE/
# ---------------------------------------------------------

# Directorio de trabajo (donde esté este script)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"

cd "$SCRIPT_DIR"

# Tamaños de problema (ciudades)
PROBLEMAS=(4 5 10 20 40 50)

# Núcleos a probar
NUCLEOS=(1 2 4 8 16)

# Ejecutable secuencial (en la raíz del proyecto)
EXEC_SECUENCIAL="$PROJ_DIR/tspsec"

# Ejecutable paralelo (en la raíz del proyecto)
EXEC_PARALELO="$PROJ_DIR/tsp_mpi"

# Directorio de resultados en la raíz del proyecto
RES_DIR="$PROJ_DIR/Resultados"
mkdir -p "$RES_DIR"

echo "=== Generando scripts SGE para el estudio ==="
echo "Proyecto: $PROJ_DIR"
echo "Problemas: ${PROBLEMAS[@]}"
echo "Núcleos: ${NUCLEOS[@]}"
echo "Resultados en: $RES_DIR"

for n_ciudades in "${PROBLEMAS[@]}"; do
    archivo_matriz="$PROJ_DIR/EjemplosCiudades/tsp${n_ciudades}.1"

    if [ ! -f "$archivo_matriz" ]; then
        echo "ADVERTENCIA: No existe $archivo_matriz, saltando."
        continue
    fi

    for p in "${NUCLEOS[@]}"; do
        if [ "$p" -eq 1 ]; then
            # --- Versión secuencial ---
            nombre_job="tspsec_${n_ciudades}"
            script_name="sge_${nombre_job}.sh"
            output_name="$RES_DIR/tspsec_${n_ciudades}.out"
            error_name="$RES_DIR/tspsec_${n_ciudades}.err"
            salida_txt="$RES_DIR/tspsec_${n_ciudades}.txt"

            cat > "$script_name" <<EOF
#!/bin/bash
#$ -N $nombre_job
#$ -cwd
#$ -j y
#$ -o $output_name
#$ -e $error_name

cd "$PROJ_DIR"

echo "=== Ejecución secuencial TSP ==="
echo "Ciudades: ${n_ciudades}"
echo "Ejecutable: $EXEC_SECUENCIAL"
echo "Archivo: $archivo_matriz"

time $EXEC_SECUENCIAL $n_ciudades $archivo_matriz > $salida_txt

echo "=== Ejecución secuencial finalizada ==="
EOF

        else
            # --- Versión paralela MPI ---
            nombre_job="tspmpi_${n_ciudades}_${p}"
            script_name="sge_${nombre_job}.sh"
            output_name="$RES_DIR/tspmpi_${n_ciudades}_${p}.out"
            error_name="$RES_DIR/tspmpi_${n_ciudades}_${p}.err"
            salida_txt="$RES_DIR/tspmpi_${n_ciudades}_${p}.txt"

            cat > "$script_name" <<EOF
#!/bin/bash
#$ -N $nombre_job
#$ -cwd
#$ -j y
#$ -o $output_name
#$ -e $error_name
#$ -pe mpi $p

cd "$PROJ_DIR"

# Preparar archivo de máquinas para MPICH (como pide tu profesor)
MPICH_MACHINES=\$TMPDIR/mpich_machines
cat \$PE_HOSTFILE | awk '{print \$1":\"\$2}' > \$MPICH_MACHINES

echo "=== Iniciando trabajo TSP MPI ==="
echo "Ciudades: ${n_ciudades}"
echo "Slots solicitados: \$NSLOTS"
echo "Nodos asignados por SGE:"
cat \$PE_HOSTFILE
echo "----------------------------------------"

time mpiexec -f \$MPICH_MACHINES -n \$NSLOTS $EXEC_PARALELO $n_ciudades $archivo_matriz > $salida_txt

echo "----------------------------------------"
echo "Trabajo ejecutado con \$NSLOTS procesos."
EOF
        fi

        chmod +x "$script_name"
        echo "Generado: $script_name"
    done
done

echo "=== Todos los scripts SGE generados ==="