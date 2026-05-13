/* ************************************************************************ */
/*    Libreria para Branch-Bound, manejo de pila y Gestion de memoria       */
/* ************************************************************************ */
 
#include <cstdio>
const unsigned int MAXPILA =150;
extern unsigned int NCIUDADES;
extern long int TotalNodos;
 
#ifndef NULO_T
#define NULO_T
const int NULO = -1;
#endif
 
#ifndef INFINITO_T
#define INFINITO_T
const long int INFINITO = 9999;
#endif
 
extern unsigned int NCIUDADES;
 
/* ************************************************************************ */
/* ******** Representacion del problema T.S.P. **************************** */
/* ************************************************************************ */
 
/* ************************************************************************ */
/*
 # Una instancia del problema TSP es una matriz bidimensional de
   Nciudades X Nciudades entradas, donde:
     * las entradas (i,j) corresponden a la distancia desde la ciudad
       i a la ciudad j (siendo 0<=i<Nciudades, 0<=j<Nciudades, i<>j)
     * las entradas (i,i) tienen valor infinito (0<=i<N.ciudades)
 
 # VECTOR DE DISTANCIAS (REPRESENTACION COMPLETA DEL TSP)
   ------------------------------------------------------
   Los procedimientos de la libreria libtsp representan la matriz del TSP
   como un vector de distancias:
         int tsp[NCIUDADES][NCIUDADES];
   donde la entrada (i,j) corresponde al elemento tsp[i][j]
 
 
 # DESCRIPCION ABREVIADA DE TRABAJO.
   ---------------------------------
   Representa un (sub)problema del TSP, y contiene informacion de los
   arcos includos e incluidos.
   Una descripcion completa del (sub)problema (vector de distancias) se
   reconstruye a partir de la descripcion abreviada y del vector de
   distancias del problema inicial.
 
   Una descripcion abreviada de trabajo se almacena en la siguiente
   estructura:
                struct sNodo {
                  int   ci;
                  int   incl[NCIUDADES];
                  int   orig_excl;
                  int   dest_excl[NCIUDADES-2];
                };
                typedef struct sNodo tNodo;
                tNodo nodo;
   donde:
    * nodo.ci  es la cota inferior del subproblema
    * nodo.incl[0], nodo.incl[1], ... , nodo.incl[NCIUDADES-1]
      tienen el siguiente significado:
      - si nodo.incl[i] = j   (0<=j<NCIUDADES)
        se ha incluido el arco <i,j>
      - si nodo.incl[i] = NULO
        no hay ningun arco <i,*> incluido en el camino
      - cualquier otro valor sera considerado erroneo
   *  Los arcos excluidos explicitamente en la busqueda corresponden siempre
      a la misma fila (la busqueda se realiza de ciudad en ciudad).
      Cuando en una fila i tengamos (explicita o implicitamente) excluidos
      NCIUDADES-2 arcos (sin contar el arco <i,i>) esto supondra la inclusion
      forzosa del arco que queda.
      Por tanto solo tenemos que anotar como maximo NCIUDADES-2 arcos
      explicitamente excluidos.
      - si nodo.orig_excl = k  (0<=k<N.ciudades)
        los arcos excluidos explicitamente son de la forma <k,*>
      - si  nodo.orig_excl = NULO
        no quedan mas arcos por excluir explicitamente
   * nodo.dest_excl[0], nodo.dest_excl[1], ... ,nodo.dest_excl[NCIUDADES-2]
     tienen el siguiente significado:
     - si nodo.dest_excl[l]=NULO
       no corresponde a ningun arco excluido
     - si nodo.dest_excl[l]=m  (0<=m<NCIUDADES)
       se ha excluido (de forma explicita) el arco <nodo.orig_excl , m>
     - cualquier otro valor se considera erroneo
*/
/* *********************************************************************** */
 
#ifndef TARCO_T
#define TARCO_T
struct tArco {
	int   v;
	int   w;
};
#endif
 
 
#ifndef TNODO_T
#define TNODO_T
class tNodo {
	public:
        long int id;
		int ci;
		int* incl;
		int orig_excl;
		int* dest_excl;
 
