// Replace PUT_USERID_HERE with your actual BYU CS user id, which you can find
// by running `id -u` on a CS lab machine.
#define USERID 1823702043

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "sockhelper.h"

int verbose = 0;

void print_bytes(unsigned char *bytes, int byteslen);

int main(int argc, char *argv[]) {

	int level = atoi(argv[3]);
	int seed = atoi(argv[4]);
//	int port = atoi(argv[2]);
	char* serverstr = argv[1];

//	printf("level: %d\n", level);
//	printf("seed: %d\n", seed);
//	printf("port: %d\n", port);
//	printf("server: %s\n", serverstr);

	unsigned char first_req[8];
	unsigned long user_id = htonl(USERID);
	unsigned short short_seed = htons(seed);

	first_req[0] = 0;
	first_req[1] = level;
	memcpy(&first_req[2], &user_id, 4);
	memcpy(&first_req[6], &short_seed, 2);

//	print_bytes(first_req, 8);

	int addr_fam = AF_INET;
	int ai_socktype = SOCK_DGRAM;
	socklen_t addr_len;
	int sfd, s;

//	struct sockaddr_in serv_addr;
	struct sockaddr_in remote_addr_in;
	struct sockaddr_in6 remote_addr_in6;
	struct sockaddr *remote_addr;

	struct sockaddr_in local_addr_in;
	struct sockaddr_in6 local_addr_in6;
	struct sockaddr *local_addr;

	struct addrinfo hints;
	struct addrinfo *result;

//	unsigned short remote_port;
//	unsigned short local_port;

	char remote_addr_str[INET6_ADDRSTRLEN];
	char local_addr_str[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = addr_fam;
	hints.ai_socktype = ai_socktype;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	s = getaddrinfo(argv[1], argv[2], &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}
	sfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

	addr_fam = result->ai_family;
	addr_len = result->ai_addrlen;
	if(addr_fam == AF_INET) {
		remote_addr_in = *(struct sockaddr_in *)result->ai_addr;
		inet_ntop(addr_fam, &remote_addr_in.sin_addr, serverstr, addr_len);
//		unsigned short remote_port = ntohs(remote_addr_in.sin_port);
		remote_addr = (struct sockaddr *)&remote_addr_in;
		local_addr = (struct sockaddr *)&local_addr_in;
	} else {
		remote_addr_in6 = *(struct sockaddr_in6 *)result->ai_addr;
		inet_ntop(addr_fam, &remote_addr_in6.sin6_addr, remote_addr_str, addr_len);
//		unsigned short remote_port = ntohs(remote_addr_in6.sin6_port);
		remote_addr = (struct sockaddr *)&remote_addr_in6;
		local_addr = (struct sockaddr *)&local_addr_in6;
	}

	if (result == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

	s = getsockname(sfd, local_addr, &addr_len);
	if (addr_fam == AF_INET) {
		inet_ntop(addr_fam, &local_addr_in.sin_addr, local_addr_str, addr_len);
//		unsigned short local_port = ntohs(local_addr_in.sin_port);
	} else {
		inet_ntop(addr_fam, &local_addr_in6.sin6_addr, local_addr_str, addr_len);
//		unsigned short local_port = ntohs(local_addr_in6.sin6_port);
	}

	char treasure[1024];
	int t_ptr = 0;

	sendto(sfd, first_req, 8, 0, remote_addr, addr_len);
	unsigned char response_0[256];
	int r = recvfrom(sfd, response_0, 256, 0, NULL, NULL);
	if (r < 0){
		perror("Read server response");
		exit(-1);
	}

//	printf("response size: %d\n", r);
//	print_bytes(response_0, 256);

	int chunk_len_0 = response_0[0];
	if (chunk_len_0 == 0){
		printf("No TrEaSuRe fOr yOu!\n");
		return 0;
	}
	else if (chunk_len_0 > 127) {
		printf("Error getting response... Error: %d\n", chunk_len_0);
		return -1;
	}

	memcpy(&treasure[t_ptr], &response_0[1], chunk_len_0);
	treasure[t_ptr + chunk_len_0] = 0;
	t_ptr += chunk_len_0;
	int op_code_0 = response_0[chunk_len_0 + 1];
	unsigned short op_param_0;
	memcpy(&op_param_0, &response_0[chunk_len_0 + 2], 2);
	unsigned short op_param_rev_0 = ntohs(op_param_0);

	if (op_code_0 == 1) {
		remote_addr_in.sin_port = htons(op_param_rev_0);
	}
	else if (op_code_0 == 2) {
		close(sfd);

		sfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

		local_addr_in.sin_family = AF_INET;
		local_addr_in.sin_port = htons(op_param_rev_0);
		local_addr_in.sin_addr.s_addr = 0;

		local_addr_in6.sin6_family = AF_INET6;
		local_addr_in6.sin6_port = htons(op_param_rev_0);
		bzero(local_addr_in6.sin6_addr.s6_addr, 16);

		if (bind(sfd, local_addr, addr_len) < 0) {
			perror("bind()");
		}
	}

	unsigned int nonce_0;
	memcpy(&nonce_0, &response_0[chunk_len_0 + 4], 4);

	char request[4];
	unsigned int nonce_0_h = ntohl(nonce_0);
	nonce_0_h += 1;

	if (op_code_0 == 3) {
		unsigned short m = op_param_rev_0;
		unsigned int sum = 0;
		char buff[1];
		struct sockaddr_in new_port;
		int len = sizeof(new_port);

		for (unsigned int i = 0; i < m; i++) {
			recvfrom(sfd, buff, 1, 0, (struct sockaddr *)&new_port, (socklen_t *)&len);
			sum += ntohs(new_port.sin_port);
		}

		nonce_0_h = sum + 1;
	}

	unsigned int nonce_rev_0 = htonl(nonce_0_h);
	memcpy(&request, &nonce_rev_0, 4);

	int chunk_len = 0;

	do {
		sendto(sfd, request, 4, 0, remote_addr, addr_len);
		unsigned char response[256];
		int r = recvfrom(sfd, response, 256, 0, NULL, NULL);
		if (r < 0){
			perror("Read server response");
			exit(-1);
		}

		chunk_len = response[0];
		if (chunk_len > 127){
			printf("Error getting response... Error %d\n", chunk_len);
			return -1;
		}

		memcpy(&treasure[t_ptr], &response[1], chunk_len);
		treasure[t_ptr + chunk_len] = 0;
		t_ptr += chunk_len;

		int op_code = response[chunk_len + 1];
		unsigned short op_param;
		memcpy(&op_param, &response[chunk_len + 2], 2);
		unsigned short op_param_rev = ntohs(op_param);

		if (op_code == 1){
			remote_addr_in.sin_port = htons(op_param_rev);
		}
		else if (op_code == 2) {
			close(sfd);

			sfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);

			local_addr_in.sin_family = AF_INET;
			local_addr_in.sin_port = htons(op_param_rev);
			local_addr_in.sin_addr.s_addr = 0;

			local_addr_in6.sin6_family = AF_INET6;
			local_addr_in6.sin6_port = htons(op_param_rev);
			bzero(local_addr_in6.sin6_addr.s6_addr, 16);

			if (bind(sfd, local_addr, addr_len) < 0) {
				perror("bind()");
			}
		}
		else if (op_code == 4) {
			break;
		}

		unsigned int nonce;
		memcpy(&nonce, &response[chunk_len + 4], 4);
		unsigned int nonce_h = ntohl(nonce);
		nonce_h += 1;

		if (op_code == 3) {
			unsigned short m = op_param_rev;
			unsigned int sum = 0;
			char buff[1];
			struct sockaddr_in new_port;
			int len = sizeof(new_port);

			for (unsigned int i = 0; i < m; i++) {
				recvfrom(sfd, buff, 1, 0, (struct sockaddr *)&new_port, (socklen_t *)&len);
				sum += ntohs(new_port.sin_port);
			}

			nonce_h = sum + 1;
		}

		unsigned int nonce_rev = htonl(nonce_h);
		memcpy(&request, &nonce_rev, 4);
	} while (chunk_len > 0);

	printf("%s\n", treasure);

	return 0;

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
