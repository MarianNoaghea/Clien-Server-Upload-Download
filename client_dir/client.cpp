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
#include <sys/stat.h>
#include "../helpers.h"
#include <dirent.h> 

using namespace std;

void usage(char *file) {
    fprintf(stderr, "Usage: %s server_address server_port\n", file);
    exit(0);
}

int main(int argc, char *argv[]) {
    int sockfd, ret;
    struct sockaddr_in serv_addr;
    char buffer[BUFLEN];

    if (argc < 3) {
        usage(argv[0]);
    }

    fd_set read_fds, tmp_fds;
    int fdmax;

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
        tmp_fds = read_fds;
        select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);

        if (FD_ISSET(0, &tmp_fds)) {
            memset(buffer, 0, BUFLEN);
            fgets(buffer, BUFLEN - 1, stdin);

            if (strncmp(buffer, "exit", 4) == 0) {
                break;
            }

            if (strncmp(buffer, "upload", 6) == 0) {
                char nume_fisier[50];
                char nume_pe_server[50];
                sscanf(buffer, "%*s %s %s", nume_fisier, nume_pe_server);

                int miner_sursa = open(nume_fisier, O_RDONLY);
                if (miner_sursa < 0) {
                    perror("Error opening file");
                    continue;
                }

                int fileSize = lseek(miner_sursa, 0, SEEK_END);
                lseek(miner_sursa, 0, SEEK_SET);

                int numberOfPayloads = (fileSize + (MAX_LEN - 2 * sizeof(int) - 1)) / (MAX_LEN - 2 * sizeof(int));
                printf("filesize = %d, number of payloads = %d\n", fileSize, numberOfPayloads);

                Msg message;
                memset(&message, 0, sizeof(Msg));
                message.type = INFO_UPLOAD;

                Info info;
                strcpy(info.fileName, nume_pe_server);
                info.nrPayloads = numberOfPayloads;
                memcpy(message.payload, &info, sizeof(Info));

                send(sockfd, &message, sizeof(Msg), 0);

                Msg response;
                recv(sockfd, &response, sizeof(Msg), 0);

                if (response.type == ACK) {
                    printf("Server acknowledged upload.\n");
                } else if (response.type == ALREADY_UPLOADED) {
                    printf("File already uploaded: %s\n", response.payload);
                    close(miner_sursa);
                    continue;
                }

                char stringMessage[MAX_LEN];
                while (numberOfPayloads > 0) {
                    Msg msg;
                    memset(&msg, 0, sizeof(Msg));
                    msg.type = MSG;

                    msg.size = read(miner_sursa, stringMessage, MAX_LEN - 2 * sizeof(int));
                    memcpy(msg.payload, stringMessage, msg.size);

                    send(sockfd, &msg, sizeof(Msg), 0);
                    recv(sockfd, &response, sizeof(Msg), 0);

                    if (response.type == ACK) {
                        printf("Payload acknowledged by server.\n");
                    }

                    numberOfPayloads--;
                }

                close(miner_sursa);
            }

            if (strncmp(buffer, "download", 8) == 0) {
                char nume_fisier[50];
                char nume_local[50];
                sscanf(buffer, "%*s %s %s", nume_fisier, nume_local);

                Msg msg;
                memset(&msg, 0, sizeof(Msg));
                msg.type = INFO_DOWNLOAD;
                strcpy(msg.payload, nume_fisier);

                send(sockfd, &msg, sizeof(Msg), 0);

                Msg response;
                recv(sockfd, &response, sizeof(Msg), 0);

                if (response.type == FILE_NO_EXISTS) {
                    printf("Server response: %s\n", response.payload);
                    continue;
                }

                Info *info = (Info*) response.payload;
                int miner_destinatie = open(nume_local, O_WRONLY | O_TRUNC | O_CREAT, 0644);
                DIE(miner_destinatie < 0, "Error creating file");

                for (int i = 0; i < info->nrPayloads; i++) {
                    Msg data;
                    recv(sockfd, &data, sizeof(Msg), 0);
                    write(miner_destinatie, data.payload, data.size);

                    Msg ack;
                    ack.type = ACK;
                    send(sockfd, &ack, sizeof(Msg), 0);
                }

                close(miner_destinatie);
                printf("Download complete.\n");
            }

            if (strncmp(buffer, "ls", 2) == 0) {
                DIR *d;
                struct dirent *dir;
                d = opendir(".");
                if (d) {
                    while ((dir = readdir(d)) != NULL) {
                        if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0)
                            printf("%s ", dir->d_name);
                    }
                    closedir(d);
                }
                printf("\n");
            }
        }
    }

    close(sockfd);
    return 0;
}
