/*****************************************************************************
 ************************** r_attenu.c ***************************************
 *****************************************************************************
 *
 * r_attenu - execute programs according to the pressed remote control buttons,
 *		handle button events and control relay attenuator.
 *
 * Based on irexec by Trent Piepho <xyzzy@u.washington.edu> 
 *		     Christoph Bartelmus <lirc@bartelmus.de>
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/time.h>
#include "config_file.h"
#include "ir_remote.h"
#include <sys/wait.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <lirc/lirc_client.h>
#include <signal.h>
#include <limits.h>
#include <stdbool.h> 

void print_usage(char *prog)
{
	printf ("Usage: %s [options] [config_file]\n" \
		"\t-d --daemon\t\tRun in background\n" \
		"\t-h --help\t\tDisplay usage summary\n" \
		"\t-v --version\t\tDisplay version\n" \
		"\t-l --withoutLIRC\tProgram will work without IR control\n" \
		"\t-n --name=progname\tUse this program name for lircrc matching\n" \
		"\t-c --lircdconfig=configfile\tLIRCD config file\n", prog);
}

static const struct option options[] = {
	{ "help",     	no_argument,	 NULL, 'h' },
	{ "version",  	no_argument,	 NULL, 'v' },
	{ "daemon",   	no_argument,	 NULL, 'd' },
	{ "withoutLIRC",no_argument, 	 NULL, 'l' },
	{ "name",     	required_argument, NULL, 'n' },
	{ "lircdconfig",required_argument, NULL, 'c' },
	{ 0,          	0,		 0,    0   }
};

#define INT_GPIO 	5
//#define PACKET_SIZE 	256
#define TIMEOUT 	3 /* three seconds */
#define DEFAULT_VOL	0x1f
#define IRCTL_FILE	"/etc/r_attenu.conf"
#define UNIX_SOCK_PATH 	"/tmp/ratt"
#define R_ATTENU_VERSION "1.0"

static int opt_daemonize	= 0;
static char *opt_progname	= "r_attenu";
static char *progname 		= "r_attenu";
static char *opt_lircdconfig	= LIRCDCFGFILE;
static int switchAddr		= 0x20;
static int relayAddr		= 0x21;
static unsigned int vol		= DEFAULT_VOL;
static unsigned int end		= 0;
static unsigned char  mute	= 0x00;
static unsigned int swFd, rlyFd, fd_soc, timeout;
char *glb_string[4], buff[257];
struct ir_remote *config_remotes;
struct ir_ncode *ptr[4];
struct lirc_config *config;
unsigned int lircfd, rafd;
bool ir_Enable = true;

/** Relay Attenuator **/
extern void process_event(void);
static int socket_communication_for_switch(void);


int waitfordata(__u32 maxusec)
{
	return 0;
}


static char *get_config(struct lirc_config *config, char *button)
{
	struct lirc_config_entry *scan;
	struct lirc_code *code;
	struct lirc_list *code_config;

	scan = config->next;
	while(scan != NULL)
	{
		code = scan->code;
		code_config = scan->config;
		if (!strcmp(button, code->button))
		{
			return code_config->string;
		}
		scan = scan->next;
	}	
	return NULL;
}

const char *read_string(int fd)
{
	static char buffer[PACKET_SIZE + 1] = "";
	char *end;
	static int ptr = 0;
	ssize_t ret;

	if (ptr > 0) {
		memmove(buffer, buffer + ptr, strlen(buffer + ptr) + 1);
		ptr = strlen(buffer);
		end = strchr(buffer, '\n');
	} else {
		end = NULL;
	}
	alarm(TIMEOUT);
	while (end == NULL) {
		if (PACKET_SIZE <= ptr) {
			fprintf(stderr, "%s: bad packet\n", progname);
			ptr = 0;
			return (NULL);
		}
		ret = read(fd, buffer + ptr, PACKET_SIZE - ptr);

		if (ret <= 0 || timeout) {
			if (timeout) {
				fprintf(stderr, "%s: timeout\n", progname);
			} else {
				alarm(0);
			}
			ptr = 0;
			return (NULL);
		}
		buffer[ptr + ret] = 0;
		ptr = strlen(buffer);
		end = strchr(buffer, '\n');
	}
	alarm(0);
	timeout = 0;

	end[0] = 0;
	ptr = strlen(buffer) + 1;
#       ifdef DEBUG
#	printf("buffer: -%s-\n", buffer);
#       endif
	return (buffer);
}

