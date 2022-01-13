#include <stdio.h>
#include <stdlib.h>
#include "../inc/shm.h"
/**
	* @param fifo_p : la file shm qui contient les éléments
	* @param element : L'élément à ajouté
	* @param buffer_size : la taille de la file du shm
	* Ajoute un élement data_t qui est le pid dans la file du segment de mémoire partagée
*/
void add_element(struct fifo *fifo_p, data_t element, const size_t buffer_size) {
	if (buffer_size == 0) {
		fprintf(stderr, "Erreur d'ajouter d'un élement car la taille du buffer vaut %lu\n", buffer_size);
		exit(EXIT_FAILURE);
	}
	fifo_p->buffer[fifo_p->head] = element;
	fifo_p->head = (fifo_p->head + 1) % buffer_size;
}
/**
	* @param fifo_p : la file shm qui contient les éléments
	* @param buffer_size : la taille de la file du shm
	* @return Retourne l'élément retiré de la file du segment de mémoire partagée
	* Retire un élement data_t dans la file
*/
data_t remove_element(struct fifo *fifo_p, const size_t buffer_size) {
	if (buffer_size == 0) {
		fprintf(stderr, "Erreur de retrait d'un élement car la taille du buffer vaut %lu\n", buffer_size);
		exit(EXIT_FAILURE);
	}
	data_t element = fifo_p->buffer[fifo_p->tail];
	fifo_p->tail = (fifo_p->tail + 1) % buffer_size;
	return element;
}
