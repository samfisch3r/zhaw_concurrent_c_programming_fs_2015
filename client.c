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

typedef struct
{
	const char *name;
	const char *port;
} config;

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
	printf("Usage: %s <name> [port]\n", argv);
	printf("- Name is a mandatory parameter\n");
	printf("- Port is an optional parameter, default is: 1234\n");
	exit(0);
}

config process_options(int argc, char const *argv[], config client)
{
	if (argc == 2)
	{
		client.name = argv[1];
		if (strcmp(client.name, ""))
			client.port = "1234";
		else
			printUsage(argv[0]);
	}
	else if (argc == 3)
	{
		client.name = argv[1];
		client.port = argv[2];
		if (!(atoi(client.port) && strcmp(client.name, "")))
			printUsage(argv[0]);
	}
	else
		printUsage(argv[0]);

	return client;
}

int main(int argc, char const *argv[])
{
	config client;
	client = process_options(argc, argv, client);

	const char *host = "127.0.0.1";

	struct addrinfo hints = init_hints(SOCK_STREAM, 0);

	struct addrinfo *servinfo = resolve_dns(&hints, host, client.port);
	sock_t sock = connect_socket_to_address(servinfo);

	if (sock.addr == NULL)
	{
		fprintf(stderr, "client: failed to connect\n");
		exit(2);
	}

	char server_in_addr_s[INET6_ADDRSTRLEN];
	void *server_in_addr = get_in_addr((struct sockaddr *)&sock.addr);
	inet_ntop(sock.addr->ai_family, server_in_addr, server_in_addr_s, sizeof server_in_addr_s);

	fprintf(stderr, "client: connecting to %s\n", server_in_addr_s);

	freeaddrinfo(servinfo);

	int sent;

	sent = send(sock.fd, "HELLO\n", 7, 0);
	if (sent < 0)
		perror("send");

	char buf[MAXDATA];
	int size = 0;

	int nbytes = recv(sock.fd, buf, MAXDATA - 1, 0);
	if (nbytes < 0)
	{
		perror("recv");
		exit(1);
	}
	buf[nbytes] = '\0';

	fprintf(stderr, "client: received %s", buf);

	if (strncmp(buf, "SIZE", 4) == 0)
	{
		size = atoi(buf+5);
		memset(buf, 0, sizeof(buf));

		while(1)
		{
			nbytes = recv(sock.fd, buf, MAXDATA - 1, 0);
			if (nbytes < 0)
			{
				perror("recv");
				exit(1);
			}
			buf[nbytes] = '\0';
			if (strcmp(buf, "START\n") == 0)
				break;
			if (strcmp(buf, "NACK\n") == 0)
			{
				close(sock.fd);
				exit(0);
			}
		}

		while(1)
		{
			int x,y;
			for (y = 0; y < size; ++y)
			{
				for (x = 0; x < size; ++x)
				{
					memset(buf, 0, sizeof(buf));

					char take[MAXDATA] = "TAKE ";
					char field[32];

					sprintf(field, "%d", x);
					strcat(field, " ");
					strcat(take, field);
					memset(field, 0, sizeof(field));

					sprintf(field, "%d", y);
					strcat(field, " ");
					strcat(take, field);
					memset(field, 0, sizeof(field));

					strcat(take, client.name);
					strcat(take, "\n");

					sent = send(sock.fd, take, sizeof(take), 0);
					if (sent < 0)
						perror("send");

					do
					{
						nbytes = recv(sock.fd, buf, MAXDATA - 1, 0);
						if (nbytes < 0)
						{
							perror("recv");
							exit(1);
						}
						
					} while(nbytes == 0);
					buf[nbytes] = '\0';
					fprintf(stderr, "client: received %s", buf);
					usleep(50000);
				}
			}
		}
	}

	close(sock.fd);

	return 0;
}