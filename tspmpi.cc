#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mpi.h>
#include "libtsp.h"

#define TAG_PIDE_TRAB  1   // Worker    -> Maestro: pide trabajo
#define TAG_TRABAJO    2   // Maestro   -> Worker: envía nodo (subproblema)
#define TAG_FIN        3   // Maestro   -> Worker: finalización
#define TAG_NUEVA_CS   4   // Maestro   -> Workers: nueva cota global (U)
#define TAG_SOLUCION   5   // Worker    -> Maestro: nueva solución encontrada

#define M_MEJORES 3

MPI_Datatype MPI_NODO_T;

void CrearTipoDerivado() {
    int tamano = sizeof(long int) + (2 + NCIUDADES + (NCIUDADES - 2)) * sizeof(int);
    MPI_Type_contiguous(tamano, MPI_BYTE, &MPI_NODO_T);
    MPI_Type_commit(&MPI_NODO_T);
}

int TamanoBufferNodo() {
    return sizeof(long int) + (2 + NCIUDADES + (NCIUDADES - 2)) * sizeof(int);
}

void SerializarNodo(const tNodo *origen, char *buf) {
    char *ptr = buf;
    memcpy(ptr, &origen->id,        sizeof(long int)); ptr += sizeof(long int);
    memcpy(ptr, &origen->ci,        sizeof(int));       ptr += sizeof(int);
    memcpy(ptr, &origen->orig_excl, sizeof(int));       ptr += sizeof(int);
    for (unsigned int i = 0; i < NCIUDADES;   i++) { memcpy(ptr, &origen->incl[i],      sizeof(int)); ptr += sizeof(int); }
    for (unsigned int i = 0; i < NCIUDADES-2; i++) { memcpy(ptr, &origen->dest_excl[i], sizeof(int)); ptr += sizeof(int); }
}

void DeserializarNodo(const char *buf, tNodo *destino) {
    const char *ptr = buf;
    memcpy(&destino->id,        ptr, sizeof(long int)); ptr += sizeof(long int);
    memcpy(&destino->ci,        ptr, sizeof(int));       ptr += sizeof(int);
    memcpy(&destino->orig_excl, ptr, sizeof(int));       ptr += sizeof(int);
    for (unsigned int i = 0; i < NCIUDADES;   i++) { memcpy(&destino->incl[i],      ptr, sizeof(int)); ptr += sizeof(int); }
    for (unsigned int i = 0; i < NCIUDADES-2; i++) { memcpy(&destino->dest_excl[i], ptr, sizeof(int)); ptr += sizeof(int); }
}

/* Array de las M mejores soluciones, ordenado de menor a mayor coste.
   costes[0] es la cota de poda; costes[count-1] es el umbral de admisión. */
struct tMejores {
    tNodo nodos[M_MEJORES];
    int   costes[M_MEJORES];
    int   count;
};

static void inicMejores(tMejores *m) {
    m->count = 0;
    for (int i = 0; i < M_MEJORES; i++) {
        InicNodo(&m->nodos[i]);
        m->costes[i] = INFINITO;
    }
}

static int cotaPoda(const tMejores *m) {
    return (m->count > 0) ? m->costes[0] : INFINITO;
}

/* Inserta un candidato en el ranking. Devuelve true si cambió la mejor solución. */
static bool insertarMejor(tMejores *m, tNodo *nodo) {
    int ci = nodo->ci;

    if (m->count == M_MEJORES && ci >= m->costes[m->count - 1])
        return false;

    int pos = (m->count < M_MEJORES) ? m->count : M_MEJORES - 1;
    for (int i = 0; i < m->count && i < M_MEJORES; i++) {
        if (ci < m->costes[i]) { pos = i; break; }
    }

    int hasta = (m->count < M_MEJORES) ? m->count : M_MEJORES - 1;
    for (int i = hasta; i > pos; i--) {
        CopiaNodo(&m->nodos[i-1], &m->nodos[i], false);
        m->costes[i] = m->costes[i-1];
    }
    CopiaNodo(nodo, &m->nodos[pos], false);
    m->costes[pos] = ci;
    if (m->count < M_MEJORES) m->count++;

    return (pos == 0);
}

