#ifndef _CONFIG_H_
		#define _CONFIG_H_
		/*----------*/
		/*	DEFINES	*/
		/*----------*/
		#define MAX_LENGTH_LINE 4096
		#define MAX_LENGTH_VARNAME 64
		#define DEFAULT_CONFIG_FILE "../config/app.config"
		// 300 secondes équivalent à 30 minutes au maximum
		#define MAX_TIMEOUT 1800
		// 10 secondes minimum
		#define MIN_TIMEOUT 10
		// Nom des variables dans le fichier de configuration
		#define VAR_ONE_RUNNING_DAEMON		"/sem_daemon_1"
		#define VAR_MAX_BUFFER_SHM			"MAX_BUFFER_SHM"
		#define VAR_NAME_SHM				"NAME_SHM"
		#define VAR_NAME_EMPTY_SEMAPHORE	"NAME_EMPTY_SEMAPHORE"
		#define VAR_NAME_FULL_SEMAPHORE		"NAME_FULL_SEMAPHORE"
		#define VAR_NAME_MUTEX_SEMAPHORE	"NAME_MUTEX_SEMAPHORE"
		#define VAR_PREFIX_PIPEQUESTION		"PREFIX_PIPEQUESTION"
		#define VAR_PREFIX_PIPEANSWER		"PREFIX_PIPEANSWER"
		#define VAR_TIMEOUT_DAEMON			"TIMEOUT_DAEMON"
		#define VAR_TIMEOUT_CLIENT			"TIMEOUT_CLIENT"
		#define VAR_ANTIFLOOD_CLIENT		"ANTIFLOOD_CLIENT"
		// Phase de traitement des noms de fichiers
		#define CASE_FILENAME_SHM 		'M'
		#define CASE_FILENAME_SEMAPHORE 'S'
		#define CASE_FILENAME_PIPE		'P'
		/*--------------------------------------*/
		/* DÉCLARATION DES VARIABLES GLOBALES	*/
		/*--------------------------------------*/
		struct config {
			// Sémaphores
			char *name_empty_semaphore;
			char *name_full_semaphore;
			char *name_mutex_semaphore;
			// Shm
			size_t buffer_size;
			char *name_shm;
			// Pipes
			char *prefix_pipequestion;
			char *prefix_pipeanswer;
			// Timeout
			size_t timeout_daemon;
			size_t timeout_client;
			// Antiflood
			size_t antiflood_client;
		};
		/*--------------*/
		/*	PROTOTYPES	*/
		/*--------------*/
		int start_config(char *config_file, struct config *conf);
		int parse_configfile(char *buf, struct config *conf);
		int filename_format(char *value, const char type);
#endif
