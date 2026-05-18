#!/bin/bash
#$ -N tsp_mpi
#$ -cwd
#$ -j y
#$ -o tsp_mpi.out
#$ -pe mpi 4

# Opcional: Generar el archivo de máquinas por si tu versión de MPI lo exige de forma estricta.
# Usamos un formato más compatible (nodo slots) o simplemente dejamos que MPI use el PE_HOSTFILE.
MPICH_MACHINES=$TMPDIR/mpich_machines
cat $PE_HOSTFILE | awk '{print $1" slots="$2}' > $MPICH_MACHINES

echo "=== Iniciando trabajo TSP con MPI ==="
echo "Slots solicitados: $NSLOTS"
echo "Nodos asignados por SGE:"
cat $PE_HOSTFILE
echo "----------------------------------------"

# Ejecución del binario (MPICH moderno detecta $NSLOTS automáticamente, 
# pero le pasamos el archivo generado para asegurar compatibilidad).
time mpiexec -f $MPICH_MACHINES -n $NSLOTS ./tsp_mpi 4 ./EjemplosCiudades/tsp4.1 > tsp_mpi_resultado_${NSLOTS}.txt

echo "----------------------------------------"
echo "Trabajo ejecutado con éxito."