static void imprimirMejores(const tMejores *m) {
    printf("\n=== %d mejores soluciones encontradas ===\n", m->count);
    for (int i = 0; i < m->count; i++) {
        printf("  #%d Coste=%d : ", i+1, m->costes[i]);
        EscribeNodo(const_cast<tNodo*>(&m->nodos[i]));
        printf("\n");
    }
}

void GenerarBolsaInicial(tPila *pila, int **tsp0, int objetivo,
                          int *U, tMejores *mejores) {
    while (PilaTamanio(pila) < objetivo && !PilaVacia(pila)) {
        tNodo nodo_actual;
        PilaPop(pila, &nodo_actual);

        if (nodo_actual.ci >= *U) continue;

        if (Solucion(&nodo_actual)) {
            if (insertarMejor(mejores, &nodo_actual)) {
                *U = cotaPoda(mejores);
                PilaAcotar(pila, *U);
            }
            continue;
        }

        tNodo hijo_izq, hijo_der;
        InicNodo(&hijo_izq);
        InicNodo(&hijo_der);
        Ramifica(&nodo_actual, &hijo_izq, &hijo_der, tsp0);

        if (hijo_der.ci < *U && !PilaPush(pila, &hijo_der))
            printf("[MAESTRO] Aviso: pila llena.\n");
        if (hijo_izq.ci < *U && !PilaPush(pila, &hijo_izq))
            printf("[MAESTRO] Aviso: pila llena.\n");
    }
    PilaAcotar(pila, *U);
}

/* Comprueba de forma no bloqueante si llegó una nueva cota del maestro.
   El valor -1 en buffer_cota indica señal de fin. */
void ProbarNuevaCota(MPI_Request *req_cota, int *buffer_cota,
                     int *U, tPila *pila_local, int *terminar) {
    int flag = 0;
    MPI_Test(req_cota, &flag, MPI_STATUS_IGNORE);
    if (flag) {
        if (*buffer_cota == -1) {
            *terminar = 1;
        } else if (*buffer_cota < *U) {
            *U = *buffer_cota;
            PilaAcotar(pila_local, *U);
        }
        if (!*terminar) {
            MPI_Irecv(buffer_cota, 1, MPI_INT, 0, TAG_NUEVA_CS,
                      MPI_COMM_WORLD, req_cota);
        }
    }
}

