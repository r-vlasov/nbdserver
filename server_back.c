/**
* server.c
* Main unit
* Vlasov Roman. June 2021
**/

#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

// custom
#include "nbd.h"    // lib with useful NBD structures and constants

#define ERROR(...) fprintf(stderr, __VA_ARGS__"\n")	
#define INFO(...)  fprintf(stdout, __VA_ARGS__"\n")

/**
 * Structure described server options
**/

typedef struct
{
	char* 					exportname;
	uint16_t				fd;
	uint64_t				size;
} RESOURCE;


typedef struct 
{
	uint32_t 				port;
	uint32_t 				socket;
	RESOURCE*				resources;
} NBD_SERVER;


/**
 * Server initialization (create socket)
**/
NBD_SERVER* 
init_server(uint32_t port)
{
	NBD_SERVER* s = (NBD_SERVER*) malloc(sizeof(NBD_SERVER));
	if (s == NULL) 
	{
		ERROR("malloc error");
		return NULL;
	}
	s->port = port;
	
	// open server-side socket
	s->socket = socket(AF_INET, SOCK_STREAM, 0);
	if (s->socket == 0)
	{
		ERROR("socket lcall error");
		free(s);
		return NULL;
	}

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);	

	// binding
	if (bind(s->socket, (struct sockaddr*) &serv_addr, sizeof(serv_addr)))
	{
		ERROR("bind lcall error");
		free(s);
		return NULL;
	}
	return s;	
}

/**
 * Struct of command line arguments
**/
typedef struct
{
	uint32_t 	port;
	char** 		lf_path_name;
} CMD_ARGS;

/**
 * Function that check argv line
 *	 1st arg - binded port
 *	 2nd arg - path to shared file
**/
CMD_ARGS*
valid_cmdline(int argc, char* argv[])
{
	if (argc < 5)
	{
		if (strcmp(argv[1], "-p") || strcmp(argv[3], "-d"))
		{	
			INFO("usage: nbd-server -p [port] -d [[file]...]");
			return NULL;
		}
		uint32_t port = atoi(argv[2]);
		if (port < 65536 && !access(argv[2], F_OK))
		{
			CMD_ARGS* ca = (CMD_ARGS*) malloc(sizeof(CMD_ARGS));
			if (ca == NULL)
			{
				ERROR("malloc error");
				exit(EXIT_FAILURE);
			}
			ca->port = port;
			ca->lf_path = argv[2];	
			return ca;
		}
		else
		{
			INFO("Please, valid your argument line");
			return NULL;
		}
	}
	INFO("usage: nbd-server -p [port] -d [[file]...]");
	return NULL;
}

void
send_socket(int socket, void* data, int len)
{
	if (write(socket, data, len) == -1)
	{
		ERROR("write to socket error");
		exit(EXIT_FAILURE);
	}
} 

void
recv_socket(int socket, void* data, int len)
{	
	int cnt = read(socket, data, len);
	int acc = cnt;
	while (acc != len && cnt != -1)
	{
		cnt = read(socket, data, len);
		acc += cnt;
	}
	if (cnt == 0)
	{
		ERROR("read from socket error");
		exit(EXIT_FAILURE);
	}
} 

/* initial phase : S -> C */
void 
handshake_server(uint32_t socket, uint32_t hs_flags)
{
	HANDSHAKE_SERVER s_header = {
		htonll(NBDMAGIC),
		htonll(IHAVEOPT),
		htons(hs_flags),
	};
	send_socket(socket, &s_header, sizeof(s_header));
}

/*
 *	function that return type of chosen handshake by client (get + valid) : C -> S
 *	returns:
 *		1  -> newstyle negotiation		
 *		0  -> fixed newstyle negotiation : our decision
 *		-1 -> unsupported flags
*/
int
handshake_client(uint32_t socket)
{
	HANDSHAKE_CLIENT c_header;
	recv_socket(socket, &c_header, sizeof(c_header));

	int flags = ntohl(c_header.clflags);
	if (flags == NBD_FLAG_C_NO_ZEROES)
	{
		INFO("--- Using newstyle negotiation...");
		return 1;
	}
	if (flags == NBD_FLAG_C_FIXED_NEWSTYLE | NBD_FLAG_C_NO_ZEROES)
	{
		INFO("--- Using fixed newstyle negotiation...");
		return 0;
	}
	ERROR("Unsupported handshake flag from client");
	return -1;
}

