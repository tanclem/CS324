#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include "sockhelper.h"

/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_SIZE 1049000
#define BIG_BUFF_SIZE 102400
#define MAXEVENTS 64
#define MAX_LINE 2048
#define BUF_SIZE 500
#define MAXLINE 8192
#define SO_REUSEPORT 15

#define READ_REQUEST 0
#define SEND_REQUEST 1
#define READ_RESPONSE 2
#define SEND_RESPONSE 3

struct client_info {
	int fd;
	int sfd;
	int state;
	int in_req_offset;
	int out_req_offset;
	int in_res_offset;
	int out_res_offset;
	char *in_buf_ptr;
	char *out_buf_ptr;
	char *res_ptr;
	int n_read_from_c;
};

struct addrinfo hints;
struct addrinfo *result, *rp;
int sfd, connfd, s, j, portindex;
size_t len;
ssize_t nread;
char but[BUF_SIZE];
int hostindex = 2;
int af = AF_INET;
int addr_fam;
socklen_t addr_len = sizeof(struct sockaddr_storage);
struct sockaddr_in remote_addr_in;
struct sockaddr *remote_addr;
unsigned short remote_port;
struct sockaddr_in local_addr_in;
struct scokaddr *local_addr;
unsigned short local_port;

struct epoll_event event;
struct epoll_event events[MAXEVENTS];
int i;

struct client_info *new_client;
struct client_info *listener;
struct client_info *active_client;

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int complete_request_received(char *);
void parse_request(char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);
int open_sfd();
void handle_new_clients(int sfd, int _efd);
void handle_client(struct client_info *active_client, int _efd);

int main(int argc, char *argv[])
{
	int efd;
	if ((efd = epoll_create1(0)) < 0) {
		perror("Error with epoll_create1");
		exit(EXIT_FAILURE);
	}

	int sfd;
	if ((sfd = open_sfd(argc, argv)) < 0) {
		perror("opening file server socket");
		return -1;
	}

	listener = malloc(sizeof(struct client_info));
	listener->fd = sfd;

	event.data.ptr = listener;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(EXIT_FAILURE);
	}

	while (1) {
		int n;
		if ((n = epoll_wait(efd, events, MAXEVENTS, 1000)) < 0) {
			perror("epoll_wait");
			exit(1);
		}

		for (i = 0; i < n; i++) {
			active_client = (struct client_info *)(events[i].data.ptr);
			if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (events[i].events & EPOLLRDHUP)) {
				fprintf(stderr, "epoll error on fd %d\n", events[i].data.fd);
				close(events[i].data.fd);
				free(active_client);
				continue;
			}

			int _efd = efd;
			if (sfd == active_client->fd) {
				handle_new_clients(active_client->fd, _efd);
			}
			else {
				handle_client(active_client, _efd);
			}
		}
	}
	free(listener);
	return 0;
}

int open_sfd (int argc, char *argv[]) {
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

	if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		fprintf(stderr, "error setting socket option\n");
		exit(1);
	}

	if (bind(sfd, local_addr, local_addr_len) < 0) {
		perror("Could not bind");
		exit(EXIT_FAILURE);
	}

	if (listen(sfd, 100) < 0) {
		perror("Could not listen");
		exit(EXIT_FAILURE);
	}

	return sfd;
}

void handle_new_clients (int fd, int _efd) {
	int connfd;
	socklen_t remote_addr_len = sizeof(struct sockaddr_storage);

	while(1) {
		remote_addr_len = sizeof(struct sockaddr_storage);
		connfd = accept(fd, (struct sockaddr *)&remote_addr, &remote_addr_len);
		if (connfd < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) {
				break;
			} else {
				perror("accept");
				exit(EXIT_FAILURE);
			}
		}
		if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		struct client_info *_new_client = (struct client_info *)malloc(sizeof(struct client_info));
		_new_client->fd = connfd;
		_new_client->in_req_offset = 0;
		_new_client->out_req_offset = 0;
		_new_client->in_res_offset = 0;
		_new_client->out_res_offset = 0;

		char *request = (char *)malloc(BIG_BUFF_SIZE);
		char *fwd = (char *)malloc(BIG_BUFF_SIZE);
		memset(&request[0], 0, BIG_BUFF_SIZE);
		memset(&fwd[0], 0, BIG_BUFF_SIZE);
		_new_client->in_buf_ptr = &request[0];
		_new_client->out_buf_ptr = &fwd[0];

		char *response = (char *)malloc(BIG_BUFF_SIZE);
		memset(&response[0], 0, BIG_BUFF_SIZE);
		_new_client->res_ptr = &response[0];

		_new_client->state = READ_REQUEST;

		event.data.ptr = _new_client;
		event.events = EPOLLIN | EPOLLET;

		if (epoll_ctl(_efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
			fprintf(stderr, "error adding event\n");
			exit(1);
		}

	}
}

