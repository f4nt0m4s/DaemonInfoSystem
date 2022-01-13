#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <semaphore.h>
#include <time.h>
/*--------------*/
/*	PROTOTYPES	*/
/*--------------*/
#ifndef __UTILS__C
	#define __UTILS__C
	// Sémaphores
	void P(sem_t *s);
	void V(sem_t *s);
	// Fonctions utiles
	size_t count_digit(int value);
	int is_numeric(const char *str);
	void set_filename_pid(char **dest, const char *format, pid_t pid);
	void trim(char *str);
	int prefix(const char *pre, const char *str);
	int atoi(const char* str);
	char *strndup(const char *s, size_t n);
	char *get_time_formatted();
	/*--------------------------------------------------------------*/
	/*							SEMAPHORE							*/
	/*--------------------------------------------------------------*/
	/**
	 * Fonctions notations Dijkstra pour les actions du sémaphore
	*/

	/**
		* P : Proberen (Tester) 
		* Si utilisé, alors bloque en prenant le jeton
		* @param s : le sémaphore à tester
	*/
	void P(sem_t *s) {
		if (sem_wait(s) == -1) {
			perror("sem_wait");
			exit(EXIT_FAILURE);
		}
	}
	/**
		 * V : Verhogen (Incrémenter)
		 * Si utilisé, débloque en redonnant le jeton utlisé
		 * @param s : le sémaphore à incrémenter
	*/
	void V(sem_t *s) {
		if (sem_post(s) == -1) {
			perror("sem_post");
			exit(EXIT_FAILURE);
		}
	}
	/*--------------------------------------------------------------*/
	/*						FONCTIONS UTILES						*/
	/*--------------------------------------------------------------*/
	/**
		* @param value : la valeur dont il faut compter les chiffres
		* @return le nombre de chiffre
		* Retourne le nombre de chiffre d'une valeur
	*/
	size_t count_digit(int value) {
		size_t nbDigit = 0;
		while( value != 0 ) {
			value /= 10;
			nbDigit++;
		}
		return nbDigit;
	}
	/**
		* @param str : la chaîne de caractères à tester
		* @return 0 est un chiffre ou un nombre, sinon -1
	*/
	int is_numeric(const char *str)
	{
		while(*str != '\0') {
			if(*str < '0' || *str > '9') {
				return -1;
			}
			str++;
		}
		return 0;
	}
	/**
		* @param dest : la chaîne de caractères à affecter
		* @param format : le format de la chaîne de caractères
		* @param pid : le pid à ajouter
		* Affecte un nom de fichier avec son chemin à un tube nommée ou un fichier avec comme argument un pid
		* Exemple : 
		* format : tube_%d et pid : 1000 -> dest = tube_1000
	*/
	void set_filename_pid(char **dest, const char *format, pid_t pid) {
		size_t lengthpid = count_digit(pid);
		size_t lengthname = strlen(format);
		size_t lengthpipename = (size_t) (sizeof(char) * (lengthname + lengthpid + 1));
		*dest = (char *) malloc(lengthpipename);
		if (dest == NULL) {
			fprintf(stderr, "Erreur d'allocation mémoire %lu pour la variable dest\n", lengthpipename);
			exit(EXIT_FAILURE);
		}
		if (snprintf(*dest, lengthpipename, format, pid) == -1) {
			fprintf(stderr, "Erreur snprintf %s", format);
			exit(EXIT_FAILURE);
		}
	}
	/**
		* Enlever les espaces avant et après
		* Source : https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
		* @param : la chaîne de caractères où il faut enlever les espaces avant et après
	*/
	void trim(char *str) {
		int i;
		int begin = 0;
		int end = ((int) strlen(str)) - 1;
		// Fait avancer le curseur du début jusqu'à ce qu'il n'y est plus d'espace au début
		while (isspace((unsigned char) str[begin])) {
			begin++;
		}
		// Fait avancer le curseur de fin jusqu'à ce qu'il n'y est plus d'espace à la fin
		while ((end >= begin) && isspace((unsigned char) str[end])) {
			end--;
		}
		// Shift all characters back to the start of the string array.
		for (i = begin; i <= end; i++) {
			str[i - begin] = str[i];
		}
		// Null terminate string.
		str[i - begin] = '\0';
	}

	/**
		* Préfixe dans une chaîne de caractères
		* Source : https://stackoverflow.com/questions/4770985/how-to-check-if-a-string-starts-with-another-string-in-c
		* @param pre : le préfix (caractère ou chaine de caractères)
		* @param str : la chaîne de caractères où il faut chercher le préfix
		* @return 0 si aucune correspondance, 1 sinon.
		Exemple : prefix("#", str)
	*/
	int prefix(const char *pre, const char *str) {
		if (pre == NULL || str == NULL) {
			return 0;
		}
		return strncmp(pre, str, strlen(pre)) == 0;
	}
	/**
		* Recréation de la fonction atoi de la bibliothèque stdlib.h
		* Source : https://stackoverflow.com/questions/7021725/how-to-convert-a-string-to-integer-in-c
		* @param str : la chaîne de caractères à convertir
		* @return le nombre entier correspond à la chaîne de caractères
	*/
	int atoi(const char* str) {
		int num = 0;
		int i = 0;
		size_t isNegetive = (size_t) 0;
		if (str[i] == '-') {
			isNegetive = 1;
			i++;
		}
		while (str[i] && (str[i] >= '0' && str[i] <= '9')) {
			num = num * 10 + (str[i] - '0');
			i++;
		}
		if (isNegetive == 1) {
			num = - 1 * num;
		}
		return num;
	}
	/**
		* strndup que sous POSIX 1-2008
		* Faire un free de la variable retournée lorsqu'elle n'est plus utilisée
		Source : https://stackoverflow.com/questions/46013382/c-strndup-implicit-declaration/46013414
		* @param s : la chaîne de caractères à dupliquer
		* @param n : le nombre d'octets à dupliquer
		* @return la chaîne de caractères dupliquée
		* Exemple : char *dup = strndup(s, sizeof(char) * strlen(s));
	*/
	char *strndup(const char *s, size_t n) {
		char *p;
		size_t n1;
		for (n1 = 0; n1 < n && s[n1] != '\0'; n1++)
			continue;
		p = malloc(n + 1);
		if (p != NULL) {
			memcpy(p, s, n1);
			p[n1] = '\0';
		}
		return p;
	}
	/**
		* @param path : la chaîne de caractères où il faut extraire l'extension
		* @return l'extension de la chaîne de caractères
		* Source : https://stackoverflow.com/questions/5309471/getting-file-extension-in-c
		* Exemple : (strncmp(get_file_extension("fichier.c"), ".c", strlen("fichier.c")) == 0)
	*/
	const char *get_file_extension(const char path[]) {
		const char *result;
		int i, n;
		if (path == NULL) {
			return NULL;
		}
		n = (int) strlen(path);
		i = n - 1;
		while ((i >= 0) && (path[i] != '.') && (path[i] != '/') & (path[i] != '\\')) {
			i--;
		}
		if ((i > 0) && (path[i] == '.') && (path[i - 1] != '/') && (path[i - 1] != '\\')) {
			result = path + i;
		} else {
			result = path + n;
		}
		return result;
	}
	/**
		* @return une heure minute(s) seconde(s) sous le format : [HEURE:MINUTE(S):SECONDE(S)]
		* Faire un free de la variable retournée lorsqu'elle n'est plus utilisée
		* Exemple : char *s = get_time_formatted(); ...; free(s);
	*/
	char *get_time_formatted() {
		size_t length_max = (size_t) 256;
		size_t size_stime = sizeof(char) * length_max;
		char *stime = malloc(size_stime);
		if (stime == NULL) {
			fprintf(stderr, "Erreur d'allocation mémoire %lu pour la variable stime\n", size_stime);
			exit(EXIT_FAILURE);
		}
		time_t timestamp = time(NULL);
		struct tm *time = NULL;
		time = localtime(&timestamp);
		if (time == NULL) {
			perror("Erreur time");
			exit(EXIT_FAILURE);
		}
		if (strftime(stime, length_max, "[%H:%M:%S]", time) == 0) {
			fprintf(stderr, "Erreur strftime pour la taille %lu", length_max);
			exit(EXIT_FAILURE);
		}
		trim(stime);
		return stime;
	}
#endif
