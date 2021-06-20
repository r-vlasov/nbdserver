#ifndef __ARGS_NBD_SERVER_H
#define __ARGS_NBD_SERVER_H

#include <stdint.h>

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
CMD_ARGS* valid_cmdline(int argc, char* argv[]);


/**
 * Structures described server options
**/
typedef struct
{
	char* 		exportname;
	uint16_t	fd;
	uint64_t	size;
} RESOURCE;

/*
 * function that separate argv line (dev1, name1), (dev2, name2) to RESOURCEs' array
*/
RESOURCE** parse_devices_line(CMD_ARGS* ca);


/**
 * function that frees allocated resources
**/
void free_resources_cmd_line(RESOURCE** r, int n);

#endif