void handle_client(struct client_info *client, int efd) {
	if (client == NULL) return;

	if (client->state == READ_REQUEST) {
		int rbytes_read = 0;
		do {
			rbytes_read = recv(client->fd, client->in_buf_ptr + client->in_req_offset, 512, 0);
			if (rbytes_read == 0){
				break;
			}
			else if (rbytes_read < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					return;
				}
				else {
					perror("recv");
					close(client->fd);
					free(client);
					return;
				}
			}
			client->in_req_offset += rbytes_read;
		} while(!complete_request_received(client->in_buf_ptr) && (client->in_req_offset) < BIG_BUFF_SIZE);
		client->in_buf_ptr[client->in_req_offset] = 00;

		char method[16], hostname[64], port[8], path[64];
		parse_request(client->in_buf_ptr, method, hostname, port, path);
		strcat(client->out_buf_ptr, method);
		strcat(client->out_buf_ptr, " ");
		strcat(client->out_buf_ptr, path);
		strcat(client->out_buf_ptr, " ");
		strcat(client->out_buf_ptr, "HTTP/1.0\r\n");
		char host_header[100] = "Host: ";
		strcat(host_header, hostname);
		if(strcmp(port, "80") != 0) {
			strcat(host_header, ":");
			strcat(host_header, port);
		}
		strcat(host_header, "\r\n");
		strcat(client->out_buf_ptr, host_header);
		strcat(client->out_buf_ptr, user_agent_hdr);
		strcat(client->out_buf_ptr, "\r\n");
		strcat(client->out_buf_ptr, "Connection: close\r\n");
		strcat(client->out_buf_ptr, "Proxy-Connection: close\r\n");
		strcat(client->out_buf_ptr, "\r\n");

		int server_sfd2;
		struct addrinfo hints2, *result2, *rp2;
		struct sockaddr_in remote_addr_in2, local_addr_in2;
		socklen_t addr_len2;
		struct sockaddr *remote_addr2, *local_addr2;
		char local_addr_str[INET6_ADDRSTRLEN];
		char remote_addr_str[INET6_ADDRSTRLEN];

		memset(&hints2, 0, sizeof(struct addrinfo));
		hints2.ai_family = af;
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

		if (fcntl(server_sfd2, F_SETFL, fcntl(server_sfd2, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		client->sfd = server_sfd2;
		client->state = SEND_REQUEST;

		event.data.ptr = client;
		event.events = EPOLLOUT | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, client->sfd, &event) < 0) {
			perror("add event for writing");
			return;
		}

	}

	else if (client->state == SEND_REQUEST) {
		int total_sent = 0;
		int bytes_sent = 0;
		do {
			if (strlen(client->out_buf_ptr) - (client->out_req_offset + total_sent) < 512) {
				bytes_sent = write(client->sfd, client->out_buf_ptr + client->out_req_offset + total_sent, strlen(client->out_buf_ptr) - (client->out_req_offset + total_sent));
			}
			else {
				bytes_sent = write(client->sfd, client->out_buf_ptr + client->out_req_offset + total_sent, 512);
			}
			if (bytes_sent < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					client->out_req_offset += total_sent;
					return;
				}
				else {
					perror("send request");
					close(client->fd);
					close(client->sfd);
					free(client);
					return;
				}
			}
			total_sent += bytes_sent;
		} while (bytes_sent == 512 && (client->out_req_offset + total_sent) < strlen(client->out_buf_ptr));

		event.data.ptr = client;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_MOD, client->sfd, &event) < 0) {
			fprintf(stderr, "error adding event\n");
			exit(EXIT_FAILURE);
		}

		client->state = READ_RESPONSE;
	}

	else if (client->state == READ_RESPONSE) {
		int restotal_read = 0;
		int resbytes_read = 0;
		do {
			resbytes_read = recv(client->sfd, client->res_ptr + client->in_res_offset + restotal_read, 512, 0);
			if (resbytes_read == 0) {
				client->in_res_offset += restotal_read;
				close(client->sfd);
				event.data.ptr = client;
				event.events = EPOLLOUT | EPOLLET;
				if (epoll_ctl(efd, EPOLL_CTL_MOD, client->fd, &event) < 0) {
					perror("add event for writing");
					return;
				}

				client->state = SEND_RESPONSE;
				return;
			}
			else if (resbytes_read < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					client->in_res_offset += restotal_read;
					return;
				}
				else {
					perror("read response");
					close(client->fd);
					close(client->sfd);
					free(client);
					return;
				}
			}
			restotal_read += resbytes_read;
		} while ((client->in_res_offset + restotal_read) < BIG_BUFF_SIZE);
	}

	else if (client->state == SEND_RESPONSE) {
		int rtotal_sent = 0;
		int rbytes_sent = 0;
		do {
			if (client->in_res_offset - rtotal_sent < 512) {
				rbytes_sent = write(client->fd, client->res_ptr + client->out_res_offset + rtotal_sent, client->in_res_offset - (client->out_res_offset + rtotal_sent));
			}
			else {
				rbytes_sent = write(client->fd, client->res_ptr + client->out_req_offset + rtotal_sent, 512);
			}
			if (rbytes_sent < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					client->out_res_offset += rtotal_sent;
					return;
				}
				else {
					perror("send response");
					close(client->fd);
					free(client);
					return;
				}
			}
			rtotal_sent += rbytes_sent;
		} while ((client->out_res_offset + rtotal_sent) < client->in_res_offset);

		close(client->fd);
		free(client);
	}

	else {
		fprintf(stderr, "Can't Handle Client: No state set for request\n");
		return;
	}
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
