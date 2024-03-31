#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "sockhelper.h"

/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400
#define MAX_LINE 256
#define SO_REUSEPORT 15
#define BIG_BUFF 50000
#define NTHREADS 8
#define SBUFSIZE 5

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int complete_request_received(char *);
void parse_request(char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);
int open_sfd();
void handle_client(int);
void *handle_clients(void *);

size_t len;
ssize_t nread;
char buf[500];
int hostindex = 2;
int sfd, connfd, s, j, portindex;
struct addrinfo hints;
struct addrinfo *result, *rp;
int addr_fam;
socklen_t addr_len = sizeof(struct sockaddr_storage);
struct sockaddr_in remote_addr_in;
struct sockaddr *remote_addr;
unsigned short remote_port;
struct sockaddr_in local_addr_in;
struct sockaddr *local_addr;
unsigned short local_port;

/* $begin sbuft*/
typedef struct {
	int *buf;
	int n;
	int front;
	int rear;
	sem_t mutex;
	sem_t slots;
	sem_t items;
} sbuf_t;
/* $end sbuft */

void sbuf_init1(sbuf_t *sp, int n);
void sbuf_deinit1(sbuf_t *sp);
void sbuf_insert1(sbuf_t *sp, int item);
int sbuf_remove1(sbuf_t *sp);


void sbuf_init1(sbuf_t *sp, int n) {
	sp->buf = calloc(n, sizeof(int));
	sp->n = n;
	sp->front = sp->rear = 0;
	sem_init(&sp->mutex, 0, 1);
	sem_init(&sp->slots, 0, n);
	sem_init(&sp->items, 0, 0);
}

void sbuf_deinit1(sbuf_t *sp) {
	free(sp->buf);
}

void sbuf_insert1(sbuf_t *sp, int item) {
	sem_wait(&sp->slots);
	sem_wait(&sp->mutex);
	sp->buf[(++sp->rear)%(sp->n)] = item;
	sem_post(&sp->mutex);
	sem_post(&sp->items);
}


int sbuf_remove1(sbuf_t *sp) {
	int item;
	sem_wait(&sp->items);
	sem_wait(&sp->mutex);
	item = sp->buf[(++sp->front)%(sp->n)];
	sem_post(&sp->mutex);
	sem_post(&sp->slots);
	return item;
}

sbuf_t sbuf;



int main(int argc, char *argv[])
{
	pthread_t tid;

//	test_parser();
	if((sfd = open_sfd(argc, argv)) < 0) {
		perror("opening file server socket");
		return -1;
	}

	sbuf_init1(&sbuf, SBUFSIZE);
	for (int i = 0; i < NTHREADS; i++)
		pthread_create(&tid, NULL, handle_clients, NULL);

	while(1) {
		connfd = accept(sfd, remote_addr, &addr_len);
		sbuf_insert1(&sbuf, connfd);
	}

//	printf("%s\n", user_agent_hdr);
	return 0;
}

int complete_request_received(char *request) {
	return !strstr(request, "\r\n\r\n") ? 0 : 1;
}

void parse_request(char *request, char *method,
		char *hostname, char *port, char *path) {

	int meth_1 = sizeof(method);
	memset(&method[0], 0, meth_1);
	int host_1 = sizeof(hostname);
	memset(&hostname[0], 0, host_1);
	int port_1 = sizeof(port);
	memset(&port[0], 0, port_1);
	int path_1 = sizeof(path);
	memset(&path[0], 0, path_1);

	char *first_sp = strchr(request, ' ');
	strncpy(method, request, first_sp - request);

	char url[MAX_LINE];
	char *sec_sp = strchr(first_sp + 1, ' ');
	strncpy(url, first_sp + 1, (sec_sp - 1) - first_sp);
	url[(sec_sp - 1) - first_sp] = 00;

	char *slashes = strstr(url, "//");
	char *sec_colon = strchr(slashes + 2, ':');
	char *third_fs = strchr(slashes + 2, '/');
	if (!sec_colon) {
		strncpy(hostname, slashes + 2, (third_fs - 1) - (slashes + 1));
		strcpy(port, "80");
	}
	else {
		strncpy(hostname, slashes + 2, (sec_colon - 1) - (slashes + 1));
		hostname[(sec_colon - 1) - (slashes + 1)] = 00;
		strncpy(port, sec_colon + 1, third_fs - (sec_colon + 1));
		port[third_fs - (sec_colon + 1)] = 00;
	}
	strcpy(path, third_fs);
//	char *end_f_line = strstr(request, "\r\n");
//	strcpy(headers, end_f_line + 2);

	return;

}

