#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mutex>
#include <vector>
#include <string>
#include "../helpers.h"

using namespace std;

vector<string> all_sse_messages;  // To store all general messages for /events
vector<string> all_file_events;  // To store all file-related events for /files-events
mutex all_messages_mutex;        // Mutex for all_sse_messages
mutex all_file_events_mutex;     // Mutex for all_file_events

vector<string> uploadedFiles;

// Global message container and mutex
vector<string> my_messages;
mutex messages_mutex;

// List of active SSE clients
vector<int> sse_clients;
mutex sse_clients_mutex;

void broadcast_sse(const string& message) {
    lock_guard<mutex> lock(sse_clients_mutex);

    // Add the message to the global persistent container
    {
        lock_guard<mutex> lock(all_messages_mutex);
        all_sse_messages.push_back(message);
    }

    string sse_message = "data: " + message + "\n\n";
    for (auto it = sse_clients.begin(); it != sse_clients.end();) {
        if (send(*it, sse_message.c_str(), sse_message.size(), 0) <= 0) {
            close(*it);
            it = sse_clients.erase(it);
        } else {
            ++it;
        }
    }
}

void broadcast_file_event(const string& filename) {
    lock_guard<mutex> lock(sse_clients_mutex);

    // Add the event to the global persistent container
    {
        lock_guard<mutex> lock(all_file_events_mutex);
        all_file_events.push_back("File uploaded: " + filename);
    }

    string sse_message = "data: File uploaded: " + filename + "\n\n";
    for (auto it = sse_clients.begin(); it != sse_clients.end();) {
        if (send(*it, sse_message.c_str(), sse_message.size(), 0) <= 0) {
            close(*it);
            it = sse_clients.erase(it);
        } else {
            ++it;
        }
    }
}

void handle_http_request(int client_sock) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));

    int received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        close(client_sock);
        return;
    }

    char method[16], path[256], protocol[16];
    sscanf(buffer, "%s %s %s", method, path, protocol);

    printf("HTTP Request: %s %s %s\n", method, path, protocol);

    if (strncmp(method, "POST", 4) == 0) {
        char *content_length_str = strstr(buffer, "Content-Length: ");
        int content_length = 0;
        if (content_length_str) {
            sscanf(content_length_str, "Content-Length: %d", &content_length);
        }

        char *body_start = strstr(buffer, "\r\n\r\n");
        int body_offset = 0;
        if (body_start) {
            body_start += 4;
            body_offset = received - (body_start - buffer);
        }

        char message[content_length + 1];
        memset(message, 0, sizeof(message));
        if (body_start) {
            strncpy(message, body_start, body_offset);
        }
        int remaining = content_length - body_offset;
        while (remaining > 0) {
            int bytes_read = recv(client_sock, message + (content_length - remaining), remaining, 0);
            if (bytes_read <= 0) break;
            remaining -= bytes_read;
        }

        {
            lock_guard<mutex> lock(messages_mutex);
            my_messages.push_back(message);
        }

        // Broadcast the message to all connected clients
        broadcast_sse(message);

        const char *response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n\r\n"
            "Message received and logged.";
        send(client_sock, response, strlen(response), 0);

    } else if (strncmp(method, "GET", 3) == 0) {
        if (strcmp(path, "/events") == 0) {
            const char *header =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: keep-alive\r\n\r\n";
            send(client_sock, header, strlen(header), 0);

            // Send all persistent messages
            {
                lock_guard<mutex> lock(all_messages_mutex);
                for (const auto &message : all_sse_messages) {
                    string sse_message = "data: " + message + "\n\n";
                    send(client_sock, sse_message.c_str(), sse_message.size(), 0);
                }
            }

            // Add the client to the list of active SSE clients
            {
                lock_guard<mutex> lock(sse_clients_mutex);
                sse_clients.push_back(client_sock);
            }

            return;
        }

		if (strcmp(path, "/files-events") == 0) {
			const char *header =
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/event-stream\r\n"
				"Cache-Control: no-cache\r\n"
				"Connection: keep-alive\r\n\r\n";
			send(client_sock, header, strlen(header), 0);

			// Send all persistent file events
			{
				lock_guard<mutex> lock(all_file_events_mutex);
				for (const auto& file_event : all_file_events) {
					string sse_message = "data: " + file_event + "\n\n";
					send(client_sock, sse_message.c_str(), sse_message.size(), 0);
				}
			}

			{
				lock_guard<mutex> lock(sse_clients_mutex);
				sse_clients.push_back(client_sock);
			}
			return;
		}

        if (strcmp(path, "/") == 0) {
            strcpy(path, "/index.html");
        }

        char file_path[256];
        snprintf(file_path, sizeof(file_path), ".%s", path);

        int file = open(file_path, O_RDONLY);
        if (file < 0) {
            const char *response =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/html\r\n\r\n"
                "<html><body><h1>404 Not Found</h1></body></html>";
            send(client_sock, response, strlen(response), 0);
            close(client_sock);
            return;
        }

        struct stat file_stat;
        fstat(file, &file_stat);

        char header[512];
        snprintf(header, sizeof(header), 
                "HTTP/1.1 200 OK\r\n"
                "Content-Length: %ld\r\n"
                "Content-Type: text/html\r\n\r\n",
                file_stat.st_size);
        send(client_sock, header, strlen(header), 0);

        char file_buffer[4096];
        int bytes_read;
        while ((bytes_read = read(file, file_buffer, sizeof(file_buffer))) > 0) {
            send(client_sock, file_buffer, bytes_read, 0);
        }

        close(file);
    } else if (strcmp(path, "/uploaded-files") == 0) {
        lock_guard<mutex> lock(messages_mutex);
        string response = "[";
        for (size_t i = 0; i < uploadedFiles.size(); ++i) {
            response += "\"" + uploadedFiles[i] + "\"";
            if (i != uploadedFiles.size() - 1) response += ",";
        }
        response += "]";
        const char *header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n\r\n";
        send(client_sock, header, strlen(header), 0);
        send(client_sock, response.c_str(), response.size(), 0);
        close(client_sock);
        return;
    } else if (strcmp(path, "/files-events") == 0) {
        const char *header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n\r\n";
        send(client_sock, header, strlen(header), 0);

        {
            lock_guard<mutex> lock(sse_clients_mutex);
            sse_clients.push_back(client_sock);
        }
        return;
    }

    close(client_sock);
}