/*
 * handle option request
 * return : option code or -1 (error)
*/
OPTION_REQUEST*
option_request(uint32_t socket) 
{
	OPTION_REQUEST_HEADER* header = malloc(sizeof(OPTION_REQUEST_HEADER));
	recv_socket(socket, header, sizeof(OPTION_REQUEST_HEADER));
	header->magic 	= ntohll(header->magic);
	header->option 	= ntohl(header->option);
	header->len 	= ntohl(header->len);

	OPTION_REQUEST* op_client = malloc(sizeof(OPTION_REQUEST));
	op_client->header = header;
	op_client->data = NULL;

	// valid magic constant
	if (header->magic != IHAVEOPT)
	{
		ERROR("invalid expectable magic constant in client's request option message");
		exit(EXIT_FAILURE);
	}
	if (header->len != 0)
	{
		op_client->data = malloc(header->len);
		if (op_client->data == NULL)
		{
			ERROR("malloc error");
			exit(EXIT_FAILURE);
		}
		recv_socket(socket, op_client->data, header->len);
	}
	return op_client;
}

/*
 * function that reply on setting option from client
*/
void 
option_reply(uint32_t socket, uint32_t opt, uint32_t reply_type, uint32_t datasize, void* data) 
{
	OPTION_REPLY_HEADER header = {
		htonll(NBD_OPTION_REPLY_MAGIC),
		htonl(opt),
		htonl(reply_type),
		htonl(datasize),
	};
	send_socket(socket, &header, sizeof(header));
	if(data != NULL) {
		send_socket(socket, data, datasize);
	}
}

/*
 * handling option requests (make a reply if it can)
*/
int 
handle_option(uint32_t socket, OPTION_REQUEST* op_req)
{
	uint32_t option = op_req->header->option;
	switch (option) 
	{
		case NBD_OPT_GO:
		{
			OPTION_GO_DATA* ogd = (OPTION_GO_DATA*)op_req->data;
			int len = ntohl(ogd->len);
			fprintf(stderr, "len = %d\n", len);
			INFO("option : GO");
			if (len == 0) 
			{
				// 0
				OPTION_GO_REP_INFO_EXPORT rie = {
					htons(NBD_INFO_EXPORT),
					htonll(60948480),
					htons(NBD_FLAG_HAS_FLAGS)
				};
				option_reply(socket, option, NBD_REP_INFO, sizeof(rie), &rie);
				// 1
				char defname[] = "default";
				char* ri = (char*) malloc(strlen(defname) + sizeof(uint16_t));
				*(uint16_t*)ri = htons(NBD_INFO_NAME);
				memcpy(ri + sizeof(uint16_t), defname, strlen(defname)); 
				free(ri);
				option_reply(socket, option, NBD_REP_INFO, sizeof(uint16_t) + strlen(defname), &ri);
				struct {
					uint16_t a;
					uint32_t b;
					uint32_t c;
					uint32_t d;
				} __attribute__((packed)) pa = {
					htons(3),
					htonl(1024),
					htonl(2048),
					htonl(4096)
				};
				option_reply(socket, option, NBD_REP_INFO, sizeof(pa), &pa);	
			}
			else 
			{
				char* goname = (char*) malloc(len + 1);
				memset(goname, '\0', len + 1);
				fprintf(stderr, "len = %d\n", len);
				memcpy(goname, (char*)ogd + 4, len);
				goname[len] = '\0';
				fprintf(stderr, "Chosen name: %s\n", goname);
				free(goname);
			}	
			option_reply(socket, option, NBD_REP_ACK, 0, NULL);
			break;
		}
		case NBD_OPT_LIST:	
		{
			INFO("option : LIST");
			char* servname = "default";
			uint32_t len = htonl(strlen(servname));
			char buf[2000];
			memcpy(buf, &len, sizeof(len));
			strncpy(buf + sizeof(len), servname, sizeof(servname));
			option_reply(socket, option, NBD_REP_SERVER, strlen(servname) + sizeof(len), buf);	
			option_reply(socket, option, NBD_REP_ACK, 0, NULL);
			break;
		}
		case NBD_OPT_ABORT:
		{
			INFO("option : ABORT\n");
			break;
		}	
		case NBD_OPT_STRUCTURED_REPLY:
		{
			//option_reply(socket, option, NBD_REP_ERR_UNSUP, 0, NULL);
			option_reply(socket, option, NBD_REP_ACK, 0, NULL);
			break;
		}	
		default:
		{
			fprintf(stderr, "option == %d\n", option);
			ERROR("unknown option");
			return -1;
		}
	}
	return option;
}
	