		tNodo() {incl = new int[NCIUDADES]; dest_excl = new int[NCIUDADES-2];};
		~tNodo() {delete [] incl; delete [] dest_excl;};
};
#endif
 
 
 
/* ******************************************************************* */
/*  Pila de nodos para el problema del viajante (TSP) con estrategia   */
/*  primero en profundidad.                                            */
/* ******************************************************************* */
/*                                                                     */
/*  Una pila es almacenada, declarada e inicializada de la forma       */
/*  siguiente:                                                         */
/*                                                                     */
/*        struct sPila {                                               */
/*          int tope;                                                  */
/*          struct sNodo nodos[MAXPILA];                               */
/*        };                                                           */
/*        typedef struct sPila tPila;                                  */
/*        sPila pila_nodos;                                            */
/*        PilaInic (*pila_nodos);                                      */
/*                                                                     */
/*  Cada elemento de pila_nodos almacena un nodo del arbol             */
/*  de busqueda (una descripcion abreviadada de trabajo).              */
/*                                                                     */
/***********************************************************************/
 
#ifndef TPILA_T
#define TPILA_T
struct tPila {
	int tope;
	tNodo nodos[MAXPILA];
};
#endif
 
 
/* ********************************************************************* */
/* *** Cabeceras de funciones para el algoritmo de Branch-and-Bound *** */
/* ********************************************************************* */
 
void LeerMatriz (char archivo[], int** tsp) ;
 
bool Inconsistente  (int** tsp);
  /* tsp   -  matriz de inicidencia del problema o subproblema            */
  /* Si un subproblema tiene en alguna fila o columna todas sus entradas  */
  /* a infinito entonces es inconsistente: no conduce a ninguna solucion  */
  /* del TSP                                                              */
 
void Reduce (int** tsp, int *ci);
  /* tsp   -  matriz de incidencia del problema o subproblema        */
  /* ci    -  cota Inferior del problema                             */
  /* Reduce la matriz tsp e incrementa ci con las cantidades         */
  /* restadas a las filas/columnas de la matriz tsp.                 */
  /* La matriz reducida del TSP se obtiene restando a cada           */
  /* entrada de cada fila/columna de la matriz de incidencia,        */
  /* la entrada minima de dicha fila/columna. La cota inferior del   */
  /* (sub)problema es la suma de las cantidades restadas en todas    */
  /* las filas/columnas.                                             */
 
bool EligeArco (tNodo *nodo, int** tsp, tArco *arco);
  /*  nodo  -  descripcion de trabajo de un nodo del arbol (subproblema)   */
  /*  tsp   -  matriz de incidencia del subproblema (se supone reducida)   */
  /*  Busca una arco con valor 0 en una fila de la que no se haya incluido */
  /*  todavia ningun arco.                                                 */
 
void IncluyeArco(tNodo *nodo, tArco arco);
  /* nodo    - descripcion abreviada de trabajo                  */
  /* arco    - arco a incluir en la descripcion de trabajo       */
  /* Incluye el arco 'arco' en la descripcion de trabajo 'nodo'  */
 
bool ExcluyeArco(tNodo *nodo, tArco arco);
  /* nodo  - descripcion abreviada de trabajo                           */
  /* arco  - arco a excluir (explicitam) en la descripcion de trabajo   */
  /* Excluye el arco en la descripcion de trabajo 'nodo'                */
 
void PonArco(int** tsp, tArco arco);
  /*  tsp   -   matriz de incidencia                             */
  /*  Pone las entradas <v,?> y <?,w> a infinito, excepto <v,w>  */
 
void QuitaArco(int** tsp, tArco arco);
  /* tsp   -    matriz de incidencia                          */
  /* Pone la entrada <v,w> a infinito (excluye este arco)     */
 
void EliminaCiclos(tNodo *nodo, int** tsp);
  /* Elimina en 'tsp' los posibles ciclos que puedan formar los arcos */
  /* incluidos en 'nodo'                                              */
 
void ApuntaArcos(tNodo *nodo, int** tsp);
/* Dada una descripcion de trabajo 'nodo' y una matriz de inicidencia 'tsp' */
/* llama a Pon.Arco() para los arcos incluidos en 'nodo' y a Quita.Arco()   */
/* para los excluidos. Despues llama a Elimina.Ciclos para eliminar ciclos  */
/* en los caminos                                                           */
 