enum packet_state {
	P_BEGIN,
	P_MESSAGE,
	P_STATUS,
	P_DATA,
	P_N,
	P_DATA_N,
	P_END
};

int send_packet(int fd, const char *packet)
{
	int done, todo;
	const char *string, *data;
	char *endptr;
	enum packet_state state;
	int status, n;
	__u32 data_n = 0;

	todo = strlen(packet);
	data = packet;
	while (todo > 0) {
		done = write(fd, (void *)data, todo);
		if (done < 0) {
			fprintf(stderr, "%s: could not send packet\n", progname);
			perror(progname);
			return (-1);
		}
		data += done;
		todo -= done;
	}

	/* get response */
	status = 0;
	state = P_BEGIN;
	n = 0;
	while (1) {
		string = read_string(fd);
		if (string == NULL)
			return (-1);
		switch (state) {
			case P_BEGIN:
				if (strcasecmp(string, "BEGIN") != 0) {
					continue;
				}
				state = P_MESSAGE;
				break;
			case P_MESSAGE:
				if (strncasecmp(string, packet, strlen(string)) != 0 || strlen(string) + 1 != strlen(packet)) 				     {
					state = P_BEGIN;
					continue;
				}
				state = P_STATUS;
				break;
			case P_STATUS:
				if (strcasecmp(string, "SUCCESS") == 0) {
					status = 0;
				} else if (strcasecmp(string, "END") == 0) {
					status = 0;
					return (status);
				} else if (strcasecmp(string, "ERROR") == 0) {
					fprintf(stderr, "%s: command failed: %s", progname, packet);
					status = -1;
				} else {
					goto bad_packet;
				}
				state = P_DATA;
				break;
			case P_DATA:
				if (strcasecmp(string, "END") == 0) {
					return (status);
				} else if (strcasecmp(string, "DATA") == 0) {
					state = P_N;
					break;
				}
				goto bad_packet;
			case P_N:
				errno = 0;
				data_n = (__u32) strtoul(string, &endptr, 0);
				if (!*string || *endptr) {
					goto bad_packet;
				}
				if (data_n == 0) {
					state = P_END;
				} else {
					state = P_DATA_N;
				}
				break;
			case P_DATA_N:
				fprintf(stderr, "%s: %s\n", progname, string);
				n++;
				if (n == data_n)
					state = P_END;
				break;
			case P_END:
				if (strcasecmp(string, "END") == 0) {
					return (status);
				}
				goto bad_packet;
				break;
		}
	}
bad_packet:
	fprintf(stderr, "%s: bad return packet\n", progname);
	return (-1);
}


static int retriveVol()
{
	FILE *fp;
	int data=0;

	if ((fp = fopen(IRCTL_FILE, "r")) == NULL)
	{
		fputs("Cannot retrive Volume\n", stderr);
		return DEFAULT_VOL;
	}
	fscanf(fp, "%x", &data);
	fclose(fp);
	if ((data < 0) || (data > 0x3f))
		return DEFAULT_VOL;
	return data;
}

static void saveVol(int data)
{
	FILE *fp;

	if ((fp = fopen(IRCTL_FILE, "w")) == NULL)
	{
		fputs("Cannot save Volume\n", stderr);
		return;
	}
	fprintf(fp, "%x", data);
	fclose(fp);
}

static int ra_read(int fd)
{
	int data=0;

	if ((data = wiringPiI2CRead(fd)) < 0)
		fputs("Error: Reading from I2C\n", stderr);

	return data;
}

static void ra_write(int fd, unsigned short data)
{
	if((data == 0xffe0) || (data == 0xffdf) || (data == 0xffd0) || (data == 0xffcf))
	{
		wiringPiI2CWrite(fd, 0x3f); // Bug fix: to avoid noise
		usleep(500);
	}
	if (wiringPiI2CWrite(fd, data) < 0)
		fputs("Error: Writing on I2C\n", stderr);
	//printf("Vol:%x\n", data); // For debugging
}

static int ra_set_mute(int fd, int data) // data: 0-unmute 1-mute
{
	if ((data == 0) || (data == 1))
	{
		mute = data;
		ra_write(fd, (mute? 0: (~vol)|0x40 ));
		return 0;
	}
	else
		return 1;
}

inline static int ra_get_mute()
{
	return mute;
}

