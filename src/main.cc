

#include "jet/File.h"
#include "jet/Exception.h"
#include "jet/Utf8String.h"

using namespace jet;


/*
#include <iostream>
#include <bitset>


using namespace std;
using namespace jet;


template< class T >
void print_as_binary( T value, ostream& output_stream ){

    bitset< sizeof(T) * 8 > x( value );

    output_stream << x ;

}


void show_usage(){

    cout << "Usage: my_exe <filename>" << endl;

}


void red( Utf8String text ){

    cout << "\033[1;31m" << text << "\033[0m";

}
*/

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

#define MAXEVENTS 40000



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



int write_flush( int connection_socket_fd, const Utf8String &response_message ){

    int s = write( connection_socket_fd, response_message.getCString(), response_message.getLength() );
    if( s == -1 ){
        perror( "write to connection error" );
        abort();
    }
    flush( connection_socket_fd );  //uncorks

    return 0;

}


int write_noflush( int connection_socket_fd, const Utf8String &response_message ){

    int s = write( connection_socket_fd, response_message.getCString(), response_message.getLength() );
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

        fprintf( stdout, "Binding address length: %d\n", rp->ai_addrlen );

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


int send_response( int connection_socket_fd, const Utf8String &response_body ){

    Utf8String response_headers(
        "HTTP/1.1 200 OK\n"
        "Content-Encoding: gzip\n"
        "Content-Length: " + Utf8String( response_body.getLength() ) + "\n\n"
    );
    write_noflush( connection_socket_fd, response_headers );

    write_flush( connection_socket_fd, response_body );

    return 0;

}



int main( int argc, char *argv[] ){

    int sfd, s;
    int efd;
    struct epoll_event event;
    struct epoll_event *events;

    int number_of_requests = 0;

    if( argc != 2 ){
        fprintf( stderr, "Usage: %s [port]\n", argv[0] );
        exit( EXIT_FAILURE );
    }

    sfd = create_and_bind( argv[1] );
    if( sfd == -1 ){
        abort();
    }


    s = make_socket_non_blocking( sfd );
    if( s == -1 ){
        abort();
    }

    s = listen( sfd, SOMAXCONN );
    if( s == -1 ){
        perror( "listen" );
        abort();
    }

    efd = epoll_create1(0);
    if( efd == -1 ){
        perror( "epoll_create" );
        abort();
    }

    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl( efd, EPOLL_CTL_ADD, sfd, &event );
    if( s == -1 ){
        perror( "epoll_ctl" );
        abort();
    }

    File response_file( "responses/001.html.gz" );

    Utf8String response_body = response_file.getContents();


    /* Buffer where events are returned */
    events = (struct epoll_event *) calloc( MAXEVENTS, sizeof event );

    /* The event loop */
    while( 1 ){

        int n, i;

        n = epoll_wait( efd, events, MAXEVENTS, -1 );

        for( i = 0; i < n; i++ ){

            if(
                (events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))
            ){


                /* An error has occured on this fd, or the socket is not ready
                * for reading (why were we notified then?) */
                fprintf( stderr, "Hangup on descriptor %d\n", events[i].data.fd );
                close( events[i].data.fd );
                continue;


            }else if( sfd == events[i].data.fd ){


                /* We have a notification on the listening socket, which
                means one or more incoming connections. */

                while( 1 ){

                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept( sfd, &in_addr, &in_len );

                    if( infd == -1 ){

                        if( errno == EAGAIN || errno == EWOULDBLOCK ){
                            /* We have processed all incoming connections. */
                            break;
                        }else{
                            perror( "accept" );
                            break;
                        }

                    }

                    s = getnameinfo(
                            &in_addr, in_len,
                            hbuf, sizeof hbuf,
                            sbuf, sizeof sbuf,
                            NI_NUMERICHOST | NI_NUMERICSERV
                    );

                    if( s == 0 ){
                        printf("Accepted connection on descriptor %d (host=%s, port=%s)\n", infd, hbuf, sbuf);
                    }

                    /* Make the incoming socket non-blocking and add it to the
                    list of fds to monitor. */

                    s = make_socket_non_blocking( infd );
                    if( s == -1 ){
                        abort();
                    }

                    cork( infd );

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl( efd, EPOLL_CTL_ADD, infd, &event );
                    if( s == -1 ){
                        perror( "epoll_ctl" );
                        abort();
                    }


                }

                continue;

            }else{


                /* We have data on the fd waiting to be read. Read and
                display it. We must read whatever data is available
                completely, as we are running in edge-triggered mode
                and won't get a notification again for the same
                data. */

                int done = 0;
                int connection_socket_fd;

                while( 1 ){

                    ssize_t count;
                    char buf[4096];

                    connection_socket_fd = events[i].data.fd;

                    count = read( connection_socket_fd, buf, sizeof buf );

                    if( count == -1 ){

                        // If errno == EAGAIN, that means we have read all
                        // data. So go back to the main loop.
                        if( errno != EAGAIN ){
                            perror( "read" );
                            done = 1;
                        }
                        break;

                    }else if( count == 0 ){

                        // End of file. The remote has closed the connection.
                        done = 1;
                        break;

                    }


                    // Write the buffer to standard output
                    /*
                    s = write (1, buf, count);
                    if( s == -1 ){
                    perror ("write");
                    abort ();
                    }*/

                    send_response( connection_socket_fd, response_body );

                    number_of_requests++;

                    if( number_of_requests > 100000 ){
                        return EXIT_SUCCESS;
                    }

                    //close( events[i].data.fd );
                    //done = 1;
                    //break;

                }

                if( done ){

                    printf( "Closed connection on descriptor %d\n", connection_socket_fd );

                    /* Closing the descriptor will make epoll remove it
                     from the set of descriptors which are monitored. */
                    close( connection_socket_fd );

                }

            }

        }

    }

    free (events);

    close (sfd);

    return EXIT_SUCCESS;


}





    /*
    Utf8String my_string( "Hello", 5 );

    cout << my_string << endl;


    unsigned char current_byte = 0;

    unsigned int value = 0x80 >> 7;

    int shift_bytes = sizeof(unsigned int) - 1;

    current_byte = value;

    cout << shift_bytes << endl;

    //cout << value << endl;

    //cout << current_byte << endl;

    cout << "Value as Binary: ";
    print_as_binary( value, cout );
    cout << endl;

    cout << "Current Byte as Binary: ";
    print_as_binary( current_byte, cout );
    cout << endl;

    cout << "Shift Bytes as Binary: ";
    print_as_binary( shift_bytes, cout );
    cout << endl;

    cout << "Literal as Binary: ";
    print_as_binary( 0x01, cout );
    cout << endl;

    //print_as_binary( my_string, cout );

    */

