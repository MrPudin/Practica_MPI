#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mpi.h>
#include "libtsp.h"

/* ── Tags de comunicación ──────────────────────────────────────────────── */
#define TAG_PIDE_TRAB  1   // Worker  -> Maestro: solicita subproblema
#define TAG_TRABAJO    2   // Maestro -> Worker:  envía subproblema
#define TAG_FIN        3   // Maestro -> Worker:  señal de finalización
#define TAG_NUEVA_CS   4   // Maestro -> Workers: nueva cota superior global
#define TAG_SOLUCION   5   // Worker  -> Maestro: solución encontrada

#define M_MEJORES    3     // Número de mejores soluciones a mantener
#define LOTE_MAESTRO 64    // Nodos que computa el maestro entre sondeos

MPI_Datatype MPI_NODO_T;

/* ══════════════════════════════════════════════════════════════════════════
   TIPO DERIVADO MPI: serializa tNodo como bloque contiguo de bytes.
══════════════════════════════════════════════════════════════════════════ */
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
    for (unsigned int i = 0; i < NCIUDADES;   i++) {
        memcpy(ptr, &origen->incl[i],      sizeof(int)); ptr += sizeof(int);
    }
    for (unsigned int i = 0; i < NCIUDADES-2; i++) {
        memcpy(ptr, &origen->dest_excl[i], sizeof(int)); ptr += sizeof(int);
    }
}

