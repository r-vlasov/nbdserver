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

#define ERROR(...) fprintf(stderr, __VA_ARGS__)
#define INFO(...)  fprintf(stdout, __VA_ARGS__)

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
	uint32_t				quantity;
	RESOURCE**				res;
	uint16_t				seq; // if sequence replies are setting
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
	
	// sequence reply flag
	s->seq = 0;
	return s;	
}

/**
 * Struct of command line arguments
**/
typedef struct
{
	uint32_t 	port;
	char** 		lf_path_name;
	uint32_t	n;
} CMD_ARGS;

/**
 * Function that check argv line
 *	 1st arg - binded port
 *	 2nd arg - path to shared file
**/
CMD_ARGS*
valid_cmdline(int argc, char* argv[])
{
	if (argc > 4)
	{
		if (strcmp(argv[1], "-p") || strcmp(argv[3], "-d"))
		{	
			INFO("usage: nbd-server -p [port] -d [[file]...]\n");
			return NULL;
		}

		CMD_ARGS* ca = (CMD_ARGS*) malloc(sizeof(CMD_ARGS));
		if (ca == NULL)
		{
			ERROR("malloc error");
			free(ca);
			exit(EXIT_FAILURE);
		}

		uint32_t port = atoi(argv[2]);  // port
		if (port >= 65536) 
		{
			ERROR("invalid port");
			free(ca);
			return NULL;
		}
		ca->port = port;
		ca->lf_path_name = argv + 4; 	// array with device:name
			

		int file_numb = argc - 4;
		if (file_numb % 2 != 0)
		{
			INFO("Invalid number of shared devices\n");
			free(ca);
			return NULL;
		}
		for (int i = 0; i < file_numb; i++)
		{
			if (i % 2 == 0 && access(ca->lf_path_name[i], F_OK))
			{
				ERROR("failed to access file");
				free(ca);
				return NULL;
			}
		}
		ca->n = file_numb / 2;
		return ca;
	}
	INFO("usage: nbd-server -p [port] -d [[file] [name]...]\n");
	return NULL;
}


void
free_resources_cmd_line(RESOURCE** r, int n)
{
	for (int i = 0; i < n; i++)
		free(r[i]);
	free(r);
}

/*
 * function that separate argv line (dev1, name1), (dev2, name2) to RESOURCEs' array
*/
RESOURCE**
parse_devices_line(CMD_ARGS* ca)
{
	fprintf(stderr, "<<< Parsing arg line >>>\n\n");
	fprintf(stderr, "Shared resources : \n");
	RESOURCE** r = (RESOURCE**) malloc(sizeof(RESOURCE*) * ca->n);

	struct stat st;
	if (r == NULL)
	{
		ERROR("malloc error");
		free(ca);
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < ca->n; i++)
	{
		r[i] = (RESOURCE*) malloc(sizeof(RESOURCE));
		if (r[i] == NULL)
		{
			ERROR("malloc error");
			free_resources_cmd_line(r, i);
			free(ca);
			exit(EXIT_FAILURE);
		}

		r[i]->fd = open(ca->lf_path_name[2 * i], O_RDONLY);
		if (r[i]->fd == -1)
		{
			ERROR("Failed to open file");
			free_resources_cmd_line(r, i);
			free(ca);
			exit(EXIT_FAILURE);
		}
		r[i]->exportname = ca->lf_path_name[2 * i + 1];

		r[i]->size = lseek(r[i]->fd, 0, SEEK_END) + 1;
		if (r[i]->size == -1)
		{
			ERROR("Failed to stat file");
			free_resources_cmd_line(r, i);
			free(ca);
			exit(EXIT_FAILURE);
		}

		fprintf(stderr, "Name = %s\n", r[i]->exportname);
		fprintf(stderr, "Path = %s\n", ca->lf_path_name[2 * i]);
		fprintf(stderr, "File size = %ld\n-------------\n", r[i]->size);
	}
	return r;
}

