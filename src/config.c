#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../inc/config.h"
#include "../inc/shm.h"
#include "utils.c"

int start_config(char *config_file, struct config *conf) {
	FILE *f = fopen(config_file, "r");
	if (f == NULL) {
		fprintf(stderr, "Impossible d'ouvrir le fichier : %s\n", config_file);
		return -1;
	}
	const size_t max_length = sizeof(char) * MAX_LENGTH_LINE;
	char *buffer = malloc(max_length);
	if (buffer == NULL) {
		fprintf(stderr, "Erreur malloc pour allouer une taille mémoire de %lu\n", max_length);
		return -1;
	}
	size_t line_number = 0;
	while (!feof(f)) {
		if (fgets(buffer, max_length, f) == NULL) {
			if (ferror(f) != 0) {
				fprintf(stderr, "Erreur lors de la lecture, code d'erreur : %d\n", errno);
				return -1;
			}
		} else {
			++line_number;
			trim(buffer);
			if (buffer[0] == '#' || prefix("//", buffer)) {
				continue;
			} else {
				if (parse_configfile(buffer, conf) == -1) {
					fprintf(stderr, "Erreur de syntaxe à la ligne : %ld\n", line_number);
					return -1;
				}
			}
		}
	}
	free(buffer);
	// EndOfFile == erreur
	if (fclose(f) == EOF) {
		fprintf(stderr, "Erreur lors de la fermeture du fichier %s\n", config_file);
		return -1;
	}
	return 0;
}