/*
 * main function to handle handshake phase (initial handshake + set options)
*/
int
handshake(uint32_t socket, uint16_t hs_flags)
{
	handshake_server(socket, hs_flags);
	int hs_type = handshake_client(socket);
	
	// choose type of handshake in execution flow
	switch (hs_type)
	{
		case -1:
			ERROR("Initial stage of handshake error");
			return -1;
		case 0:	
			/* phase of setting options */
			INFO("--- Setting options...");	
			OPTION_REQUEST* op_client;
			OPTION_REQUEST_HEADER* oc_header;
			int last_opt;
			do
			{
				op_client = option_request(socket);
				oc_header = op_client->header;
				fprintf(stderr, "------\n");	
				fprintf(stderr, "	magic %llx\n", oc_header->magic);
				fprintf(stderr, "	opt %x\n", oc_header->option);
				fprintf(stderr, "	len %d\n", oc_header->len);
				fprintf(stderr, "------\n");	
				
				last_opt = handle_option(socket, op_client);
				// free dynamic allocated memory
				free(op_client->data);
				free(op_client->header);
				free(op_client);
			} while (last_opt != NBD_OPT_GO && last_opt != NBD_OPT_ABORT);
			
			if (last_opt == NBD_OPT_ABORT) 
			{
				INFO("abord (handshake)");
				return -1;
			}
			return 0;
		case 1:
			ERROR("initial phase of handshake client unsupported");
			return -1;
	}
}


/* 
 * function returned 0 when request's header in transmition phase is correct
*/
int
valid_request_header(NBD_REQUEST_HEADER* header)
{
	return 0;
}


/* 
 * create an reply to request (transmission 
*/
void 
transmission_reply(uint32_t socket, uint32_t error, uint64_t handle, uint32_t datasize, void* data) 
{
	NBD_RESPONSE_HEADER header = {
		htonl(0x67446698),
		htonl(error),
		htonll(handle),
	};
	send_socket(socket, &header2, sizeof(header));
	fprintf(stderr, "%d\n", datasize);
	if(data != NULL) {
		send_socket(socket, data, datasize);
	}
}

/*
 * handle all transmission commands
*/
int
handle_transmission(uint32_t socket, NBD_REQUEST_HEADER* header, uint32_t fd)
{
	switch(header->type)
	{
		case NBD_CMD_READ:
			if (lseek(fd, header->offset, SEEK_SET) == -1)
			{
				ERROR("Error to read from file");
				exit(EXIT_FAILURE);
			}
			void* data = malloc(header->length);
			if (data == NULL)
			{
				ERROR("malloc error");
				exit(EXIT_FAILURE);
			}
			if (read(fd, data, header->length) == -1)
			{
				free(data);
				ERROR("read file error");
				exit(EXIT_FAILURE);
			}
			transmission_reply(socket, 0, header->handle, header->length, data);
			free(data);
			return NBD_CMD_READ;

		case NBD_CMD_WRITE:
			transmission_reply(socket, 0, header->handle, 0, NULL);
			return NBD_CMD_READ;

		case NBD_CMD_DISC:
			INFO("Close connection");
			return NBD_CMD_DISC;

		default:
			ERROR("Unsupported transmission command");
			return -1;
	}
}
			