/*
 * function finded file descriptor of file by exportname
 * returns:
		-1   -> is not able to find name
		!=-1 -> file descriptor
*/
RESOURCE*
find_res_by_name(NBD_SERVER* serv, char* name)
{
	int q = serv->quantity;
	RESOURCE** r = serv->res;
	for (int i = 0; i < q; i++)
	{
		if (!strcmp(r[i]->exportname, name))
			return r[i];
	}
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
		INFO("--- Using newstyle negotiation...\n");
		return 1;
	}
	if (flags == NBD_FLAG_C_FIXED_NEWSTYLE | NBD_FLAG_C_NO_ZEROES)
	{
		INFO("--- Using fixed newstyle negotiation...\n");
		return 0;
	}
	ERROR("Unsupported handshake flag from client\n");
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
		op_client->data = (char*) malloc(header->len);
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
option_reply(uint32_t socket, uint32_t opt, uint32_t reply_type, int32_t datasize, void* data) 
{
	OPTION_REPLY_HEADER header = {
		htonll(NBD_OPTION_REPLY_MAGIC),
		htonl(opt),
		htonl(reply_type),
		htonl(datasize),
	};
	send_socket(socket, &header, sizeof(header));
	if (datasize < 0 && data != NULL)
	{
		datasize = strlen(data);
	}
	if(data != NULL) {
		send_socket(socket, data, datasize);
	}
}

void
option_go_handle(NBD_SERVER* serv, uint32_t socket, OPTION_REQUEST* req)
{
	OPTION_GO_DATA* ogd = (OPTION_GO_DATA*)req->data;
	uint32_t len = ntohl(ogd->len);
	uint32_t option = req->header->option;
	char* export = NULL;
	RESOURCE* res; // chosen export

	if (len > req->header->len - 6)
	{
		option_reply(socket, option, NBD_REP_ERR_UNKNOWN, -1, "Incorrect length in option data field");
		exit(EXIT_FAILURE);
	}

	if (len != 0)
	{
		export = (char*) malloc(len + 1);
		if (export == NULL)
		{
			ERROR("malloc failed\n");
			exit(EXIT_FAILURE);
		}
		export[len] = '\0';
		strncpy(export, (const char*) &ogd->name, len);
		res = find_res_by_name(serv, export);
		if (res == NULL)
		{
			option_reply(socket, option, NBD_REP_ERR_UNKNOWN, -1, "Can't find requested resource");
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		res = find_res_by_name(serv, "default");
		if (res == NULL)
		{
			option_reply(socket, option, NBD_REP_ERR_UNKNOWN, -1, "Can't find default resource");
			exit(EXIT_FAILURE);
		}		
	}
	// 0 (non-read only) : Sending EXPORT INFO (size + flags)
	OPTION_GO_REP_INFO_EXPORT rie = {
		htons(NBD_INFO_EXPORT),
		htonll(res->size),
		htons(NBD_FLAG_HAS_FLAGS)
	};
	option_reply(socket, option, NBD_REP_INFO, sizeof(rie), &rie);
	// start transmission
	option_reply(socket, option, NBD_REP_ACK, 0, NULL);
}

void
option_list_handle(NBD_SERVER* serv, uint32_t socket, OPTION_REQUEST* req)
{
	uint32_t option = req->header->option;
	if (req->header->len != 0)
	{
		option_reply(socket, option, NBD_REP_ERR_INVALID, -1, "Non-empty data field in NBD_OPT_LIST option");
		exit(EXIT_FAILURE);
	}
	RESOURCE** r = serv->res;
	for (int i = 0; i < serv->quantity; i++)
	{
		uint32_t servname_len = strlen(r[i]->exportname);
		uint32_t nlen = htonl(servname_len);
		char* buf = (char*) malloc(sizeof(nlen) + servname_len);
		
		memcpy(buf, &nlen, sizeof(nlen));
		strncpy(buf + sizeof(nlen), r[i]->exportname, servname_len);
		option_reply(socket, option, NBD_REP_SERVER, servname_len + sizeof(nlen), buf);	
		free(buf);
	}
	option_reply(socket, option, NBD_REP_ACK, 0, NULL);
}

/*
 * handling option requests (make a reply if it can)
*/
int 
handle_option(NBD_SERVER* serv, uint32_t socket, OPTION_REQUEST* op_req)
{
	uint32_t option = op_req->header->option;
	switch (option) 
	{
		case NBD_OPT_GO:
		{
			INFO("option : GO\n");
			option_go_handle(serv, socket, op_req);
			break;
		}
		case NBD_OPT_LIST:	
		{
			INFO("option : LIST\n");
			option_list_handle(serv, socket, op_req);
			break;
		}
		case NBD_OPT_ABORT:
		{
			INFO("option : ABORT\n");
			break;
		}	
		case NBD_OPT_STRUCTURED_REPLY:
		{
			INFO("option : structed reply\n");
			serv->seq = 1;
			option_reply(socket, option, NBD_REP_ERR_UNSUP, 0, NULL);
			break;
		}	
		default:
		{
			fprintf(stderr, "unknown option == %d\n", option);
			ERROR("unknown option\n");
			return -1;
		}
	}
	return option;
}
	

/*
 * main function to handle handshake phase (initial handshake + set options)
*/
int
handshake(NBD_SERVER* serv, uint32_t socket, uint16_t hs_flags)
{
	handshake_server(socket, hs_flags);
	int hs_type = handshake_client(socket);
	
	// choose type of handshake in execution flow
	switch (hs_type)
	{
		case -1:
			ERROR("Initial stage of handshake error\n");
			return -1;
		case 0:	
			/* phase of setting options */
			INFO("--- Setting options...\n");	
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
				
				last_opt = handle_option(serv, socket, op_client);
				// free dynamic allocated memory
				free(op_client->data);
				free(op_client->header);
				free(op_client);
			} while (last_opt != NBD_OPT_GO && last_opt != NBD_OPT_ABORT);
			
			if (last_opt == NBD_OPT_ABORT) 
			{
				INFO("abord (handshake)\n");
				return -1;
			}
			return 0;
		case 1:
			ERROR("initial phase of handshake client unsupported\n");
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
	send_socket(socket, &header, sizeof(header));
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
				ERROR("Error to read from file\n");
				exit(EXIT_FAILURE);
			}
			void* data = malloc(header->length);
			if (data == NULL)
			{
				ERROR("malloc error\n");
				exit(EXIT_FAILURE);
			}
			if (read(fd, data, header->length) == -1)
			{
				free(data);
				ERROR("read file error\n");
				exit(EXIT_FAILURE);
			}
			transmission_reply(socket, 0, header->handle, header->length, data);
			free(data);
			return NBD_CMD_READ;

		case NBD_CMD_WRITE:
			transmission_reply(socket, 0, header->handle, 0, NULL);
			return NBD_CMD_READ;

		case NBD_CMD_DISC:
			INFO("Close connection\n");
			return NBD_CMD_DISC;

		default:
			ERROR("Unsupported transmission command\n");
			return -1;
	}
}
			