int parse_configfile(char *buf, struct config *conf) {
	if (buf == NULL) {
		return -1;
	}
	const size_t maxvarname = (size_t) (sizeof(char) * MAX_LENGTH_VARNAME);
	char *varname = (char *) malloc(maxvarname);
	if (varname == NULL) {
		fprintf(stderr, "Erreur d'allocation mémoire %lu pour la varname\n", maxvarname);
		return -1;
	}
	const size_t maxfilename = (size_t) (sizeof(char) * FILENAME_MAX);
	char *value = (char *) malloc(sizeof(char) * FILENAME_MAX);
	if (value == NULL) {
		fprintf(stderr, "Erreur d'allocation mémoire %u pour la varname\n", FILENAME_MAX);
		return -1;
	}
	const char *separators = "=\n";
	// Variable
	char *token = strtok(buf, separators);
	if (token == NULL) {
		return -1;
	}
	if (snprintf(varname, maxvarname, "%s", token) == -1) {
		fprintf(stderr, "Erreur snprintf\n");
		exit(EXIT_FAILURE);
	}
	trim(varname);
	// Valeur
	token = strtok(NULL, separators);
	if (token == NULL) {
		return -1;
	}
	if (snprintf(value, maxfilename, "%s", token) == -1) {
		fprintf(stderr, "Erreur snprintf\n");
		return -1;
	}
	trim(value);
	// printf("value -> %s / is finished by zero terminator  : %d\n", value, value[strlen(value)] == '\0');
	
	if (strncmp(varname, VAR_MAX_BUFFER_SHM, strlen(VAR_MAX_BUFFER_SHM)) == 0) {
		if (is_numeric(value) == -1) {
			fprintf(stderr, "MAX_BUFFER_SHM doit être un chiffre ou un nombre\n");
			return -1;
		} else {
			int tmp = atoi(value);
			if (tmp < 0 || tmp > MAX_BUFFER_SIZE) {
				fprintf(stderr, "La taille du segment de mémoire partagée ne peut pas valoir %d (0 <= MAX_BUFFER_SHM >= MAX_BUFFER_SHM = %lu)\n", tmp, (size_t) MAX_BUFFER_SIZE);
				return -1;
			}
			conf->buffer_size = (size_t) tmp;
		}
	} else if (strncmp(varname, VAR_NAME_SHM, strlen(VAR_NAME_SHM)) == 0) {
		if (strchr(value, '/') != NULL) {
			fprintf(stderr, "Le valeur de %s (%s) ne doit pas contenir de '/'\n", VAR_NAME_SHM, value);
			return -1;
		}
		filename_format(value, CASE_FILENAME_SHM);
		conf->name_shm = strndup(value, strlen(value));
		if (conf->name_shm == NULL) { perror("strndup name_shm"); exit(EXIT_FAILURE); }
		return 1;
	} else if (strncmp(varname, VAR_NAME_EMPTY_SEMAPHORE, strlen(VAR_NAME_EMPTY_SEMAPHORE)) == 0) {
		if (strchr(value, '/') != NULL) {
			fprintf(stderr, "Le nom du sémaphore vide %s ne doit pas contenir de '/'\n", value);
			fprintf(stderr, "Le valeur de %s (%s) ne doit pas contenir de '/'\n", VAR_NAME_EMPTY_SEMAPHORE, value);
			return -1;
		}
		filename_format(value, CASE_FILENAME_SEMAPHORE);
		conf->name_empty_semaphore = strndup(value, strlen(value));
		if (conf->name_empty_semaphore == NULL) { perror("strndup name_empty_semaphore"); exit(EXIT_FAILURE); }
		return 1;
	} else if (strncmp(varname, VAR_NAME_FULL_SEMAPHORE, strlen(VAR_NAME_FULL_SEMAPHORE)) == 0) {
		if (strchr(value, '/') != NULL) {
			fprintf(stderr, "Le valeur de %s (%s) ne doit pas contenir de '/'\n", VAR_NAME_FULL_SEMAPHORE, value);
			return -1;
		}
		filename_format(value, CASE_FILENAME_SEMAPHORE);
		conf->name_full_semaphore = strndup(value, strlen(value));
		if (conf->name_full_semaphore == NULL) { perror("strndup name_full_semaphore"); exit(EXIT_FAILURE); }
		return 1;
	} else if (strncmp(varname, VAR_NAME_MUTEX_SEMAPHORE, strlen(VAR_NAME_MUTEX_SEMAPHORE)) == 0) {
		if (strchr(value, '/') != NULL) {
			fprintf(stderr, "Le valeur de %s (%s) ne doit pas contenir de '/'\n", VAR_NAME_MUTEX_SEMAPHORE, value);
			return -1;
		}
		filename_format(value, CASE_FILENAME_SEMAPHORE);
		conf->name_mutex_semaphore = strndup(value, strlen(value));
		if (conf->name_mutex_semaphore == NULL) { perror("strndup name_mutex_semaphore"); exit(EXIT_FAILURE); }
		return 1;
	} else if (strncmp(varname, VAR_PREFIX_PIPEQUESTION, strlen(VAR_PREFIX_PIPEQUESTION)) == 0) {
		// Le '/' pour pas de dossier
		if (strchr(value, '/') != NULL) {
			fprintf(stderr, "Le valeur de %s (%s) ne doit pas contenir de '/'\n", VAR_PREFIX_PIPEQUESTION, value);
			return -1;
		}
		// Le '%d' pour pas de format : pipeanswer_%d
		if (strchr(value, '%') != NULL) {
			fprintf(stderr, "La valeur de %s (%s) ne doit pas contenir de '%%'\n", VAR_PREFIX_PIPEQUESTION, value);
			return -1;
		}
		// value : pipequestion -> snprintf : /tmp/pipequestion_%d
		filename_format(value, CASE_FILENAME_PIPE);
		conf->prefix_pipequestion = strndup(value, strlen(value));
		if (conf->prefix_pipequestion == NULL) { perror("strndup prefix_pipequestion"); exit(EXIT_FAILURE); }
		return 1;
	} else if (strncmp(varname, VAR_PREFIX_PIPEANSWER, strlen(VAR_PREFIX_PIPEANSWER)) == 0) {
		// Le '/' pour pas de dossier
		if (strchr(value, '/') != NULL) {
			fprintf(stderr, "La valeur de %s (%s) ne doit pas contenir de '/'\n", VAR_PREFIX_PIPEANSWER, value);
			return -1;
		}
		// Le '%d' pour pas de format : pipeanswer_%d
		if (strchr(value, '%') != NULL) {
			fprintf(stderr, "La valeur de %s (%s) ne doit pas contenir de '%%'\n", VAR_PREFIX_PIPEANSWER, value);
			return -1;
		}
		// value : pipeanswer -> snprintf : /tmp/pipeanswer_%d
		filename_format(value, CASE_FILENAME_PIPE);
		conf->prefix_pipeanswer = strndup(value, strlen(value));
		if (conf->prefix_pipeanswer == NULL) { perror("strndup prefix_pipeanswer"); exit(EXIT_FAILURE); }
		return 1;
	} else if (strncmp(varname, VAR_TIMEOUT_DAEMON, strlen(VAR_TIMEOUT_DAEMON)) == 0) {
		if (is_numeric(value) == -1) {
			fprintf(stderr, "VAR_TIMEOUT_DAEMON doit être un chiffre ou un nombre\n");
			return -1;
		} else {
			int tmp = atoi(value);
			if (tmp < MIN_TIMEOUT || tmp > MAX_TIMEOUT) {
				fprintf(stderr, "Le timeout du démon ne peut pas valoir %d (%lu = MIN_TIMEOUT <= timeout >= MAX_TIMEOUT = %lu)\n", tmp, (size_t) MIN_TIMEOUT,(size_t) MAX_TIMEOUT);
				return -1;
			}
			conf->timeout_daemon = (size_t) tmp;
		}
	} else if (strncmp(varname, VAR_TIMEOUT_CLIENT, strlen(VAR_TIMEOUT_CLIENT)) == 0) {
		if (is_numeric(value) == -1) {
			fprintf(stderr, "VAR_TIMEOUT_CLIENT doit être un chiffre ou un nombre\n");
			return -1;
		} else {
			int tmp = atoi(value);
			if (tmp < MIN_TIMEOUT || tmp > MAX_TIMEOUT) {
				fprintf(stderr, "Le timeout du démon ne peut pas valoir %d (%lu = MIN_TIMEOUT <= timeout >= MAX_TIMEOUT = %lu)\n", tmp, (size_t) MIN_TIMEOUT,(size_t) MAX_TIMEOUT);
				return -1;
			}
			conf->timeout_client = (size_t) tmp;
		}
	} else if (strncmp(varname, VAR_ANTIFLOOD_CLIENT, strlen(VAR_ANTIFLOOD_CLIENT)) == 0) {
		if (is_numeric(value) == -1) {
			fprintf(stderr, "VAR_ANTIFLOOD_CLIENT doit être un chiffre ou un nombre\n");
			return -1;
		}
		conf->antiflood_client = (size_t) atoi(value);
	}
	free(varname);
	free(value);
	return 0;
}