void DeserializarNodo(const char *buf, tNodo *destino) {
    const char *ptr = buf;
    memcpy(&destino->id,        ptr, sizeof(long int)); ptr += sizeof(long int);
    memcpy(&destino->ci,        ptr, sizeof(int));       ptr += sizeof(int);
    memcpy(&destino->orig_excl, ptr, sizeof(int));       ptr += sizeof(int);
    for (unsigned int i = 0; i < NCIUDADES;   i++) {
        memcpy(&destino->incl[i],      ptr, sizeof(int)); ptr += sizeof(int);
    }
    for (unsigned int i = 0; i < NCIUDADES-2; i++) {
        memcpy(&destino->dest_excl[i], ptr, sizeof(int)); ptr += sizeof(int);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   RANKING DE M MEJORES SOLUCIONES
   costes[] ordenado de menor a mayor. costes[M-1] es el umbral de admisión:
   una solución con coste >= costes[M-1] no puede entrar en el top-M.
══════════════════════════════════════════════════════════════════════════ */
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

/*
 * Cota de poda = PEOR de las M mejores (costes[M-1]). Mientras no haya M
 * soluciones, INFINITO (no se puede descartar nada todavía).
 * El umbral NO es costes[0]: usar el mejor descartaría candidatos válidos
 * para los puestos 2º y 3º del ranking.
 */
static int cotaPoda(const tMejores *m) {
    if (m->count < M_MEJORES) return INFINITO;
    return m->costes[M_MEJORES - 1];
}

/* Inserta un candidato. Devuelve true si el umbral de admisión bajó
 * (entonces hay que propagar el nuevo U a los workers). */
static bool insertarMejor(tMejores *m, tNodo *nodo) {
    int ci       = nodo->ci;
    int old_cota = cotaPoda(m);

    if (m->count == M_MEJORES && ci >= m->costes[M_MEJORES - 1])
        return false;

    int pos = (m->count < M_MEJORES) ? m->count : M_MEJORES - 1;
    for (int i = 0; i < m->count && i < M_MEJORES; i++)
        if (ci < m->costes[i]) { pos = i; break; }

    int hasta = (m->count < M_MEJORES) ? m->count : M_MEJORES - 1;
    for (int i = hasta; i > pos; i--) {
        CopiaNodo(&m->nodos[i-1], &m->nodos[i], false);
        m->costes[i] = m->costes[i-1];
    }
    CopiaNodo(nodo, &m->nodos[pos], false);
    m->costes[pos] = ci;
    if (m->count < M_MEJORES) m->count++;

    return cotaPoda(m) < old_cota;
}

static void imprimirMejores(const tMejores *m) {
    printf("\n=== %d mejores soluciones encontradas ===\n", m->count);
    for (int i = 0; i < m->count; i++) {
        printf("  #%d Coste=%d : ", i+1, m->costes[i]);
        EscribeNodo(const_cast<tNodo*>(&m->nodos[i]));
        printf("\n");
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   SIEMBRA INICIAL: expande la pila hasta tener 'objetivo' subproblemas
   independientes listos para repartir entre los workers.
══════════════════════════════════════════════════════════════════════════ */
void GenerarBolsaInicial(tPila *pila, int **tsp0, int objetivo,
                         int *U, tMejores *mejores) {
    while (PilaTamanio(pila) < objetivo && !PilaVacia(pila)) {
        tNodo nodo;
        PilaPop(pila, &nodo);
        if (nodo.ci >= *U) continue;

        if (Solucion(&nodo)) {
            if (insertarMejor(mejores, &nodo)) { *U = cotaPoda(mejores); PilaAcotar(pila, *U); }
            continue;
        }
        tNodo izq, der;
        InicNodo(&izq); InicNodo(&der);
        Ramifica(&nodo, &izq, &der, tsp0);
        if (der.ci < *U) PilaPush(pila, &der);
        if (izq.ci < *U) PilaPush(pila, &izq);
    }
    PilaAcotar(pila, *U);
}

/* ── Helpers del maestro ────────────────────────────────────────────────── */

/* Propaga la nueva cota a todos los workers vivos, salvo a 'excluir'. */
static void propagarCota(int U, const bool *vivo, int num_procs, int excluir) {
    for (int p = 1; p < num_procs; p++)
        if (vivo[p] && p != excluir)
            MPI_Send(&U, 1, MPI_INT, p, TAG_NUEVA_CS, MPI_COMM_WORLD);
}

/* Reparte nodos a los workers ociosos mientras quede trabajo en la pila. */
static void reactivarOciosos(tPila *pila, bool *ocioso, const bool *vivo,
                             int num_procs, char *bufCom) {
    for (int p = 1; p < num_procs; p++) {
        if (vivo[p] && ocioso[p] && !PilaVacia(pila)) {
            tNodo tmp;
            PilaPop(pila, &tmp);
            SerializarNodo(&tmp, bufCom);
            MPI_Send(bufCom, 1, MPI_NODO_T, p, TAG_TRABAJO, MPI_COMM_WORLD);
            ocioso[p] = false;
        }
    }
}

/* ── Helper del worker ──────────────────────────────────────────────────────
   Drena (sin bloquear) todas las cotas pendientes del maestro. Sustituye al
   antiguo mecanismo Irecv + Cancel + Wait: aquí solo se sondea con un tag
   concreto, lo que elimina por completo ese código frágil y costoso.
   El valor -1 es el centinela de finalización.
──────────────────────────────────────────────────────────────────────────── */
static void drenarCotas(int *U, tPila *pila, int *terminar) {
    int flag;
    MPI_Status st;
    MPI_Iprobe(0, TAG_NUEVA_CS, MPI_COMM_WORLD, &flag, &st);
    while (flag) {
        int nuevaU;
        MPI_Recv(&nuevaU, 1, MPI_INT, 0, TAG_NUEVA_CS, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (nuevaU == -1) { *terminar = 1; return; }
        if (nuevaU < *U)  { *U = nuevaU; PilaAcotar(pila, *U); }
        MPI_Iprobe(0, TAG_NUEVA_CS, MPI_COMM_WORLD, &flag, &st);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   MAIN
══════════════════════════════════════════════════════════════════════════ */
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
        if (mi_rango == 0) printf("Error: se necesitan al menos 2 procesos MPI.\n");
        MPI_Finalize();
        return -1;
    }

    NCIUDADES  = atoi(argv[1]);
    TotalNodos = 0;
    CrearTipoDerivado();

    char *bufCom = new char[TamanoBufferNodo()];
    int **tsp0   = reservarMatrizCuadrada(NCIUDADES);
    if (mi_rango == 0) LeerMatriz(argv[2], tsp0);

    /* Colectiva: difundir la matriz de distancias a todos los procesos */
    MPI_Bcast(tsp0[0], (int)(NCIUDADES * NCIUDADES), MPI_INT, 0, MPI_COMM_WORLD);

    int    U        = INFINITO;
    double t_inicio = MPI_Wtime();

    /* ════════════════════════════════════════════════════════════════════
       MAESTRO (rango 0)

       Política del bucle, en orden de prioridad:
        1) Servir TODAS las peticiones pendientes (los workers van primero).
        2) Si aún queda trabajo en la bolsa, computar un LOTE de nodos con los
           ciclos sobrantes del maestro (aprovecha el núcleo que en un
           maestro-trabajador puro quedaría ocioso).
        3) Si la bolsa está vacía: terminar si todos los workers están ociosos;
           si no, BLOQUEAR en MPI_Probe hasta que un worker reporte algo.

       Clave de rendimiento: el maestro nunca hace busy-polling. Cuando no
       tiene trabajo, bloquea y cede el núcleo a los workers.
    ════════════════════════════════════════════════════════════════════ */
    if (mi_rango == 0) {

        tMejores mejores;  inicMejores(&mejores);
        tPila    bolsa;    PilaInic(&bolsa);

        tNodo raiz;
        InicNodo(&raiz);
        int **tsp_raiz = reservarMatrizCuadrada(NCIUDADES);
        Reconstruye(&raiz, tsp0, tsp_raiz);
        liberarMatriz(tsp_raiz);
        PilaPush(&bolsa, &raiz);

        int objetivo = (num_procs - 1) * 8;
        if (objetivo > (int)MAXPILA - 1) objetivo = (int)MAXPILA - 1;
        if (objetivo < num_procs - 1)    objetivo = num_procs - 1;
        GenerarBolsaInicial(&bolsa, tsp0, objetivo, &U, &mejores);
        printf("[MAESTRO] Bolsa inicial: %d subproblemas, cota=%d\n",
               PilaTamanio(&bolsa), U);

        bool *ocioso = new bool[num_procs];
        bool *vivo   = new bool[num_procs];
        for (int i = 0; i < num_procs; i++) { ocioso[i] = false; vivo[i] = (i != 0); }
        int finEnviados = 0;

        if (U < INFINITO) propagarCota(U, vivo, num_procs, -1);

        while (finEnviados < num_procs - 1) {

            /* (1) Servir todas las peticiones pendientes sin bloquear */
            int flag = 1;
            while (flag) {
                MPI_Status st;
                MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &st);
                if (!flag) break;
                int origen = st.MPI_SOURCE;

                if (st.MPI_TAG == TAG_PIDE_TRAB) {
                    int dummy;
                    MPI_Recv(&dummy, 1, MPI_INT, origen, TAG_PIDE_TRAB,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    if (!PilaVacia(&bolsa)) {
                        tNodo n;
                        PilaPop(&bolsa, &n);
                        SerializarNodo(&n, bufCom);
                        MPI_Send(bufCom, 1, MPI_NODO_T, origen, TAG_TRABAJO, MPI_COMM_WORLD);
                        ocioso[origen] = false;
                    } else {
                        ocioso[origen] = true;  // sin trabajo ahora; no enviamos FIN aún
                    }
                } else if (st.MPI_TAG == TAG_SOLUCION) {
                    MPI_Recv(bufCom, 1, MPI_NODO_T, origen, TAG_SOLUCION,
                             MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    tNodo sol;
                    DeserializarNodo(bufCom, &sol);
                    if (insertarMejor(&mejores, &sol)) {
                        U = cotaPoda(&mejores);
                        printf("[MAESTRO] Nueva CS=%d (proc %d)\n", U, origen);
                        PilaAcotar(&bolsa, U);
                        propagarCota(U, vivo, num_procs, origen);
                        reactivarOciosos(&bolsa, ocioso, vivo, num_procs, bufCom);
                    }
                }
            }

            /* (2) Computar un lote con los ciclos sobrantes del maestro */
            if (!PilaVacia(&bolsa)) {
                for (int k = 0; k < LOTE_MAESTRO && !PilaVacia(&bolsa); k++) {
                    tNodo nodo;
                    PilaPop(&bolsa, &nodo);
                    if (nodo.ci >= U) continue;

                    if (Solucion(&nodo)) {
                        if (insertarMejor(&mejores, &nodo)) {
                            U = cotaPoda(&mejores);
                            printf("[MAESTRO] Nueva CS (local)=%d\n", U);
                            PilaAcotar(&bolsa, U);
                            propagarCota(U, vivo, num_procs, -1);
                        }
                    } else {
                        tNodo izq, der;
                        InicNodo(&izq); InicNodo(&der);
                        Ramifica(&nodo, &izq, &der, tsp0);
                        if (der.ci < U) PilaPush(&bolsa, &der);
                        if (izq.ci < U) PilaPush(&bolsa, &izq);
                    }
                }
                reactivarOciosos(&bolsa, ocioso, vivo, num_procs, bufCom);

            /* (3) Sin trabajo: terminar o bloquear esperando a un worker */
            } else {
                bool todosOciosos = true;
                for (int p = 1; p < num_procs; p++)
                    if (vivo[p] && !ocioso[p]) { todosOciosos = false; break; }

                if (todosOciosos) {
                    int fin = -1;
                    for (int p = 1; p < num_procs; p++) {
                        if (vivo[p]) {
                            MPI_Send(&fin, 1, MPI_INT, p, TAG_NUEVA_CS, MPI_COMM_WORLD);
                            MPI_Send(&fin, 1, MPI_INT, p, TAG_FIN,      MPI_COMM_WORLD);
                            vivo[p] = false;
                            finEnviados++;
                        }
                    }
                } else {
                    /* Algún worker sigue ocupado: bloquear (sin quemar CPU).
                       El mensaje se servirá en la fase (1) de la próxima vuelta. */
                    MPI_Status st;
                    MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
                }
            }
        }

        /* Colectiva: total de nodos explorados por todos los procesos */
        long total_nodos;
        MPI_Reduce(&TotalNodos, &total_nodos, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

        double t_final = MPI_Wtime() - t_inicio;
        imprimirMejores(&mejores);
        printf("\n=============================================================\n");
        printf("Total nodos explorados: %ld\n", total_nodos);
        printf("PROCESAMIENTO CONCLUIDO. Tiempo: %.6f s\n", t_final);
        printf("=============================================================\n");
        EscribeSolucion(&mejores.nodos[0], t_final);

        delete[] ocioso;
        delete[] vivo;

    /* ════════════════════════════════════════════════════════════════════
       TRABAJADOR (rangos 1..N-1)

       B&B local sobre su pila. En cada vuelta drena las cotas nuevas con un
       simple MPI_Iprobe (solapamiento cómputo/comunicación sin Irecv ni
       Cancel). Cuando se queda sin trabajo, pide otro subproblema y bloquea
       hasta recibirlo (sin busy-polling).
    ════════════════════════════════════════════════════════════════════ */
    } else {
        tPila pila;  PilaInic(&pila);
        int terminar = 0;

        while (!terminar) {
            drenarCotas(&U, &pila, &terminar);
            if (terminar) break;

            if (!PilaVacia(&pila)) {
                /* ── Un paso de B&B local ─────────────────────────────── */
                tNodo nodo;
                PilaPop(&pila, &nodo);
                if (nodo.ci >= U) continue;

                if (Solucion(&nodo)) {
                    /* No tocar U localmente: el worker no conoce el ranking
                       M-best del maestro y bajaría U de más, descartando
                       candidatos válidos. El maestro fija y propaga el U real. */
                    SerializarNodo(&nodo, bufCom);
                    MPI_Send(bufCom, 1, MPI_NODO_T, 0, TAG_SOLUCION, MPI_COMM_WORLD);
                } else {
                    tNodo izq, der;
                    InicNodo(&izq); InicNodo(&der);
                    Ramifica(&nodo, &izq, &der, tsp0);
                    if (der.ci < U) PilaPush(&pila, &der);
                    if (izq.ci < U) PilaPush(&pila, &izq);
                }

            } else {
                /* ── Pila vacía: pedir trabajo y esperar respuesta ────── */
                int peticion = 1;
                MPI_Send(&peticion, 1, MPI_INT, 0, TAG_PIDE_TRAB, MPI_COMM_WORLD);

                bool recibido = false;
                while (!recibido && !terminar) {
                    MPI_Status st;
                    MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);

                    if (st.MPI_TAG == TAG_NUEVA_CS) {
                        int nuevaU;
                        MPI_Recv(&nuevaU, 1, MPI_INT, 0, TAG_NUEVA_CS,
                                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        if (nuevaU == -1) terminar = 1;
                        else if (nuevaU < U) { U = nuevaU; PilaAcotar(&pila, U); }
                    } else if (st.MPI_TAG == TAG_FIN) {
                        int f;
                        MPI_Recv(&f, 1, MPI_INT, 0, TAG_FIN, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        terminar = 1;
                    } else {  // TAG_TRABAJO
                        MPI_Recv(bufCom, 1, MPI_NODO_T, 0, TAG_TRABAJO,
                                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                        tNodo n;
                        DeserializarNodo(bufCom, &n);
                        PilaPush(&pila, &n);
                        recibido = true;
                    }
                }
            }
        }

        /* Colectiva: aportar el recuento local de nodos explorados */
        long descartar;
        MPI_Reduce(&TotalNodos, &descartar, 1, MPI_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    }

    liberarMatriz(tsp0);
    delete[] bufCom;
    MPI_Type_free(&MPI_NODO_T);
    MPI_Finalize();
    return 0;
}