void test_parser() {
	int i;
	char method[16], hostname[64], port[8], path[64];

       	char *reqs[] = {
		"GET http://www.example.com/index.html HTTP/1.0\r\n"
		"Host: www.example.com\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html?foo=1&bar=2 HTTP/1.0\r\n"
		"Host: www.example.com:8080\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://localhost:1234/home.html HTTP/1.0\r\n"
		"Host: localhost:1234\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html HTTP/1.0\r\n",

		NULL
	};

	for (i = 0; reqs[i] != NULL; i++) {
		printf("Testing %s\n", reqs[i]);
		if (complete_request_received(reqs[i])) {
			printf("REQUEST COMPLETE\n");
			parse_request(reqs[i], method, hostname, port, path);
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("PATH: %s\n", path);
		} else {
			printf("REQUEST INCOMPLETE\n");
		}
	}
}

int open_sfd(int argc, char *argv[]) {
	int portindex;
	unsigned short port;
	int address_family;
	int sock_type;
	struct sockaddr_in ipv4addr;
	int sfd;
	struct sockaddr *local_addr;
	socklen_t local_addr_len;

	portindex = 1;
	port = atoi(argv[portindex]);
	address_family = AF_INET;
	sock_type = SOCK_STREAM;

	ipv4addr.sin_family = address_family;
	ipv4addr.sin_addr.s_addr = INADDR_ANY;
	ipv4addr.sin_port = htons(port);

	local_addr = (struct sockaddr *)&ipv4addr;
	local_addr_len = sizeof(ipv4addr);

	if((sfd = socket(address_family, sock_type, 0)) < -1) {
		perror("Error creating socket");
		exit(EXIT_FAILURE);
	}

	int optval = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
	if (bind(sfd, local_addr, local_addr_len) < 0){
		perror("Could not bind");
		exit(EXIT_FAILURE);
	}

	listen(sfd, 100);
	return sfd;
}


