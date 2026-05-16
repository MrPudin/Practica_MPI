# Generated automatically from Makefile.in by configure.
ALL: default
##### User configurable options #####
SHELL       = /bin/sh
ARCH        = LINUX
COMM        = ch_p4
MPIR_HOME   = /opt/mpich
CC          = mpicxx
CLINKER     = $(CC)
AR          = ar crl
RANLIB      = ranlib
PMPILIB     = -lpmpich
OPTFLAGS    = -O3 -g
MPE_LIBS    = @MPE_LIBS@
MPE_DIR     = /opt/mpich/mpe
MPE_GRAPH   = @MPE_GRAPHICS@
#
### End User configurable options ###
CFLAGS	  = $(OPTFLAGS)
CFLAGSMPE = $(CFLAGS) -I$(MPE_DIR) $(MPE_GRAPH)
CCFLAGS	  = $(CFLAGS)
EXECS	  = tsp_sec tsp_mpi
default: $(EXECS)
all: default

# Versión secuencial (también sirve de referencia para speedup con P=1)
tsp_sec: tspsec.o libtsp.o
	$(CLINKER) $(OPTFLAGS) -o $@ $^

# Versión paralela MPI
tsp_mpi: tspmpi.o libtsp.o
	$(CLINKER) $(OPTFLAGS) -o $@ $^

clean:
	/bin/rm -f *.o $(EXECS)

%.o:	%.cc
	$(CC) $(CFLAGS) -c -o $@ $<