static void ra_mute(int fd)
{
	mute=(~mute)&0x1;
	ra_write(fd, (mute ? (~vol)&0xbf : (~vol)|0x40));
}

static int ra_vol_inc(int fd)
{ 
	if (vol >= 0x3f) 
	{
		return(1);
	}
	else
	{
		vol += 1;
		ra_write(fd, ((~vol)|0x40));
		saveVol(vol);
		mute = 0;
	}
	return(0);
}

static int ra_vol_dec(int fd)
{
	if (vol <= 0)
	{
		return(1);
	}
	else
	{
		vol -= 1;
		ra_write(fd, ((~vol)|0x40));
		saveVol(vol);
		mute = 0;
	}
	return(0);
}

 static int ra_vol_set(int fd, int data)
{
        if ((data >= 0) && (data <= 0x3f))
        {
                vol = data;
                ra_write(fd, ((~vol)|0x40));
		saveVol(vol);
                mute = 0;
                return 0;
        }
        return 1;
}

inline static int ra_vol_get()
{
	return vol;
}

void send_key(char *string, int num)
{
	snprintf(buff, 257, "%s %016llx %02x %s %s\n", 
			"SIMULATE", (unsigned long long)
			((config_remotes->pre_data << 16) | (ptr[num]->code)),
			00, ptr[num]->name, config_remotes->name);
	send_packet(fd_soc, buff);
}

int open_socket(char *path)
{
	int servlen, sockfd;
	struct sockaddr_un serv_addr;

	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		perror("creating socket");
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sun_family = AF_UNIX;
	strcpy(serv_addr.sun_path, path);
	servlen=strlen(serv_addr.sun_path) +
		sizeof(serv_addr.sun_family);
	unlink(path); 
	if(bind(sockfd, (struct sockaddr *)&serv_addr, servlen)<0)
		perror("binding socket");
	listen(sockfd, 5);

	return sockfd;
}

static int initRattenuator()
{
	if (wiringPiSetup () < 0)
	{
		fputs ("Error: Unable to setup wiringPi\n", stderr);
		return EXIT_FAILURE;
	}

	swFd = wiringPiI2CSetup(switchAddr);
	rlyFd = wiringPiI2CSetup(relayAddr);
	if ((swFd < 0) || (rlyFd < 0))
	{
		fputs("Error: Opening I2C channels\n", stderr);
		return EXIT_FAILURE;
	}

	vol = retriveVol();
	ra_vol_set(rlyFd, vol);

	if ((rafd = open_socket(UNIX_SOCK_PATH)) == 1)
		return EXIT_FAILURE;

	return 0;
}

int process_IR_input(char *c)
{
	if (strstr(c, "KEY_VOLUMEUP"))
	{
		ra_vol_inc(rlyFd);
		return 0;
	}
	else if (strstr(c, "KEY_VOLUMEDOWN"))
	{
		ra_vol_dec(rlyFd);
		return 0;
	}
	else if (strstr(c, "KEY_MUTE"))
	{
		ra_mute(rlyFd);
		return 0;
	}
	return -1;
}

char *process_hw_input(char *data)
{
	char *action,*val;
	int value=70;
	action = strtok(data, "=");

	if ((val = strtok(NULL, " ")) != NULL)
		value = atoi(val);

	// printf("val = %x, action=%s\n", value, action);
	if (!strcmp(action, "SET_VOLUME"))
	{
		if (ra_vol_set(rlyFd, value) == 1)
			return ("FAILURE");
		else
			return ("SUCCESS");
	}
	else if (!strcmp(action, "GET_VOLUME"))
	{
		sprintf(data, "%d", ra_vol_get());
		return (data);
	}
	else if (!strcmp(action, "SET_MUTE"))
	{
		if (ra_set_mute(rlyFd, value) == 1)
			return ("FAILURE");
		else
			return ("SUCCESS");
	}
	else if (!strcmp(action, "GET_MUTE"))
	{
		sprintf(data, "%d", ra_get_mute());
		return (data);
	}

	return ("FAILURE");
}

