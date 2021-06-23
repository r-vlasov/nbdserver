#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fs.h>

#include "includes/functions.h"

/** 
 *   function that translate network byteorder to host
**/
unsigned long long		
ntohll(const unsigned long long input)
{
	unsigned long long rval;
	unsigned char* data = (unsigned char*) &rval;
	data[0] = input >> 56;
	data[1] = input >> 48;
	data[2] = input >> 40;
	data[3] = input >> 32;
	data[4] = input >> 24;
	data[5] = input >> 16;
	data[6] = input >> 8;
	data[7] = input >> 0;
	return rval;
}

/** 
 *   inverse of ntohll
**/
unsigned long long
htonll(const unsigned long long input)
{
	return ntohll(input);
}

// send to socket
void
send_socket(int socket, void* data, int len)
{
	if (write(socket, data, len) == -1)
	{
		ERROR("write to socket error");
		exit(EXIT_FAILURE);
	}
} 

// get data from socket
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

/*
 * return size of file (even file is blk)
*/
int 
get_file_size(int fd) 
{
	struct stat st;
	if (fstat(fd, &st) < 0)
	{ 
		return -1;
	}
	if (S_ISBLK(st.st_mode))
	{
		uint64_t bs;
		if (ioctl(fd, BLKGETSIZE64, &bs) != 0)
		{
			return -1;
		}
		return bs;
	}
	else
	{
		if (S_ISREG(st.st_mode))
			return st.st_size;
		return -1;
	}
}
