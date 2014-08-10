//
//  server.c
//  layer7
//
//  Created by Theodore Elhourani.
//  Copyright (c) 2009 Theodore Elhourani. All rights reserved.
//
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <pthread.h>
#include <errno.h>

#define MAX_FLOWS 40
#define MAX_MESSAGE_SIZE  34000000 // this is 16 megabytes //138518230 //277036460 //1048576  //65535 //250
#define MAX_BACKLOG 20

int starting_local_port;
char* local_host;
struct sockaddr_storage sa_lock[MAX_FLOWS];
int sd[MAX_FLOWS];
int client_sd;
char* message_buffer[MAX_FLOWS];
int NUM_FLOWS;
pthread_t threads[MAX_FLOWS];

struct tcp_sock
{
    int* sd;
    struct sockaddr_storage* sa;
    char* message_buffer;
};

int
usage(char* program)
{
    printf("Error using program %s\n", program);
    return 0;
}

int
socket_error(int i)
{
    printf("Error creating socket %d\n", i);
    return 0;
}

int
bind_error(int i)
{
    printf("Error binding socket %d\n",i);
    return 0;
}

int
listen_error(int i)
{
    printf("Error listening socket %d\n", i);
    return 0;
}

int
accept_error()
{
    printf("Error accepting connection\n");
    return 0;
}

int
receive_error()
{
    printf("Receiving error\n");
    return 0;
}

void*
tcp_flow(void *arg)
{
    int* sd = ((struct tcp_sock *)arg)->sd;
    struct sockaddr_storage* sa = ((struct tcp_sock *)arg)->sa;
    char* message_buffer = ((struct tcp_sock *)arg)->message_buffer;
    int total_bytes = 0;
    int optval = 1;
    struct sockaddr_in c_addr;
    int client_sd, c_addr_len;
    long message_size;
  
    if( bind(*sd, (struct sockaddr*) sa, sizeof(struct sockaddr) ) < 0 )
    {
        perror("bind");
        printf("Error binding socket %d error %d\n", *sd, errno);
        exit( 0 );
    }
    
    if( listen(*sd, MAX_BACKLOG ) < 0)
    {
        printf("Error listening on socket %d\n",*sd);;
        exit( 0 );
    }
    
    c_addr_len = sizeof(struct sockaddr_in);
    
    while(1)
    {
        // Wait for a client to connect
        if( (client_sd = accept(*sd, (struct sockaddr *) &c_addr, (socklen_t*)&c_addr_len)) < 0)
        {
            accept_error();
            exit( 0 );
        }
        
        setsockopt (client_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        memset(message_buffer, 0, MAX_MESSAGE_SIZE);
        
        // Receive message from client
        if ((message_size = recv(client_sd, message_buffer, MAX_MESSAGE_SIZE, 0)) < 0)
        {
            receive_error();
            exit( 0 );
        }
        //  printf("size %d\n",message_size);
        total_bytes += message_size;
        
        // Send received string and receive again until end of transmission
        while ( message_size > 0)      // zero indicates end of transmission
        {
            memset(message_buffer, 0, MAX_MESSAGE_SIZE);
            // See if there are more data to receive
            if ((message_size = recv(client_sd, message_buffer, MAX_MESSAGE_SIZE, 0)) < 0)
            {
                receive_error();
                exit( 0 );
            }
            total_bytes += message_size;
        }
        total_bytes = 0;
        shutdown(client_sd, 2);
        close( client_sd );
    }
    
}


int
main(int argc, char** argv)
{
    int c,i;
    int optval = 1;
    struct sockaddr_in* t_addr;
    struct in_addr      local_addr;
    struct tcp_sock ts;
    void* res;
    
    while( ( c= getopt(argc, argv, "H:P:F:")) >= 0 )
    {
        switch( c ) {
            case 'H':
                local_host = optarg;
                break;
            case 'P':
                starting_local_port = atoi( optarg );
                break;
            case 'F':
                NUM_FLOWS = atoi( optarg );
                if( NUM_FLOWS > MAX_FLOWS )
                {
                    printf("More flows than allowed error !\n");
                    exit( 0 );
                }
                break;
            default:
                usage( argv[0] );
                exit( 0 );
        }
    }
    
    for( i = 0 ; i < NUM_FLOWS; i++ )
    {
        message_buffer[i] = (char *)malloc( sizeof(char ) * MAX_MESSAGE_SIZE);
        if( message_buffer[i] == NULL )
        {
            printf("Error allocating message buffer !");
            exit( 0 );
        }
    }
    
    local_addr.s_addr =  INADDR_ANY;
    for( i = 0; i < NUM_FLOWS; i++ )
    {
        t_addr = (struct sockaddr_in*)&sa_lock[i];
        memset(t_addr,0,sizeof(struct sockaddr_in));
        t_addr->sin_family = AF_INET;
        t_addr->sin_port   = htons( starting_local_port + i );
        t_addr->sin_addr   = local_addr;
    }
    
    for( i = 0 ; i < NUM_FLOWS; i++ )
    {    if( (sd[i] = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 )
    {
        socket_error(i);
        exit( 0 );
    }
        setsockopt (sd[i], SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    }
  
    // create a thread for each flow
    for( i = 0; i < NUM_FLOWS; i++ )
    {
        ts.sd = &sd[i];
        ts.sa = &sa_lock[i];
        ts.message_buffer = message_buffer[i];
        if( pthread_create(&threads[i], NULL, &tcp_flow, &ts) )
        {
            printf("Thread create error !\n");
            exit(1);
        }
    }
    
    for( i = 0 ; i < NUM_FLOWS; i++ )
    {
        if( pthread_join( threads[i], &res) )
        {
            printf("Thread join error !\n");
            exit(1);
        }
    }
    
    for( i =0 ; i < NUM_FLOWS; i++ )
    {
        close( sd[i] );
        free( message_buffer[i] );
    }
    return 0;
}
