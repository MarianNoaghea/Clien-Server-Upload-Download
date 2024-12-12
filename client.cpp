#include <stdio.h>  
#include <stdlib.h>
#include <unistd.h>
#include <string.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <bits/stdc++.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "helpers.h"

using namespace std;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s server_address server_port\n", file);
	exit(0);
}

int main(int argc, char *argv[])
{
	int sockfd, n, ret;
	struct sockaddr_in serv_addr;
	char buffer[BUFLEN];

	if (argc < 3) {
		usage(argv[0]);
	}

	fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

	FD_ZERO(&tmp_fds);
	FD_ZERO(&read_fds);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfd < 0, "socket");

	FD_SET(sockfd, &read_fds);
	FD_SET(0, &read_fds);
	fdmax = sockfd;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[2]));
	ret = inet_aton(argv[1], &serv_addr.sin_addr);
	DIE(ret == 0, "inet_aton");

	ret = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
	DIE(ret < 0, "connect");

	printf("---------------------------\n");

	while (1) {
  		// se citeste de la tastatura
		tmp_fds = read_fds;

		select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);

		if(FD_ISSET(0, &tmp_fds)) {
			memset(buffer, 0, BUFLEN);
			fgets(buffer, BUFLEN - 1, stdin);

			Msg msg;

			if (strncmp(buffer, "exit", 4) == 0) {
				break;
			} 
			
			if (strncmp(buffer, "upload", 5) == 0) {
				char nume_fisier[50];
				char nume_pe_server[50];
				sscanf(buffer, "%*s %s %s", nume_fisier, nume_pe_server);
				nume_fisier[strlen(nume_fisier)] = '\0';
				nume_pe_server[strlen(nume_pe_server)] = '\0';

				int miner_sursa, fileSize;

				miner_sursa = open(nume_fisier, O_RDONLY);
				fileSize = lseek(miner_sursa, 0, SEEK_END);
				lseek(miner_sursa, 0, SEEK_SET);

				Msg message;

				int numberOfPayloads = 0;
				printf("filesize= %d\n", fileSize);
				if(fileSize % (MAX_LEN - 2*sizeof(int)) == 0) {
					numberOfPayloads = fileSize / (MAX_LEN - 2*sizeof(int));
				} 
				else {
					numberOfPayloads = fileSize / (MAX_LEN - 2*sizeof(int)) + 1;
  				}

				message.type = INFO_UPLOAD;
				Info info;
				strcpy(info.fileName, nume_pe_server);
				info.nrPayloads = numberOfPayloads;

				memcpy(message.payload, &info, sizeof(Info));


				send(sockfd, &message, sizeof(Msg), 0);

				Msg msg2;
				recv(sockfd, &msg2, sizeof(int), 0); // receive ACK

				if (msg2.type == ACK) {
					printf("\nACK\n");	
				} else if (msg2.type = ALREADY_UPLOADED) {
					printf("%s",msg2.payload);
					continue;
				}


				// printf("%s|%s|%d", nume_fisier, nume_pe_server, numberOfPayloads);

				char *stringMessage =(char*) calloc(MAX_LEN, sizeof(char)); 

				while (numberOfPayloads) {
					Msg msg;
					msg.type = MSG;
					
					msg.size = read(miner_sursa, stringMessage, MAX_LEN - 2*sizeof(int));
					memcpy(msg.payload, stringMessage, MAX_LEN - 2*sizeof(int));

					// printf("########%s\n", msg.payload);

					printf("\ntrimit %d charuri\n", msg.size);
					send(sockfd, &msg, sizeof(Msg), 0);
					
					Msg msg2;
					recv(sockfd, &msg2, sizeof(int), 0); // receive ACK


					if (msg2.type == ACK) {
						printf("\nACK\n");	
					}

					memset(stringMessage, 0, MAX_LEN - 2*sizeof(int));
					memset(msg.payload, 0, MAX_LEN - 2*sizeof(int));

					numberOfPayloads--;
				}
			}

			if (strncmp(buffer, "download", 5) == 0) {
				char nume_fisier[50];
				char nume_local[50];

				sscanf(buffer, "%*s %s %s %*s", nume_fisier, nume_local);
				nume_fisier[strlen(nume_fisier)] = '\0';
				nume_local[strlen(nume_local)] = '\0';

				Msg msg;
				msg.type = INFO_DOWNLOAD;
				strcpy(msg.payload, nume_fisier);

				send(sockfd, &msg, sizeof(Msg), 0);
				Msg msgRecv;

				recv(sockfd, &msgRecv, sizeof(Msg), 0);

				printf("type = %d\n", msgRecv.type);

				if (msgRecv.type == FILE_NO_EXISTS) {
					printf("%s\n",msgRecv.payload);
					continue;
				} else if(msg.type == ACK) {
					printf("ACK\n");
				}

				
				Info* info;
				int miner_destinatie;
				memset(&msgRecv, 0, sizeof(Msg));
				recv(sockfd, &msgRecv, sizeof(Msg), 0);

				

				

				if (msgRecv.type == INFO_DOWNLOAD) {
					info = (Info*) msgRecv.payload;
					printf("%s %d\n", nume_local, info->nrPayloads);

					miner_destinatie = open(nume_local, O_WRONLY | O_TRUNC | O_CREAT, 0644);
					lseek(miner_destinatie, 0, SEEK_SET);

					while (info->nrPayloads) {
						Msg m;
						recv(sockfd, &m, sizeof(Msg), 0);
						printf("\nprimesc %d charuri\n", m.size);

						write(miner_destinatie, m.payload, m.size);

						Msg m2;
						m2.type = ACK;
						send(sockfd, &m2, sizeof(int), 0); // ACK

						info->nrPayloads--;
					}
				}
			}


			
			DIE(n < 0, "send");
		} else {
			// Msg msgRecv;
			// memset(&msgRecv, 0, MAX_LEN);
			// recv(sockfd, &msgRecv, MAX_LEN, 0);

			// printf("%s", msgRecv.payload);
		}
	}

	close(sockfd);

	return 0;
}