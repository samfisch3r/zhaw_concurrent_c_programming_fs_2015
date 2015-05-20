#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#define CHECK 5
#define MAXDATA 256

typedef struct
{
	int size;
	const char *port;
} config;

typedef struct
{
	int free;
} field;

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

void *get_in_addr(struct sockaddr *sa)
{
	return sa->sa_family == AF_INET
		? (void *) &(((struct sockaddr_in*)sa)->sin_addr)
		: (void *) &(((struct sockaddr_in6*)sa)->sin6_addr);
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

static void allow_port_reuse (int sockfd)
{
	int yes = 1;

	int err = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	if (err)
	{
		perror("setsockopt");
		exit(1);
	}
}

sock_t bind_socket_to_address(struct addrinfo* servinfo)
{
	struct addrinfo *p;
	int sockfd;

	for (p = servinfo; p != NULL; p = p->ai_next)
	{
		sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sockfd == -1)
		{
			perror("server: socket");
			continue;
		}

		allow_port_reuse(sockfd);

		int err = bind(sockfd, p->ai_addr, p->ai_addrlen);
		if (err)
		{
			close(sockfd);
			perror("server: bind");
			continue;
		}
		break;
	}
	return (sock_t) { sockfd, p };
}

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

static void reap_dead_processes()
{
	struct sigaction sa;
	sa.sa_handler = sigchld_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	int err = sigaction(SIGCHLD, &sa, NULL);
	if (err)
	{
		perror("sigaction");
		exit(1);
	}
}

static void listen_on(int sockfd, int backlog)
{
	int err = listen(sockfd, backlog);
	if (err)
	{
		perror("listen");
		exit(1);
	}
}

static void accept_clients(int sockfd, int size)
{
	struct sockaddr_storage client_addr;
	socklen_t sin_size;
	int client_sock_fd;
	char client_in_addr_s[INET6_ADDRSTRLEN];
	int is_child_process;

	while(1)
	{
		sin_size = sizeof client_addr;
		client_sock_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);

		if (client_sock_fd == -1)
		{
			perror("accept");
			continue;
		}

		void *client_in_addr = get_in_addr((struct sockaddr *)&client_addr);

		inet_ntop(client_addr.ss_family, client_in_addr, client_in_addr_s, sizeof client_in_addr_s);
		printf("server: got connection from %s\n", client_in_addr_s);

		is_child_process = !fork();
		if (is_child_process)
		{
			close(sockfd);


			char buf[MAXDATA];
			int nbytes = recv(client_sock_fd, buf, MAXDATA - 1, 0);
			if (nbytes < 0)
			{
				perror("recv");
				exit(1);
			}
			buf[nbytes] = '\0';

			printf("server: received %s", buf);

			char number[32];
			sprintf(number, "%d", size);

			char size_string[64] = "SIZE ";
			strcat(size_string, number);
			strcat(size_string, "\n");

			int sent = send(client_sock_fd, size_string, sizeof(size_string), 0);
			if (sent < 0)
				perror("send");

			close(client_sock_fd);
			exit(0);
		}
		close(client_sock_fd);
	}
}

void printUsage(char const argv[])
{
	printf("Usage: %s <size> [port]\n", argv);
	printf("- Size has to be at least 4\n");
	printf("- Port is an optional parameter, default is: 1234\n");
	exit(0);
}

config process_options(int argc, char const *argv[], config server)
{
	if (argc == 2)
	{
		server.size = atoi(argv[1]);
		if (server.size >= 4)
			server.port = "1234";
		else
			printUsage(argv[0]);
	}
	else if (argc == 3)
	{
		server.size = atoi(argv[1]);
		server.port = argv[2];
		if (!((server.size >= 4) && atoi(server.port)))
			printUsage(argv[0]);
	}
	else
		printUsage(argv[0]);

	return server;
}

field **playground;

void create_field(int size)
{
	int i;
	playground = (field **) malloc(sizeof(field *) * size);
	for (i = 0; i < size; ++i)
		playground[i] = (field *) malloc(sizeof(field) * size);
}

int main(int argc, char const *argv[])
{
	config server;
	server = process_options(argc, argv, server);

	struct addrinfo hints = init_hints(SOCK_STREAM, AI_PASSIVE);

	struct addrinfo *servinfo = resolve_dns(&hints, NULL, server.port);

	sock_t sock = bind_socket_to_address(servinfo);

	if (sock.addr == NULL)
	{
		fprintf(stderr, "server: failed to bind to any of the resolved addresses\n");
		exit(2);
	}

	freeaddrinfo(servinfo);

	create_field(server.size);

	listen_on(sock.fd, 10);

	reap_dead_processes();

	printf("server: waiting for connections on port %s ...\n", server.port);

	accept_clients(sock.fd, server.size);

	return 0;
}