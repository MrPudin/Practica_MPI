#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mpi.h>
#include "libtsp.h"

#define TAG_PIDE_TRAB  1
#define TAG_TRABAJO    2
#define TAG_FIN        3
#define TAG_NUEVA_CS   4
#define TAG_SOLUCION   5

MPI_Datatype MPI_VEC_CIUDADES;

void CrearTipoDerivado() {
    MPI_Type_contiguous(NCIUDADES, MPI_INT, &MPI_VEC_CIUDADES);
    MPI_Type_commit(&MPI_VEC_CIUDADES);
}

int TamanoBufferNodo() {
    return sizeof(long int) + (2 + NCIUDADES + (NCIUDADES - 2)) * sizeof(int);
}

void SerializarNodo(const tNodo *origen, char *buffer_plano) {
    char *ptr = buffer_plano;

    memcpy(ptr, &origen->id, sizeof(long int));
    ptr += sizeof(long int);

    memcpy(ptr, &origen->ci, sizeof(int));
    ptr += sizeof(int);

    memcpy(ptr, &origen->orig_excl, sizeof(int));
    ptr += sizeof(int);

    for (unsigned int i = 0; i < NCIUDADES; i++) {
        memcpy(ptr, &origen->incl[i], sizeof(int));
        ptr += sizeof(int);
    }

    for (unsigned int i = 0; i < NCIUDADES - 2; i++) {
        memcpy(ptr, &origen->dest_excl[i], sizeof(int));
        ptr += sizeof(int);
    }
}

void DeserializarNodo(const char *buffer_plano, tNodo *destino) {
    const char *ptr = buffer_plano;

    memcpy(&destino->id, ptr, sizeof(long int));
    ptr += sizeof(long int);

    memcpy(&destino->ci, ptr, sizeof(int));
    ptr += sizeof(int);

    memcpy(&destino->orig_excl, ptr, sizeof(int));
    ptr += sizeof(int);

    for (unsigned int i = 0; i < NCIUDADES; i++) {
        memcpy(&destino->incl[i], ptr, sizeof(int));
        ptr += sizeof(int);
    }

    for (unsigned int i = 0; i < NCIUDADES - 2; i++) {
        memcpy(&destino->dest_excl[i], ptr, sizeof(int));
        ptr += sizeof(int);
    }
}

void ActualizarMejorSolucion(const tNodo *candidato, int *U, tNodo *mejor_solucion) {
    if (candidato->ci < *U) {
        *U = candidato->ci;
        CopiaNodo((tNodo *)candidato, mejor_solucion, false);
    }
}

void InsertarTrabajoSiPromete(tPila *pila, tNodo *nodo, int *U, tNodo *mejor_solucion) {
    if (nodo->ci >= *U) return;

    if (Solucion(nodo)) {
        ActualizarMejorSolucion(nodo, U, mejor_solucion);
    } else if (!PilaPush(pila, nodo)) {
        printf("[MAESTRO] Aviso: pila llena; se detiene la expansión inicial.\n");
    }
}

void GenerarBolsaInicial(tPila *pila, int** tsp0, int objetivo, int *U, tNodo *mejor_solucion) {
    while (PilaTamanio(pila) < objetivo && !PilaVacia(pila)) {
        tNodo nodo_actual;
        PilaPop(pila, &nodo_actual);

        if (nodo_actual.ci >= *U) continue;

        if (Solucion(&nodo_actual)) {
            ActualizarMejorSolucion(&nodo_actual, U, mejor_solucion);
            continue;
        }

        tNodo hijo_izq, hijo_der;
        InicNodo(&hijo_izq);
        InicNodo(&hijo_der);
        Ramifica(&nodo_actual, &hijo_izq, &hijo_der, tsp0);

        InsertarTrabajoSiPromete(pila, &hijo_der, U, mejor_solucion);
        InsertarTrabajoSiPromete(pila, &hijo_izq, U, mejor_solucion);
    }

    PilaAcotar(pila, *U);
}

