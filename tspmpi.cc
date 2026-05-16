/* ************************************************************************ */
/*        TSP Branch-and-Bound Paralelo — MPI (CORREGIDO SIN DEADLOCK)      */
/* ************************************************************************ */

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mpi.h>
#include "libtsp.h"

using namespace std;

/* ─── Tags ───────────────────────────────────────────────────────────── */
#define TAG_TRABAJO   1
#define TAG_PIDE      2
#define TAG_SOLUCION  3
#define TAG_FIN       4

/* ─── Parámetros ─────────────────────────────────────────────────────── */
#define SEMILLA_FACTOR      4
#define MAX_NODOS_ENVIO      8

/* ─── Mensaje solución ───────────────────────────────────────────────── */
struct tMensajeSol {
    int ci;
    int incl[50];
    int orig_excl;
    int dest_excl[48];
};

/* ────────────────────────────────────────────────────────────────────── */
/* SERIALIZACIÓN                                                          */
/* ────────────────────────────────────────────────────────────────────── */

static inline int tamNodo() {
    return 2 * (int)NCIUDADES + 1;
}

static void nodoABuf(const tNodo *nd, int *buf) {
    int k = 0;
    buf[k++] = (int)nd->id;
    buf[k++] = nd->ci;

    for (unsigned i = 0; i < NCIUDADES; i++)
        buf[k++] = nd->incl[i];

    buf[k++] = nd->orig_excl;

    for (unsigned i = 0; i < NCIUDADES - 2; i++)
        buf[k++] = nd->dest_excl[i];
}

static void bufANodo(const int *buf, tNodo *nd) {
    int k = 0;
    nd->id = buf[k++];
    nd->ci = buf[k++];

    for (unsigned i = 0; i < NCIUDADES; i++)
        nd->incl[i] = buf[k++];

    nd->orig_excl = buf[k++];

    for (unsigned i = 0; i < NCIUDADES - 2; i++)
        nd->dest_excl[i] = buf[k++];
}

static int pilaABuf(tPila *pila, int *buf, int maxN) {
    int tn = tamNodo();
    int n = 0;
    tNodo tmp;

    while (n < maxN && PilaPop(pila, &tmp)) {
        nodoABuf(&tmp, buf + n * tn);
        n++;
    }
    return n;
}

static void bufAPila(const int *buf, int n, tPila *pila) {
    int tn = tamNodo();
    tNodo tmp;

    for (int i = 0; i < n; i++) {
        bufANodo(buf + i * tn, &tmp);
        if (!PilaPush(pila, &tmp)) {
            fprintf(stderr, "ERROR: pila llena\n");
        }
    }
}

/* ────────────────────────────────────────────────────────────────────── */
/* MAESTRO                                                                */
/* ────────────────────────────────────────────────────────────────────── */

