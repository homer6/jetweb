#ifndef NETWORK_H
#define NETWORK_H

#include "jet/File.h"
#include "jet/Exception.h"
#include "jet/Utf8String.h"

using namespace jet;


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <pthread.h>

#define MAX_EPOLL_EVENTS 40000
#define MAX_FDS 70000
#define NUMBER_OF_THREADS 16

extern int epoll_listening_fd;
extern int epoll_connections_fd;
extern int listening_fd;

extern int number_of_requests;

extern Utf8String *response_body;

//pthread_mutex_t mutex_event;
//pthread_mutex_t mutex_number_of_requests;
extern pthread_mutex_t mutex_fd[ MAX_FDS ];



int make_socket_non_blocking( int sfd );
int flush( int socket );
int cork( int socket );
int write_flush( int connection_socket_fd, const Utf8String *response_message );
int write_noflush( int connection_socket_fd, const Utf8String *response_message );
int create_and_bind( char *port );
int send_response( int connection_socket_fd, const Utf8String *response_body );


void *connection_worker( void *args );








#endif /* NETWORK_H */
