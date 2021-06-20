#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>


#include "includes/functions.h"
#include "includes/args.h"

/**
 * Struct of command line arguments
**/


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

/*
 * function that separate argv line (dev1, name1), (dev2, name2) to RESOURCEs' array
*/
RESOURCE**
parse_devices_line(CMD_ARGS* ca)
{
	fprintf(stderr, "\n<<< Parsing arg line >>>\n\n");
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

		r[i]->size = get_file_size(r[i]->fd);
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

/**
 * function that frees allocated resources
**/
void
free_resources_cmd_line(RESOURCE** r, int n)
{
	for (int i = 0; i < n; i++)
	{
		close(r[i]->fd);
		free(r[i]);
	}
	free(r);
}


