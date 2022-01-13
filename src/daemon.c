#ifdef _XOPEN_SOURCE
	#undef _XOPEN_SOURCE
	#define _XOPEN_SOURCE 500
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include "../inc/color_ansi.h"
#include "../inc/config.h"
#include "utils.c"
#include "config.c"
#include "shm.c"

/**
 * Fichier daemon.c
 * Compilation 					: gcc -c daemon.c
 * Génération de l'éxécutable 	: gcc -o daemon daemon.o -pthread -lrt
 * Exécution : ./daemon
*/

/*----------*/
/*	DEFINES	*/
/*----------*/
/*--------------*/
/*	PROTOTYPES	*/
/*--------------*/
// Segment de mémoire partagée (SHM)
void consumer(void);
// Thread
void * run(void * arg);
void processing_request(const char buf[], int pipe[]);
// // Traitement des commandes
// Création d'un processus fils pour éxécuter la commande
void do_subshell(const char type, const char * const argv[], int pipe[]);
int info_proc(pid_t pid);
int info_user(const char type, uid_t uid, const char *name); // Informations sur un utilisateur
// Traitement des signaux
void handler(int signum);
// Libération des ressources
void dispose_all(void);
int send_request_close(const char type);

/*----------------------*/
/*	VARIABLES GLOBALES	*/
/*----------------------*/
// Configuration
char *config_file = NULL;
struct config *conf = NULL;
// Communication
// Sémaphores
sem_t *mutex;
sem_t *empty; 
sem_t *full;
// Segment de mémoire partagée (SHM)
int shm_fd;
size_t size_shm;
struct fifo *fifo_p = NULL;
// Structure pour enregistrer les ressources à libérer pour chaque thread
struct pthread_allocated {
	// -1 sinon id = pid
	int id;
	// Descripteur de fichier du tube question à fermer pour le thread
	int fdquestion;
	// Descripteur de fichier du tube réponse(answer) à fermer pour le thread
	int fdanswer;
};
struct pthread_allocated *pthread_alloc = NULL;