static void ejecutarMaestro(int **tsp0, int nProcs, double *tSol) {
    int nTrab = nProcs - 1;
    int tn = tamNodo();

    // El búfer ahora viaja acompañado del valor actual de U al inicio
    int *bufEnv = new int[1 + MAX_NODOS_ENVIO * tn];
    bool *vivo = new bool[nProcs];
    bool *idle = new bool[nProcs];

    for (int i = 0; i < nProcs; i++) {
        vivo[i] = (i != 0);
        idle[i] = false;
    }

    tNodo nodo, lnodo, rnodo, sol;
    tPila pila;
    int U = INFINITO;

    InicNodo(&nodo);
    InicNodo(&sol);
    sol.id = 0;
    PilaInic(&pila);

    bool activo = !Inconsistente(tsp0);
    int objetivo = SEMILLA_FACTOR * nTrab;

    /* ─── Siembra ─────────────────────────────────────────────────── */
    while (activo && PilaTamanio(&pila) < objetivo) {
        Ramifica(&nodo, &lnodo, &rnodo, tsp0);

        if (Solucion(&rnodo)) {
            if (rnodo.ci < U) {
                U = rnodo.ci;
                CopiaNodo(&rnodo, &sol, false);
                *tSol = MPI_Wtime();
            }
        } else if (rnodo.ci < U) {
            PilaPush(&pila, &rnodo);
        }

        if (Solucion(&lnodo)) {
            if (lnodo.ci < U) {
                U = lnodo.ci;
                CopiaNodo(&lnodo, &sol, false);
                *tSol = MPI_Wtime();
            }
        } else if (lnodo.ci < U) {
            PilaPush(&pila, &lnodo);
        }

        if (U < INFINITO)
            PilaAcotar(&pila, U);

        activo = PilaPop(&pila, &nodo);
    }

    /* ─── Bucle principal ─────────────────────────────────────────── */
    int finEnviados = 0;

    while (finEnviados < nTrab) {
        MPI_Status st;
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &st);

        int src = st.MPI_SOURCE;
        int tag = st.MPI_TAG;

        /* ─── PETICIÓN DE TRABAJO ───────────────────────────────── */
        if (tag == TAG_PIDE) {
            int dummy;
            MPI_Recv(&dummy, 1, MPI_INT, src, TAG_PIDE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            idle[src] = true;

            bool todosIdle = true;
            for (int p = 1; p < nProcs; p++) {
                if (vivo[p] && !idle[p]) {
                    todosIdle = false;
                    break;
                }
            }

            if (PilaVacia(&pila) && todosIdle) {
                // Enviamos señal de FIN junto con la mejor cota global
                MPI_Send(&U, 1, MPI_INT, src, TAG_FIN, MPI_COMM_WORLD);
                vivo[src] = false;
                finEnviados++;
            } 
            else if (!PilaVacia(&pila)) {
                // Empaquetamos primero la cota global 'U' para que el trabajador se actualice
                bufEnv[0] = U; 
                int n = pilaABuf(&pila, bufEnv + 1, MAX_NODOS_ENVIO);

                MPI_Send(bufEnv, 1 + n * tn, MPI_INT, src, TAG_TRABAJO, MPI_COMM_WORLD);
                idle[src] = false;
            } 
            else {
                // Si la pila está vacía pero hay otros trabajando, dejamos al nodo en espera devolviendo U vacío
                bufEnv[0] = U;
                MPI_Send(bufEnv, 1, MPI_INT, src, TAG_TRABAJO, MPI_COMM_WORLD);
            }
        }
        /* ─── NUEVA SOLUCIÓN ─────────────────────────────────────── */
        else if (tag == TAG_SOLUCION) {
            tMensajeSol msg;
            MPI_Recv(&msg, sizeof(tMensajeSol), MPI_BYTE, src, TAG_SOLUCION, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            idle[src] = false;

            if (msg.ci < U) {
                U = msg.ci;
                *tSol = MPI_Wtime();

                sol.ci = U;
                sol.orig_excl = msg.orig_excl;
                memcpy(sol.incl, msg.incl, NCIUDADES * sizeof(int));
                memcpy(sol.dest_excl, msg.dest_excl, (NCIUDADES - 2) * sizeof(int));

                printf("\n[MAESTRO] Nueva CS=%d (proc %d)\n", U, src);
                PilaAcotar(&pila, U);
            }
        }
    }

    printf("\n[MAESTRO] SOLUCIÓN ÓPTIMA\n\t");
    EscribeNodo(&sol);
    printf("\nCoste = %d\n", sol.ci);
    EscribeSolucion(&sol, *tSol);

    delete[] bufEnv;
    delete[] vivo;
    delete[] idle;
}

/* ────────────────────────────────────────────────────────────────────── */
/* TRABAJADOR                                                             */
/* ────────────────────────────────────────────────────────────────────── */

static void ejecutarTrabajador(int **tsp0, int rango) {
    int tn = tamNodo();
    // Búfer recibe: [Cota U] + [Nodos]
    int *bufR = new int[1 + MAX_NODOS_ENVIO * tn];

    tNodo nodoAct, lnodo, rnodo, sol;
    tPila pila;
    int U = INFINITO;
    bool terminar = false;

    InicNodo(&sol);
    sol.id = 0;
    PilaInic(&pila);

    while (!terminar) {
        int dummy = rango;
        MPI_Send(&dummy, 1, MPI_INT, 0, TAG_PIDE, MPI_COMM_WORLD);

        MPI_Status st;
        MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);

        /* ─── FIN ─────────────────────────────────────────────── */
        if (st.MPI_TAG == TAG_FIN) {
            MPI_Recv(&U, 1, MPI_INT, 0, TAG_FIN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            terminar = true;
            break;
        }

        /* ─── TRABAJO ─────────────────────────────────────────── */
        int count;
        MPI_Get_count(&st, MPI_INT, &count);
        MPI_Recv(bufR, count, MPI_INT, 0, TAG_TRABAJO, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // El primer elemento del búfer siempre trae la cota óptima global actualizada
        int nuevaU = bufR[0];
        if (nuevaU < U) {
            U = nuevaU;
            PilaAcotar(&pila, U);
        }

        // Si llegaron nodos (count > 1), los metemos en la pila
        if (count > 1) {
            bufAPila(bufR + 1, (count - 1) / tn, &pila);
        }

        // Procesamos la pila local de manera síncrona y segura
        while (PilaPop(&pila, &nodoAct)) {
            
            if (nodoAct.ci >= U) continue; // Poda inmediata

            Ramifica(&nodoAct, &lnodo, &rnodo, tsp0);
            bool nuevaSol = false;

            /* ─── Hijo derecho ─────────────────────────────── */
            if (Solucion(&rnodo)) {
                if (rnodo.ci < U) {
                    U = rnodo.ci;
                    CopiaNodo(&rnodo, &sol, false);
                    nuevaSol = true;
                }
            } else if (rnodo.ci < U) {
                PilaPush(&pila, &rnodo);
            }

            /* ─── Hijo izquierdo ───────────────────────────── */
            if (Solucion(&lnodo)) {
                if (lnodo.ci < U) {
                    U = lnodo.ci;
                    CopiaNodo(&lnodo, &sol, false);
                    nuevaSol = true;
                }
            } else if (lnodo.ci < U) {
                PilaPush(&pila, &lnodo);
            }

            /* ─── Enviar nueva solución (Síncrono/Seguro) ───── */
            if (nuevaSol) {
                PilaAcotar(&pila, U);

                tMensajeSol msg;
                msg.ci = U;
                msg.orig_excl = sol.orig_excl;
                memcpy(msg.incl, sol.incl, NCIUDADES * sizeof(int));
                memcpy(msg.dest_excl, sol.dest_excl, (NCIUDADES - 2) * sizeof(int));

                printf("[P%d] Nueva solución %d\n", rango, U);
                
                // Usamos un envío síncrono estándar para evitar saturar al maestro
                MPI_Send(&msg, sizeof(tMensajeSol), MPI_BYTE, 0, TAG_SOLUCION, MPI_COMM_WORLD);
            }
        }
    }

    delete[] bufR;
}

/* ────────────────────────────────────────────────────────────────────── */
/* MAIN                                                                   */
/* ────────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rango, nProcs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rango);
    MPI_Comm_size(MPI_COMM_WORLD, &nProcs);

    if (argc != 3) {
        if (rango == 0) {
            cerr << "Uso: mpirun -np P ./tsp <N> <archivo>\n";
        }
        MPI_Finalize();
        return 1;
    }

    NCIUDADES = atoi(argv[1]);
    int **tsp0 = reservarMatrizCuadrada(NCIUDADES);

    if (rango == 0) {
        LeerMatriz(argv[2], tsp0);
    }

    for (unsigned i = 0; i < NCIUDADES; i++) {
        MPI_Bcast(tsp0[i], NCIUDADES, MPI_INT, 0, MPI_COMM_WORLD);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double tInicio = MPI_Wtime();
    double tSol = tInicio;

    if (nProcs == 1) {
        printf("Ejecuta versión secuencial aparte\n");
    }
    else if (rango == 0) {
        ejecutarMaestro(tsp0, nProcs, &tSol);
    }
    else {
        ejecutarTrabajador(tsp0, rango);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double tTotal = MPI_Wtime() - tInicio;

    if (rango == 0) {
        printf("\nTiempo total = %.6f s\n", tTotal);
    }

    liberarMatriz(tsp0);
    MPI_Finalize();
    return 0;
}