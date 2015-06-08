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
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>

#define CHECK 2 // set between 1 and 30
#define MAXDATA 256
#define MAXPLAYER 32767

typedef struct
{
	int size;
	const char *port;
} config;

typedef struct
{
	sem_t *lock;
	char name[256];
} field;

struct sock_s
{
	int fd;
	struct addrinfo *addr;
};

typedef struct sock_s sock_t;

field *playground;
int *playercount;
char *end;

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

int check_field(int size)
{
	int x,y;
	int stage = 0;
	int fail = 0;
	char *lastfield;

	do
	{
		for (y = 0; y < size; ++y)
		{
			for (x = 0; x < size; ++x)
			{
				if (!stage)
				{
					if (sem_wait(playground[x+y*size].lock))
						perror("sem_wait");
				}
				else if (stage == 1)
				{
					if (x+y*size != 0)
					{
						if ((strcmp(playground[x+y*size].name, lastfield) != 0) || (strcmp(playground[x+y*size].name, "") == 0))
							fail++;
					}
					// fprintf(stderr, "FIELD X=%i, Y=%i, NAME=%s\n", x, y, playground[x+y*size].name);
					lastfield = playground[x+y*size].name;
				}
				else
				{
					if (sem_post(playground[x+y*size].lock))
						perror("sem_post");
				}
			}
		}
		stage++;
	} while (stage < 3);
	if (!fail)
		strcpy(end, lastfield);
	return fail;
}

