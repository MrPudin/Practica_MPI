/* ************************************************************************ */
/*  Libreria de funciones para el Branch-Bound y manejo de la pila          */
/* ************************************************************************ */
 
#include <cstdio>
#include <cstdlib>
#include <string.h>
#include "libtsp.h"

unsigned int NCIUDADES;
long int TotalNodos = 0;

char *input_file;
char MsgError[300];
 
/* ************************************************************************ */
/* ****************** Funciones para el Branch-Bound  ********************* */
/* ************************************************************************ */
 
void LeerMatriz (char archivo[], int** tsp) {
  FILE *fp;
  int i, j, n;
  
  input_file = archivo;
 
  if (!(fp = fopen(archivo, "r" ))) {
    printf ("ERROR abriendo archivo %s en modo lectura.\n", archivo);
    exit(1);
  }
  fscanf( fp, "%d", &n);
  printf ("-------------------------------------------------------------\n");
  for (i=0; i<NCIUDADES; i++) {
    for (j=0; j<NCIUDADES; j++) {
      fscanf( fp, "%d", &tsp[i][j] );
      printf ("%3d ", tsp[i][j]);
    }
    fscanf (fp, "\n");
    printf ("\n");
  }
  printf ("-------------------------------------------------------------\n");
}
 
 
bool Inconsistente (int** tsp) {
  int  fila, columna;
  for (fila=0; fila<NCIUDADES; fila++) {   /* examina cada fila */
    int i, n_infinitos;
    for (i=0, n_infinitos=0; i<NCIUDADES; i++)
      if (tsp[fila][i]==INFINITO && i!=fila)
        n_infinitos ++;
    if (n_infinitos == NCIUDADES-1)
      return true;
  }
  for (columna=0; columna<NCIUDADES; columna++) { /* examina columnas */
    int i, n_infinitos;
    for (i=0, n_infinitos=0; i<NCIUDADES; i++)
      if (tsp[columna][i]==INFINITO && i!=columna)
        n_infinitos++;               /* increm el num de infinitos */
    if (n_infinitos == NCIUDADES-1)
      return true;
  }
  return false;
}
 
void Reduce (int** tsp, int *ci)
{
  int min, v, w;

  // Reducción por filas
  for (v=0; v<NCIUDADES; v++)
  {
    for (w=0, min=INFINITO; w<NCIUDADES; w++)
      if (tsp[v][w] < min && v!=w)
        min = tsp[v][w];

    if (min!=0) {
      for (w=0; w<NCIUDADES; w++)
        if (tsp[v][w] != INFINITO && v!=w)
          tsp[v][w] -= min;
      *ci += min;       /* acumula el total restado para calc c.i. */
    }
  }

  // Reducción por columnas
  for (w=0; w<NCIUDADES; w++) {
    for (v=0, min=INFINITO; v<NCIUDADES; v++)
      if (tsp[v][w] < min && v!=w)
        min = tsp[v][w];
    if (min !=0) {
      for (v=0; v<NCIUDADES; v++)
        if (tsp[v][w] != INFINITO && v!=w)
          tsp[v][w] -= min;
      *ci += min;     /* acumula cantidad restada en ci */
    }
  }
}
 
bool EligeArco (tNodo *nodo, int** tsp, tArco *arco)
{
  int i, j;
  for (i=0; i<NCIUDADES; i++)
    if (nodo->incl[i] == NULO)
      for (j=0; j<NCIUDADES; j++)
        if (tsp[i][j] == 0 && i!=j) {
          arco->v = i;
          arco->w = j;
          return true;
        }
  return false;
}
 
void IncluyeArco(tNodo *nodo, tArco arco) {
  nodo->incl[arco.v] = arco.w;
  if (nodo->orig_excl == arco.v) {
    int i;
    nodo->orig_excl++;
    for (i=0; i<NCIUDADES-2; i++)
      nodo->dest_excl[i] = NULO;
  }
}
 
 
bool ExcluyeArco(tNodo *nodo, tArco arco) {
  int i;
  if (nodo->orig_excl != arco.v)
    return false;
  for (i=0; i<NCIUDADES-2; i++)
    if (nodo->dest_excl[i]==NULO) {
      nodo->dest_excl[i] = arco.w;
      return true;
    }
  return false;
}
 
void PonArco(int** tsp, tArco arco)
{
    int j;

    for (j=0; j<NCIUDADES; j++)
    {
      // fila arco.v a infinito
      if (j!=arco.w)
          tsp[arco.v][j] = INFINITO;

      // Columna arco.w a infinito
      if (j!=arco.v)
          tsp[j][arco.w] = INFINITO;
    }
}
 
void QuitaArco(int** tsp, tArco arco) {
  tsp[arco.v][arco.w] = INFINITO;
}
 
void EliminaCiclos(tNodo *nodo, int** tsp)
{
  int cnt, i, j;

  for (i=0; i<NCIUDADES; i++)
    for (cnt=2, j=nodo->incl[i]; j!=NULO && cnt<NCIUDADES;
         cnt++, j=nodo->incl[j])
      tsp[j][i] = INFINITO; /* pone <nodo[j],i> infinito */
}
 
