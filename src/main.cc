

#include "network.h"


int main( int argc, char *argv[] ){

    int s;

    pthread_t threads[ NUMBER_OF_THREADS ];
    int return_code;
    long t;

    //pthread_mutex_init( &mutex_event, NULL );
    //pthread_mutex_init( &mutex_number_of_requests, NULL );
    for( int x = 0; x < MAX_FDS; x++ ){
        pthread_mutex_init( &mutex_fd[x], NULL );
    }


    number_of_requests = 0;

    if( argc != 2 ){
        fprintf( stderr, "Usage: %s [port]\n", argv[0] );
        exit( EXIT_FAILURE );
    }

    listening_fd = create_and_bind( argv[1] );
    if( listening_fd == -1 ){
        abort();
    }


    s = make_socket_non_blocking( listening_fd );
    if( s == -1 ){
        abort();
    }

    s = listen( listening_fd, SOMAXCONN );
    if( s == -1 ){
        perror( "listen" );
        abort();
    }




    epoll_listening_fd = epoll_create1(0);
    if( epoll_listening_fd == -1 ){
        perror( "epoll_create" );
        abort();
    }
    struct epoll_event event;
    event.data.fd = listening_fd;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl( epoll_listening_fd, EPOLL_CTL_ADD, listening_fd, &event );
    if( s == -1 ){
        perror( "epoll_ctl" );
        abort();
    }



    epoll_connections_fd = epoll_create1(0);
    if( epoll_connections_fd == -1 ){
        perror( "epoll_create" );
        abort();
    }
    struct epoll_event connections_event;



    File response_file( "responses/001.html.gz" );

    response_body = new Utf8String( response_file.getContents() );

    //printf( "Size of contents: %d\n", response_body->getLength() );

    printf( "Starting server with %d threads.\n", NUMBER_OF_THREADS );


    for( t = 0; t < NUMBER_OF_THREADS; t++ ){

        return_code = pthread_create( &threads[t], NULL, connection_worker, (void *)t );
        if( return_code ){
            //printf( "ERROR; return code from pthread_create() is %d\n", return_code );
            exit(-1);
        }

    }

    struct epoll_event *epoll_events;

    /* Buffer where events are returned */
    epoll_events = (struct epoll_event *) calloc( MAX_EPOLL_EVENTS, sizeof(struct epoll_event) );



    /* The event loop */
    while( 1 ){

        int n, i;

        n = epoll_wait( epoll_listening_fd, epoll_events, MAX_EPOLL_EVENTS, -1 );



        for( i = 0; i < n; i++ ){

            if(
                (epoll_events[i].events & EPOLLERR) ||
                (epoll_events[i].events & EPOLLHUP) ||
                (!(epoll_events[i].events & EPOLLIN))
            ){


                /* An error has occured on this fd, or the socket is not ready
                * for reading (why were we notified then?) */
                //fprintf( stderr, "Hangup on descriptor %d\n", epoll_events[i].data.fd );
                close( epoll_events[i].data.fd );

            }else if( listening_fd == epoll_events[i].data.fd ){


                /* We have a notification on the listening socket, which
                means one or more incoming connections. */

                while( 1 ){

                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int new_connection_fd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    new_connection_fd = accept( listening_fd, &in_addr, &in_len );

                    if( new_connection_fd == -1 ){

                        if( errno == EAGAIN || errno == EWOULDBLOCK ){
                            /* We have processed all incoming connections. */
                            //printf( "We have processed all incoming connections." );
                            break;
                        }else{
                            perror( "accept" );
                            break;
                        }

                    }

                    //printf( "Main: locking connection fd: %d \n", new_connection_fd );
                    //pthread_mutex_lock( &mutex_fd[ new_connection_fd ] );
                    //printf( "Main: locked connection fd: %d \n", new_connection_fd );


                    s = getnameinfo(
                            &in_addr, in_len,
                            hbuf, sizeof hbuf,
                            sbuf, sizeof sbuf,
                            NI_NUMERICHOST | NI_NUMERICSERV
                    );

                    if( s == 0 ){
                        //printf( "Accepted connection on descriptor %d (host=%s, port=%s)\n", new_connection_fd, hbuf, sbuf);
                    }

                    /* Make the incoming socket non-blocking and add it to the
                    list of fds to monitor. */

                    s = make_socket_non_blocking( new_connection_fd );
                    if( s == -1 ){
                        abort();
                    }

                    cork( new_connection_fd );

                    connections_event.data.fd = new_connection_fd;
                    connections_event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl( epoll_connections_fd, EPOLL_CTL_ADD, new_connection_fd, &connections_event );
                    if( s == -1 ){
                        perror( "epoll_ctl" );
                        abort();
                    }


                    //printf( "Main: unlocking connection fd: %d \n", new_connection_fd );
                    //pthread_mutex_unlock( &mutex_fd[ new_connection_fd ] );
                    //printf( "Main: unlocked connection fd: %d \n", new_connection_fd );

                }


            }else{

                printf( "Not expected: %d ", epoll_events[i].data.fd );

            }


        }
    }

    pthread_exit( NULL );
    //pthread_mutex_destroy( &mutex_event );
    //pthread_mutex_destroy( &mutex_number_of_requests );
    for( int x = 0; x < MAX_FDS; x++ ){
        pthread_mutex_destroy( &mutex_fd[x] );
    }

    close( listening_fd );

    delete response_body;

    printf( "Exiting main" );

    return EXIT_SUCCESS;


}


