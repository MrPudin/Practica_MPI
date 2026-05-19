#!/bin/bash
# ---------------------------------------------------------
# Script para generar múltiples scripts SGE para el estudio
# de speedup y escalabilidad del TSP paralelo con MPI.
# Se ejecuta desde el directorio SGE/
# ---------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_DIR="$(dirname "$SCRIPT_DIR")"
cd "$SCRIPT_DIR"

# --- Parámetros del estudio ---
PROBLEMAS=(4 5 10 20 40 50)
NUCLEOS=(1 2 4 8 16)

EXEC_SECUENCIAL="$PROJ_DIR/tsp_sec"
EXEC_PARALELO="$PROJ_DIR/tsp_mpi"
RES_DIR="$PROJ_DIR/Resultados"
EJEMPLOS_DIR="$PROJ_DIR/EjemplosCiudades"

mkdir -p "$RES_DIR"

echo "=== Generando scripts SGE ==="
echo "Proyecto   : $PROJ_DIR"
echo "Problemas  : ${PROBLEMAS[*]}"
echo "Núcleos    : ${NUCLEOS[*]}"
echo "Resultados : $RES_DIR"
echo ""

generados=0
saltados=0

for n_ciudades in "${PROBLEMAS[@]}"; do

    archivo_matriz="$EJEMPLOS_DIR/tsp${n_ciudades}.1"

    if [ ! -f "$archivo_matriz" ]; then
        echo "ADVERTENCIA: No existe $archivo_matriz, saltando."
        ((saltados++))
        continue
    fi

    for p in "${NUCLEOS[@]}"; do

        if [ "$p" -eq 1 ]; then
            # ── Versión secuencial ────────────────────────────────
            nombre_job="tspsec_${n_ciudades}"
            script_name="sge_${nombre_job}.sh"
            salida_out="$RES_DIR/tspsec_${n_ciudades}.out"
            salida_txt="$RES_DIR/tspsec_${n_ciudades}.txt"

            cat > "$script_name" << SGEOF
#!/bin/bash
#$ -N $nombre_job
#$ -cwd
#$ -j y
#$ -o $salida_out

echo "=== Ejecución secuencial TSP ==="
echo "Ciudades : $n_ciudades"
echo "Nodo     : \$(hostname)"
echo "Fecha    : \$(date)"
echo "----------------------------------------"

cd $PROJ_DIR
time $EXEC_SECUENCIAL $n_ciudades $archivo_matriz > $salida_txt 2>&1

echo "----------------------------------------"
echo "Ejecución finalizada: \$(date)"
SGEOF

        else
            # ── Versión paralela MPI ──────────────────────────────
            nombre_job="tspmpi_${n_ciudades}_${p}"
            script_name="sge_${nombre_job}.sh"
            salida_out="$RES_DIR/tspmpi_${n_ciudades}_${p}.out"
            salida_txt="$RES_DIR/tspmpi_${n_ciudades}_${p}.txt"

            cat > "$script_name" << SGEOF
#!/bin/bash
#$ -N $nombre_job
#$ -cwd
#$ -j y
#$ -o $salida_out
#$ -pe mpi $p

echo "=== Iniciando trabajo TSP con MPI ==="
echo "Ciudades        : $n_ciudades"
echo "Slots pedidos   : \$NSLOTS"
echo "Nodo maestro    : \$(hostname)"
echo "Fecha inicio    : \$(date)"
echo "Nodos asignados :"
cat \$PE_HOSTFILE
echo "----------------------------------------"

cd $PROJ_DIR

# Generar fichero de máquinas compatible con Hydra (MPICH):
# Hydra NO soporta "nodo slots=N"; necesita un hostname por linea.
MPICH_MACHINES=\$TMPDIR/mpich_machines
awk '{for(i=0;i<\$2;i++) print \$1}' \$PE_HOSTFILE > \$MPICH_MACHINES

echo "Fichero de maquinas:"
cat \$MPICH_MACHINES
echo "----------------------------------------"

time mpiexec -machinefile \$MPICH_MACHINES -n \$NSLOTS \
    $EXEC_PARALELO $n_ciudades $archivo_matriz > $salida_txt 2>&1

echo "----------------------------------------"
echo "Trabajo finalizado: \$(date)"
SGEOF

        fi

        chmod +x "$script_name"
        echo "Generado: $script_name"
        ((generados++))

    done
done

echo ""
echo "=== Resumen ==="
echo "Scripts generados : $generados"
echo "Problemas saltados: $saltados"