void *start_http_server(void *arg) {
    int http_port = *(int *)arg;

    int http_sockfd, client_sock;
    struct sockaddr_in http_serv_addr, http_cli_addr;
    socklen_t clilen;

    http_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (http_sockfd < 0) {
        perror("HTTP socket");
        pthread_exit(NULL);
    }

    memset((char *)&http_serv_addr, 0, sizeof(http_serv_addr));
    http_serv_addr.sin_family = AF_INET;
    http_serv_addr.sin_port = htons(http_port);
    http_serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(http_sockfd, (struct sockaddr *)&http_serv_addr, sizeof(struct sockaddr)) < 0) {
        perror("HTTP bind");
        close(http_sockfd);
        pthread_exit(NULL);
    }

    if (listen(http_sockfd, 5) < 0) {
        perror("HTTP listen");
        close(http_sockfd);
        pthread_exit(NULL);
    }

    printf("HTTP server listening on port %d\n", http_port);

    while (1) {
        clilen = sizeof(http_cli_addr);
        client_sock = accept(http_sockfd, (struct sockaddr *)&http_cli_addr, &clilen);
        if (client_sock < 0) {
            perror("HTTP accept");
            continue;
        }

        handle_http_request(client_sock);
    }

    close(http_sockfd);
    pthread_exit(NULL);
}

