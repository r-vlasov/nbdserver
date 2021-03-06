/**
 * nbd.h
 * Vlasov Roman. June 2021
 * Unit with useful NBD structures and magics 
**/

#ifndef _NBDSERVER_H
#define _NBDSERVER_H

/* 
 *
 *   handshake phase 
 *
*/	
#define NBDMAGIC 0x4e42444d41474943
#define IHAVEOPT 0x49484156454F5054

// server
#define NBD_FLAG_FIXED_NEWSTYLE	(1 << 0)	
#define NBD_FLAG_NO_ZEROES		(1 << 1)	
// client
#define NBD_FLAG_C_FIXED_NEWSTYLE 	NBD_FLAG_FIXED_NEWSTYLE
#define NBD_FLAG_C_NO_ZEROES		NBD_FLAG_NO_ZEROES

/*
 * structure of initial server's handshake 
 * (first phase in a sequence of handshake)
*/
typedef struct {
	unsigned long long 	magic_1;
	unsigned long long 	magic_2;
	unsigned short 		hsflags;
} __attribute__((packed)) HANDSHAKE_SERVER;

/*
 * structure of initial client's handshake
 * (reply to server's handshake)
*/
typedef struct {
	unsigned int 		clflags;
} __attribute__((packed)) HANDSHAKE_CLIENT;


/* 
 *
 *   setting of options phase
 *
*/	
#define NBD_OPT_ABORT				2
#define NBD_OPT_LIST				3
#define NBD_OPT_GO					7
#define NBD_OPT_STRUCTURED_REPLY	8

// reply
#define NBD_OPTION_REPLY_MAGIC		0x3e889045565a9
#define NBD_REP_ACK 				1
#define NBD_REP_SERVER 				2
#define NBD_REP_INFO				3
// reply errors
#define NBD_REP_ERR_UNSUP			(1 | (1 << 31))
#define NBD_REP_ERR_INVALID			(3 | (1 << 31))
#define NBD_REP_ERR_UNKNOWN			(6 | (1 << 31))

// NBD_REP_INFO types
#define NBD_INFO_EXPORT				0
#define NBD_INFO_NAME				1
#define NBD_INFO_DESCRIPTION		2
/*
 * structure described request of client to set 'option'
*/
typedef struct {
	unsigned long long 	magic;
	unsigned int 		option;
	unsigned int		len;
} __attribute__((packed)) OPTION_REQUEST_HEADER;

typedef struct {
	OPTION_REQUEST_HEADER*  header;
	char*                   data;
} __attribute__((packed)) OPTION_REQUEST;


/*
 * response to request (option)
*/
typedef struct {
	unsigned long long  magic;
	unsigned int        opt;
	unsigned int        reply_type;
	unsigned int        datasize;
} __attribute__ ((packed)) OPTION_REPLY_HEADER;

typedef struct {
    OPTION_REPLY_HEADER*    header;
	void*                   data;
} __attribute__ ((packed)) OPTION_REPLY;


/* 
 * structure of data field while setting 'GO' option
*/
typedef struct {
	unsigned int    len;
    char*           name;        
} __attribute__((packed)) OPTION_GO_DATA;

/* NBD_REP_INFO types */
typedef struct {
	unsigned short		type;
	unsigned long long	size;
	unsigned short		flags;
} __attribute__((packed)) OPTION_GO_REP_INFO_EXPORT;


/* transmission phase */
#define NBD_REQUEST_MAGIC			0x25609513
#define NBD_SIMPLE_REPLY_MAGIC		0x67446698
#define NBD_STRUCTURED_REPLY_MAGIC 	0x668e33ef

#define NBD_FLAG_HAS_FLAGS  (1 << 0)
#define NBD_FLAG_READ_ONLY 	(1 << 1)

// structured reply chunk
#define NBD_REPLY_TYPE_NONE			0
#define NBD_REPLY_TYPE_OFFSET_DATA	1
#define NBD_REPLY_FLAG_DONE			(1 << 0)

#define NBD_CMD_READ        0
#define NBD_CMD_WRITE       1
#define NBD_CMD_DISC        2

/*
 * transmition request (from client)
*/
typedef struct {
	unsigned int 	    magic;
	unsigned short 	    flags;
	unsigned short 	    type;
	unsigned long long	handle;
	unsigned long long 	offset;
	unsigned int		length;
} __attribute__((packed)) NBD_REQUEST_HEADER;

typedef struct {
	NBD_REQUEST_HEADER* header;
	void*               data;
} __attribute__((packed)) NBD_REQUEST;


/*
 * transmition reply (to client)
 * SIMPLE REPLY
*/
typedef struct {
	unsigned int        magic;
	unsigned int        error;
	unsigned long long  handle;
} __attribute__((packed)) NBD_RESPONSE_HEADER;

/*
 * transmition reply (to client)
 * STRUCTURED REPLY CHUNK MESSAGE
*/
typedef struct {
	unsigned int        magic;
	unsigned short 		flags;
	unsigned short		type;
	unsigned long long	handle;
	unsigned int		length;
} __attribute__((packed)) NBD_STRUCTURED_RESPONSE_HEADER;


#endif
