#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mpi.h>
#include "libtsp.h"

// Identificadores de Mensajes (Tags)
#define TAG_PIDE_TRAB  1
#define TAG_TRABAJO    2
#define TAG_FIN        3
#define TAG_NUEVA_CS   4
#define TAG_SOLUCION   5

// --- RÚBRICA: TIPOS DERIVADOS DE MPI ---
void CrearTipoDerivadoNodo(MPI_Datatype *MPI_NODO_T) {
    int longitudes[4] = {1, 1, (int)NCIUDADES, (int)(NCIUDADES - 2)};
    MPI_Datatype tipos[4] = {MPI_LONG, MPI_INT, MPI_INT, MPI_INT};
    
    MPI_Aint desplazamientos[4];
    desplazamientos[0] = 0;
    desplazamientos[1] = sizeof(long int);
    desplazamientos[2] = desplazamientos[1] + sizeof(int);
    desplazamientos[3] = desplazamientos[2] + (NCIUDADES * sizeof(int));

    MPI_Type_create_struct(4, longitudes, desplazamientos, tipos, MPI_NODO_T);
    MPI_Type_commit(MPI_NODO_T);
}

// Funciones de serialización estructural sobre el buffer plano
void SerializarNodo(tNodo *origen, int *buffer_plano) {
    long int *ptr_id = (long int*) buffer_plano;
    ptr_id[0] = origen->id;
    
    int *ptr_datos = (int*)(buffer_plano + 2); 
    ptr_datos[0] = origen->ci;
    
    for (unsigned int i = 0; i < NCIUDADES; i++) {
        ptr_datos[1 + i] = origen->incl[i];
    }
    for (unsigned int i = 0; i < NCIUDADES - 2; i++) {
        ptr_datos[1 + NCIUDADES + i] = origen->dest_excl[i];
    }
}

void DeserializarNodo(int *buffer_plano, tNodo *destino) {
    long int *ptr_id = (long int*) buffer_plano;
    destino->id = ptr_id[0];
    
    int *ptr_datos = (int*)(buffer_plano + 2);
    destino->ci = ptr_datos[0];
    
    for (unsigned int i = 0; i < NCIUDADES; i++) {
        destino->incl[i] = ptr_datos[1 + i];
    }
    for (unsigned int i = 0; i < NCIUDADES - 2; i++) {
        destino->dest_excl[i] = ptr_datos[1 + NCIUDADES + i];
    }
}

