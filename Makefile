# Variables du Makefile
CC = gcc
# Option de compilation
CFLAGS = -std=c11 -Wpedantic -Wall -Wextra -Wconversion -Wwrite-strings -Werror -fstack-protector-all -fpie -D_FORTIFY_SOURCE=2 -O2 -D_XOPEN_SOURCE -g
# Option d'édition de lien
LDFLAGS= -Wl,-z,relro,-z,now -pie -lrt -pthread
# Dossiers contenant les sources, objets et éxécutables
SRCDIR = src
OBJDIR = obj
BINDIR = bin
LIBDIR = lib
# Nom des fichiers sans l'extension c et o
DAEMON = daemon
CLIENT = client
SHM = shm
# Fichier de configuration
CONFIG_FILE = ./config/app.config

#---------------------------------------------------------#
# Construction des dossiers bin et obj : make start_build #
#---------------------------------------------------------#
start_build :
	mkdir -p ./$(OBJDIR) ./$(BINDIR) ./$(LIBDIR)

#-------------------------------------------#
# Lancement d'un client : make start_client #
#-------------------------------------------#
# Éxécution du fichier éxécutable client
start_client : ./$(BINDIR)/$(CLIENT)
	./$(BINDIR)/$(CLIENT) $(CONFIG_FILE)
# Génération de l'éxécutable client et édition des liens
./$(BINDIR)/$(CLIENT) : ./$(OBJDIR)/$(CLIENT).o
	$(CC) -o ./$(BINDIR)/$(CLIENT) ./$(OBJDIR)/$(CLIENT).o $(LDFLAGS)
# Compilation client.c pour générer le fichier objet dans le dossier ./$(OBJDIR)
./$(OBJDIR)/$(CLIENT).o : ./$(SRCDIR)/$(CLIENT).c
	$(CC) -c ./$(SRCDIR)/$(CLIENT).c $(CFLAGS) -o ./$(OBJDIR)/$(CLIENT).o

#----------------------------------------#
# Lancement du démon : make start_daemon # 
#----------------------------------------#
# Éxécution du fichier éxécutable daemon
start_daemon : ./$(BINDIR)/$(DAEMON)
	./$(BINDIR)/$(DAEMON) $(CONFIG_FILE)
# Génération de l'éxécutable demon et édition des liens
./$(BINDIR)/$(DAEMON) : ./$(OBJDIR)/$(DAEMON).o
	$(CC) -o ./$(BINDIR)/$(DAEMON) ./$(OBJDIR)/$(DAEMON).o $(LDFLAGS)
# Compilation daemon.c pour générer le fichier objet dans le dossier ./$(OBJDIR)
./$(OBJDIR)/$(DAEMON).o : ./$(SRCDIR)/$(DAEMON).c
	$(CC) -c ./$(SRCDIR)/$(DAEMON).c $(CFLAGS) -o ./$(OBJDIR)/$(DAEMON).o

#-------------------------------------------------------#
# Bibliothèque dynamique                                #
# Fonctionnalités de la zone de mémoire partagée (SHM)  #
#-------------------------------------------------------#
lib_shm : ./$(OBJDIR)/$(SHM).o
	$(CC) -shared -o ./$(LIBDIR)/lib$(SHM).so ./$(OBJDIR)/$(SHM).o
./$(OBJDIR)/$(SHM).o : ./$(SRCDIR)/$(SHM).c
	$(CC) -c ./$(SRCDIR)/$(SHM).c $(CFLAGS) -fPIC -o ./$(OBJDIR)/$(SHM).o

#-----------#
# Nettoyage #
#-----------#

# clean : supprime les seulement les fichiers objets
clean :
	rm -rf ./$(OBJDIR)/*.o ./$(BINDIR)/* ./$(LIBDIR)/* /tmp/pipe_* /dev/shm/shm_name_* /dev/shm/sem_name_* /dev/shm/sem.sem_daemon*

# mrproper : supprime le fichier éxécutable
mrproper : clean
	rm -rf ./$(BINDIR)/$(DAEMON) ./$(BINDIR)/$(CLIENT) ./$(OBJDIR) ./$(BINDIR) ./$(LIBDIR)
	

# =========================================================================
# ~~~~~~~~~~~~~~~~~~~~~~~~~~~ Tutoriel Makefile ~~~~~~~~~~~~~~~~~~~~~~~~~~~
# =========================================================================

# Variables pour les règles générales (mais inutile pour ce projet) \
EXEC = main \
SRC = $(wildcard *.c) \
OBJ = $(SRC:.c=.o) \

#------------------#
# Règles générales #
#------------------#
#Règles pour compiler, édition des liens et éxécuter \
all : $(EXEC) \
	./$(EXEC) \
 Création du fichier éxécutable \
$(EXEC) : $(OBJ) \
	$(CC) -o $(EXEC) $(OBJDIR)/$(OBJ) $(LDFLAGS) \
 Génération des fichiers objets \
$(OBJDIR)/%.o : %.c \
	$(CC) -c $(SRC) $(CFLAGS) \
Les règles générales sont inutiles car pour ce projet.

# Tutoriel : https://www.youtube.com/watch?v=-riHEHGP2DU&ab_channel=FormationVid%C3%A9o

# ========================= 
# ~~ Variables spéciales ~~
# =========================

# $@ : nom de la cible se trouvant le $@
# $< : nom de la première dépendance
# $^ : la liste des dépendances
# $? : la liste des dépendances plus récentes que la cible
# $* : le nom du fichier sans son extension

# %.c : Tous les fichiers .c
# %.o : Tous les fichiers .o