static void accept_clients(int sockfd, int size)
{
	struct sockaddr_storage client_addr;
	socklen_t sin_size;
	int client_sock_fd;
	char client_in_addr_s[INET6_ADDRSTRLEN];
	int is_child_process;

	sem_t *playcountlock = sem_open("playerSem", O_CREAT | O_EXCL, 0644, 0);
	if (playcountlock == SEM_FAILED)
		perror("sem_open");
	if(sem_unlink("playerSem"))
		perror("sem_unlink");
	sem_post(playcountlock);

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
		fprintf(stderr, "server: got connection from %s\n", client_in_addr_s);

		is_child_process = !fork();
		if (is_child_process)
		{
			close(sockfd);

			char buf[MAXDATA];
			int nbytes;
			int sent;

			nbytes = recv(client_sock_fd, buf, MAXDATA - 1, 0);
			if (nbytes < 0)
			{
				perror("recv");
				exit(1);
			}

			buf[nbytes] = '\0';
			fprintf(stderr, "server: received %s", buf);

			if (strcmp(buf, "HELLO\n") == 0)
			{
				if (*playercount == MAXPLAYER)
				{
					sent = send(client_sock_fd, "NACK\n", 6, 0);
					if (sent < 0)
						perror("send");
					close(client_sock_fd);
					exit(0);
				}

				if (sem_wait(playcountlock))
					perror("sem_wait");
				*playercount += 1;
				if (sem_post(playcountlock))
					perror("sem_post");

				char number[32];
				sprintf(number, "%d", size);

				char size_string[64] = "SIZE ";
				strcat(size_string, number);
				strcat(size_string, "\n");

				sent = send(client_sock_fd, size_string, sizeof(size_string), 0);
				if (sent < 0)
					perror("send");
				
			}
			else
			{
				close(client_sock_fd);
				exit(0);
			}

			while(*playercount < size/2)
			{
				// wait for enough players
			}
			usleep(1);

			sent = send(client_sock_fd, "START\n", 7, 0);
			if (sent < 0)
				perror("send");

			while(1)
			{
				memset(buf, 0, sizeof(buf));

				nbytes = recv(client_sock_fd, buf, MAXDATA - 1, 0);
				if (nbytes < 0)
				{
					perror("recv");
					exit(1);
				}

				if (nbytes > 1)
				{
					buf[nbytes] = '\0';
					fprintf(stderr, "server: received %s", buf);

					if (strcmp(end, "") != 0)
					{
						char won[256] = "END ";
						strcat(won, end);
						sent = send(client_sock_fd, won, sizeof(won), 0);
						if (sent < 0)
							perror("send");
						break;
					}

					char string[256];
					char delimiter[] = " ";
					char *ptr;

					strncpy(string, buf, nbytes);

					ptr = strtok(string, delimiter);

					ptr = strtok(NULL, delimiter);
					int x = atoi(ptr);
					ptr = strtok(NULL, delimiter);
					int y = atoi(ptr);

					if (strncmp(buf, "TAKE", 4) == 0)
					{
						char *ret;
						ptr = strtok(NULL, delimiter);

						if (sem_trywait(playground[x+y*size].lock) < 0)
							ret = "INUSE\n";
						else
						{
							strcpy(playground[x+y*size].name, ptr);
							ret = "TAKEN\n";
							if (sem_post(playground[x+y*size].lock))
								perror("sem_post");
						}

						sent = send(client_sock_fd, ret, sizeof(ret), 0);
						if (sent < 0)
							perror("send");
					}

					if (strncmp(buf, "STATUS", 6) == 0)
					{
						char name[256];
						if (sem_wait(playground[x+y*size].lock))
							perror("sem_wait");
						strcpy(name, playground[x+y*size].name);
						if (sem_post(playground[x+y*size].lock))
							perror("sem_post");
						sent = send(client_sock_fd, name, sizeof(name), 0);
						if (sent < 0)
							perror("send");
					}
				}
			}

			close(client_sock_fd);

			if (sem_wait(playcountlock))
				perror("sem_wait");
			*playercount -= 1;
			if (sem_post(playcountlock))
				perror("sem_post");

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

void create_field(int size)
{
	int i;
	int x,y;
	for (y = 0; y < size; ++y)
	{
		for (x = 0; x < size; ++x)
		{
			char number[12];
			sprintf(number, "%d", x+y*size);
			char lock_name[16] = "Lock";
			strcat(lock_name, number);
			playground[x+y*size].lock = sem_open(lock_name, O_CREAT | O_EXCL, 0644, 0);
			if (playground[x+y*size].lock == SEM_FAILED)
				perror("sem_open");
			if(sem_unlink(lock_name))
				perror("sem_unlink");
			strcpy(playground[x+y*size].name, "");
			if(sem_post(playground[x+y*size].lock))
				perror("sem_post");
		}
	}
}

int main(int argc, char const *argv[])
{
	config server;
	server = process_options(argc, argv, server);

	end = mmap(NULL, sizeof(*end), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (end == MAP_FAILED)
	{
		perror("mmap");
		exit(1);
	}
	strcpy(end, "");

	struct addrinfo hints = init_hints(SOCK_STREAM, AI_PASSIVE);

	struct addrinfo *servinfo = resolve_dns(&hints, NULL, server.port);

	sock_t sock = bind_socket_to_address(servinfo);

	if (sock.addr == NULL)
	{
		fprintf(stderr, "server: failed to bind to any of the resolved addresses\n");
		exit(2);
	}

	freeaddrinfo(servinfo);

	playercount = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (playercount == MAP_FAILED)
	{
		perror("mmap");
		exit(1);
	}
	*playercount = 0;

	playground = malloc(sizeof(field) * server.size);

	playground = mmap(NULL, sizeof(*playground), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (playground == MAP_FAILED)
	{
		perror("mmap");
		exit(1);
	}

	create_field(server.size);

	int is_child_process;
	is_child_process = !fork();
	if (is_child_process)
	{
		while(1)
		{
			if(!check_field(server.size))
				exit(0);
			sleep(CHECK);
		}
	}

	listen_on(sock.fd, 10);

	reap_dead_processes();

	fprintf(stderr, "server: waiting for connections on port %s ...\n", server.port);

	accept_clients(sock.fd, server.size);

	return 0;
}