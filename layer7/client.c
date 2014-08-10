//
//  client.c
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
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>

#define MAX_FLOWS 40

int MAX_MESSAGE_SIZE;

int starting_local_port;
char* server_host;
struct sockaddr_storage sa_lock[MAX_FLOWS];

int sd[MAX_FLOWS];
pthread_t threads[MAX_FLOWS];

int client_sd;
int NUM_FLOWS;


struct tcp_sock
{
    int thread_index;
    int number_of_flows;
    int sd;
    struct sockaddr_storage* sa;
    char* filename;
};

struct tcp_sock ts[MAX_FLOWS];

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


int get_file_size(FILE* f)
{
    int file_size;
    if( f == NULL )
    {
        perror("fopen");
        exit( 1 );
    }
    fseek(f,0,SEEK_END);
    file_size = (int)ftell(f);
    fseek(f,0,SEEK_SET);
    return file_size;
}


unsigned long input_stream(FILE* file, int thread_index, int number_of_flows,int message_index ,char* message_buffer)
{
    unsigned long number_of_items_sent;
    unsigned long message_size = 0;
    unsigned long first_byte;
    unsigned long last_byte;
    int bytes_per_flow;
    static int file_size = -1;
    unsigned long current_byte;
    
    if( file_size == -1)
        file_size = get_file_size( file );
    
    
    bytes_per_flow = file_size / number_of_flows;
    first_byte = thread_index * bytes_per_flow;
    
    current_byte = first_byte + message_index*MAX_MESSAGE_SIZE;
    fseek( file, current_byte, 0);
    
    // if last flow then add remaining bytes
    if( thread_index + 1 == number_of_flows )
        bytes_per_flow = bytes_per_flow + file_size % number_of_flows;
    
    last_byte = first_byte + bytes_per_flow - 1 ;
    
    if( current_byte < last_byte )
    {
        if( current_byte + MAX_MESSAGE_SIZE > last_byte )
        {
            number_of_items_sent = fread(message_buffer, (last_byte - current_byte) + 1, 1, file);
            message_size = number_of_items_sent * (last_byte - current_byte + 1);
            current_byte = last_byte;
        }
        else
        {
            number_of_items_sent = fread( message_buffer, MAX_MESSAGE_SIZE, 1, file);
            current_byte = current_byte + MAX_MESSAGE_SIZE ;
            message_size = number_of_items_sent * MAX_MESSAGE_SIZE;
        }
    }
    return message_size;
}


void*
tcp_flow(void *arg)
{
    int thread_index = ((struct tcp_sock *)arg)->thread_index;
    int number_of_flows = ((struct tcp_sock *)arg)->number_of_flows;
    int sd = ((struct tcp_sock *)arg)->sd;
    struct sockaddr_storage* sa = ((struct tcp_sock *)arg)->sa;
    char* filename = ((struct tcp_sock *)arg)->filename;
    char* message_buffer;
    unsigned long   message_size;
    FILE* file ;
    int i;
    int file_size;
    int total_bytes;
    unsigned long bytes_sent = 0;
    int message_index = 0;
    char* total_time;
    clock_t disk_read_start, disk_read_finish;
    
    long total_transmission_time = 0;
    time_t  t0, t1;
    
    total_time = (char*)malloc(sizeof(char*)* 10);
    if( total_time == NULL)
    {
        printf("Error allocating total_time string \n");
        exit(1);
    }
    
    file = fopen(filename, "r");
    if( file == NULL )
    {
        perror("fopen");
        exit(1);
    }
    
    file_size = get_file_size( file );
    total_bytes = file_size / number_of_flows;
    // if last flow then add remaining bytes
    if( thread_index + 1 == number_of_flows )
        total_bytes = total_bytes + file_size % number_of_flows;

    if( connect( sd, (struct sockaddr*)sa, sizeof( struct sockaddr ) ) < 0)
    {
        printf("%d: Connect error for socket %d\n", thread_index, sd);
        perror("connect");
        exit(1);
    }
    
    message_buffer = (char*) malloc(sizeof(char*)*MAX_MESSAGE_SIZE);
    if( message_buffer ==  NULL )
    {
        printf("Error allocating message buffer in thread %d !\n", thread_index);
        exit( 0 );
    }

    memset(message_buffer,0,MAX_MESSAGE_SIZE);
    
    disk_read_start = clock();
    message_size = input_stream(file, thread_index, number_of_flows,message_index ,message_buffer);
    disk_read_finish = clock();

    // progress bar
    printf("\r");
    for( i = 0; i < thread_index ; i++ )
        printf("\t");
    printf("%d%%", (int)( (float)bytes_sent/total_bytes * 100) );
    fflush(stdout);
 
    while( message_size > 0 )
    {
        t0 = time(NULL);
        if (send(sd, message_buffer, message_size, 0) != message_size)
        {
            perror("send");
            exit( 0 );
        }
        t1 = time(NULL);

        total_transmission_time += (long)(t1 - t0);
        bytes_sent = bytes_sent + message_size;
        message_index++;
        memset(message_buffer, 0, MAX_MESSAGE_SIZE);
        
        // progress bar
        printf("\r");
        for( i = 0; i < thread_index ; i++ )
            printf("\t");
        printf("%d%%", (int)( (float)bytes_sent/total_bytes *100) );
        
        fflush(stdout);
        // disk_read_start = clock();
        message_size = input_stream(file, thread_index, number_of_flows, message_index, message_buffer);
    }
    
    sprintf(total_time, "%ld",   total_transmission_time );
    close( (int)file );
    shutdown(sd,2);
    return total_time ;
}