void usage(char *file) {
    fprintf(stderr, "Usage: %s server_port http_port\n", file);
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


void *start_upload_download_server(void *arg) {
    int main_port = *(int *)arg;

    int sockfd, newsockfd, portno = main_port;
    char buffer[MAX_LEN - sizeof(int)];
    struct sockaddr_in serv_addr, cli_addr;
    int n, i, ret;
    socklen_t clilen;
    fd_set read_fds;
    fd_set tmp_fds;
    int fdmax;

    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket");

    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(portno);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    ret = bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr));
    DIE(ret < 0, "bind");

    ret = listen(sockfd, MAX_CLIENTS);
    printf("Upload-download server listening on %d\n", portno);

    DIE(ret < 0, "listen");

    FD_SET(sockfd, &read_fds);
    fdmax = sockfd;

    while (1) {
        tmp_fds = read_fds;
        ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select");

        for (i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &tmp_fds)) {
                if (i == sockfd) {
                    clilen = sizeof(cli_addr);
                    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
                    DIE(newsockfd < 0, "accept");

                    FD_SET(newsockfd, &read_fds);
                    if (newsockfd > fdmax) {
                        fdmax = newsockfd;
                    }

                    printf("New connection from %s:%d, [%d]\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), newsockfd);
                } else { // message received from one of the connected clients
                    Msg msg;

                    memset(&msg, 0, MAX_LEN);
                    n = recv(i, &msg, MAX_LEN, 0);

                    // if it was a disconnect message
                    if (n == 0) {
                        printf("[%d] closed connection\n", i);
                        close(i);
                        FD_CLR(i, &read_fds);
                    }

                    Info* info;
                    int miner_destinatie;

                    if (msg.type == INFO_UPLOAD) {
                        info = (Info*) msg.payload;
                        printf("%s %d\n", info->fileName, info->nrPayloads);

                        // Check if file already exists
                        if (!fileExists(info->fileName)) {
                            uploadedFiles.push_back(info->fileName);

                            // Broadcast file uploaded event to SSE clients
    						broadcast_file_event(string(info->fileName));

                            // Send ACK to client
                            Msg m2;
                            m2.type = ACK;
                            send(i, &m2, sizeof(int), 0); // ACK
                        } else {
                            Msg m;
                            m.type = ALREADY_UPLOADED;
                            strcpy(m.payload, "This file is already uploaded!\n");
                            send(i, &m, sizeof(Msg), 0);
                            continue;
                        }

                        printf("The file is being uploaded! Send ACK!\n");

                        miner_destinatie = open(info->fileName, O_WRONLY | O_TRUNC | O_CREAT, 0644);
                        lseek(miner_destinatie, 0, SEEK_SET);

                        while (info->nrPayloads) {
                            Msg m;
                            recv(i, &m, sizeof(Msg), 0);
                            printf("\n writing %d bytes to server\n", m.size);

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
                            strcpy(m.payload, "Cannot download, file not present on server\n");
                            send(i, &m, sizeof(Msg), 0);
                            continue;
                        } else {
                            Msg m2;
                            m2.type = ACK;
                            send(i, &m2, sizeof(int), 0); // ACK
                            printf("Sending ACK\n");
                        }

                        printf("Downloading %s bytes\n", msg.payload);

                        miner_sursa = open(msg.payload, O_RDONLY);
                        fileSize = lseek(miner_sursa, 0, SEEK_END);
                        lseek(miner_sursa, 0, SEEK_SET);

                        Msg message;

                        int numberOfPayloads = 0;
                        printf("filesize= %d\n", fileSize);
                        if(fileSize % (MAX_LEN - sizeof(int)) == 0) {
                            numberOfPayloads = fileSize / (MAX_LEN - sizeof(int));
                        } else {
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
    }

    close(sockfd);
    pthread_exit(NULL);
}




int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage(argv[0]);
    }

    int main_port = atoi(argv[1]);
    int http_port = atoi(argv[2]);
    if (main_port == 0 || http_port == 0) {
        usage(argv[0]);
    }

    pthread_t http_thread, upload_download_thread;

    // Create HTTP server thread
    if (pthread_create(&http_thread, NULL, start_http_server, &http_port) != 0) {
        perror("HTTP thread creation failed");
        exit(1);
    }

    // Create upload/download server thread
    if (pthread_create(&upload_download_thread, NULL, start_upload_download_server, &main_port) != 0) {
        perror("Upload/Download thread creation failed");
        exit(1);
    }

    // Wait for threads to finish
    pthread_join(http_thread, NULL);
    pthread_join(upload_download_thread, NULL);

    return 0;
}