int filename_format(char *value, const char type) {
	const char *prefix;
	const char *format = "%s";
	size_t size_tmp;
	char *tmp = NULL;
	// Copie value dans copy_value
	size_t size_copy_value;
	char *copy_value = NULL;
	size_copy_value = sizeof(char) * (strlen(value) + 1); // +1 pour '\0'
	copy_value = (char *) malloc(size_copy_value);
	if (copy_value == NULL) {
		fprintf(stderr, "Erreur d'allocation mémoire %lu pour la variable copy_value\n", size_copy_value);
		return -1;
	}
	if (snprintf(copy_value, size_copy_value, "%s", value) == -1) {
		fprintf(stderr, "Erreur snprintf\n");
		return -1;
	}
	// printf("copy_value is finished by zero terminator ? %d\n", copy_value[strlen(copy_value)] == '\0');
	// Traitement du nom de fichier
	char upper_type = (char) toupper(type);
	switch (upper_type) {
		// shm name : /dev/shm/shm_name_value
		case CASE_FILENAME_SHM : {
			// Le fichier est placé dans le dossier /dev/shm/shm_name_
			prefix = "/";
			// Concaténation du prefix et value
			size_tmp = sizeof(char) * (strlen(prefix) + strlen(copy_value) + 1); // +1 pour '\0'
			tmp = (char *) malloc(size_tmp);
			if (tmp == NULL) {
				fprintf(stderr, "Erreur d'allocation mémoire %lu pour la variable tmp\n", size_tmp);
				return -1;
			}
			if (snprintf(tmp, size_tmp, "%s%s", prefix, copy_value) == -1) {
				fprintf(stderr, "Erreur snprintf\n");
				return -1;
			}
			break;
		}
		// semaphore name : /dev/shm/sem_name_value
		case CASE_FILENAME_SEMAPHORE : {
			// Le fichier est placé dans le dossier /dev/shm/sem_name_
			prefix = "/";
			// Concaténation du prefix et value
			size_tmp = sizeof(char) * (strlen(prefix) + strlen(copy_value) + 1); // +1 pour '\0'
			tmp = (char *) malloc(size_tmp);
			if (tmp == NULL) {
				fprintf(stderr, "Erreur d'allocation mémoire %lu pour la variable tmp\n", size_tmp);
				return -1;
			}
			if (snprintf(tmp, size_tmp, "%s%s", prefix, copy_value) == -1) {
				fprintf(stderr, "Erreur snprintf\n");
				return -1;
			}
			break;
		} 
		// pipe name : /tmp/pipe_name_value_%d (%d : pid)
		case CASE_FILENAME_PIPE : {
			// Le fichier est placé dans le dossier /tmp/pipe_name_
			prefix = "/tmp/";
			const char *suffix = "_%d";
			// Concaténation du prefix, value et suffix
			size_tmp = sizeof(char) * (strlen(prefix) + strlen(copy_value) + strlen(suffix) + 3); // +1 pour '\0'
			tmp = (char *) malloc(size_tmp);
			if (tmp == NULL) {
				fprintf(stderr, "Erreur d'allocation mémoire %lu pour la variable tmp\n", size_tmp);
				return -1;
			}
			if (snprintf(tmp, size_tmp, "%s%s%s", prefix, copy_value, suffix) == -1) {
				fprintf(stderr, "Erreur snprintf\n");
				return -1;
			}
			break;
		}
		default : {
			fprintf(stderr, "Erreur aucune correspondance de type pour le type %c\n", type);
			return -1;
		}
	}
	// printf("tmp is finished by zero terminator  ? %d\n", tmp[strlen(tmp)] == '\0');
	// Réallocation de la mémoire de value pour lui concaténer tmp
	value = (char *) realloc(value, size_tmp);
	if (value == NULL) {
		fprintf(stderr, "Erreur de réallocation mémoire %lu pour la variable value\n", size_tmp);
		return -1;
	}
	if (snprintf(value, size_tmp, format, tmp) == -1) {
		fprintf(stderr, "Erreur snprintf %s\n", format);
		return -1;
	}
	// printf("value is finished by zero terminator  ? %d\n", value[strlen(value)] == '\0');
	// Libération des ressources mémoires occupées sauf value car elle est utilisé après
	free(copy_value);
	free(tmp);
	return 0;
}