void process_input(struct lirc_config* config)
{
	fd_set readfds;
	int addrlen, new_socket, client_socket[30], max_clients = 30;
	int max_sd, ret, i, valread, sd,w_soc;
	struct sockaddr_un address;
	char buffer[1025], *retu;

	for (i = 0; i < max_clients; i++)
		client_socket[i] = 0;

	while(1)
	{
		FD_ZERO(&readfds);
		FD_SET(rafd, &readfds);
		max_sd = rafd;
		if (lircfd > 0)
		{
			FD_SET(lircfd, &readfds);
			max_sd = lircfd;
		}
		//printf ("rafd:%d lircfd:%d\n",rafd, lircfd);

		for ( i = 0; i < max_clients ; i++)
		{
			sd = client_socket[i];
			if(sd > 0)
				FD_SET(sd, &readfds);
			if(sd > max_sd)
				max_sd = sd;
		}
		ret = select( max_sd + 1 , &readfds , NULL , NULL , NULL);
		if ((ret < 0) && (errno == EINTR) && (end))
			break;
		else if (ret <= 0)
			continue;

		if (FD_ISSET(rafd, &readfds))
		{
			addrlen = sizeof(address);
			if ((new_socket = accept(rafd, (struct sockaddr *) &address,(socklen_t *) &addrlen))<0)
			{
				perror("accept");
				//exit(EXIT_FAILURE);
			}
			//printf("New connection , socket fd is %d \n" , new_socket);
			for (i = 0; i < max_clients; i++)
			{
				if( client_socket[i] == 0 )
				{
					client_socket[i] = new_socket;
					break;
				}
			}
		}
		else if (FD_ISSET(lircfd, &readfds))
		{
			char *code, *string;
			int r;

			if (lirc_nextcode(&code) == 0) {
				if (code == NULL)
					continue;
				//printf("\n code: %s ",code);
				r = lirc_code2char(config, code, &string);
				while (r == 0 && string != NULL) {
					if(strcasecmp(string, "hardware_control") == 0) 
						process_IR_input(code);
					else
						system(string);
					r = lirc_code2char(config, code, &string);
				}
				free(code);

				if (r == -1)
					break;
			}
		}

		for (i = 0; i < max_clients; i++)
		{
			sd = client_socket[i];
			if (FD_ISSET(sd, &readfds))
			{
				if ((valread = read(sd , buffer, 1024)) == 0)
				{
					close(sd); //Disconnect
					client_socket[i] = 0;
				}
				else
				{
					buffer[valread] = '\0';
					//printf ("DATA from client:%s\n", buffer);
					retu = process_hw_input(buffer);
					w_soc= write(sd, retu, strlen(retu));
					if (w_soc < 0) 
						perror("ERROR: writing to socket"); 
				}
			}
		}
	}

}

void process_event(void)
{
	char *string;
	int swStatus;
	
	swStatus = ra_read(swFd);
        //printf(" %x \n",swStatus); //debug
	switch (swStatus)
	{
	case 0xf7: // Mute
		if(!ir_Enable)
			ra_mute(rlyFd);
		else
		{
			string = get_config(config, "BUTTON1");
			if (string == NULL) {
				printf("\n NULL string \n");
			} else {
				if (strstr(string, "hardware_control"))
					ra_mute(rlyFd);
				else
					send_key(string, 0); 
			} 
		}
		break;		

	case 0xfd: // Vol dec
		if(!ir_Enable)
			ra_vol_dec(rlyFd);
		else
		{
			string = get_config(config, "BUTTON3");
			//printf(" %s \n",string);	
			if (string == NULL) {
				printf("\n NULL string \n");
			} else {
				if (strstr(string, "hardware_control"))
					ra_vol_dec(rlyFd);
				else
					send_key(string,2);
			}
		}
		break; 

	case 0xfe: // Vol inc
		if(!ir_Enable)
			ra_vol_inc(rlyFd);
		else
		{
			string = get_config(config, "BUTTON4");
			//printf(" %s \n",string);	
			if (string == NULL) {
				printf("\n NULL string \n");
			} else {
				if (strstr(string, "hardware_control"))
					ra_vol_inc(rlyFd);
				else
					send_key(string,3);
			}
		}	
		break;

	case 0xfb: // Play/Pause
		if(!ir_Enable)
			printf("Sorry! Play/Pause button used only when LIRC runs!!!!\n");
		else
		{
			string = get_config(config, "BUTTON2");
			if (string == NULL)
				printf("\n NULL string \n");
			else
				send_key(string,1);
		}
		break;

	case -1: // Read error
		fputs("Error: Reading from I2C\n", stderr);
		break;

	default:
		break;
	} 
}