/* 
 * transmission phase
*/
int
transmission(uint32_t socket, uint32_t fd)
{
	INFO("<<<Transmission phase>>>\n");
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
				ERROR("malloc error\n");
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
		ERROR("Unsupported commands, exit\n");
		return -1;	
	}
	INFO("Transmission is closed\n");
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

	NBD_SERVER* serv 	= init_server(cmd_args->port);
	if (!serv) 		return 0;

	serv->quantity = cmd_args->n;

	serv->res = parse_devices_line(cmd_args);

	// listening socket
	if (listen(serv->socket, 10))
	{
		ERROR("listen lcall error\n");
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
			ERROR("accept lcall error\n");
			close(serv->socket);
			free(serv);
			return 0;
		}
		pid_t pid = fork();
		if (pid < 0)
		{
			ERROR("Fork failed\n");
			exit(-1);
		}
		if (pid == 0) 
		{
			if (handshake(serv, connect_fd, NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES))
			{
				ERROR("... Handshake is not established ...\n");
				free(serv);
				exit(EXIT_FAILURE);
			}
			INFO("[PID = %d]... Handshake is established ....\n", getpid());

			if (transmission(connect_fd, serv->res[0]->fd))
			{
				ERROR("...Transmission error...\n");	
				free(serv);
				exit(EXIT_FAILURE);
			}
			INFO("[PID = %d]... Transmission is closed ...\n", getpid());
			INFO("<<<<<<<<<<<<<<<<>>>>>>>>>>>>>>\n");
			close(connect_fd);
			free(serv);
			return 0;	
		}
	}
	free(serv);
	return 0;
}
	
