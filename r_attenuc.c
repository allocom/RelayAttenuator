/* a client in the unix domain */

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <getopt.h>

#define PATH "/tmp/ratt"

void print_usage(char *prog)
{
	printf ("Usage: %s [options]\n" \
		"\t-h\tDisplay usage summary\n" \
		"\t-c\tCommand to execute\n" \
		"\tCommands:\n" \
		"\t\tGET_VOLUME\t\tGet volume\n" \
		"\t\tSET_VOLUME=[value]\tSet volume. value = 0 to 63\n" \
		"\t\tGET_MUTE\t\tGet mute status\n" \
		"\t\tSET_MUTE=[value]\tSet mute. value = 0/1 (0=unmute 1=mute)\n",
		prog);
}

void error(const char *);

int main(int argc, char *argv[])
{
	struct sockaddr_un  serv_addr;
	int sockfd, servlen;
	unsigned int opt;
	char buffer[82];
	char *cmd;

	while ((opt = getopt_long(argc, argv, "hc:", NULL, NULL)) != -1)
	{
		switch (opt) {
		case 'c':
			cmd = optarg;
			break;

		case 'h':
		default: /* '?' */
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (optind < 2)
	{
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	if (optind < argc-1) {
		fputs("Too many arguments\n", stderr);
		return EXIT_FAILURE;
	}

	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sun_family = AF_UNIX;
	strcpy(serv_addr.sun_path, PATH);
	servlen = strlen(serv_addr.sun_path) +
		sizeof(serv_addr.sun_family);

	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		error("Error: Creating socket");

	if (connect(sockfd, (struct sockaddr *)
				&serv_addr, servlen) < 0)
		error("Error: Connecting");
	bzero(buffer, 82);
	sprintf (buffer, "%s", cmd);
	write(sockfd, buffer, strlen(buffer));
	bzero(buffer, 82);
	read(sockfd, buffer, 80);
	printf("%s\n", buffer);
	close(sockfd);
	return 0;
}

void error(const char *msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}
