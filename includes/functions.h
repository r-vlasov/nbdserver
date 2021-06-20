#ifndef __FUNCTION_NBD_SERVER_H
#define __FUNCTION_NBD_SERVER_H

#define ERROR(...) fprintf(stderr, __VA_ARGS__)
#define INFO(...)  fprintf(stdout, __VA_ARGS__)

// in debug mode
#ifndef _DEBUG
	#define DEBUG(...)  fprintf(stderr, __VA_ARGS__)
#elif
	#define DEBUG(...)	;
#endif


/** 
 *   function that translate network byteorder to host
**/
unsigned long long ntohll(const unsigned long long input);


/** 
 *   inverse of ntohll
**/
unsigned long long htonll(const unsigned long long input);

/* 
 *	return size of file (even if file is block device
*/
int get_file_size(int fd);

/**
 * send to client
**/
void send_socket(int socket, void* data, int len);



/**
 * get data from client
**/
void recv_socket(int socket, void* data, int len);

#endif