void ApuntaArcos(tNodo *nodo, int** tsp)
{
  int i;
  tArco arco;
 
  for (arco.v=0; arco.v<NCIUDADES; arco.v++)
    if ((arco.w=nodo->incl[arco.v]) != NULO)
      PonArco (tsp, arco);

  for (arco.v=nodo->orig_excl, i=0; i<NCIUDADES-2; i++)
    if ((arco.w=nodo->dest_excl[i]) != NULO)
      QuitaArco (tsp, arco);

  EliminaCiclos (nodo, tsp);
}
 
void InfiereArcos(tNodo *nodo, int** tsp)
{
  bool cambio;
  int cont, i, j;
  tArco arco;
 
  do {
    cambio = false;
    for  (i=0; i<NCIUDADES; i++)     /* para cada fila i */
      if (nodo->incl[i] == NULO) {   /* si no hay incluido un arco <i,?> */
        for (cont=0, j=0; cont<=1 && j<NCIUDADES; j++)
          if (tsp[i][j] != INFINITO && i!=j) {
            cont++;  /* contabiliza entradas <i,?> no-INFINITO */
            arco.v = i;
            arco.w = j;
          }
        if (cont==1) {  /* hay una sola entrada <i,?> no-INFINITO */
          IncluyeArco(nodo, arco);
          PonArco(tsp, arco);
          EliminaCiclos (nodo, tsp);
          cambio = true;
        }
      }
  } while (cambio);
}
 
void Reconstruye (tNodo *nodo, int** tsp0, int** tsp)
{
  int i, j;

  for (i=0; i<NCIUDADES; i++)
    for (j=0; j<NCIUDADES; j++)
      tsp[i][j] = tsp0[i][j];

  ApuntaArcos (nodo, tsp);
  EliminaCiclos (nodo, tsp);
  nodo->ci = 0;
  Reduce (tsp,&nodo->ci);
}
 
void HijoIzq (tNodo *nodo, tNodo *lnodo, int** tsp, tArco arco)
{
  int** tsp2 = reservarMatrizCuadrada(NCIUDADES);;
  int i, j;

  CopiaNodo (nodo, lnodo, true);
  for (i=0; i<NCIUDADES; i++)
    for (j=0; j<NCIUDADES; j++)
      tsp2[i][j] = tsp[i][j];
  IncluyeArco(lnodo, arco);
  ApuntaArcos(lnodo, tsp2);
  InfiereArcos(lnodo, tsp2);
  Reduce(tsp2, &lnodo->ci);
  liberarMatriz(tsp2);
}
 
void HijoDch (tNodo *nodo, tNodo *rnodo, int** tsp, tArco arco) 
{
  int** tsp2 = reservarMatrizCuadrada(NCIUDADES);
  int i, j;
 
  CopiaNodo (nodo, rnodo, true);
  for (i=0; i<NCIUDADES; i++)
    for (j=0; j<NCIUDADES; j++)
      tsp2[i][j] = tsp[i][j];
  ExcluyeArco(rnodo, arco);
  ApuntaArcos(rnodo, tsp2);
  InfiereArcos(rnodo, tsp2);
  Reduce(tsp2, &rnodo->ci);
 
	liberarMatriz(tsp2);
}
 
void Ramifica (tNodo *nodo, tNodo *lnodo, tNodo *rnodo, int** tsp0)
{
  int** tsp = reservarMatrizCuadrada(NCIUDADES);
  tArco arco;

  Reconstruye (nodo, tsp0, tsp);
  EligeArco (nodo, tsp, &arco);
  HijoIzq (nodo, lnodo, tsp, arco);
  HijoDch (nodo, rnodo, tsp, arco);
 
  liberarMatriz(tsp);
}
 
bool Solucion(tNodo *nodo) 
{
  int i;

  for (i=0; i<NCIUDADES; i++)
    if (nodo->incl[i] == NULO)
      return false;

  return true;
}
 
int Tamanyo (tNodo *nodo) 
{
  int i, cont;

  for (i=0, cont=0; i<NCIUDADES; i++)
    if (nodo->incl[i] == NULO)
      cont++;

  return cont;
}
 
void InicNodo (tNodo *nodo)
{
  int i;

  nodo->id = ++TotalNodos;
  nodo->ci = 0;
  for (i=0; i<NCIUDADES; i++)
    nodo->incl[i] = NULO;
  nodo->orig_excl = 0;
  for (i=0; i<NCIUDADES-2; i++)
    nodo->dest_excl[i] = NULO;
}
 
void CopiaNodo (tNodo *origen, tNodo *destino, bool nuevo_nodo)
{
  int i;

  if (nuevo_nodo==false)
      destino->id = origen->id;
  else
      destino->id = ++TotalNodos;

    destino->ci = origen->ci;
  for (i=0; i<NCIUDADES; i++)
    destino->incl[i] = origen->incl[i];
  destino->orig_excl = origen->orig_excl;
  for (i=0; i<NCIUDADES-2; i++)
    destino->dest_excl[i] = origen->dest_excl[i];
}
 
