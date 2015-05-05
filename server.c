#include "stdio.h"
#include "stdlib.h"

struct config
{
	int size;
	int port;
};

void printUsage(char const argv[])
{
	printf("Usage: %s <size> [port]\n", argv);
	printf("- Size has to be at least 4\n");
	printf("- Port is an optional parameter, default is: 1234\n");
	exit(0);
}

struct config process_options(int argc, char const *argv[], struct config server)
{
	if (argc == 2)
	{
		server.size = atoi(argv[1]);
		if (server.size >= 4)
			server.port = 1234;
		else
			printUsage(argv[0]);
	}
	else if (argc == 3)
	{
		server.size = atoi(argv[1]);
		server.port = atoi(argv[2]);
		if (!((server.size >= 4) && server.port))
			printUsage(argv[0]);
	}
	else
		printUsage(argv[0]);

	return server;
}

int main(int argc, char const *argv[])
{
	struct config server;
	server = process_options(argc, argv, server);

	

	return 0;
}