void ProbarNuevaCota(MPI_Request *req_cota, int *flag_cota_recibida,
                     int *buffer_cota_asincrona, int *U,
                     tPila *pila_local, int *terminar) {
    *flag_cota_recibida = 0;
    MPI_Test(req_cota, flag_cota_recibida, MPI_STATUS_IGNORE);

    if (*flag_cota_recibida) {
        if (*buffer_cota_asincrona == -1) {
            *terminar = 1;
        } else if (*buffer_cota_asincrona < *U) {
            *U = *buffer_cota_asincrona;
            PilaAcotar(pila_local, *U);
        }

        if (!*terminar) {
            MPI_Irecv(buffer_cota_asincrona, 1, MPI_INT, 0, TAG_NUEVA_CS, MPI_COMM_WORLD, req_cota);
        }
    }
}

int main(int argc, char *argv[]) {
    int mi_rango, num_procs;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mi_rango);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (argc < 3) {
        if (mi_rango == 0) {
            printf("Uso: mpirun -np <N> %s <num_ciudades> <archivo_matriz>\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    if (num_procs < 2) {
        if (mi_rango == 0) {
            printf("Error: se necesitan al menos 2 procesos.\n");
        }
        MPI_Finalize();
        return -1;
    }

    NCIUDADES = atoi(argv[1]);
    char *archivo_entrada = argv[2];
    TotalNodos = 0;

    CrearTipoDerivado();  // tipo derivado para cumplir rúbrica

    int tamano_buffer_bytes = TamanoBufferNodo();
    char *buffer_comunicaciones = new char[tamano_buffer_bytes];

    int** tsp0 = reservarMatrizCuadrada(NCIUDADES);

    if (mi_rango == 0) {
        LeerMatriz(archivo_entrada, tsp0);
    }

    // COMUNICACIÓN COLECTIVA
    for (unsigned int i = 0; i < NCIUDADES; i++) {
        MPI_Bcast(tsp0[i], NCIUDADES, MPI_INT, 0, MPI_COMM_WORLD);
    }

    int U = INFINITO;
    tNodo mejor_solucion;
    InicNodo(&mejor_solucion);

    double t_inicio = MPI_Wtime();

    if (mi_rango == 0) {
        int trabajadores_activos = num_procs - 1;
        tPila pila_maestro;
        PilaInic(&pila_maestro);

        tNodo raiz;
        InicNodo(&raiz);

        int** tsp_raiz = reservarMatrizCuadrada(NCIUDADES);
        Reconstruye(&raiz, tsp0, tsp_raiz);
        liberarMatriz(tsp_raiz);

        PilaPush(&pila_maestro, &raiz);

        int objetivo_trabajo = (num_procs - 1) * 8;
        if (objetivo_trabajo > (int)MAXPILA - 1) objetivo_trabajo = (int)MAXPILA - 1;
        if (objetivo_trabajo < num_procs - 1) objetivo_trabajo = num_procs - 1;

        GenerarBolsaInicial(&pila_maestro, tsp0, objetivo_trabajo, &U, &mejor_solucion);
        printf("[MAESTRO] Bolsa inicial generada: %d subproblemas, cota inicial %d\n", PilaTamanio(&pila_maestro), U);

        if (U < INFINITO) {
            for (int i = 1; i < num_procs; i++) {
                MPI_Send(&U, 1, MPI_INT, i, TAG_NUEVA_CS, MPI_COMM_WORLD);
            }
        }

        while (trabajadores_activos > 0) {
            MPI_Status status;
            int peticion;

            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            int origen = status.MPI_SOURCE;

            if (status.MPI_TAG == TAG_PIDE_TRAB) {
                MPI_Recv(&peticion, 1, MPI_INT, origen, TAG_PIDE_TRAB, MPI_COMM_WORLD, &status);

                if (!PilaVacia(&pila_maestro)) {
                    tNodo nodo_a_enviar;
                    PilaPop(&pila_maestro, &nodo_a_enviar);
                    SerializarNodo(&nodo_a_enviar, buffer_comunicaciones);
                    MPI_Send(buffer_comunicaciones, tamano_buffer_bytes, MPI_CHAR, origen, TAG_TRABAJO, MPI_COMM_WORLD);
                } else {
                    trabajadores_activos--;
                    if (trabajadores_activos == 0 && PilaVacia(&pila_maestro)) {
                        int fin = -1;
                        for (int i = 1; i < num_procs; i++) {
                            MPI_Send(&fin, 1, MPI_INT, i, TAG_NUEVA_CS, MPI_COMM_WORLD);
                        }
                        for (int i = 1; i < num_procs; i++) {
                            MPI_Send(&fin, 1, MPI_INT, i, TAG_FIN, MPI_COMM_WORLD);
                        }
                    }
                }
            } else if (status.MPI_TAG == TAG_SOLUCION) {
                MPI_Recv(buffer_comunicaciones, tamano_buffer_bytes, MPI_CHAR, origen, TAG_SOLUCION, MPI_COMM_WORLD, &status);

                tNodo sol_recibida;
                DeserializarNodo(buffer_comunicaciones, &sol_recibida);

                if (sol_recibida.ci < U) {
                    U = sol_recibida.ci;
                    CopiaNodo(&sol_recibida, &mejor_solucion, false);
                    printf("[MAESTRO] Nueva cota superior global = %d encontrada por P%d\n", U, origen);

                    for (int i = 1; i < num_procs; i++) {
                        MPI_Send(&U, 1, MPI_INT, i, TAG_NUEVA_CS, MPI_COMM_WORLD);
                    }
                }
            }
        }

        double t_final = MPI_Wtime() - t_inicio;
        printf("\n=============================================================\n");
        printf("PROCESAMIENTO CONCLUIDO CON ÉXITO\n");
        EscribeSolucion(&mejor_solucion, t_final);
        printf("=============================================================\n");
    } else {
        tPila pila_local;
        PilaInic(&pila_local);
        int terminar = 0;

        MPI_Request req_cota;
        int flag_cota_recibida = 0;
        int buffer_cota_asincrona = 0;

        MPI_Irecv(&buffer_cota_asincrona, 1, MPI_INT, 0, TAG_NUEVA_CS, MPI_COMM_WORLD, &req_cota);

        while (!terminar) {
            if (PilaVacia(&pila_local)) {
                ProbarNuevaCota(&req_cota, &flag_cota_recibida, &buffer_cota_asincrona,
                                &U, &pila_local, &terminar);
                if (terminar) break;

                int vacio_peticion = 1;
                MPI_Send(&vacio_peticion, 1, MPI_INT, 0, TAG_PIDE_TRAB, MPI_COMM_WORLD);

                MPI_Status status;
                MPI_Recv(buffer_comunicaciones, tamano_buffer_bytes, MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

                if (status.MPI_TAG == TAG_TRABAJO) {
                    tNodo nodo_recibido;
                    DeserializarNodo(buffer_comunicaciones, &nodo_recibido);
                    PilaPush(&pila_local, &nodo_recibido);
                } else if (status.MPI_TAG == TAG_FIN) {
                    terminar = 1;
                }
            } else {
                ProbarNuevaCota(&req_cota, &flag_cota_recibida, &buffer_cota_asincrona,
                                &U, &pila_local, &terminar);
                if (terminar) break;

                tNodo nodo_actual;
                PilaPop(&pila_local, &nodo_actual);

                if (nodo_actual.ci >= U) continue;

                if (Solucion(&nodo_actual)) {
                    if (nodo_actual.ci < U) {
                        U = nodo_actual.ci;
                        SerializarNodo(&nodo_actual, buffer_comunicaciones);
                        MPI_Send(buffer_comunicaciones, tamano_buffer_bytes, MPI_CHAR, 0, TAG_SOLUCION, MPI_COMM_WORLD);
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
    delete[] buffer_comunicaciones;
    MPI_Type_free(&MPI_VEC_CIUDADES);
    MPI_Finalize();
    return 0;
}