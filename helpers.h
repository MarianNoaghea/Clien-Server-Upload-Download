#ifndef _HELPERS_H
#define _HELPERS_H 1
#define BUFLEN		1000
#define MAX_LEN 1024

#define INFO_UPLOAD 1
#define INFO_DOWNLOAD 2
#define MSG 3
#define ACK 4
#define NACK 5
#define ALREADY_UPLOADED 6
#define FILE_NO_EXISTS 7

#include <stdio.h>
#include <stdlib.h> 

/*
 * Macro de verificare a erorilor
 * Exemplu:
 *     int fd = open(file_name, O_RDONLY);
 *     DIE(fd == -1, "open failed");
 */

#define DIE(assertion, call_description)	\
	do {									\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
					__FILE__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);				\
		}									\
	} while(0)

typedef struct Info{
	int nrPayloads;
	char fileName[50];
} Info;


typedef struct Msg {
	int type = -1;
	int size;
	char payload[MAX_LEN - 2*sizeof(int)];
}Msg;



	// dimensiunea maxima a calupului de date
#define MAX_CLIENTS	5	// numarul maxim de clienti in asteptare



#endif
