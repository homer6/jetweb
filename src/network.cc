
#include "network.h"


int epoll_listening_fd;
int epoll_connections_fd;
int listening_fd;

int number_of_requests;

Utf8String *response_body;

//pthread_mutex_t mutex_event;
//pthread_mutex_t mutex_number_of_requests;
pthread_mutex_t mutex_fd[ MAX_FDS ];




int make_socket_non_blocking( int sfd ){

    int flags, s;

    flags = fcntl( sfd, F_GETFL, 0 );
    if( flags == -1 ){
        perror ("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl( sfd, F_SETFL, flags );
    if( s == -1 ){
        perror ("fcntl");
        return -1;
    }

    return 0;

}



int flush( int socket ){

    int state = 0;
    setsockopt( socket, IPPROTO_TCP, TCP_CORK, &state, sizeof(state) );
    state = 1;
    setsockopt( socket, IPPROTO_TCP, TCP_CORK, &state, sizeof(state) );

    return 0;

}



int cork( int socket ){

    int state = 1;
    setsockopt( socket, IPPROTO_TCP, TCP_CORK, &state, sizeof(state) );
    return 0;

}



int write_flush( int connection_socket_fd, const Utf8String *response_message ){

    int s = write( connection_socket_fd, response_message->getCString(), response_message->getLength() );
    if( s == -1 ){
        perror( "write to connection error" );
        abort();
    }
    flush( connection_socket_fd );  //uncorks

    return 0;

}


int write_noflush( int connection_socket_fd, const Utf8String *response_message ){

    int s = write( connection_socket_fd, response_message->getCString(), response_message->getLength() );
    if( s == -1 ){
        perror( "write to connection error" );
        abort();
    }

    return 0;

}





int create_and_bind( char *port ){

    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset( &hints, 0, sizeof (struct addrinfo) );
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    s = getaddrinfo( NULL, port, &hints, &result );
    if( s != 0 ){
        fprintf( stderr, "getaddrinfo: %s\n", gai_strerror(s) );
        return -1;
    }

    for( rp = result; rp != NULL; rp = rp->ai_next ){

        sfd = socket( rp->ai_family, rp->ai_socktype, rp->ai_protocol );
        if( sfd == -1 ){
            continue;
        }


        int yes = 1;
        if( setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1 ){
            perror("setsockopt");
        }

        s = bind( sfd, rp->ai_addr, rp->ai_addrlen );

        //fprintf( stdout, "Binding address length: %d\n", rp->ai_addrlen );

        if( s == 0 ){
            /* We managed to bind successfully! */
            break;
        }else{
            perror( "bind" );
        }

        close( sfd );

    }

    if( rp == NULL ){
        fprintf( stderr, "Could not bind\n" );
        return -1;
    }

    freeaddrinfo( result );

    return sfd;

}


int send_response( int connection_socket_fd, const Utf8String *response_body ){

    Utf8String response_headers(
        "HTTP/1.1 200 OK\n"
        "Content-Encoding: gzip\n"
        "Content-Length: " + Utf8String( response_body->getLength() ) + "\n\n"
    );
    write_noflush( connection_socket_fd, &response_headers );

    write_flush( connection_socket_fd, response_body );

    return 0;

}








void *connection_worker( void *args ){

   long thread_id = (long)args;
   printf( "Entering thread #%ld\n", thread_id );

   int s;

   struct epoll_event *events;

   /* Buffer where events are returned */
   events = (struct epoll_event *) calloc( MAX_EPOLL_EVENTS, sizeof(struct epoll_event) );



   /* The event loop */
   while( 1 ){

        int n, i;

        n = epoll_wait( epoll_connections_fd, events, MAX_EPOLL_EVENTS, -1 );


        if( n == -1 ){
            printf("error %d\n", errno );
            perror( "epoll_wait" );
            sleep( 10 );
            continue;
        }

        for( i = 0; i < n; i++ ){

            /*
            if( thread_id != 0 ){

                if( listening_fd == events[i].data.fd ){
                    //only thread 0 handles listening socket operations
                    continue;
                }

            }*/

            //printf( "------------Thread--%ld-------------------\n", thread_id );


            //printf( "Thread: #%ld locking fd: %d \n", thread_id, events[i].data.fd );
            pthread_mutex_lock( &mutex_fd[ events[i].data.fd ] );
            //printf( "Thread: #%ld locked fd: %d \n", thread_id, events[i].data.fd );


            if(
                (events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))
            ){


                /* An error has occured on this fd, or the socket is not ready
                * for reading (why were we notified then?) */
                fprintf( stderr, "Hangup on descriptor %d\n", events[i].data.fd );
                close( events[i].data.fd );

                //printf( "Thread: #%ld unlocking fd: %d \n", thread_id, events[i].data.fd );
                pthread_mutex_unlock( &mutex_fd[events[i].data.fd] );
                //printf( "Thread: #%ld unlocked fd: %d \n", thread_id, events[i].data.fd );

                continue;


            }else{


                /* We have data on the fd waiting to be read. Read and
                display it. We must read whatever data is available
                completely, as we are running in edge-triggered mode
                and won't get a notification again for the same
                data. */

                int done = 0;
                int connection_socket_fd = events[i].data.fd;


                while( 1 ){

                    ssize_t count;
                    char buf[4096];

                    //printf( "Connection fd: %d", connection_socket_fd );

                    count = read( connection_socket_fd, buf, sizeof buf );

                    if( count == -1 ){

                        // If errno == EAGAIN, that means we have read all
                        // data. So go back to the main loop.


                        if( errno != EAGAIN ){

                            printf( "Read error on FD: %d", connection_socket_fd );
                            perror( "read" );




                        }else{


                            //perror( "read2" );
                            /*
                            pthread_mutex_lock( &mutex_number_of_requests );
                                number_of_requests++;
                                if( number_of_requests % 100 == 0 ){
                                    printf( "Requests: %d\n", number_of_requests );
                                }
                            pthread_mutex_unlock( &mutex_number_of_requests );
                            */

                            //finished reading

                        }

                        done = 1;
                        break;


                    }else if( count == 0 ){

                        // End of file. The remote has closed the connection.
                        printf( "Thread: #%ld - End of file. The remote has closed the connection. fd: %d \n", thread_id, connection_socket_fd );
                        done = 1;
                        break;

                    }

                    s = write( 1, buf, count );
                    if( s == -1 ){
                        perror ("write");
                        abort();
                    }


                    //close( connection_socket_fd );
                    //done = 1;
                    //break;

                }

                // Write the buffer to standard output


                sleep( 1 );
                printf( "Thread: #%ld sending data to fd: %d \n", thread_id, connection_socket_fd );
                send_response( connection_socket_fd, response_body );
                printf( "Thread: #%ld finished sending data to fd: %d \n", thread_id, connection_socket_fd );


                if( done ){

                    //printf( "Closed connection on descriptor %d\n", connection_socket_fd );

                    /* Closing the descriptor will make epoll remove it
                    from the set of descriptors which are monitored. */
                    close( connection_socket_fd );

                }

                //printf( "Thread: #%ld unlocking fd: %d \n", thread_id, connection_socket_fd );
                pthread_mutex_unlock( &mutex_fd[events[i].data.fd] );
                //printf( "Thread: #%ld unlocked fd: %d \n", thread_id, connection_socket_fd );

            }


        }

    }

    free( events );

    //printf( "Exiting thread #%ld\n", thread_id );

    pthread_exit( NULL );

}