int
main(int argc, char** argv)
{
    int c,i;
    int number_of_flows = 0;
    int mixed = 0;
    char* filename = NULL;
    struct sockaddr_in* t_addr;
    struct in_addr  server_addr;
    double max_time = 0.0;
    void* res;
    FILE* output_file;
    FILE* file;
    int file_size;
    time_t  start, finish;
    
    while( ( c= getopt(argc, argv, "H:P:M:N:S:F:")) >= 0 )
    {
        switch( c ) {
            case 'H':
                server_host = optarg;
                break;
            case 'P':
                starting_local_port = atoi( optarg );
                break;
            case 'N':
                number_of_flows = atoi( optarg );
                if( number_of_flows > MAX_FLOWS )
                {
                    printf("More flows than allowed error !\n");
                    exit(0);
                }
                break;
            case 'S':
                MAX_MESSAGE_SIZE = atoi( optarg );
                if( MAX_MESSAGE_SIZE >  268435456 )
                {
                    printf("max message size cannot be greater than 268435456 bytes !\n");
                    exit( 0 );
                }
                break;
            case 'F':
                filename = optarg;
                break;
            case 'M':
                mixed = atoi( optarg );
                if( mixed != 1 && mixed != 0 )
                {
                    printf("M option indicates mixed parity when 1 and only even ports when 0\n");
                    exit( 0 );
                }
                break;
            default:
                usage( argv[0] );
                exit( 0 );
        }
    }
    
    
    file = fopen(filename, "r");
    if( file == NULL )
    {
        perror("fopen");
        exit( 1 );
    }
    
    file_size = get_file_size( file );
    close( (int) file);
    
    // progress bar
    printf("\r");
    for( i = 0; i < number_of_flows ; i++ )
        printf("%d%%\t", 0 );
    
    start = time( NULL );
    
    for( i = 0 ; i < number_of_flows; i++ )
    {
        if( (sd[i] = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 )
        {
            socket_error(i);
            exit( 0 );
        }
    }
    
    server_addr.s_addr = inet_addr(server_host);
    /* server address structures are in sa_lock array */
    for( i = 0; i < number_of_flows; i++ )
    {
        t_addr = (struct sockaddr_in*)&sa_lock[i];
        memset(t_addr, 0, sizeof(struct sockaddr_in));
        t_addr->sin_family = AF_INET;
        if(mixed)
            t_addr->sin_port   =  htons(starting_local_port + i);
        else
            t_addr->sin_port   =  htons(starting_local_port + 2 * i);
        t_addr->sin_addr   = server_addr;
    }
    
    for( i = 0; i < number_of_flows; i++ )
    {
        ts[i].thread_index = i;
        ts[i].number_of_flows = number_of_flows;
        ts[i].sd = sd[i];
        ts[i].sa = &sa_lock[i];
        ts[i].filename = filename;
    }
    
    /* create a thread for each flow */
    for(i = 0; i < number_of_flows; i++)
    {
        if( pthread_create(&threads[i], NULL, &tcp_flow, &ts[i]) )
        {
            printf("Thread create error !\n");
            exit( 0 );
        }
    }
    
    for(i = 0 ; i < number_of_flows; i++)
    {
        if( pthread_join( threads[i], &res) )
        {
            printf("Thread join error !\n");
            exit(0);
        }
        if( atof((char *)res) > max_time )
            max_time = atof((char *)res);
    }
    
    for(i = 0 ; i < number_of_flows; i++)
        close( sd[i] );
    
    output_file = fopen("output.txt","a+");
    fprintf(output_file,"%f\n",max_time);
    fclose(output_file);
    
    finish = time( NULL );
    printf("\n");
    printf("Total time   %10.3ld seconds\n", finish - start );
    printf("Goodput      %10.3d B/s  %10.3f MB/s\n", (int) (file_size / (finish -start)),
           (float)( (file_size / (finish -start))  / (1024*1024)) );
    return 0;
}