/* 
 * transmission phase
*/
int
transmission(uint32_t socket, uint32_t fd)
{
	INFO("<<<Transmission phase>>>");
	NBD_REQUEST_HEADER header;
	NBD_REQUEST req = {
		&header, 
		NULL,
	};

	int	last_cmd; // contains value of last handled command
	do 
	{

		recv_socket(socket, &header, sizeof(NBD_REQUEST_HEADER));
		header.magic = ntohl(header.magic);
		header.flags = ntohs(header.flags);
		header.type = ntohs(header.type);
		header.handle = ntohll(header.handle);
		header.offset = ntohll(header.offset);
		offset = header.offset;
		header.length = ntohl(header.length);
		// MUST VALID HEADER

		fprintf(stderr, "------\n");	
		fprintf(stderr, "	magic %x\n", header.magic);
		fprintf(stderr, "	flags %x\n", header.flags);
		fprintf(stderr, "	type %d\n", header.type);
		fprintf(stderr, "	handle %llx\n", header.handle);
		fprintf(stderr, "	offset %lld\n", header.offset);
		fprintf(stderr, "	length %d\n", header.length);
		fprintf(stderr, "------\n");	
		// when we get cmd to write we must recieve all data from socket
		if(header.type == NBD_CMD_WRITE)
		{
			void* data = malloc(header.length);
			if (data == NULL)
			{
				ERROR("malloc error");
				exit(EXIT_FAILURE);
			}
			recv_socket(socket, data, header.length);
			free(data);
		}
		
		// handle each request
		last_cmd = handle_transmission(socket, &header, fd);	
	}
	while (last_cmd != NBD_CMD_DISC && last_cmd != -1);
	if (last_cmd == -1)
	{
		ERROR("Unsupported commands, exit");
		return -1;	
	}
	INFO("Transmission is closed");
	return 0;
}


/**
 * Entry point
**/
int 
main(int argc, char* argv[])
{
	CMD_ARGS* cmd_args	= valid_cmdline(argc, argv); 
	if (!cmd_args) 	return 0;	

	// open file descriptor
	uint32_t fd = open(cmd_args->lf_path, O_RDONLY);
	struct stat st;
	stat(cmd_args->lf_path, &st);
	int size = st.st_size;
	fprintf(stderr, "File size = %d\n", size);
	if (fd == -1)
	{
		ERROR("Failed to open file");
		free(cmd_args);
		exit(EXIT_FAILURE);
	}

	NBD_SERVER* serv 	= init_server(cmd_args->port);
	if (!serv) 		return 0;

	// listening socket
	if (listen(serv->socket, 10))
	{
		ERROR("listen lcall error");
		free(serv);
		free(cmd_args);
		return 0;
	}
	
	
	// main loop
	while(1)
	{
		int connect_fd = accept(serv->socket, (struct sockaddr*) NULL, NULL);
		if (!connect_fd) 
		{
			ERROR("accept lcall error");
			close(serv->socket);
			free(serv);
			free(cmd_args);
			return 0;
		}
		if (handshake(connect_fd, NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES))
		{
			ERROR("... Handshake is not established ...");
			free(serv);
			free(cmd_args);
			close(fd);
			exit(EXIT_FAILURE);
		}
		INFO("... Handshake is established .... ");
		
		if (transmission(connect_fd, fd))
		{
			ERROR("...Transmission error...");	
			free(serv);
			free(cmd_args);
			close(fd);
			exit(EXIT_FAILURE);
		}
		INFO("... Transmission is closed ...");
		INFO("<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>");
		close(connect_fd);
		break;
	}
	close(fd);
	free(serv);
	free(cmd_args);
	return 0;
}
	
