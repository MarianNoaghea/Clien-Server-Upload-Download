#include <stdio.h>  
#include <string.h> 
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <bits/stdc++.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../helpers.h"

using namespace std;

vector<string> uploadedFiles;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s server_port\n", file);
	exit(0);
}


bool fileExists(char* filename) {
	for (auto file : uploadedFiles) {
		if (file == filename) {
			return true;
		}
	}
	return false;
}

int main(int argc, char *argv[])
{   

	
	int* sockets_vector =(int*) malloc (sizeof(int) * 10);
	int sockets_vector_len = 0;

	int sockfd, newsockfd, portno;
	char buffer[MAX_LEN - sizeof(int)];
	struct sockaddr_in serv_addr, cli_addr;
	int n, i, ret;
	socklen_t clilen;
	fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

	if (argc < 2) {
		usage(argv[0]);
	}

	// se goleste multimea de descriptori de citire (read_fds) si multimea temporara (tmp_fds)
	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfd < 0, "socket");

	portno = atoi(argv[1]);
	DIE(portno == 0, "atoi");

	memset((char *) &serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(portno);
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr));
	DIE(ret < 0, "bind");

	ret = listen(sockfd, MAX_CLIENTS);
	DIE(ret < 0, "listen");

	// se adauga noul file descriptor (socketul pe care se asculta conexiuni) in multimea 
	// read_fds
	FD_SET(sockfd, &read_fds); //singurul socket si multimea read
	fdmax = sockfd;


	while (1) {
		//copie (echiv cu set)
		tmp_fds = read_fds;
		ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
		DIE(ret < 0, "select");

		for (i = 0; i <= fdmax; i++) {	//iterez prin multime	
			if (FD_ISSET(i, &tmp_fds)) {
				if (i == sockfd) {	//daca au venit date pe socketul pasiv
					// a venit o cerere de conexiune pe socketul inactiv (cel cu listen),
					// pe care serverul o accepta
					clilen = sizeof(cli_addr);
					newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
					DIE(newsockfd < 0, "accept");

					// se adauga noul socket intors de accept() la multimea 
					// descriptorilor de citire
					FD_SET(newsockfd, &read_fds);//adaug si newsockfd
					if (newsockfd > fdmax) { 
						fdmax = newsockfd;//actualizez fdmax
					}

					sockets_vector[sockets_vector_len++] = newsockfd;
					printf("s-a adaugat in vector socketul %d\n", newsockfd);

					printf("new connection from %s:%d, [%d]\n",
							inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), newsockfd);

					printf("Socketii disponibili sunt: ");
					for (i = 0; i <= fdmax; i++) {
						printf("%d ", i);
					}

					printf("\n");

				} else {
					// s-au primit date pe unul din socketii de client,
					// asa ca serverul trebuie sa le receptioneze

					Msg msg;

					memset(&msg, 0, MAX_LEN);
					n = recv(i, &msg, MAX_LEN, 0);

					if (n == 0) {
						// conexiunea s-a inchis
						printf("[%d] closed connection\n", i);
						close(i);
						// se scoate din multimea de citire socketul inchis 
						FD_CLR(i, &read_fds);
					}

					Info* info;
					int miner_destinatie;

					if (msg.type == INFO_UPLOAD) {
						info = (Info*) msg.payload;
						printf("%s %d\n", info->fileName, info->nrPayloads);
						if (!fileExists(info->fileName)) {
							uploadedFiles.push_back(info->fileName);
							Msg m2;
							m2.type = ACK;
							send(i, &m2, sizeof(int), 0); // ACK
						} else {
							Msg m;
							m.type = ALREADY_UPLOADED;
							strcpy(m.payload, "Fisierul cu acest nume este deja uploadat!\n");
							send (i, &m, sizeof(Msg), 0);

							continue;
						}

						printf("aici nu ajunge\n");
						
						miner_destinatie = open(info->fileName, O_WRONLY | O_TRUNC | O_CREAT, 0644);
						lseek(miner_destinatie, 0, SEEK_SET);

						while (info->nrPayloads) {
							Msg m;
							recv(i, &m, sizeof(Msg), 0);
							printf("\nprimesc %d charuri\n", m.size);

							// printf("%.*s\n", m.size, m.payload);

							write(miner_destinatie, m.payload, m.size);

							Msg m2;
							m2.type = ACK;
							send(i, &m2, sizeof(int), 0); // ACK

							info->nrPayloads--;
						}
						
					}

					if (msg.type == INFO_DOWNLOAD) {
						int miner_sursa, fileSize;
						
						if (!fileExists(msg.payload)) {
							Msg m;
							m.type = FILE_NO_EXISTS;
							strcpy(m.payload, "Fisierul cu acest nume nu este uploadat!\n");
							send (i, &m, sizeof(Msg), 0);

							continue;
						} else {
							Msg m2;
							m2.type = ACK;
							send(i, &m2, sizeof(int), 0); // ACK
							printf("Trimit ACK\n");
						}

						printf("DOWNLOADEZ (%s)\n", msg.payload);

						miner_sursa = open(msg.payload, O_RDONLY);
						fileSize = lseek(miner_sursa, 0, SEEK_END);
						lseek(miner_sursa, 0, SEEK_SET);

						Msg message;

						int numberOfPayloads = 0;
						printf("filesize= %d\n", fileSize);
						if(fileSize % (MAX_LEN - sizeof(int)) == 0) {
							numberOfPayloads = fileSize / (MAX_LEN - sizeof(int));
						} 
						else {
							numberOfPayloads = fileSize / (MAX_LEN - sizeof(int)) + 1;
						}

						message.type = INFO_DOWNLOAD;

						Info info;
						info.nrPayloads = numberOfPayloads;

						memcpy(message.payload, &info, sizeof(Info));


						send(i, &message, sizeof(Msg), 0);

						char *stringMessage =(char*) calloc(MAX_LEN, sizeof(char)); 

						while (numberOfPayloads) {
							Msg msg;
							msg.type = MSG;
							
							msg.size = read(miner_sursa, stringMessage, MAX_LEN - 2*sizeof(int));
							memcpy(msg.payload, stringMessage, MAX_LEN - 2*sizeof(int));

							printf("\ntrimit %d charuri\n", msg.size);
							send(i, &msg, sizeof(Msg), 0);
							printf("am trimis\n");
							
							Msg msg2;
							recv(i, &msg2, sizeof(int), 0); // receive ACK
							if (msg2.type == ACK) {
								printf("\nACK\n");	
							} else {
								send(i, &msg, sizeof(Msg), 0);
							}

							memset(stringMessage, 0, MAX_LEN - 2*sizeof(int));
							memset(msg.payload, 0, MAX_LEN - 2*sizeof(int));

							numberOfPayloads--;
						}
					}
				}
			}
		}

		printf("---------------------------------------------\n");
	}

	close(sockfd);

	return 0;
}
