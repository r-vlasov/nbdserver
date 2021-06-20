/**
* server.c
* Main unit
* Vlasov Roman. June 2021
**/

#include <unistd.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <netinet/in.h>

// custom
#include "includes/nbd.h"    // lib with useful NBD structures and constants
#include "includes/args.h"   // work with shared resources and command line parsing
#include "includes/functions.h"  // htonll, ntohll, ERROR, INFO, DEBUG

/**
 * Structures described server options
**/


typedef struct 
{
	uint32_t 	port;
	uint32_t 	socket;
	uint32_t	quantity;
	RESOURCE**	res;
	uint16_t	seq; // if sequence replies are setting
} NBD_SERVER;
NBD_SERVER* nbd_server; // main server


/**
 * Handle ctrl+c
**/
void 
handle_signit(int _)
{
	free_resources_cmd_line(nbd_server->res, nbd_server->quantity);
	free(nbd_server);
	exit(0);
}
	
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
		ERROR("socket lcall error\n");
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
		ERROR("bind lcall error\n");
		free(s);
		return NULL;
	}
	
	// sequence reply flag
	s->seq = 0;

	// ctrl-c catch
	struct sigaction act;
	act.sa_handler = handle_signit;
/*	if(sigaction(SIGINT, &act, NULL) == -1)
	{
		ERROR("sigaction error\n");
		free(s);
		return NULL;
	}
*/	return s;	
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
		INFO("... Using newstyle negotiation ...\n");
		return 1;
	}
	if (flags == NBD_FLAG_C_FIXED_NEWSTYLE | NBD_FLAG_C_NO_ZEROES)
	{
		INFO("... Using fixed newstyle negotiation ...\n");
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

/*
 * structure contained result of option (i.e. chosen file descriptor or last set option)
*/	
typedef struct 
{
	RESOURCE* res;
	int last_opt;
} OPTION_RESULT;


RESOURCE*
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
		free(export);
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
	return res;
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

void
option_structured_reply_handle(NBD_SERVER* serv, uint32_t socket, OPTION_REQUEST* req)
{
	uint32_t option = req->header->option;
	if (req->header->len != 0)
	{
		option_reply(socket, option, NBD_REP_ERR_INVALID, -1, "Non-empty data field in NBD_STRUCTURED_REPLY option");
		exit(EXIT_FAILURE);
	}
	serv->seq = 1;
	option_reply(socket, option, NBD_REP_ACK, 0, NULL);
}

/*
 * handling option requests (make a reply if it can)
*/
OPTION_RESULT*
handle_option(NBD_SERVER* serv, uint32_t socket, OPTION_REQUEST* op_req)
{
	uint32_t option = op_req->header->option;

	OPTION_RESULT* result = (OPTION_RESULT*) malloc(sizeof(OPTION_RESULT));
	result->last_opt = option;
	result->res = NULL;	
	switch (option) 
	{
		case NBD_OPT_GO:
		{
			INFO(">>>> option : GO\n\n");
			result->res = option_go_handle(serv, socket, op_req);
			return result;
		}
		case NBD_OPT_LIST:	
		{
			INFO(">>>> option : LIST\n");
			option_list_handle(serv, socket, op_req);
			return result;
		}
		case NBD_OPT_ABORT:
		{
			INFO(">>>> option : ABORT\n");
			return result;
		}	
		case NBD_OPT_STRUCTURED_REPLY:
		{
			option_structured_reply_handle(serv, socket, op_req);
			INFO(">>>> option : STRUCTURED REPLY\n");
			return result;
		}	
		default:
		{
			ERROR(">>>> unknown option\n");
			return result;
		}
	}
}

/*
 * main function to handle handshake phase (initial handshake + set options)
*/
RESOURCE*
handshake(NBD_SERVER* serv, uint32_t socket, uint16_t hs_flags)
{
	INFO("\n<<< Handshake phase >>>\n\n");
	handshake_server(socket, hs_flags);
	OPTION_RESULT* result = NULL;
	int hs_type = handshake_client(socket);

	// choose type of handshake in execution flow
	switch (hs_type)
	{
		case -1:
			ERROR("Initial stage of handshake error\n");
			return NULL;
		case 0:	
			/* phase of setting options */
			INFO("\n<<< Option phase >>>\n\n");
			OPTION_REQUEST* op_client;
			OPTION_REQUEST_HEADER* oc_header;

			do
			{
				if (result != NULL) 
					free(result); 
				op_client = option_request(socket);
				oc_header = op_client->header;
				fprintf(stderr, "------ [ REQUEST ] ------\n");	
				fprintf(stderr, "	magic %llx\n", oc_header->magic);
				fprintf(stderr, "	opt %x\n", oc_header->option);
				fprintf(stderr, "	len %d\n", oc_header->len);
				fprintf(stderr, "-------------------------\n");	
				
				result = handle_option(serv, socket, op_client);
				// free dynamic allocated memory
				free(op_client->data);
				free(op_client->header);
				free(op_client);
			} while (result->last_opt != NBD_OPT_GO && result->last_opt != NBD_OPT_ABORT);
			
			if (result->last_opt == NBD_OPT_ABORT) 
			{
				INFO("abord (handshake)\n");
				return NULL;
			}
			RESOURCE* return_res = result->res;
			free(result);
			return return_res;
		case 1:
			ERROR("initial phase of handshake client unsupported\n");
			return NULL;
	}
}

/* 
 * function returned 0 when request's header in transmition phase is correct
*/
int
valid_nbd_request_header(NBD_REQUEST_HEADER* header)
{
	return 0;
}

/* 
 * create an reply to request (simple reply)
*/
void 
transmission_reply
(uint32_t socket, uint32_t error, uint64_t handle, uint32_t datasize, void* data) 
{
	NBD_RESPONSE_HEADER header = {
		htonl(NBD_SIMPLE_REPLY_MAGIC),
		htonl(error),
		htonll(handle),
	};
	send_socket(socket, &header, sizeof(header));
	if(data != NULL) {
		send_socket(socket, data, datasize);
	}
	fprintf(stderr, "--->>> Send - %d bytes <<< ---\n\n", datasize);
}

/* 
 * create an reply to request (structured chunked reply)
*/
void 
transmission_structured_reply
(uint32_t socket, uint16_t flags, uint16_t type, uint64_t handle, uint32_t datasize, void* data) 
{
	NBD_STRUCTURED_RESPONSE_HEADER header = {
		htonl(NBD_STRUCTURED_REPLY_MAGIC),
		htons(flags),
		htons(type),
		htonll(handle),
		htonl(datasize),	
	};
	send_socket(socket, &header, sizeof(header));
	if(data != NULL) {
		send_socket(socket, data, datasize);
	}
	fprintf(stderr, "--->>> Send Structured reply - %d bytes <<< ---\n\n", datasize);
}

/*
 * handle all transmission commands
*/
int
handle_transmission(NBD_SERVER* serv, uint32_t socket, NBD_REQUEST_HEADER* header, uint32_t fd)
{
	switch(header->type)
	{
		case NBD_CMD_READ:
			if (lseek(fd, header->offset, SEEK_SET) == -1)
			{
				ERROR("Error to read from file\n");
				exit(EXIT_FAILURE);

			}
			int data_offset = serv->seq ? sizeof(header->offset) : 0; // DRY
			char* data = (char*) malloc(header->length + data_offset);
			if (data == NULL)
			{
				ERROR("malloc error\n");
				exit(EXIT_FAILURE);
			}
			if (read(fd, data + data_offset, header->length) == -1)
			{
				free(data);
				ERROR("read file error\n");
				exit(EXIT_FAILURE);
			}
			if (serv->seq)
			{
				// this handling of transmission could be optimizated by support addtion types
				uint64_t n_offset = htonll(header->offset);
				memcpy(data, &n_offset, sizeof(header->offset));
				transmission_structured_reply(socket, NBD_REPLY_FLAG_DONE, NBD_REPLY_TYPE_OFFSET_DATA, 
						header->handle, header->length + data_offset, data);
			}
			else	
			{
				transmission_reply(socket, 0, header->handle, header->length, data);
			}
			free(data);
			return NBD_CMD_READ;

		case NBD_CMD_WRITE:
			/* SUPPORT STRUCTURED REPLY */
			if (serv->seq)
			{
				transmission_structured_reply(socket, NBD_REPLY_FLAG_DONE, NBD_REPLY_TYPE_NONE, 
						header->handle, 0, NULL);
			}
			else	
			{
				transmission_reply(socket, 0, header->handle, 0, NULL);
			}
			return NBD_CMD_READ;

		case NBD_CMD_DISC:
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
transmission(NBD_SERVER* serv, uint32_t socket, uint32_t fd)
{
	INFO("\n<<< Transmission phase >>>\n\n");
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

		fprintf(stderr, "---  [ TRANSMISSION REQUEST ] ---\n");	
		fprintf(stderr, "	magic %x\n", header.magic);
		fprintf(stderr, "	flags %x\n", header.flags);
		fprintf(stderr, "	type %d\n", header.type);
		fprintf(stderr, "	handle %llx\n", header.handle);
		fprintf(stderr, "	offset %lld\n", header.offset);
		fprintf(stderr, "	length %d\n", header.length);
		fprintf(stderr, "-------------------------------\n");	
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
		last_cmd = handle_transmission(serv, socket, &header, fd);	
	}
	while (last_cmd != NBD_CMD_DISC && last_cmd != -1);
	if (last_cmd == -1)
	{
		ERROR("Unsupported commands, exit\n");
		return -1;	
	}
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

	// main server
	nbd_server = init_server(cmd_args->port);
	if (!nbd_server) 		return 0;

	nbd_server->quantity = cmd_args->n;

	nbd_server->res = parse_devices_line(cmd_args);

	// listening socket
	if (listen(nbd_server->socket, 10))
	{
		ERROR("listen lcall error\n");
		free(nbd_server);
		free(cmd_args);
		return 0;
	}
	free(cmd_args);	
	
	RESOURCE* resource = NULL;
	// main loop
	while(1)
	{
		int connect_fd = accept(nbd_server->socket, (struct sockaddr*) NULL, NULL);
		if (!connect_fd) 
		{
			ERROR("accept lcall error\n");
			close(nbd_server->socket);
			free(nbd_server);
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
			close(nbd_server->socket);
			resource = handshake(nbd_server, connect_fd, NBD_FLAG_FIXED_NEWSTYLE | NBD_FLAG_NO_ZEROES);
			if (resource == NULL)
			{
				ERROR("... Handshake is not established ...\n");
				free(nbd_server);
				exit(EXIT_FAILURE);
			}
			INFO("[PID = %d]... Handshake is established ....\n", getpid());

			if (transmission(nbd_server, connect_fd, resource->fd))
			{
				ERROR("...Transmission error...\n");	
				free(nbd_server);
				exit(EXIT_FAILURE);
			}
			INFO("[PID = %d]... Transmission is closed ...\n", getpid());
			INFO("<<<<<<<<<<<<<<<<------------->>>>>>>>>>>>>>\n\n");
			close(connect_fd);
			free_resources_cmd_line(nbd_server->res, nbd_server->quantity);
			free(nbd_server);
			return 0;	
		}
	}
}