void EscribeNodo (tNodo *nodo)
{
  int i;

  printf ("ci=%d : ",nodo->ci);
  for (i=0; i<NCIUDADES; i++)
    if (nodo->incl[i] != NULO)
      printf ("<%d,%d> ",i,nodo->incl[i]);
  if (nodo->orig_excl < NCIUDADES)
    for (i=0; i<NCIUDADES-2; i++)
      if (nodo->dest_excl[i] != NULO)
        printf ("!<%d,%d> ",nodo->orig_excl,nodo->dest_excl[i]);

}
 

void EscribeSolucion (tNodo *nodo,double t) 
{
     FILE *Fitxer;
     int a,i;
     char output_file[200];
     
     strcpy(output_file,input_file);
     strcat(output_file, ".sol");

     Fitxer=fopen(output_file,"w+");
     if (Fitxer==NULL)
     {
     	perror("Escritura fichero salida.");
     }

     if (fprintf (Fitxer,"Recorrido: ")<1)
     	perror("Error en l'escriptura dels resultats.");
     for (i=0; i<NCIUDADES; i++)
         if (nodo->incl[i] != NULO){
             if(fprintf (Fitxer,"<%d,%d> ",i,nodo->incl[i])<1){perror("Error en l'escriptura dels resultats.");}
         }
     if (nodo->orig_excl < NCIUDADES)
         for (i=0; i<NCIUDADES-2; i++)
             if (nodo->dest_excl[i] != NULO){
                 if(fprintf (Fitxer,"!<%d,%d> ",nodo->orig_excl,nodo->dest_excl[i])<1){perror("Error en l'escriptura dels resultats.");}              
             }
    fprintf (Fitxer,"\n");
     
     if (fprintf (Fitxer,"Coste(ci): %d\n",nodo->ci)<1) perror("Error en l'escriptura dels resultats.");
             
     if (fprintf (Fitxer,"Tiempo de ejecución: %g \n",t)<1) {perror("Error en l'escriptura dels resultats.");}

    fprintf (Fitxer,"\n");
 }
 
/* ********************************************************************* */
/* ********** Funciones para manejo de la pila  de nodos *************** */
/* ********************************************************************* */
void PilaInic (tPila *pila)
{
  pila->tope = 0;
}
 
bool PilaLlena (tPila *pila)
{
  return (pila->tope == MAXPILA);
}
 
bool PilaVacia (tPila *pila)
{
  return (pila->tope == 0);
}
 
int PilaTamanio (tPila *pila)
{
  return pila->tope;
}
 
bool PilaPush (tPila *pila, tNodo *nodo)
{
  if (PilaLlena (pila))
    return false;

  CopiaNodo (nodo, &pila->nodos[pila->tope],false);
  pila->tope++;

  return true;
}
 
bool PilaPop (tPila *pila, tNodo *nodo)
{
  if (PilaVacia(pila))
    return false;

  pila->tope--;
  CopiaNodo (&pila->nodos[pila->tope], nodo,false);

  return true;
}
 
 
bool PilaDivide (tPila *pila1, tPila *pila2)
{
  int i;
 
  if (PilaVacia(pila1) || PilaTamanio(pila1)==1)
    return false;

  for (i=0; i<pila1->tope/2; i++) {
    CopiaNodo (&pila1->nodos[i*2], &pila1->nodos[i], false);
    CopiaNodo (&pila1->nodos[i*2+1], &pila2->nodos[i], false);
  }

  if (pila1->tope%2 == 0) {
    pila1->tope = pila1->tope/2;
    pila2->tope = pila1->tope;
  }
  else {
    CopiaNodo (&pila1->nodos[pila1->tope-1], &pila1->nodos[pila1->tope/2], false);
    pila1->tope = pila1->tope/2 + 1;
    pila2->tope = pila1->tope - 1;
  }

  return true;
}
 
void PilaAcotar (tPila *pila, int U)
{
  int tope2;
  int i;

  for (i=0, tope2=0; i<pila->tope; i++)
    if (pila->nodos[i].ci < U) {
      CopiaNodo (&pila->nodos[i], &pila->nodos[tope2], false); // correccion 19/2/99
      tope2++;
    }
  pila->tope = tope2;
}
 
 
/* ******************************************************************** */
//         Funciones de reserva dinamica de memoria
/* ******************************************************************** */
 
// Reserva en el HEAP una matriz cuadrada de dimension "orden".
int ** reservarMatrizCuadrada(unsigned int orden)
{
	int** m = new int*[orden];

	m[0] = new int[orden*orden];
	for (unsigned int i = 1; i < orden; i++) {
		m[i] = m[i-1] + orden;
	}
 
	return m;
}
 
// Libera la memoria dinamica usada por matriz "m"
void liberarMatriz(int** m)
{
	delete [] m[0];
	delete [] m;
}