inline static void ctrl_handler(int signum)
{
	end = 1;
}

static void setup_handlers(void)
{
	struct sigaction sa =
	{
		.sa_handler = ctrl_handler,
	};

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

inline void sigalrm(int sig)
{
	timeout = 1;
}

static int socket_communication_for_switch(void)
{
	struct sigaction act;
	struct sockaddr_un addr_un;

	act.sa_handler = sigalrm;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;       /* we need EINTR */
	sigaction(SIGALRM, &act, NULL);

	addr_un.sun_family = AF_UNIX;
	strcpy(addr_un.sun_path, LIRCD);
	fd_soc = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd_soc == -1) {
		fprintf(stderr, "Could not open socket\n");
		return -1;
	};
	if (connect(fd_soc,(struct sockaddr *)&addr_un, sizeof(addr_un)) == -1) 
	{
		fprintf(stderr, "Could not connect to socket\n");
		return -1;
	};
	return 0;
}

int lircd_config_read(char *argv)
{
	FILE *fd;

	if ((lircfd = lirc_init(opt_progname, 1)) == -1)
	{
		fprintf(stderr, "Failed to Initialize lirc\n");
		return -1;
	}

	fd = fopen(argv, "r");
	if (fd == NULL)
	{
		fprintf(stderr, "Failed to open lircd.config file\n");
		lirc_deinit();
		return -1;
	}

	config_remotes = read_config(fd, argv);
	fclose(fd);

	if (config_remotes != NULL) 
	{
		if (socket_communication_for_switch() == -1)
			fprintf(stderr, "Socket connection failed with: %s\n", argv);
	}       
	else {
		fprintf(stderr, "Failed to read Remote configuration\n");
		lirc_deinit();
		return -1;
	}

	return 0;
}

int lircrc_config_read(char *argv)
{
	if (lirc_readconfig(argv, &config, NULL) == 0)
	{
		int i;
		for (i =0 ; i < 4; i++)
		{
			sprintf(buff, "BUTTON%d", i+1);
			glb_string[i] = (char *)get_config(config, buff);
			if (glb_string[i] != NULL)
				ptr[i] = get_code_by_name(config_remotes, glb_string[i]);
		}
		return 0;
	}
	return -1; 
}


int main(int argc, char* argv[])
{
	int c;

	while ((c = getopt_long(argc, argv, "hvdln:c:", options, NULL)) != -1) {
		switch (c) {
		case 'h':
			print_usage(argv[0]);
			return EXIT_SUCCESS;
		case 'v':
			printf("%s %s", progname, R_ATTENU_VERSION);
			return EXIT_SUCCESS;
		case 'd':
			opt_daemonize = 1;
			break;
		case 'n':
			opt_progname = optarg;
			break;
		case 'l':
			puts("Without LIRC.\n");
			ir_Enable = false;
			break;
		case 'c':
			opt_lircdconfig = optarg;
			break;
		default:
			print_usage(argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (optind < argc - 1) {
		fputs("Too many arguments\n", stderr);
		return EXIT_FAILURE;
	}

	setup_handlers();

	if (initRattenuator() == 1) {
		fprintf(stderr, "Failed to Initialize the Hardware Relays volume control\n");
	}

	if (ir_Enable)
	{
		if ((lircd_config_read(opt_lircdconfig)<0) || 
			(lircrc_config_read(optind != argc ? argv[optind] : NULL)<0))
			return EXIT_FAILURE;
	}

	if (opt_daemonize) {
		if (daemon(0, 1) == -1) {
			fprintf(stderr, "%s: can't daemonize\n", progname);
			perror(progname);
			if (ir_Enable)
			{
				lirc_freeconfig(config);
				lirc_deinit();
				close(fd_soc);
				close(lircfd);
			}
			unlink(UNIX_SOCK_PATH); 
			close(rafd);
			exit(EXIT_FAILURE);
		}
	}

	if (wiringPiISR (INT_GPIO, INT_EDGE_FALLING, &process_event) < 0 )
	{	
		fputs("Error: Unable to initialize ISR\n", stderr);
		return EXIT_FAILURE;
	}

	process_input(config); 

	if (ir_Enable)
	{
		lirc_freeconfig(config);
		lirc_deinit();
		close(fd_soc);
		close(lircfd);
	}
	close(rafd);
	unlink(UNIX_SOCK_PATH); 
	saveVol(vol);

	return(0);
}
