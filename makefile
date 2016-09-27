##
## @file		makefile
## @author		Sigvald Marholm <sigvaldm@fys.uio.no>
## @copyright	University of Oslo, Norway
## @brief		PINC makefile.
## @date		10.10.15
##

CC		= mpicc
COPT	= -O3

CLOCAL = 	-Ilib/iniparser/src\
			-lm -lblas -lgsl -lhdf5

-include local.mk

EXEC	= pinc
CADD	= # Additional CFLAGS accessible from CLI
CFLAGS	=	-std=c11 -Wall $(CLOCAL) $(COPT) $(CADD)\

SDIR	= src
ODIR	= src/obj
HDIR	= src
LDIR	= lib
DDIR	= doc
TSDIR	= test
TODIR	= test/obj
THDIR	= test

HEAD_	= core.h io.h aux.h population.h grid.h pusher.h multigrid.h
SRC_	= io.c aux.c population.c grid.c pusher.c multigrid.c
OBJ_	= $(SRC_:.c=.o)
DOC_	= main.dox

TESTOBJ_= test.o $(SRC_:.c=.test.o)
#TESTOBJ_ = test.o aux.test.o
TESTHEAD_ = test.h

HEAD	= $(patsubst %,$(HDIR)/%,$(HEAD_))
SRC		= $(patsubst %,$(SDIR)/%,$(SRC_))
OBJ		= $(patsubst %,$(ODIR)/%,$(OBJ_))
TESTOBJ	= $(patsubst %,$(TODIR)/%,$(TESTOBJ_))
TESTHEAD= $(patsubst %,$(THDIR)/%,$(TESTHEAD_))



LIBOBJ_	= iniparser/libiniparser.a
LIBHEAD_= iniparser/src/iniparser.h

LIBOBJ = $(patsubst %,$(LDIR)/%,$(LIBOBJ_))
LIBHEAD = $(patsubst %,$(LDIR)/%,$(LIBHEAD_))

all: version $(EXEC) cleantestdata doc

local: version $(EXEC).local cleantestdata doc

test: version $(EXEC).test cleantestdata doc
	@echo "Running Unit Tests"
	@echo $(TEST)
	@./$(EXEC) input.ini

$(EXEC).test: $(TODIR)/main.test.o $(OBJ) $(TESTOBJ) $(LIBOBJ)
	@echo "Linking Unit Tests"
	@$(CC) $^ -o $(EXEC) $(CFLAGS)
	@echo "PINC is built"

$(EXEC).local: $(ODIR)/main.local.o $(OBJ) $(LIBOBJ)
	@echo "Linking PINC (using main.local.c)"
	@$(CC) $^ -o $(EXEC) $(CFLAGS)
	@echo "PINC is built"

$(EXEC): $(ODIR)/main.o $(OBJ) $(LIBOBJ)
	@echo "Linking PINC"
	@$(CC) $^ -o $@ $(CFLAGS)
	@echo "PINC is built"

$(ODIR)/%.o: $(SDIR)/%.c $(HEAD)
	@echo "Compiling $<"
	@echo $(HEAD) | xargs -n1 ./check.sh
	@mkdir -p $(ODIR)
	@./check.sh $<
	@$(CC) -c $< -o $@ $(CFLAGS)

$(TODIR)/%.o: $(TSDIR)/%.c $(HEAD) $(TESTHEAD)
	@echo "Compiling $<"
	@echo $(TESTHEAD) | xargs -n1 ./check.sh
	@mkdir -p $(TODIR)
	@./check.sh $<
	@$(CC) -c $< -o $@ -Isrc $(CFLAGS)

$(LDIR)/iniparser/libiniparser.a: $(LIBHEAD)
	@echo "Building iniparser"
	@cd $(LDIR)/iniparser && $(MAKE) libiniparser.a > /dev/null 2>&1

.phony: version
version:
	@echo "Embedding git version"
	@echo "#define VERSION \"$(shell git describe --abbrev=4 --dirty --always --tags)\"" > $(SDIR)/version.h

$(DDIR)/doxygen/doxyfile.inc: $(DDIR)/doxygen/doxyfile.mk $(THDIR)/test.h $(TSDIR)/test.c $(DDIR)/doxygen/$(DOC_)
	@echo INPUT	= ../../$(SDIR) ../../$(HDIR) ../../$(TSDIR) ../../$(THDIR) ../../$(DDIR)/doxygen > $(DDIR)/doxygen/doxyfile.inc
	@echo FILE_PATTERNS	= $(HEAD_) $(SRC_) $(DOC_) test.h test.c  >> $(DDIR)/doxygen/doxyfile.inc

doc: $(HEAD) $(SRC) $(DDIR)/doxygen/doxyfile.inc
	@echo "Making documentation (run \"make pdf\" to get pdf)"
	@cd $(DDIR)/doxygen && doxygen doxyfile.mk > /dev/null 2>&1
	@ln -sf doc/html/index.html doc.html

pdf: doc
	@echo "Making PDF"
	cd $(DDIR)/latex && $(MAKE)	# Intentionally verbose to spot LaTeX errors

cleandoc:
	@echo "Cleaning documentation"
	@rm -f $(DDIR)/doxygen/doxyfile.inc
	@rm -fr $(DDIR)/html $(DDIR)/latex
	@rm -f doc.html

cleantestdata:
	@echo "Cleaning test data"
	@rm -f data/*.h5 data/parsedump.txt

clean: cleandoc cleantestdata
	@echo "Cleaning compilation files (run \"make veryclean\" to clean more)"
	@rm -f *~ $(TODIR)/*.o $(ODIR)/*.o $(SDIR)/*.o $(SDIR)/*~ gmon.out ut

veryclean: clean
	@echo "Cleaning executable and iniparser"
	@rm -f $(EXEC)
	@cd $(LDIR)/iniparser && $(MAKE) veryclean > /dev/null 2>&1