int main(int argc, char *argv[]) {
	/*--------*/
	/* SIGNAL */
	/*--------*/
	// Création du gestionnaire des signaux et remplit le masque de tous les signaux à bloquer
	struct sigaction action;
	action.sa_handler = handler;
	if (sigfillset(&action.sa_mask) == -1) {
		perror("sigfillset");
		exit(EXIT_FAILURE);
	}
	// Pour ne pas transformer le(s) processus enfant(s) en zombie
	action.sa_flags = 0;
	// action.sa_flags = SA_NOCLDWAIT;
	for (int signum = 0; signum < SIGRTMAX; signum++) {
		switch (signum) {
			case SIGALRM :
			// Processus enfant(s) zombie(s) (fork -> case 0)
			case SIGCHLD :
			// Écriture dans tube où il n'y a plus de lecteur(s)
			case SIGPIPE :
			// Les signaux de fin : terminaison du processus
			// SIGINT : Interruption depuis le clavier
			case SIGINT :
			// SIGHUP : Indication que la connexion avec le terminal a été rompue
			case SIGHUP :
			// SIGTERM : Demande de terminaison du processus
			case SIGTERM :
			case SIGUSR1 :
			case SIGUSR2 :
			// Le signal Core : terminaison du processus avec sauvegarde de son contexte d’exécution dans un fichier core
			// SIGQUIT : Demande de fin depuis le clavier
			case SIGQUIT :
			// Le signal stop du processus
			// SIGTSTP : Arrêt depuis le terminal (CTRL+Z)
			case SIGTSTP : {
				// Enlève du masque de la struct action, les signaux à traiter pour ne pas les bloquer
				if (sigdelset(&action.sa_mask, signum) == -1) {
					fprintf(stderr, "Erreur sigdelset pour le signal %d", signum);
					exit(EXIT_FAILURE);
				}
				// Association du signal à la struct action
				if (sigaction(signum, &action, NULL) == -1) {
					perror("sigaction");
					exit(EXIT_FAILURE);
				}
				break;
			}
			default : {}
		}
	}
	/*--------------------------------------------*/
	/* TEST UN SEUL PROCESSUS DAEMON EN ÉXÉCUTION */
	/*--------------------------------------------*/
	// Pour lancer que un seul démon (si le fichier(/dev/shm/VAR_ONE_RUNNING_DAEMON) existe alors il y a déja un processus daemon.c en cours d'éxécution)
	sem_t *one_running_daemon = sem_open(VAR_ONE_RUNNING_DAEMON, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (one_running_daemon == SEM_FAILED) {
		fprintf(stderr, "Un processus démon est déja en cours d'éxécution, veuillez le fermer avant d'en lançer un nouveau\n");
		fprintf(stderr, "Si aucun processus démon n'est en cours d'éxécution, c'est une erreur, alors supprimer le sémaphore du démon situé dans le /dev/shm afin de ne plus avoir ce message d'erreur\n");
		exit(EXIT_FAILURE);
	}
	/*-------------------------------------*/
	/* LECTURE DU FICHIER DE CONFIGURATION */
	/*-------------------------------------*/
	if (argc != 2) {
		fprintf(stderr, "Usage : %s <fichier_de_configuration>\n", argv[0]);
		// Supprime le sémaphore démon crée précédemment en cas d'erreur
		if (sem_unlink(VAR_ONE_RUNNING_DAEMON) == -1) { perror("sem_unlink one_running_daemon"); }
		exit(EXIT_FAILURE);
	}
	if (argc == 2) {
		const char *extension = ".config";
		if (strncmp(get_file_extension(argv[1]), extension, strlen(argv[1])) == 0) {
			config_file = strndup(argv[1], strlen(argv[1]));
		} else {
			fprintf(stderr, "Erreur le fichier de configuration %s n'a pas l'extension \"%s\"\n", argv[1], extension);
			if (sem_unlink(VAR_ONE_RUNNING_DAEMON) == -1) { perror("sem_unlink one_running_daemon"); }
			exit(EXIT_FAILURE);
		}
	} else {
		config_file = strndup(DEFAULT_CONFIG_FILE, strlen(DEFAULT_CONFIG_FILE));
	}
	if (config_file == NULL) { perror("strndup config_file"); exit(EXIT_FAILURE); }
	const size_t size_struct_config = sizeof(struct config);
	conf = malloc(size_struct_config);
	if (conf == NULL) {
		fprintf(stderr, "Erreur d'allocation mémoire %lu pour la struct conf\n", size_struct_config);
		if (sem_unlink(VAR_ONE_RUNNING_DAEMON) == -1) { perror("sem_unlink one_running_daemon"); }
		exit(EXIT_FAILURE);
	}
	if (start_config(config_file, conf) == -1) {
		free(conf);
		free(config_file);
		if (sem_unlink(VAR_ONE_RUNNING_DAEMON) == -1) { perror("sem_unlink one_running_daemon"); }
		exit(EXIT_FAILURE);
	}
	// Affichage des noms
	printf("================================================\n");
	printf("      "COLOR_BOLD_YELLOW("VALEUR DES VARIABLES DE CONFIGURATION")"     \n");
	printf("================================================\n");
	printf("name_empty_semaphore : %s (length:%lu)\n",	conf->name_empty_semaphore,	strlen(conf->name_empty_semaphore));
	printf("name_full_semaphore : %s (length:%lu)\n",	conf->name_full_semaphore,	strlen(conf->name_full_semaphore));
	printf("name_mutex_semaphore : %s (length:%lu)\n",	conf->name_mutex_semaphore,	strlen(conf->name_mutex_semaphore));
	printf("name_shm : %s (length:%lu)\n",				conf->name_shm,				strlen(conf->name_shm));
	printf("buffer : %lu\n",							conf->buffer_size);
	printf("prefix_pipequestion : %s (length:%lu)\n",	conf->prefix_pipequestion,	strlen(conf->prefix_pipequestion));
	printf("prefix_pipeanswer : %s (length:%lu)\n",		conf->prefix_pipeanswer,	strlen(conf->prefix_pipeanswer));
	printf("timeout_daemon : %lu\n",					conf->timeout_daemon);
	printf("timeout_client : %lu\n",					conf->timeout_client);
	printf("================================================\n");
	printf("================================================\n");
	// Enregistrement des infos pour libération des ressources
	// Allocation de config->buffer_size structures car au maximum il doit y avoir que config->buffer_size structures
	const size_t size_pthread_alloc = sizeof(struct pthread_allocated) * conf->buffer_size;
	pthread_alloc = (struct pthread_allocated *) malloc(size_pthread_alloc);
	if (pthread_alloc == NULL) {
		fprintf(stderr, "Erreur d'allocation mémoire %lu pour la struct pthread_alloc\n", size_pthread_alloc);
		exit(EXIT_FAILURE);
	}
	for (size_t i = 0; i < conf->buffer_size; i++) {
		// Fixe les identifiants à -1 pour informer que les places sont libres
		pthread_alloc[i].id = -1;
	}
	// Si il y a eu une erreur sur la suppression lors du dernier lancement du programme, on supprime les fichiers (shm & sémaphores)
	shm_unlink(conf->name_shm);
	sem_unlink(conf->name_mutex_semaphore);
	sem_unlink(conf->name_empty_semaphore);
	sem_unlink(conf->name_full_semaphore);
	// Création du segment de mémoire partagée
	shm_fd = shm_open(conf->name_shm, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (shm_fd == -1) {
		perror("shm_open shm_fd");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	size_shm = (const size_t) (sizeof(struct fifo) + conf->buffer_size);
	if (ftruncate(shm_fd, (off_t) size_shm) == -1) {
		perror("ftruncate size_shm");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	// Création des sémaphores à partagées
	mutex = sem_open(conf->name_mutex_semaphore, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (mutex == SEM_FAILED) {
		perror("sem_open mutex");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	empty = sem_open(conf->name_empty_semaphore, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (empty == SEM_FAILED) {
		perror("sem_open empty");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	full = sem_open(conf->name_full_semaphore, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 0);
	if (full == SEM_FAILED) {
		perror("sem_open full");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	// Initialisation des sémaphores
	if (sem_init(mutex, 1, 1) == -1) {
		perror("sem_init");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	if (sem_init(empty, 1, (unsigned int) conf->buffer_size) == -1) {
		perror("sem_init empty");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	if (sem_init(full, 1, 0) == -1) {
		perror("sem_init full");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	// Projection mémoire
	fifo_p = (struct fifo *) mmap(NULL, size_shm, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (fifo_p == MAP_FAILED) {
		perror("mmap");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	// Initialisation des positions tête et queue de la file
	fifo_p->head = 0;
	fifo_p->tail = 0;
	printf("[Daemon] Lancement du démon\n");
	// Consommateur : récupère le pid du client, puis lui attribue une ressource
	consumer();
	/*----------------------------------------------*/
	/*			LIBÉRATION DES RESSOURCES			*/
	/*----------------------------------------------*/
	// Ne devrais pas arriver ici car while(1)
	printf("[Daemon] Éxécution de la libération des ressources\n");
	dispose_all();
	return EXIT_SUCCESS;
}

/**
	* Libère les ressources occupées
*/
void dispose_all() {
	const char *msg;
	if (conf == NULL || pthread_alloc == NULL) {
		msg = "[Client] Toutes les ressources servant à la communication non pas été libérées car aucune configuration n'a été initialisée\n";
		printf("%s", msg);
		// write : async signal safe
		if (write(STDOUT_FILENO, msg, sizeof(char) * strlen(msg)) == -1) {
			const char *msg_error = "Erreur write\n";
			if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
				// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
				perror("write");
			}
		}
		return;
	}
	// Libére les descripteurs de fichiers sur les tubes qui sont utilisées
	for (size_t i = 0; i < conf->buffer_size; i++) {
		if (pthread_alloc[i].id != -1) {
			if (close(pthread_alloc[i].fdquestion)	== -1)	{ perror("close fdquestion");	}
			if (close(pthread_alloc[i].fdanswer)	== -1)	{ perror("close fdanswer");		}
		}
	}
	// Ferme le descripteur de fichier associé au shm et le supprime grâce à son nom
	if (close(shm_fd) == -1)							{ perror("close shm_fd");						}
	if (shm_unlink(conf->name_shm) == -1)				{ perror("shm_unlink name_shm");				}
	// Ferme les sémaphores full, empty et mutex
	if (sem_close(mutex)	== -1)						{ perror("sem_close mutex");					}
	if (sem_close(empty)	== -1)						{ perror("sem_close empty");					}
	if (sem_close(full)		== -1)						{ perror("sem_close full");						}
	// Supprime les sémaphores ()
	if (sem_unlink(VAR_ONE_RUNNING_DAEMON) == -1)		{ perror("sem_unlink one_running_daemon");		}
	if (sem_unlink(conf->name_mutex_semaphore) == -1)	{ perror("sem_unlink name_mutex_semaphore");	}
	if (sem_unlink(conf->name_empty_semaphore) == -1)	{ perror("sem_unlink name_empty_semaphore");	}
	if (sem_unlink(conf->name_full_semaphore) == -1)	{ perror("sem_unlink name_full_semaphore");		}
	// Supprime la projection mémoire
	if (munmap(fifo_p, size_shm) == -1)					{ perror("unmap");								}
	if (pthread_alloc != NULL) {
		free(pthread_alloc);
		pthread_alloc = NULL;
	}
	msg = "[Daemon] Toutes les ressources servant à la communication ont été libérées\n";
	if (write(STDOUT_FILENO, msg, sizeof(char) * strlen(msg)) == -1) {
		const char *msg_error = "Erreur write\n";
		if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
			// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
			perror("write");
		}
	}
	// Libére la struct de configuration ainsi que le nom du fichier de configuration
	if (config_file != NULL) {
		free(config_file);
		config_file = NULL;
	}
	if (conf != NULL) {
		free(conf->name_empty_semaphore);
		free(conf->name_full_semaphore);
		free(conf->name_mutex_semaphore);
		free(conf->name_shm);
		free(conf->prefix_pipequestion);
		free(conf->prefix_pipeanswer);
		free(conf);
		conf = NULL;
	}
	msg = "[Daemon] Toutes les ressources servant à la configuration ont été libérées\n";
	if (write(STDOUT_FILENO, msg, sizeof(char) * strlen(msg)) == -1) {
		const char *msg_error = "Erreur write\n";
		if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
			// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
			perror("write");
		}
	}
}

/**
	* Méthode consommer de l'algorithme producteur/consommateur
	* Retire de la file du segment partagée un pid qui est celui du client
*/
void consumer() {
	data_t pid;
	do {
		// Bloque si la file du shm est vide (car il y a rien à retirer)
		P(full);
		// Exclusion mutuelle pour la section critique
		P(mutex);
		// Section critique : retire le client(pid) et lui alloue une ressource
		pid = remove_element(fifo_p, conf->buffer_size);
		printf("[Daemon] Le client %d a envoyé une requête pour l'allocation d'une ressource\n", pid);
		// Vérifie qu'il y a un emplacement libre dans la struct pthread_alloc qui sert à libérer les ressources
		int is_found = 0;
		for (size_t i = 0; i < conf->buffer_size; i++) {
			if (pthread_alloc[i].id == -1) {
				is_found = 1;
				break;
			}
		}
		// S'il n'y a pas d'emplacement libre, c'est qu'il y a eu une erreur lors de la recherche et l'affectation d'une place libre
		if (is_found == 0) {
			fprintf(stderr, "Erreur lors de la recherche de trouvaille d'emplacement pour le client %d\n", pid);
			exit(EXIT_FAILURE);
		} else {
			// S'il y a un emplacement libre, alors allocation d'une ressource au client
			// Création du thread en mode détaché (pour libérer les ressources lorsqu'il a terminé son travail)
			pthread_t th;
			pthread_attr_t attr;
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			int errnum;
			if ((errnum = pthread_create(&th, &attr, run, &pid)) != 0) {
				fprintf(stderr, "Erreur pthread_create: %s\n", strerror(errnum));
				exit(EXIT_FAILURE);
			} else {
				printf("[Daemon] Allocation d'une ressource au client %d\n", pid);
			}
		}
		V(mutex);
	} while (1);
}


/**
	* @param arg : pid du client
	* La routine du thread (la fonction d'éxécution du thread)
	* Ouvre le tube de question et de réponse et alloue une ressource au client
*/
void * run(void * arg) {
	// Récupération du pid
	data_t pid = *((data_t *) arg);
	// Récupération des noms des tubes
	char *pipe_question = NULL;
	set_filename_pid(&pipe_question, conf->prefix_pipequestion, pid);
	char *pipe_answer = NULL;
	set_filename_pid(&pipe_answer, conf->prefix_pipeanswer, pid);
	// Ouvertures des tubes question et answer
	// Serveur, reçoit une question seulement en lecture
	const int fdquestion = open(pipe_question, O_RDONLY);
	if (fdquestion == -1) { perror("daemon : open fdquestion"); exit(EXIT_FAILURE); }
	printf("[Daemon][Client:%d] Connexion au tube(%s) de réception de requête(question) effectuée\n", pid, pipe_question);
	free(pipe_question);
	// Serveur, envoie une réponse seulement en écriture
	const int fdanswer = open(pipe_answer, O_WRONLY);
	if (fdanswer == -1) { perror("daemon : open fdanswer"); exit(EXIT_FAILURE); }
	printf("[Daemon][Client:%d] Connexion au tube(%s) d'envoi de requête(answer) effectuée\n", pid, pipe_answer);
	free(pipe_answer);
	
	// Stocke les valeurs pour les utiliser lors de la libération de ressources
	size_t i;
	for (i = 0; i < conf->buffer_size; i++) {
		if (pthread_alloc[i].id == -1) {
			pthread_alloc[i].id 			= pid;
			pthread_alloc[i].fdquestion		= fdquestion;
			pthread_alloc[i].fdanswer		= fdanswer;
			break;
		}
	}
	// Lecture de la requête
	char buf_request[PIPE_BUF];
	size_t count_request = PIPE_BUF;
	ssize_t n_request;
	// Lecture de la réponse de la requête
	char buf_result[PIPE_BUF];
	size_t count_result = PIPE_BUF;
	ssize_t n_result;
	
	// Timeout
	fd_set rfds;
	struct timeval tv;
	/* Surveille fdquestion en attente d'entrées */
	FD_ZERO(&rfds);
	FD_SET(fdquestion, &rfds);
	/* Surveille pendant conf->timeout_daemon seconde(s) */
	tv.tv_sec = (long int) conf->timeout_daemon;
	tv.tv_usec = 0;
	int retval = select(fdquestion + 1, &rfds, NULL, NULL, &tv);
	if (retval == -1) {
		perror("select");
	} else if (retval == 0) {
		pthread_alloc[i].id = -1;
		pthread_alloc[i].fdquestion	= -1;
		pthread_alloc[i].fdanswer	= -1;
		printf("[Demon] Délai d'attente d'une requête dépassé pour le client %d\n", pid);
		const char *msg = "Votre délai d'attente est dépassé, la connexion avec le démon va être fermer\n";
		if (write(fdanswer, msg, sizeof(char) * strlen(msg)) == -1) {
			perror("write");
			if (kill(pthread_alloc[i].id, SIGINT) == -1) {
				perror("kill");
			}
		}
	} else {
		// Lecture du tube question où le client a entrée une requête
		while ((n_request = read(fdquestion, &buf_request, count_request)) > 0) {
			buf_request[n_request] = '\0';
			char *time = get_time_formatted();
			printf(COLOR_NORMAL_CYAN("%s")COLOR_NORMAL_YELLOW("[Client:%d] :")" %s", time, pid, buf_request);
			fflush(stdout);
			free(time);
			// Traitement de la requête
			if (strncmp(buf_request, "quit", sizeof(char) * strlen("quit")) == 0) {
				printf("[Daemon] Le client %d a mis fin la communication\n", pid);
				// Informe qu'une place est libre
				pthread_alloc[i].id = -1;
				pthread_alloc[i].fdquestion	= -1;
				pthread_alloc[i].fdanswer	= -1;
				break;
			} else {
				// Création d'un tube anonyme pour la réception du résultat de la requête
				int pipefd[2];
				if (pipe(pipefd) == -1) {
					perror("pipe");
					exit(EXIT_FAILURE);
				}
				// Traitement de la requête
				processing_request(buf_request, pipefd);
				if (close(pipefd[1]) == -1) {
					perror("close pipefd[1]");
					exit(EXIT_FAILURE);
				}
				// Si il y a une réponse d'écrite par le thread, alors écrit dans le tube réponse
				if ((n_result = read(pipefd[0], &buf_result, count_result)) > 0) {
					buf_result[n_result] = '\0';
					if (write(fdanswer, buf_result, sizeof(char) * strlen(buf_result)) == -1) {
						perror("write");
					}
				}
				if (close(pipefd[0]) == -1) {
					perror("close pipefd[0]");
					exit(EXIT_FAILURE);
				}
			}
			fflush(stdout);
			fflush(stderr);
			FD_ZERO(&rfds);
			FD_SET(fdquestion, &rfds);
			/* Surveille pendant conf->timeout_daemon seconde(s) */
			tv.tv_sec = (long int) conf->timeout_daemon;
			tv.tv_usec = 0;
			retval = select(fdquestion + 1, &rfds, NULL, NULL, &tv);
			if (retval == -1) {
				perror("select");
			} else if (retval == 0) {
				const pid_t pid_old = pthread_alloc[i].id;
				const int fdanswer_old = pthread_alloc[i].fdanswer;
				pthread_alloc[i].id = -1;
				pthread_alloc[i].fdquestion	= -1;
				pthread_alloc[i].fdanswer	= -1;
				printf("[Demon] Délai d'attente dépassé pour le client %d\n", pid);
				if (kill(pid_old, SIGINT) == -1) {
					perror("kill");
					const char *msg = "quit";
					if (write(fdanswer_old, msg, sizeof(char) * strlen(msg)) == -1) {
						perror("write");
					}
				}
				break;
			}
		}
	}
	printf("[Daemon] Il n'y a plus rien à lire dans le tube de requête(question) du client %d\n", pid);
	
	// Ferme les descripteurs de fichiers des tubes question et answer associé au thread alloué
	if (close(fdquestion) == -1)	{ perror("close fdquestion");	}
	if (close(fdanswer) == -1)		{ perror("close fdanswer");		}
	// Informe le producteur(client) que un client libère une place
	V(empty);
	return NULL;
}

/**
	* @param buf : la commande
	* pipe : tube anonyme pour écrire et lire
	* Traitement de la requête du client
*/
void processing_request(const char buf[], int pipe[]) {
	if (buf == NULL) {
		fprintf(stderr, "Erreur la requête est vide\n");
		return;
	}
	// proc_info 1000
	// proc_user 1200
	// unix autorise les espaces dans les noms d’utilisateur ainsi que dans les noms de fichiers.
	// ls valeur -a -v -d -f
	char *copy = strndup(buf, strlen(buf));
	const char *separator_cmd = " ";
	const char *separator_opt = " ";
	
	// Le nombre d'argument minimal est la commande après on peut réallouer (realloc)
	const size_t minarg = (size_t) 1;
	char **argv = (char **) malloc(sizeof(char *) * minarg);
	if (argv == NULL) {
		fprintf(stderr, "Erreur d'allocation mémoire %lu pour la struct argv\n", minarg);
		exit(EXIT_FAILURE);
	}
	
	// Traitement de la commande envoyée
	// Utilisation de strtok_r car strtok n'est pas une fonction réentrante (thread unsafe)
	char *saveptr;
	char *token = strtok_r(copy, separator_cmd, &saveptr);
	// printf("Commande : %s\n", token);
	if (token == NULL) {
		perror("token");
	} else {
		// Récupère la commande
		const size_t size_namecmd = sizeof(char) * strlen(token) + 1;
		argv[0] = malloc(size_namecmd);
		if (argv[0] == NULL) {
			fprintf(stderr, "Erreur d'allocation mémoire %lu pour argv[0]\n", size_namecmd);
			exit(EXIT_FAILURE);
		}
		trim(token);
		argv[0] = token;
		// printf("argv[0] vaut : %s (size : %ld)\n", argv[0], strlen(argv[0]));
		
		// Récupère les valeurs ou les options de la commande
		token = strtok_r(NULL, separator_opt, &saveptr);
		size_t ind = 1;
		size_t resize_argv;
		size_t size_argv_ind;
		while (token != NULL) {
			resize_argv = sizeof(char *) * (size_t) (ind + 1);
			argv = (char **) realloc(argv, resize_argv);
			size_argv_ind = sizeof(char) * strlen(token) + 1;
			argv[ind] = malloc(size_argv_ind);
			if (argv[ind] == NULL) {
				fprintf(stderr, "Erreur d'allocation mémoire %lu pour argv[%lu]\n", size_argv_ind, ind);
				exit(EXIT_FAILURE);
			}
			trim(token);
			if (strlen(token) == 0) {
				// Remplace le '\0' par un caractère quelconque
				token = NULL;
			}
			argv[ind] = token;
			// printf("argv[%lu] vaut : %s", ind, argv[ind]);
			// printf("opt : %s\n", token);
			token = strtok_r(NULL, separator_opt, &saveptr);
			ind++;
		}
		argv = (char **) realloc(argv, sizeof(char *) * (size_t) (ind + 1));
		argv[ind] = NULL;
		
		char type = '\0';
		if (strstr(buf, "info_proc") != NULL) {
			type = 'P';
		} else if (strstr(buf, "info_user") != NULL) {
			type = 'U';
		}
		do_subshell(type, (const char * const *) argv, pipe);
	}
	free(token);
	free(argv);
	free(copy);
}

/**
	* @param type : 'P' info_proc <pid>, 'U' info_user <uid/nom_utilisateur>, défault : commande usuelle (exemple : ls -l)
	* @return -1 erreur, 0 succès
	* Créer un processus enfant pour éxécuter la commande, le processus parent attend que l'enfant se termine afin de récupère la commande
*/
void do_subshell(const char type, const char * const argv[], int pipe[]) {
	pid_t pid = fork();
	int status;
	switch (pid) {
		case -1 : {
			perror("fork");
			exit(EXIT_FAILURE);
		}
		case 0 : {
			// Relie la sortie standard du processus enfant au tube STDOUT
			if (dup2(pipe[1], STDOUT_FILENO) == -1) {
				perror("dup2");
				exit(EXIT_FAILURE);
			}
			// Relie la sortie d'erreur du processus enfant au tube STDOUT (pour afficher le(s) erreur(s) chez le client)
			if (dup2(pipe[1], STDERR_FILENO) == -1) {
				perror("dup2");
				exit(EXIT_FAILURE);
			}
			// Ferme l'entrée du tube pour y écrire
			if (close(pipe[0]) == -1) {
				perror("close pipe[0]");
				exit(EXIT_FAILURE);
			}
			// Traitement de la commande
			if (type == 'P') {
				if (argv[1] == NULL) {
					fprintf(stderr, "Erreur sur la commande info_proc, Usage : info_proc <pid>\n");
					exit(EXIT_FAILURE);
				}
				if (is_numeric(argv[1]) == 0) {
					// printf("argv[1] : %s , - isnum %d - argv[2]  : %s isnum %d", argv[1], is_numeric(argv[1]), argv[2], is_numeric(argv[2]));
					pid_t pid = (pid_t) atoi(argv[1]);
					if (info_proc(pid) == -1) {
						exit(EXIT_FAILURE);
					}
				} else {						
					fprintf(stderr, "Erreur sur l'argument pid doit être un pid (exemple : info_proc 1000)\n");
					exit(EXIT_FAILURE);
				}
			} else if (type == 'U') {
				if (argv[1] == NULL) {
					fprintf(stderr, "Erreur sur la commande info_user, Usage : info_user <uid/nom_utilisateur>\n");
					exit(EXIT_FAILURE);
				}
				if (argv[1] != NULL && is_numeric(argv[1]) != -1) {
					uid_t uid = (uid_t) atoi(argv[1]);
					if (info_user('U', uid, NULL) == -1) {
						exit(EXIT_FAILURE);
					}
				} else {
					if (info_user('P', 0, argv[1]) == -1) {
						exit(EXIT_FAILURE);
					}
				}
			} else {
				// Ex : const char* const argv[] = {"ps", "axu", NULL};
				execvp(argv[0], (char * const *) argv);
				perror("execvp");
				exit(EXIT_FAILURE);
			}
			break;
		}
		default : {
			// Attente du processus fils qu'il termine le traitement de la requête
			// Les zombies ne pas sont traités avec le vaccin des zombies : action.sa_flags = SA_NOCLDWAIT; avec SIGCHLD
			// Mais avec le wait ci-dessous
			if(waitpid(pid, &status, 0) == -1) {
				perror("wait");
			}
			if (WIFEXITED(status)) {
				// printf("Le processus fils %d est mort normalement en retournant %d\n", pid, WEXITSTATUS(status));
				if (WEXITSTATUS(status) != 0) {
					fprintf(stderr, "Une erreur est survenue lors de l'éxécution de la commande %s\n", argv[0]);
				}
			}
			break;
		}
	}
}


/**
	* @param pid : pid du processus
	* @return -1 erreur, 0 succès
	* Affiche sur la sortie standard les informations d'un processus dont le pid est passé en paramètre
*/
int info_proc(pid_t pid) {
	char *namefile = NULL;
	// Affecte un nom de fichier
	set_filename_pid(&namefile, "/proc/%d/status", pid);
	// Ouverture du fichier contenant les informations sur le processus
	FILE *file = fopen(namefile, "r");
	if (file == NULL) {
		fprintf(stderr, "Impossible d'ouvrir le fichier %s\n", namefile);
		return -1;
	} else {
		const size_t MAX_LENGTH = MAX_LENGTH_LINE;
		char buf[MAX_LENGTH];
		// Informations à rechercher dans le fichier
		const char *name_info[] = { "Name", "State", "TGID", "PPid", "Pid" };
		const size_t length = (size_t) (sizeof(name_info)/sizeof(name_info[0]));
		// Lecture du fichier /proc/pid/status
		while (fgets(buf, MAX_LENGTH, file) != NULL) {
			for (size_t i = 0; i < length; i++) {
				if (strncmp(buf, name_info[i], sizeof(char) * strlen(name_info[i])) == 0) {
					printf("%s", buf);
					break;
				}
			}
		}
		if (fclose(file) != 0) {
			perror("fclose");
			return -1;
		}
	}
	free(namefile);
	return 0;
}

/**
	* @param type : 'U' userid, 'P' username
	* @param uid : user ID
	* @param name : user name
	* @return -1 erreur, 0 succès
	* Affiche sur la sortie standard les informations de l'utilisateur grâce à uid ou au nom de l'utilisateur
*/
int info_user(const char type, uid_t uid, const char *name) {
	struct passwd *p = NULL;
	if (type == 'U') {
		p = getpwuid(uid);
	} else if (type == 'P') {
		if (name == NULL) {
			fprintf(stderr, "Erreur le nom de l'utilisateur est vide\n");
			return -1;
		} else {
			p = getpwnam(name);
		}
	}
	if (p != NULL) {
		const size_t size = (size_t) 4096;
		char str[size];
		const char *format;
		const char *s_value;
		int i_value;
		format = "Nom d'utilisateur               : %s\n";
		s_value = p->pw_name;
		size_t length = sizeof(char) * (strlen(format) + strlen(s_value) + 1);
		if (snprintf(str, length, format, s_value) == -1) {
			fprintf(stderr, "Erreur snprintf %s", format);
			return -1;
		}
		format = "Identifiant utilisateur         : %lu\n";
		i_value = (int) p->pw_uid;
		length = size - (sizeof(char) * strlen(str));
		if (snprintf(str + sizeof(char) * strlen(str), length, format, i_value) == -1) {
			fprintf(stderr, "Erreur snprintf %s", format);
			return -1;
		}
		format = "Mot de passe de l'utilisateur   : %s\n";
		s_value = p->pw_passwd;
		length = size - (sizeof(char) * strlen(str));
		if (snprintf(str + sizeof(char) * strlen(str), length, format, s_value) == -1) {
			fprintf(stderr, "Erreur snprintf %s", format);
			return -1;
		}
		format = "Groupe de l'utilisateur         : %d\n";
		i_value = (int) p->pw_gid;
		length = size - (sizeof(char) * strlen(str));
		if (snprintf(str + sizeof(char) * strlen(str), length, format, i_value) == -1) {
			fprintf(stderr, "Erreur snprintf %s", format);
			return -1;
		}
		format = "Information sur l'utilisateur   : %s\n";
		s_value = p->pw_gecos;
		length = size - (sizeof(char) * strlen(str));
		if (snprintf(str + sizeof(char) * strlen(str), length, format, s_value) == -1) {
			fprintf(stderr, "Erreur snprintf %s", format);
			return -1;
		}
		format = "Répertoire home                 : %s\n";
		s_value = p->pw_dir;
		length = size - (sizeof(char) * strlen(str));
		if (snprintf(str + sizeof(char) * strlen(str), length, format, s_value) == -1) {
			fprintf(stderr, "Erreur snprintf %s", format);
			return -1;
		}
		format = "Programme shell                 : %s\n";
		s_value = p->pw_shell;
		length = size - (sizeof(char) * strlen(str));
		if (snprintf(str + sizeof(char) * strlen(str), length, format, s_value) == -1) {
			fprintf(stderr, "Erreur snprintf %s", format);
			return -1;
		}
		/*
			printf("Nom d'utilisateur               : %s\n", p->pw_name);
			printf("Identifiant utilisateur         : %d\n", (int) p->pw_uid);
			printf("Mot de passe de l'utilisateur   : %s\n", p->pw_passwd);
			printf("Groupe de l'utilisateur         : %d\n", (int) p->pw_gid);
			printf("Information sur l'utilisateur   : %s\n", p->pw_gecos);
			printf("Répertoire home                 : %s\n", p->pw_dir);
			printf("Programme shell                 : %s\n", p->pw_shell);
		*/
		trim(str);
		printf("%s\n", str);
	} else {
		fprintf(stderr, "Erreur aucun uid ou nom de l'utilisateur n'a été trouvé\n");
		return -1;
	}
	return 0;
}

/**
	* @param signum : le numéro du signalre
	* Gestionnaire de signaux
	* Pas de printf dans le handler car printf n'est pas une fonction sûre donc write car c'est une fonction sûre (async-signal-safe)
	* Fonction async-signal-safe : man 7 signal
*/
void handler(int signum) {
	// Signaux de demande de terminaison
	switch (signum) {
		case SIGCHLD : {
				// On sauvegarde errno pour pouvoir la restaurer à la fin du gestionnaire.
				int errno_old = errno;
				// Comme plusieurs fils ont pu mourir avant que l'ordonnanceur
				// ne nous redonne la main, plusieurs signaux SIGCHLD ont pu
				// nous être adressés.
				// Comme lorsque le processus est élu par l'ordonnanceur un seul
				// signal SIGCHLD sera finalement traité, il faut faire une boucle
				// pour retirer tous les fils zombies.
				int r; // Valeur de retour du wait
				do {
					// Comme on ne sait combien il y a de zombis, on
					// fait des wait non bloquants jusqu'à ce qu'il n'y ai plus de zombies.
					r = waitpid(-1, NULL, WNOHANG);
				} while (r > 0);
				// Si r = 0, il y a des fils toujours vivant mais plus de zombies
				// Si r = -1, soit il y a une erreur, soit il n'y a plus de zombies
				if (r == -1) {
					// Si wait a retourné parcequ'il n'y a plus de zombies, alors
					// errno vaut ECHILD
					if (errno != ECHILD) {
						const char *msg_error = "waitpid\n";
						if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
							// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
							perror("waitpid");
						}
						exit(EXIT_FAILURE);
					}
				}
				errno = errno_old;
				break;
		}
		case SIGALRM :
		case SIGPIPE :
		case SIGINT :
		case SIGHUP :
		case SIGTERM :
		case SIGUSR1 :
		case SIGUSR2 :
		case SIGQUIT :
		case SIGTSTP : {
			int status = EXIT_SUCCESS;
			const char *msg = "\n[Daemon] signal de terminaison reçu\n";
			if (write(STDOUT_FILENO, msg, sizeof(char) * strlen(msg)) == -1) {
				const char *msg_error = "Erreur write\n";
				if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
					// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
					perror("write");
					status = EXIT_FAILURE;
				}
			}
			// Informe les clients que le démon doit s'éteindre
			// Si signum == SIGPIPE, pas d'écriture dans le tube ('\0'), sinon peut être écriture tube ('P')
			char type = (signum == SIGPIPE) ? '\0' : 'P';
			status = send_request_close(type) == EXIT_FAILURE ? EXIT_FAILURE : status;
			// Libération des ressources
			dispose_all();
			msg = "[Daemon] Fermeture du demon\n";
			if (write(STDOUT_FILENO, msg, sizeof(char) * strlen(msg)) == -1) {
				const char *msg_error = "Erreur write\n";
				if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
					// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
					perror("write");
					status = EXIT_FAILURE;
				}
			}
			exit(status);
		}
		default :
			break;
	}
}

/**
	* @param type : envoie un signal de fermeture au client, si type 'P' ça écrit aussi dans le tube en cas d'erreur d'envoie du signal de terminaison
	* Requête de type fermeture : informe les clients qu'ils doivent s'éteindre car le démon s'éteint
*/
int send_request_close(const char type) {
	int returnval = EXIT_SUCCESS;
	for (size_t i = 0; i < conf->buffer_size; i++) {
		if (pthread_alloc[i].id != -1) {
			// Envoie un signal SIGINT pour informer que le client doit fermer
			if (kill(pthread_alloc[i].id, SIGINT) == -1) {
				const char *msg_error = "Erreur kill";
				if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
					// Tente avec perror pour informer même si ce n'est pas async-signal-safe
					perror("write");
					returnval = EXIT_FAILURE;
				}
				// Ecriture dans le PIPE (tube)
				if (type == 'P') {
					// Si l'envoi du signal échoue alors, tentative d'informer via le tube de réponse (answer)
					const char *msg_quit = "quit";
					if (write(pthread_alloc[i].fdanswer, msg_quit, sizeof(char) * strlen(msg_quit)) == -1) {
						perror("write");
						fprintf(stderr, "Erreur lors de l'envoie du message de fermerture pour le client %d\n", pthread_alloc[i].id);
						returnval = EXIT_FAILURE;
					}
				}
			}
		}
	}
	return returnval;
}