void InfiereArcos(tNodo *nodo, int** tsp);
  /* Infiere nuevos arcos a incluir en 'nodo' para aquellas filas que  */
  /* tengan N.ciudades-2 arcos a infinito en 'tsp'                     */
 
void Reconstruye (tNodo *nodo, int** tsp0, int** tsp);
  /* A partir de la descripcion del problema inicial 'tsp0' y de la  */
  /* descripcion abreviada de trabajo 'nodo', construye la matriz de */
  /* incidencia reducida 'tsp' y la cota Inferior 'ci'.              */
 
void HijoIzq (tNodo *nodo, tNodo *lnodo, int** tsp, tArco arco);
  /* Dada la descripcion de trabajo 'nodo', la matriz de incidencia        */
  /* reducida 'tsp', construye la descripcion de trabajo 'l.nodo'          */
  /* a partir de la inclusion del arco                                     */
 
void HijoDch (tNodo *nodo, tNodo *rnodo, int** tsp, tArco arco);
  /* Dada la descripcion de trabajo 'nodo' y la matriz de incidencia */
  /* reducida 'tsp', construye la descripcion de trabajo 'r.nodo'    */
  /* a partir de la exclusion del arco <v,w>.                        */
 
void Ramifica (tNodo *nodo, tNodo *rnodo, tNodo *lnodo, int** tsp0);
  /* Expande nodo, obteniedo el hijo izquierdo lnodo    */
  /* y el hijo derecho rnodo                            */
 
bool Solucion(tNodo *nodo);
  /* Devuelve TRUE si la descripcion de trabajo 'nodo' corresponde a una */
  /* solucion del problema (si tiene N.ciudades arcos incluidos).        */
 
int Tamanio (tNodo *nodo);
  /* Devuelve el tamanio del subproblema de la descripcion de trabajo nodo */
  /* (numero de arcos que quedan por incluir)                              */
 
void InicNodo (tNodo *nodo);
  /* Inicializa la descripcion de trabajo 'nodo' */

void CopiaNodo (tNodo *origen, tNodo *destino, bool nuevo_nodo);
  /* Copia una descripcion de trabajo origen en otra destino */
 
void EscribeNodo (tNodo *nodo);
  /* Escribe en pantalla el contenido de la descripcion de trabajo nodo */
 
void EscribeSolucion (tNodo *nodo, double t); 
  /* Escribe en fichero la solución del problema, junto el tiempo requerido para resolverlo */
 
/* ********************************************************************* */
/* ***   Cabeceras de Funciones para manejo de la pila de nodos      *** */
/* ********************************************************************* */
 
void PilaInic (tPila *pila);
  /* Inicializa la pila */
bool PilaLlena (tPila *pila);
  /* Devuelve TRUE si la pila esta llena, FALSE en caso contrario */
bool PilaVacia (tPila *pila);
  /* Devuelve TRUE si la pila esta vacia, FALSE en caso contrario */
int PilaTamanio (tPila *pila);
  /* Devuelve el numero de elementos en la pila */
 
bool PilaPush (tPila *pila, tNodo *nodo);
  /* Inserta nuevo elemento en la pila.       */
  /* devuelve FALSE si pila llena.            */
 
bool PilaPop (tPila *pila, tNodo *nodo);
  /* Saca un elemento de la pila.  */
  /* devuelve FALSE si pila vacia.  */
 
bool PilaDivide (tPila *pila1, tPila *pila2);
  /* Divide pila1 en dos (pila1 y pila2) tomando elementos desde el      */
  /* fondo al tope.                                                      */
  /* Si la pila esta vacia o contiene un solo elemento devuelve FALSE.   */
 
void PilaAcotar (tPila *pila, int U);
  /* Elimina los elementos que tengan valor de cota inferior >= U */
 
 
/* ******************************************************************** */
// Funciones para reserva dinamica de memoria
int ** reservarMatrizCuadrada(unsigned int orden);
void liberarMatriz(int** m);
/* ******************************************************************** */