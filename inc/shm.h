#ifndef _SHM_H_
		#define _SHM_H_
		#include <unistd.h>
		/*--------------------------------------*/
		/* DÉCLARATION DES VARIABLES GLOBALES	*/
		/*--------------------------------------*/
		// Taille du tampon max, 1024 pid max dans le tampon
		#define MAX_BUFFER_SIZE 1024
		typedef pid_t data_t;
		struct fifo {
			size_t head;		// indice de lecture
			size_t tail;		// indice d'écriture
			data_t buffer[MAX_BUFFER_SIZE];
		};
		/*--------------*/
		/*	PROTOTYPES	*/
		/*--------------*/
		void add_element(struct fifo *fifo_p, data_t element, const size_t buffer_size);
		data_t remove_element(struct fifo *fifo_p, const size_t buffer_size);
#endif
