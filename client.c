#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define MAXDATA 256

struct sock_s
{
	int fd;
	struct addrinfo *addr;
};

typedef struct sock_s sock_t;

struct addrinfo init_hints(int sock_type, int flags)
{
	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = sock_type;
	if (flags)
		hints.ai_flags = flags;
	return hints;
}

struct addrinfo *resolve_dns(struct addrinfo *hints, const char* host, const char *port)
{
	struct addrinfo *servinfo;

	int err = getaddrinfo(host, port, hints, &servinfo);
	if (err)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
		exit(1);
	}
	return servinfo;
}

void *get_in_addr(struct sockaddr *sa)
{
	return sa->sa_family == AF_INET
		? (void *) &(((struct sockaddr_in*)sa)->sin_addr)
		: (void *) &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static sock_t connect_socket_to_address(struct addrinfo *servinfo)
{
	struct addrinfo *p;
	int sockfd;
	for (p = servinfo; p != NULL; p = p->ai_next)
	{
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd == -1)
		{
			perror("client: socket");
			continue;
		}

		int err = connect(sockfd, p->ai_addr, p->ai_addrlen);
		if (err)
		{
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}
	return (sock_t) { sockfd, p };
}

void printUsage(char const argv[])
{
	printf("Usage: %s [port]\n", argv);
	printf("- Port is an optional parameter, default is: 1234\n");
	exit(0);
}

const char* process_options(int argc, char const *argv[])
{
	const char *port;
	if (argc == 1)
		port = "1234";
	else if (argc == 2)
	{
		port = argv[1];
		if (!(atoi(port)))
			printUsage(argv[0]);
	}
	else
		printUsage(argv[0]);

	return port;
}

int main(int argc, char const *argv[])
{
	const char *port = process_options(argc, argv);

	const char *host = argc == 2 ? argv[1] : "localhost";

	struct addrinfo hints = init_hints(SOCK_STREAM, 0);
	struct addrinfo *servinfo = resolve_dns(&hints, host, port);
	sock_t sock = connect_socket_to_address(servinfo);

	if (sock.addr == NULL)
	{
		fprintf(stderr, "client: failed to connect\n");
		exit(2);
	}

	char server_in_addr_s[INET6_ADDRSTRLEN];
	void *server_in_addr = get_in_addr((struct sockaddr *)&sock.addr);
	inet_ntop(sock.addr->ai_family, server_in_addr, server_in_addr_s, sizeof server_in_addr_s);

	printf("client: connecting to %s\n", server_in_addr_s);

	freeaddrinfo(servinfo);

	char buf[MAXDATA];
	int nbytes = recv(sock.fd, buf, MAXDATA - 1, 0);
	if (nbytes < 0)
	{
		perror("recv");
		exit(1);
	}
	buf[nbytes] = '\0';

	printf("client: received '%s'\n", buf);

	close(sock.fd);

	return 0;
}