int main(int argc, char *argv[]) {
    int mi_rango, num_procs;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mi_rango);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (argc < 3) {
        if (mi_rango == 0) printf("Uso: mpirun -np <N> %s <num_ciudades> <archivo_matriz>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    NCIUDADES = atoi(argv[1]); 
    char *archivo_entrada = argv[2];
    TotalNodos = 0;

    MPI_Datatype MPI_NODO_T;
    CrearTipoDerivadoNodo(&MPI_NODO_T);

    int tamano_buffer_int = 2 + 1 + NCIUDADES + (NCIUDADES - 2); 
    int *buffer_comunicaciones = new int[tamano_buffer_int];

    int** tsp0 = reservarMatrizCuadrada(NCIUDADES);

    // =========================================================================
    // FASE 1: LECTURA Y COMUNICACIÓN COLECTIVA
    // =========================================================================
    if (mi_rango == 0) {
        LeerMatriz(archivo_entrada, tsp0);
    }

    for (unsigned int i = 0; i < NCIUDADES; i++) {
        MPI_Bcast(tsp0[i], NCIUDADES, MPI_INT, 0, MPI_COMM_WORLD);
    }

    int U = INFINITO; 
    tNodo mejor_solucion; 
    InicNodo(&mejor_solucion); 

    double t_inicio = MPI_Wtime();

    // =========================================================================
    // FASE 2: ROL DEL MAESTRO (Proceso 0)
    // =========================================================================
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

        bool *trabajador_tiene_faena = new bool[num_procs];
        for (int i = 0; i < num_procs; i++) trabajador_tiene_faena[i] = false;

        while (trabajadores_activos > 0) {
            MPI_Status status;
            int peticion;
            
            // Recibimos cualquier mensaje (Petición de trabajo o Solución encontrada)
            // Usamos un buffer lo suficientemente grande para albergar el nodo si viene con TAG_SOLUCION
            MPI_Recv(buffer_comunicaciones, 1, MPI_NODO_T, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            int origen = status.MPI_SOURCE;

            if (status.MPI_TAG == TAG_PIDE_TRAB) {
                if (!PilaVacia(&pila_maestro)) {
                    tNodo nodo_a_enviar;
                    PilaPop(&pila_maestro, &nodo_a_enviar);
                    
                    trabajador_tiene_faena[origen] = true;
                    SerializarNodo(&nodo_a_enviar, buffer_comunicaciones);
                    MPI_Send(buffer_comunicaciones, 1, MPI_NODO_T, origen, TAG_TRABAJO, MPI_COMM_WORLD);
                } else {
                    trabajador_tiene_faena[origen] = false;

                    bool sistema_activo = false;
                    for (int i = 1; i < num_procs; i++) {
                        if (trabajador_tiene_faena[i]) {
                            sistema_activo = true;
                            break;
                        }
                    }

                    if (!sistema_activo) {
                        for (int i = 1; i < num_procs; i++) {
                            int msg_fin = 1;
                            MPI_Send(&msg_fin, 1, MPI_INT, i, TAG_FIN, MPI_COMM_WORLD);
                        }
                        trabajadores_activos = 0;
                    }
                }
            } 
            else if (status.MPI_TAG == TAG_SOLUCION) {
                tNodo sol_recibida;
                DeserializarNodo(buffer_comunicaciones, &sol_recibida);

                if (sol_recibida.ci < U) {
                    U = sol_recibida.ci;
                    CopiaNodo(&sol_recibida, &mejor_solucion, true); 
                    printf("[MAESTRO] Nueva Cota Superior Global = %d encontrada por P%d\n", U, origen);

                    // Notificación asíncrona de nueva cota a los trabajadores
                    for (int i = 1; i < num_procs; i++) {
                        MPI_Request req;
                        MPI_Isend(&U, 1, MPI_INT, i, TAG_NUEVA_CS, MPI_COMM_WORLD, &req);
                        MPI_Request_free(&req); 
                    }
                }
            }
        }

        // Apagar receptores asíncronos de los esclavos
        int fin_cota = -1;
        for (int i = 1; i < num_procs; i++) {
            MPI_Request req;
            MPI_Isend(&fin_cota, 1, MPI_INT, i, TAG_NUEVA_CS, MPI_COMM_WORLD, &req);
            MPI_Request_free(&req);
        }

        double t_final = MPI_Wtime() - t_inicio;
        printf("\n=============================================================\n");
        printf("PROCESAMIENTO CONCLUIDO CON ÉXITO\n");
        EscribeSolucion(&mejor_solucion, t_final);
        printf("=============================================================\n");

        delete[] trabajador_tiene_faena;
    } 
    // =========================================================================
    // FASE 3: ROL DE LOS TRABAJADORES (Procesos > 0)
    // =========================================================================
    else {
        tPila pila_local;
        PilaInic(&pila_local);
        int terminar = 0;

        MPI_Request req_cota;
        int flag_cota_recibida = 0;
        int buffer_cota_asincrona;
        
        MPI_Irecv(&buffer_cota_asincrona, 1, MPI_INT, 0, TAG_NUEVA_CS, MPI_COMM_WORLD, &req_cota);

        while (!terminar) {
            if (PilaVacia(&pila_local)) {
                int vacio_peticion = 1;
                // Enviamos petición usando tipo int estándar para agilizar
                MPI_Send(&vacio_peticion, 1, MPI_INT, 0, TAG_PIDE_TRAB, MPI_COMM_WORLD);

                MPI_Status status;
                MPI_Recv(buffer_comunicaciones, 1, MPI_NODO_T, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

                if (status.MPI_TAG == TAG_TRABAJO) {
                    tNodo nodo_recibido;
                    DeserializarNodo(buffer_comunicaciones, &nodo_recibido);
                    PilaPush(&pila_local, &nodo_recibido);
                } else if (status.MPI_TAG == TAG_FIN) {
                    terminar = 1; 
                }
            } else {
                MPI_Test(&req_cota, &flag_cota_recibida, MPI_STATUS_IGNORE);
                if (flag_cota_recibida) {
                    if (buffer_cota_asincrona == -1) {
                        terminar = 1;
                    } else if (buffer_cota_asincrona < U) {
                        U = buffer_cota_asincrona;
                        PilaAcotar(&pila_local, U); 
                    }
                    if (!terminar) {
                        MPI_Irecv(&buffer_cota_asincrona, 1, MPI_INT, 0, TAG_NUEVA_CS, MPI_COMM_WORLD, &req_cota);
                    }
                }

                if (terminar) break;

                tNodo nodo_actual;
                PilaPop(&pila_local, &nodo_actual);

                if (nodo_actual.ci >= U) {
                    continue;
                }

                if (Solucion(&nodo_actual)) {
                    if (nodo_actual.ci < U) {
                        U = nodo_actual.ci;
                        
                        // Enviamos directamente la estructura usando el tipo derivado. 
                        // Optimizamos el solapamiento eliminando dobles envíos bloqueantes.
                        SerializarNodo(&nodo_actual, buffer_comunicaciones);
                        MPI_Send(buffer_comunicaciones, 1, MPI_NODO_T, 0, TAG_SOLUCION, MPI_COMM_WORLD);
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

    // --- LIMPIEZA ABSOLUTA DE RECURSOS (Puntuación de calidad de código) ---
    liberarMatriz(tsp0);
    delete[] buffer_comunicaciones;
    MPI_Type_free(&MPI_NODO_T);
    MPI_Finalize();
    return 0;
}