int main(int argc, char *argv[]) {
    int mi_rango, num_procs;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mi_rango);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (argc < 3) {
        if (mi_rango == 0)
            printf("Uso: mpirun -np <N> %s <num_ciudades> <archivo>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }
    if (num_procs < 2) {
        if (mi_rango == 0)
            printf("Error: se necesitan al menos 2 procesos.\n");
        MPI_Finalize();
        return -1;
    }

    NCIUDADES = atoi(argv[1]);
    TotalNodos = 0;

    CrearTipoDerivado();

    int    tamBuf = TamanoBufferNodo();
    char  *bufCom = new char[tamBuf];

    int **tsp0 = reservarMatrizCuadrada(NCIUDADES);

    if (mi_rango == 0) LeerMatriz(argv[2], tsp0);
    MPI_Bcast(tsp0[0], (int)(NCIUDADES * NCIUDADES), MPI_INT, 0, MPI_COMM_WORLD);

    int    U = INFINITO;
    double t_inicio = MPI_Wtime();

    /* ── MAESTRO ─────────────────────────────────────────────────────── */
    if (mi_rango == 0) {

        tMejores mejores;
        inicMejores(&mejores);

        tPila pila_maestro;
        PilaInic(&pila_maestro);

        tNodo raiz;
        InicNodo(&raiz);
        int **tsp_raiz = reservarMatrizCuadrada(NCIUDADES);
        Reconstruye(&raiz, tsp0, tsp_raiz);
        liberarMatriz(tsp_raiz);
        PilaPush(&pila_maestro, &raiz);

        int objetivo = (num_procs - 1) * 8;
        if (objetivo > (int)MAXPILA - 1) objetivo = (int)MAXPILA - 1;
        if (objetivo < num_procs - 1)    objetivo = num_procs - 1;
        GenerarBolsaInicial(&pila_maestro, tsp0, objetivo, &U, &mejores);
        printf("[MAESTRO] Bolsa inicial: %d subproblemas, cota=%d\n",
               PilaTamanio(&pila_maestro), U);

        if (U < INFINITO) {
            for (int i = 1; i < num_procs; i++)
                MPI_Send(&U, 1, MPI_INT, i, TAG_NUEVA_CS, MPI_COMM_WORLD);
        }

        /* ocioso[p]=true: el trabajador p está esperando trabajo */
        bool *ocioso = new bool[num_procs];
        bool *vivo   = new bool[num_procs];
        for (int i = 0; i < num_procs; i++) {
            ocioso[i] = false;
            vivo[i]   = (i != 0);
        }
        int finEnviados = 0;

        /* Caso especial: la siembra agotó el árbol antes de que los
           trabajadores siquiera pidan trabajo. */
        if (PilaVacia(&pila_maestro)) {
            for (int p = 1; p < num_procs; p++) {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, MPI_ANY_SOURCE, TAG_PIDE_TRAB,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
            int fin = -1;
            for (int p = 1; p < num_procs; p++) {
                MPI_Send(&fin, 1, MPI_INT, p, TAG_NUEVA_CS, MPI_COMM_WORLD);
                MPI_Send(&fin, 1, MPI_INT, p, TAG_FIN,      MPI_COMM_WORLD);
            }
            finEnviados = num_procs - 1;
        }

        while (finEnviados < num_procs - 1) {
            MPI_Status status;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            int origen = status.MPI_SOURCE;

            if (status.MPI_TAG == TAG_PIDE_TRAB) {
                int dummy;
                MPI_Recv(&dummy, 1, MPI_INT, origen, TAG_PIDE_TRAB,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                if (!PilaVacia(&pila_maestro)) {
                    tNodo nodo_a_enviar;
                    PilaPop(&pila_maestro, &nodo_a_enviar);
                    SerializarNodo(&nodo_a_enviar, bufCom);
                    MPI_Send(bufCom, 1, MPI_NODO_T, origen,
                             TAG_TRABAJO, MPI_COMM_WORLD);
                    ocioso[origen] = false;
                } else {
                    ocioso[origen] = true;

                    bool todosOciosos = true;
                    for (int p = 1; p < num_procs; p++)
                        if (vivo[p] && !ocioso[p]) { todosOciosos = false; break; }

                    if (todosOciosos) {
                        int fin = -1;
                        for (int p = 1; p < num_procs; p++) {
                            if (vivo[p]) {
                                MPI_Send(&fin, 1, MPI_INT, p, TAG_NUEVA_CS,
                                         MPI_COMM_WORLD);
                                MPI_Send(&fin, 1, MPI_INT, p, TAG_FIN,
                                         MPI_COMM_WORLD);
                                vivo[p] = false;
                                finEnviados++;
                            }
                        }
                    }
                }

            } else if (status.MPI_TAG == TAG_SOLUCION) {
                MPI_Recv(bufCom, 1, MPI_NODO_T, origen, TAG_SOLUCION,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                tNodo sol_recibida;
                DeserializarNodo(bufCom, &sol_recibida);

                if (insertarMejor(&mejores, &sol_recibida)) {
                    U = cotaPoda(&mejores);
                    printf("[MAESTRO] Nueva CS=%d (proc %d)\n", U, origen);
                    PilaAcotar(&pila_maestro, U);

                    for (int p = 1; p < num_procs; p++) {
                        if (vivo[p] && p != origen)
                            MPI_Send(&U, 1, MPI_INT, p, TAG_NUEVA_CS,
                                     MPI_COMM_WORLD);
                    }

                    /* Reactivar trabajadores ociosos con nodos disponibles */
                    for (int p = 1; p < num_procs; p++) {
                        if (vivo[p] && ocioso[p] && !PilaVacia(&pila_maestro)) {
                            tNodo tmp;
                            PilaPop(&pila_maestro, &tmp);
                            SerializarNodo(&tmp, bufCom);
                            MPI_Send(bufCom, 1, MPI_NODO_T, p,
                                     TAG_TRABAJO, MPI_COMM_WORLD);
                            ocioso[p] = false;
                        }
                    }
                }
            }
        }

        double t_final = MPI_Wtime() - t_inicio;
        imprimirMejores(&mejores);
        printf("\n=============================================================\n");
        printf("PROCESAMIENTO CONCLUIDO. Tiempo: %.6f s\n", t_final);
        printf("=============================================================\n");
        EscribeSolucion(&mejores.nodos[0], t_final);

        delete[] ocioso;
        delete[] vivo;

    /* ── TRABAJADOR ──────────────────────────────────────────────────── */
    } else {
        tPila pila_local;
        PilaInic(&pila_local);
        int terminar = 0;

        /* Receptor asíncrono para actualizaciones de cota del maestro */
        MPI_Request req_cota;
        int buffer_cota = INFINITO;
        MPI_Irecv(&buffer_cota, 1, MPI_INT, 0, TAG_NUEVA_CS,
                  MPI_COMM_WORLD, &req_cota);

        while (!terminar) {

            if (PilaVacia(&pila_local)) {
                ProbarNuevaCota(&req_cota, &buffer_cota, &U,
                                &pila_local, &terminar);
                if (terminar) break;

                int peticion = 1;
                MPI_Send(&peticion, 1, MPI_INT, 0, TAG_PIDE_TRAB,
                         MPI_COMM_WORLD);

                /* Cancelar el Irecv antes de esperar la respuesta del maestro
                   para evitar que un TAG_NUEVA_CS se confunda con TAG_TRABAJO. */
                int completado = 0;
                MPI_Test(&req_cota, &completado, MPI_STATUS_IGNORE);
                if (!completado) {
                    MPI_Cancel(&req_cota);
                    MPI_Wait(&req_cota, MPI_STATUS_IGNORE);
                }

                MPI_Status status;
                MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

                /* Drenar TAG_NUEVA_CS que pudieran haber llegado antes
                   que la respuesta al TAG_PIDE_TRAB. */
                while (status.MPI_TAG == TAG_NUEVA_CS) {
                    int nuevaU;
                    MPI_Recv(&nuevaU, 1, MPI_INT, 0, TAG_NUEVA_CS,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    if (nuevaU == -1) { terminar = 1; break; }
                    if (nuevaU < U)   { U = nuevaU; PilaAcotar(&pila_local, U); }
                    MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
                }
                if (terminar) break;

                if (status.MPI_TAG == TAG_FIN) {
                    MPI_Recv(&terminar, 1, MPI_INT, 0, TAG_FIN,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    terminar = 1;
                    break;
                }

                MPI_Recv(bufCom, 1, MPI_NODO_T, 0, TAG_TRABAJO,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                tNodo nodo_recibido;
                DeserializarNodo(bufCom, &nodo_recibido);
                PilaPush(&pila_local, &nodo_recibido);

                buffer_cota = INFINITO;
                MPI_Irecv(&buffer_cota, 1, MPI_INT, 0, TAG_NUEVA_CS,
                          MPI_COMM_WORLD, &req_cota);

            } else {
                // B&B 
                ProbarNuevaCota(&req_cota, &buffer_cota, &U,
                                &pila_local, &terminar);
                if (terminar) break;

                tNodo nodo_actual;
                PilaPop(&pila_local, &nodo_actual);

                if (nodo_actual.ci >= U) continue;

                if (Solucion(&nodo_actual)) {
                    if (nodo_actual.ci < U) {
                        U = nodo_actual.ci;
                        PilaAcotar(&pila_local, U);
                        SerializarNodo(&nodo_actual, bufCom);
                        MPI_Send(bufCom, 1, MPI_NODO_T, 0, TAG_SOLUCION,
                                 MPI_COMM_WORLD);
                    }
                } else {
                    tNodo hijo_izq, hijo_der;
                    InicNodo(&hijo_izq);
                    InicNodo(&hijo_der);
                    Ramifica(&nodo_actual, &hijo_izq, &hijo_der, tsp0);

                    if (hijo_der.ci < U) PilaPush(&pila_local, &hijo_der);
                    if (hijo_izq.ci < U) PilaPush(&pila_local, &hijo_izq);
                }
            }
        }

        int completado = 0;
        MPI_Test(&req_cota, &completado, MPI_STATUS_IGNORE);
        if (!completado) {
            MPI_Cancel(&req_cota);
            MPI_Request_free(&req_cota);
        }
    }

    liberarMatriz(tsp0);
    delete[] bufCom;
    MPI_Type_free(&MPI_NODO_T);
    MPI_Finalize();
    return 0;
}