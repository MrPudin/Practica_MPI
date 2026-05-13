/* ******************************************************************** */
/*               Algoritmo Branch-And-Bound Secuencial                  */
/* ******************************************************************** */
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <mpi.h>

#include "libtsp.h"
 
using namespace std;

int main (int argc, char **argv)
{

	switch (argc) {
		case 3:		NCIUDADES = atoi(argv[1]);
					break;
		default:	cerr << "La sintaxis es: bbseq <tamaño> <archivo>" << endl;
					exit(1);
					break;
	}
 
	int** tsp0 = reservarMatrizCuadrada(NCIUDADES);
	tNodo	nodo,         // nodo a explorar
			lnodo,        // hijo izquierdo
			rnodo,        // hijo derecho
			solucion;     // mejor solucion
	bool activo,        // condicion de fin
		nueva_U;       // hay nuevo valor de c.s.
	int  U;             // valor de c.s.
	tPila pila;         // pila de nodos a explorar

    MPI::Init(argc, argv);

	U = INFINITO;                  // inicializa cota superior
	InicNodo (&nodo);              // inicializa estructura nodo
	PilaInic (&pila);              // inicializa pila
	LeerMatriz (argv[2], tsp0);    // lee matriz de fichero
    InicNodo (&solucion);
    solucion.id = 0;
    TotalNodos--;
    double inicio = clock();
	
	activo = !Inconsistente(tsp0);
	
	while (activo)
    {
        // ciclo del Branch&Bound
        if ((TotalNodos%100001)==0)
        {
            printf("\r[calcular_soluciones] Procesando nodo %12ld (%ld), pendientes:%3d,  CS:%4d -> ", nodo.id, TotalNodos, pila.tope, U);
            EscribeNodo(&nodo);
            fflush(stdout);
        }

		Ramifica (&nodo, &lnodo, &rnodo, tsp0);
		nueva_U = false;
		if (Solucion(&rnodo)) {
			if (rnodo.ci < U) {    // se ha encontrado una solucion mejor
				U = rnodo.ci;
				nueva_U = true;
				CopiaNodo (&rnodo, &solucion, false);
                if (true)
                {
                    printf("\n[calcular_soluciones] Encontrada solución righ %ld (CS:%d) -> ", rnodo.id, U);
                    EscribeNodo(&rnodo);
                    printf("\n"); fflush(stdout);
                }
			}
		}
		else {                    //  no es un nodo solucion
			if (rnodo.ci < U) {     //  cota inferior menor que cota superior
				if (!PilaPush (&pila, &rnodo)) {
					printf ("Error: pila agotada\n");
					liberarMatriz(tsp0);
					exit (1);
				}
			}
		}
		if (Solucion(&lnodo)) {
			if (lnodo.ci < U) {    // se ha encontrado una solucion mejor
				U = lnodo.ci;
				nueva_U = true;
				CopiaNodo (&lnodo,&solucion,false);
                if (true)
                {
                    printf("\n[calcular_soluciones] Encontrada solución left %ld (CS:%d) -> ", lnodo.id, U);
                    EscribeNodo(&lnodo);
                    printf(".\n");
                    fflush(stdout);
                }
			}
		}
		else {                     // no es nodo solucion
			if (lnodo.ci < U) {      // cota inferior menor que cota superior
				if (!PilaPush (&pila, &lnodo)) {
					printf ("Error: pila agotada\n");
					liberarMatriz(tsp0);
					exit (1);
				}
			}
		}
		if (nueva_U) PilaAcotar (&pila, U);
		activo = PilaPop (&pila, &nodo);
	}

    double t = (double)(clock() - inicio) / CLOCKS_PER_SEC;
    printf("\n\n[TSP] \tSolución cámino óptimo %d ciudades (%s): \n \t", NCIUDADES, argv[2]);
    EscribeNodo(&solucion);
    printf("\n[TSP] \tTiempo redquerido soloventar %s: %05.6f secs.\n", argv[2], t);

	EscribeSolucion(&solucion, t);
	liberarMatriz(tsp0);

    MPI::Finalize();
}