void handle_client(int connfd) {
	char request[BIG_BUFF], fwd[BIG_BUFF];
	memset(&request[0], 0, BIG_BUFF);
	memset(&fwd[0], 0, BIG_BUFF);
	int rtotal_read = 0;
	int rbytes_read = 0;
//	int debug = 0;
	do {
		rbytes_read = recv(connfd, request + rtotal_read, 512, 0);
		rtotal_read += rbytes_read;
		if(rbytes_read <= 0){
			break;
		}
	} while(!complete_request_received(request) && rbytes_read > 0 && rtotal_read <= BIG_BUFF);
	request[rtotal_read] = 00;

//	printf("Request Received: \n%s\n", request);
//	print_bytes(request, sizeof(request));
	char *method, *hostname, *port, *path;
	method = (char *) malloc(16);
	hostname = (char *) malloc(64);
	port = (char *) malloc(8);
	path = (char *) malloc(64);
//	headers = (char *) malloc(1024);
	parse_request(request, method, hostname, port, path);

	strcat(fwd, method);
	strcat(fwd, " ");
	strcat(fwd, path);
	strcat(fwd, " ");
	strcat(fwd, "HTTP/1.0\r\n");
	char host_header[100] = "Host: ";
	strcat(host_header, hostname);
	if(strcmp(port, "80") != 0) {
		strcat(host_header, ":");
		strcat(host_header, port);
	}
	strcat(host_header, "\r\n");
	strcat(fwd, host_header);
	strcat(fwd, user_agent_hdr);
	strcat(fwd, "\r\n");
	strcat(fwd, "Connection: close\r\n");
	strcat(fwd, "Proxy-Connection: close\r\n");
	strcat(fwd, "\r\n");




	int server_sfd2;
	struct addrinfo hints2, *result2, *rp2;
	struct sockaddr_in remote_addr_in2, local_addr_in2;
	socklen_t addr_len2;
	struct sockaddr *remote_addr2, *local_addr2;
	char local_addr_str[INET6_ADDRSTRLEN];
	char remote_addr_str[INET6_ADDRSTRLEN];

	memset(&hints2, 0, sizeof(struct addrinfo));
	hints2.ai_family = AF_INET;
	hints2.ai_socktype = SOCK_STREAM;
	hints2.ai_flags = 0;
	hints2.ai_protocol = 0;

	s = getaddrinfo(hostname, port, &hints2, &result2);
	if(s != 0) {
		perror("server getaddrinfo");
		exit(EXIT_FAILURE);
	}

	for (rp2 = result2; rp2 != NULL; rp = rp2->ai_next) {
		server_sfd2 = socket(rp2->ai_family, rp2->ai_socktype, rp2->ai_protocol);
		if(server_sfd2 == -1) {
			printf("bad fd\n");
			continue;
		}

		addr_fam = rp2->ai_family;
		addr_len2 = rp2->ai_addrlen;

		remote_addr_in2 = *(struct sockaddr_in *)rp2->ai_addr;
		inet_ntop(addr_fam, &remote_addr_in2.sin_addr, remote_addr_str, addr_len2);
		remote_addr2 = (struct sockaddr *)&remote_addr_in2;
		local_addr2 = (struct sockaddr *)&local_addr_in2;

		if(connect(server_sfd2, remote_addr2, addr_len2) != -1)
			break;

		close(server_sfd2);
	}

	if(rp2 == NULL) {
		fprintf(stderr, "Could not connect to server\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result2);

	s = getsockname(server_sfd2, local_addr2, &addr_len2);
	inet_ntop(addr_fam, &local_addr_in2.sin_addr, local_addr_str, addr_len2);

	int total_sent = 0;
	int bytes_sent = 0;
	do {
		if(strlen(fwd) - total_sent < 512) {
			bytes_sent = write(server_sfd2, fwd + total_sent, strlen(fwd) - total_sent);
		}
		else {
			bytes_sent = write(server_sfd2, fwd + total_sent, 512);
		}
		if(bytes_sent < 0) {
			break;
		}
		total_sent += bytes_sent;
	} while(bytes_sent == 512 && total_sent <= strlen(fwd));

	char response[BIG_BUFF];
	memset(&response[0], 0, BIG_BUFF);
	int restotal_read = 0;
	int resbytes_read = 0;
	do {
		resbytes_read = recv(server_sfd2, response + restotal_read, 512, 0);
		if(resbytes_read < 0) {
			perror("reading from server");
			break;
		}
		restotal_read += resbytes_read;
	} while(resbytes_read > 0 && restotal_read <= BIG_BUFF);

	close(server_sfd2);

	if(write(connfd, response, restotal_read) < 0) {
		perror("returning response to client");
	}

	free(hostname);
	free(port);
	free(path);
//	free(headers);
	sleep(2);

}

void *handle_clients(void *vargp) {
	pthread_detach(pthread_self());
	while(1) {
		int connfd = sbuf_remove1(&sbuf);
		handle_client(connfd);
		close(connfd);
	}
}

void print_bytes(unsigned char *bytes, int byteslen) {
	int i, j, byteslen_adjusted;

	if (byteslen % 8) {
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	} else {
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++) {
		if (!(i % 8)) {
			if (i > 0) {
				for (j = i - 8; j < i; j++) {
					if (j >= byteslen_adjusted) {
						printf("  ");
					} else if (j >= byteslen) {
						printf("  ");
					} else if (bytes[j] >= '!' && bytes[j] <= '~') {
						printf(" %c", bytes[j]);
					} else {
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted) {
				printf("\n%02X: ", i);
			}
		} else if (!(i % 4)) {
			printf(" ");
		}
		if (i >= byteslen_adjusted) {
			continue;
		} else if (i >= byteslen) {
			printf("   ");
		} else {
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
	fflush(stdout);
}
