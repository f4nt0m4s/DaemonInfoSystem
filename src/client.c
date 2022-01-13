#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
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
 * Fichier client.c
 * Compilation 					: gcc -c client.c
 * Génération de l'éxécutable 	: gcc -o client client.o -pthread -lrt
 * Exécution : ./client
*/

/*--------------*/
/*	PROTOTYPES	*/
/*--------------*/
// Segment de mémoire partagée (SHM)
void producer(void);
// Tubes
void communicate_pipe(void);
// Signal
void handler(int signum);
// Libération des ressources
void dispose_config(void);
void dispose_tools(void);
void dispose_all(void);
int close_on_signal(const char type);

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
// Tubes
char *pipe_question = NULL;
char *pipe_answer = NULL;
int fdquestion;
int fdanswer;


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
	action.sa_flags = 0;
	for (int signum = 0; signum < SIGRTMAX; signum++) {
		switch (signum) {
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
	if (one_running_daemon != SEM_FAILED) {
		fprintf(stderr, "Aucun processus démon n'a encore été éxécuté, fermeture du client...\n");
		sem_unlink(VAR_ONE_RUNNING_DAEMON);
		exit(EXIT_FAILURE);
	}
	/*-------------------------------------*/
	/* LECTURE DU FICHIER DE CONFIGURATION */
	/*-------------------------------------*/
	// Lecture des données de configuration
	if (argc != 2) {
		fprintf(stderr, "Usage : %s <configuration_file>", argv[0]);
		exit(EXIT_FAILURE);
	}
	if (argc == 2) {
		config_file = strndup(argv[1], strlen(argv[1]));
	} else {
		config_file = strndup(DEFAULT_CONFIG_FILE, strlen(DEFAULT_CONFIG_FILE));
	}
	const size_t size_struct_config = sizeof(struct config);
	conf = (struct config *) malloc(size_struct_config);
	if (conf == NULL) {
		fprintf(stderr, "Erreur d'allocation mémoire %lu pour la struct conf\n", size_struct_config);
		exit(EXIT_FAILURE);
	}
	if (start_config(config_file, conf) == -1) {
		free(conf);
		free(config_file);
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
	printf("antiflood_client : %lu\n",					conf->antiflood_client);
	printf("================================================\n");
	printf("================================================\n");
	// Test si le fichier a été configuré de sorte qu'il y est personne dans la file du shm
	if (conf->buffer_size == 0) {
		printf("[Client] Aucun client n'est admissible (Taille du segment de mémoire partagée : %lu)\n", conf->buffer_size);
		dispose_config();
		exit(EXIT_FAILURE);
	}
	/*--------------*/
	/*		SHM		*/
	/*--------------*/
	// Descripteur de fichier pour récupérer le shm déja créé par daemon.c
	shm_fd = shm_open(conf->name_shm, O_RDWR, S_IRUSR | S_IWUSR);
	if (shm_fd == -1) {
		fprintf(stderr, "Le segment de mémoire partagée est inexistant\n");
		perror("shm_open");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	/*----------------------*/
	/* 		SÉMAPHORES		*/
	/*----------------------*/
	// Ouverture des sémaphores partagées par consommateur.c
	mutex = sem_open(conf->name_mutex_semaphore, O_RDWR);
	if (mutex == SEM_FAILED) {
		perror("sem_open");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	empty = sem_open(conf->name_empty_semaphore, O_RDWR);
	if (empty == SEM_FAILED) {
		perror("sem_open");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	full = sem_open(conf->name_full_semaphore, O_RDWR);
	if (full == SEM_FAILED) {
		perror("sem_open");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	// Projection mémoire
	size_shm = (const size_t) (sizeof(struct fifo) + conf->buffer_size);
	fifo_p = (struct fifo *) mmap(NULL, size_shm, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	if (fifo_p == MAP_FAILED) {
		perror("sem_open");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	/*--------------*/
	/*		PIPE	*/
	/*--------------*/
	pid_t pid = getpid();
	// Création des noms des tubes en associant le nom au pid du processus client
	set_filename_pid(&pipe_question, conf->prefix_pipequestion, pid);
	set_filename_pid(&pipe_answer, conf->prefix_pipeanswer, pid);
	// Suppression s'il existe déja
	unlink(pipe_question);
	unlink(pipe_answer);
	// Création des tubés nommés
	if (mkfifo(pipe_question, S_IRUSR | S_IWUSR) == -1) {
		perror("mkfifo question");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	printf("[Client] Création du tube %s pour l'envoi de requête\n", pipe_question);
	if (mkfifo(pipe_answer, S_IRUSR | S_IWUSR) == -1) {
		perror("mkfifo answer");
		dispose_all();
		exit(EXIT_FAILURE);
	}
	printf("[Client] Création du tube %s pour la réception de requête\n", pipe_answer);
	// Producteur : envoie son pid dans le shm
	producer();
	// Communication
	communicate_pipe();
	// Libération des ressources utilisés
	dispose_all();
	return EXIT_SUCCESS;
}

/**
	* Méthode produire de l'algorithme producteur/consommateur
	* Récupère le pid du client et le place son pid dans la file du segment de mémoire partagée 
*/
void producer() {
	data_t pid;
	// Récupère le pid du client pour l'enfiler dans la file de pid (shm)
	pid = getpid();
	// Sémaphores
	printf("En attente d'attribution d'une ressource...\n");
	// Récupère la valeur du sémaphore empty pour savoir si la file du shm est pleine
	int sem_empty_value;
	if (sem_getvalue(empty, &sem_empty_value) == -1) {
		perror("sem_getvalue empty");
		exit(EXIT_FAILURE);
	}
	// Si la file vaut 0, c'est que toutes les places dans la file du shm sont prises
	if (sem_empty_value == (int) 0) {
		printf("La file est pleine pour le moment, il faut attendre que une place se libère\n");
	}
	// Bloque si la file du shm est pleine (car il n'y a plus de place pour ajouter)
	P(empty);
	// Exclusion mutuelle pour la section critique
	P(mutex);
	// Section critique : ajout du client dans le shm
	add_element(fifo_p, pid, conf->buffer_size);
	V(mutex);
	// Informe le consommateur(daemon) que la file vient de reçevoir un nouveau client
	V(full);
	printf("Le démon vous a attribué une ressource\n");
}

/**
	* Communication avec le démon
	* Ouvre les tubes crées puis lit la commande écrite sur le terminal puis l'envoie au serveur
	* Se met en attente d'une réponse et l'affiche
*/
void communicate_pipe() {
	// Pour le client, ouverture du tube question en écriture seulement
	// Peut être pas besoin de dup2
	fdquestion = open(pipe_question, O_WRONLY);
	if (fdquestion == -1) { perror("client : open fdquestion"); exit(EXIT_FAILURE); }
	// Pour le client, ouverture du tube réponse en lecture seulement
	fdanswer = open(pipe_answer, O_RDONLY);
	if (fdanswer == -1) { perror("client :  open fdanswer"); exit(EXIT_FAILURE); }
	// Entrée de l'utilisateur
	char buf_input[PIPE_BUF];
	size_t count_input = PIPE_BUF;
	ssize_t n_input;
	
	// Lecture de la réponse
	char buf_answer[PIPE_BUF];
	size_t count_answer = PIPE_BUF;
	ssize_t n_answer;
	
	// Déclaration des vars du timeout
	fd_set rfds;
	struct timeval tv;
	/* Surveille fdquestion en attente d'entrées */
	FD_ZERO(&rfds);
	FD_SET(fdanswer, &rfds);
	/* Sureveille pendant conf->timeout_client seconde(s) */
	tv.tv_sec = (long int) conf->timeout_client;
	tv.tv_usec = 0;
	int retval;
	
	char *s_time = get_time_formatted();
	printf(COLOR_NORMAL_CYAN("%s")COLOR_NORMAL_YELLOW("[Client] : "), s_time);
	fflush(stdout);
	free(s_time);
	
	// Antiflood
	time_t begin = 0;
	time_t end = 0;
	unsigned long secondes;

	// En attente de requête du client pour la lire
	while ((n_input = read(STDIN_FILENO, &buf_input, count_input)) > 0) {
		buf_input[n_input] = '\0';
		// Antiflood
		end = time( NULL);
		secondes = (unsigned long) difftime(end, begin);
		if (secondes < conf->antiflood_client) {
			printf(COLOR_SLOW_BLINK_RED("[Anti-flood]")" Veuillez attendre encore %lu secs\n", conf->antiflood_client - secondes);	
			fflush(stdout);
			continue;
		}
		begin = time(NULL);
		// Si le client entre le message "quit" pour informer qu'il quitte l'application
		if (strncmp("quit", buf_input, strlen("quit")) == 0) {
			// Informe le démon que la ressource du client souhaitant quitter doit être libérée
			if (write(fdquestion, buf_input, sizeof(char) * strlen((char *) buf_input)) == -1) {
				perror("write");
				exit(EXIT_FAILURE);
			}
			// Envoie un signal de terminaison au client lui même
			if (kill(getpid(), SIGINT) == -1) {
				perror("kill SIGINT");
				dispose_all();
			}
			break;
		} else {
			// Envoie la commande souhaitée par le client dans le tube question
			if (write(fdquestion, buf_input, sizeof(char) * strlen((char *) buf_input)) == -1) {
				perror("write");
				exit(EXIT_FAILURE);
			} else {
				// Réception de la réponse de la commande
				// Timeout
				FD_ZERO(&rfds);
				FD_SET(fdanswer, &rfds);
				/* Sureveille pendant conf->timeout_client seconde(s) */
				tv.tv_sec = (long int) conf->timeout_client;
				tv.tv_usec = 0;
				retval = select(fdanswer + 1, &rfds, NULL, NULL, &tv);
				if (retval == -1) {
					perror("select");
				} else if (retval == 0) {
					printf("[Client] Délai d'attente d'une réponse du démon dépassé\n");
					// Informe le démon que la ressource du client souhaitant quitter doit être libérée
					if (write(fdquestion, "quit", sizeof(char) * strlen("quit")) == -1) {
						perror("write");
						exit(EXIT_FAILURE);
					}
					// Envoie un signal de terminaison au client lui même
					if (kill(getpid(), SIGINT) == -1) {
						perror("kill");
						exit(EXIT_FAILURE);
					}
				} else {
					// En attente de la réponse pour la lire (que une seule lecture)
					if ((n_answer = read(fdanswer, &buf_answer, count_answer)) > 0) {
						buf_answer[n_answer] = '\0';
						fflush(stdout);
						fflush(stderr);
						// Si le démon envoie le message "quit" pour informer qu'il quitte l'application
						if (strncmp("quit", buf_input, sizeof(char) * strlen("quit")) == 0) {
							// Envoie un signal de terminaison
							if (kill(getpid(), SIGINT) == -1) {
								// Si le signal n'a pas été envoyé, tente la libération direct avec dispose_all()
								perror("kill SIGINT");
								dispose_all();
							}
						} else {
							s_time = get_time_formatted();
							printf(COLOR_NORMAL_CYAN("%s")COLOR_NORMAL_YELLOW("[Daemon] :")"\n", s_time);
							free(s_time);
							printf("%s", buf_answer);
							fflush(stdout);
						}
					}
					if (n_answer == -1) {
						fprintf(stderr, "Erreur lors de la lecture du tube réponse\n");
						exit(EXIT_FAILURE);
					}
				}
			}
		}
		s_time = get_time_formatted();
		printf(COLOR_NORMAL_CYAN("%s")COLOR_NORMAL_YELLOW("[Client] : "), s_time);
		free(s_time);
		fflush(stdout);
	}
}

/**
	* @param signum : le numéro du signal
	* Utilisation que de fonctions async-signal safe (man 7 signal)
	* Gestionnaire de signaux
*/
void handler(int signum) {
	switch (signum) {
		// Plus d'écrivain dans le tube de réponse(answer)
		case SIGPIPE :
		// Signaux de demande de terminaison
		case SIGINT :
		case SIGHUP :
		case SIGTERM :
		case SIGUSR1 :
		case SIGUSR2 :
		case SIGQUIT :
		case SIGTSTP : {
			// Pas d'écriture dans le tube si réception du signal SIGPIPE
			char type = (signum == SIGPIPE) ? '\0' : 'P';
			int status = close_on_signal(type);
			exit(status);
		}
		default :
			break;
	}
}

/**
	* Libère les ressources servant à la configuration
*/
void dispose_config() {
	if (config_file != NULL) {
		free(config_file);
		config_file = NULL;
	}
	if (conf != NULL) {
		// strndup -> malloc
		free(conf->name_empty_semaphore);
		free(conf->name_full_semaphore);
		free(conf->name_mutex_semaphore);
		free(conf->name_shm);
		free(conf->prefix_pipequestion);
		free(conf->prefix_pipeanswer);
		// malloc conf
		free(conf);
		conf = NULL;
	}
	// write : async signal safe
	const char *msg = "[Client] Toutes les ressources de configuration ont été libérées\n";
	if (write(STDOUT_FILENO, msg, sizeof(char) * strlen(msg)) == -1) {
		const char *msg_error = "Erreur write\n";
		if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
			// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
			perror("write");
		}
	}
}

/**
	* Libère les ressources servant à la communication avec le démon
*/
void dispose_tools() {
	// Ferme et supprime les descripteur de fichiers fdquestion et fdanswer
	if (close(fdquestion)			== -1)	{ perror("close fdquestion");		}
	if (close(fdanswer)				== -1)	{ perror("close fdanswer");			}
	if (unlink(pipe_question)		== -1)	{ perror("unlink pipequestion");	}
	if (unlink(pipe_answer)			== -1)	{ perror("unlink pipeanswer");		}
	// Ferme le descripteur de fichier associé au shm
	if (close(shm_fd)				== -1)	{ perror("close shm_fd");			}
	// Ferme les sémaphores full, empty et mutex
	if (sem_close(full)				== -1)	{ perror("sem_close full");			}
	if (sem_close(empty)			== -1)	{ perror("sem_close empty");		}
	if (sem_close(mutex)			== -1)	{ perror("sem_close mutex");		}
	// Supprime la projection mémoire
	if (munmap(fifo_p, size_shm)	== -1)	{ perror("unmap fifo_p");			}
	// Libération de la structure du shm et des tubes
	free(pipe_question);
	free(pipe_answer);
}

/**
	* Libère les ressources
*/
void dispose_all() {
	dispose_config();
	dispose_tools();
	const char *msg = "[Client] Toutes les ressources ont été libérées\n";
	if (write(STDOUT_FILENO, msg, sizeof(char) * strlen(msg)) == -1) {
		const char *msg_error = "Erreur write\n";
		if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
			// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
			perror("write");
		}
	}
}

/**
	* @param type : si type vaut 'P' alors écriture dans le tube de question pour informer le démon
	* Le client reçoit un signal de terminaison il doit alors s'éteindre
*/
int close_on_signal(const char type) {
	int is_error = EXIT_SUCCESS;
	const char *msg;
	if (type == 'P') {
		msg = "quit";
		// Informe le démon que le client doit s'éteindre
		if (write(fdquestion, msg, sizeof(char) * strlen(msg)) == -1) {
			const char *msg_error = "Erreur write dans fdquestion\n";
			if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
				is_error = EXIT_FAILURE;
			}
		}
	}
	// Libération des ressources
	dispose_all();
	msg = "[Client] Fermeture du client\n";
	if (write(STDOUT_FILENO, msg, sizeof(char) * strlen(msg)) == -1) {
		const char *msg_error = "Erreur write\n";
		if (write(STDERR_FILENO, msg_error, sizeof(char) * strlen(msg_error)) == -1) {
			// Même si perror n'est pas async-signal-safe, il faut quand même tenter d'informer l'erreur
			perror("write");
			is_error = EXIT_FAILURE;
		}
	}
	return is_error;
}
