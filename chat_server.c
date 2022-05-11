// Lab 9: chat_server.c
// The goal of this lab is to to write a chat server using pthreads that allows
//      clients to chat with each other using nc (or jtelnet). 
//      The syntax of your server is:
//      UNIX> ./chat-server port Chat-Room-Names ...
//      So, for example, if you'd like to serve chat rooms for football, bridge
//      and politics on hydra3 port 8005, you would do:
//      UNIX> ./chat_server hydra3.eecs.utk.edu 8005 Football Bridge Politics

//      See http://web.eecs.utk.edu/~jplank/plank/classes/cs360/360/labs/Lab-9-Chat/index.html
//      for more information and lab specifications. 

// COSC 360
// Swasti Mishra 
// May 11, 2022
// James Plank 

// Libraries
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <pthread.h>
# include "sockettome.h"

# include "jrb.h"
# include "dllist.h"

// Keeps track of client information
typedef struct {
    int fd ;
    FILE *fin  ;
    FILE *fout ;
    char* user ;
    struct room* room ;
} client ;

// Keeps track of room information 
typedef struct {
    char* user ;
    Dllist clients  ;
    Dllist messages ;
    pthread_cond_t  *cond ;
    pthread_mutex_t *lock ;
} room ;

JRB threadTree ;

// Handles threading for each client/user 
void *clientThread( void *arg ){
    JRB tmpTree ;
    char cmd[1000] ;
    char buf[1000] ;
    room *currRoom ;
    client *tmpClient  ;
    client *currClient ;
    Dllist tmpDLL ;
    Dllist cleanDLL ;

    // Get client and connect input and output to stdin and stdout respectively
    currClient = ( void* ) arg ;
    currClient->fin = fdopen( currClient->fd, "room" ) ;
    if( currClient->fin == NULL ){
        exit(1) ;
    }
    currClient->fout = fdopen( currClient->fd, "w" ) ;
    if( currClient->fout == NULL ){ 
        exit(1) ; 
    }

    // Outputs room names
    if( fputs ( "Chat Rooms:\n\n", currClient->fout ) == EOF ){}
    if( fflush( currClient->fout ) == EOF ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }

    // Outputs who's online! (Each room user.)
    // There is probably a more efficient way to do this, but one I removed
    //      the extra fclose statements, I would get a "bad file descriptor"
    //      error. If I had more time, I would investigate, but I really need
    //      to just study for the exam lol 
    jrb_traverse( tmpTree, threadTree ) {
        currRoom = ( room* ) tmpTree->val.v ;
        if( fputs( currRoom->user, currClient->fout ) == EOF){
            fclose( currClient->fin ) ;
            fclose( currClient->fout ) ;
            pthread_exit( NULL ) ;
        }
        if( fflush( currClient->fout ) == EOF){
            fclose( currClient->fin ) ;
            fclose( currClient->fout ) ;
            pthread_exit( NULL ) ;
        }
        if( fputs( ":", currClient->fout ) == EOF){
            fclose( currClient->fin ) ;
            fclose( currClient->fout ) ;
            pthread_exit( NULL ) ;
        }
        if( fflush( currClient->fout ) == EOF){
            fclose( currClient->fin ) ;
            fclose( currClient->fout ) ;
            pthread_exit( NULL ) ;
        }

        // Goes through clients 
        dll_traverse( tmpDLL, currRoom->clients ){
            if( fputs( " ", currClient->fout ) == EOF){
                fclose( currClient->fin ) ;
                fclose( currClient->fout ) ;
                pthread_exit( NULL ) ;
            }
            if( fflush( currClient->fout ) == EOF ){
                fclose( currClient->fin ) ;
                fclose( currClient->fout ) ;
                pthread_exit( NULL ) ;
            }
            tmpClient = ( client* ) tmpDLL->val.v ;
            if( fputs( tmpClient->user, currClient->fout ) == EOF){
                fclose( currClient->fin ) ;
                fclose( currClient->fout ) ;
                pthread_exit( NULL ) ;
            }
            if ( fflush( currClient->fout ) == EOF ){
                fclose( currClient->fin ) ;
                fclose( currClient->fout ) ;
                pthread_exit( NULL ) ;
            }
        }
		if( fputs( "\n", currClient->fout ) == EOF ){
			fclose( currClient->fin ) ;
			fclose( currClient->fout ) ;
			pthread_exit( NULL ) ;
		}
        fflush( currClient->fout ) ;
    }
	if( fputs( "\n", currClient->fout ) == EOF ){
		fclose( currClient->fin ) ;
		fclose( currClient->fout ) ;
		pthread_exit( NULL ) ;
	}
    if( fflush( currClient->fout ) == EOF ){
        fclose( currClient->fin ) ;
		fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
	}

    // Ask client for their username 
    if( fputs( "Enter your chat name (no spaces):\n", currClient->fout ) == EOF ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }
    if( fflush( currClient->fout ) == EOF ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }
    if( fgets( cmd, sizeof(cmd), currClient->fin ) == NULL ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }
    cmd[ strlen(cmd) - 1 ] = '\0' ;
	printf( "Client name entered: %s\n", cmd ) ;
	currClient->user = strdup( cmd ) ;

    // Ask user which room they want to go to 
    if( fputs( "Enter chat room:\n", currClient->fout ) == EOF ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }
    if( fflush( currClient->fout ) == EOF ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }
    if( fgets( cmd, sizeof(cmd), currClient->fin ) == NULL ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }
	cmd[ strlen(cmd) - 1 ] = '\0' ;
	printf( "Room name entered: %s\n", cmd ) ;

    // Take user to requested room 
    tmpTree = jrb_find_str( threadTree, cmd ) ;
    if( tmpTree == NULL ){
		fprintf( stderr, "Chatroom %s does not exist\n", currRoom->user ) ;
		exit(1) ;
	}
    else{
        currRoom = ( room* ) tmpTree->val.v ;
        currClient->room = currRoom ;

        // Lock room 
        pthread_mutex_lock( currRoom->lock ) ;
        
        // Add and introduce user
        dll_append( currRoom->clients, new_jval_v( ( void* ) currClient ) ) ;
        strcpy( cmd, currClient->user ) ;
        strcat( cmd, " has joined\n\0" ) ;
        dll_append( currRoom->messages, new_jval_s( strdup(cmd) ) ) ;

        // Unlock room 
        pthread_cond_signal( currRoom->cond ) ;
        pthread_mutex_unlock( currRoom->lock ) ;
    }

    // Connects user to room 
    while(1){
        strcpy( buf, currClient->user ) ;
        strcat( buf, ": " ) ;

        // Reads client messages
        if( fgets( cmd, sizeof(cmd), currClient->fin ) == NULL ){
            fclose( currClient->fin ) ;

            // Outputs when client leaves
            strcpy( buf, currClient->user ) ;
            strcat( buf, " has left\n" ) ;
            pthread_mutex_lock( currRoom->lock ) ;
            dll_append( currRoom->messages, new_jval_s( strdup(buf) ) ) ;

            // Removes the user from the DLList
            if( currClient->fout != 0 ){ 
                fclose( currClient->fout ) ;
                cleanDLL = NULL ;
                dll_traverse( tmpDLL, currRoom->clients ){
                    tmpClient = ( client* ) tmpDLL->val.v ;
                    if( strcmp( currClient->user, tmpClient->user ) == 0){
                        cleanDLL = tmpDLL ;
                    }
                }
                if( cleanDLL != NULL ){
                    dll_delete_node(cleanDLL) ;
                }
            }

            // Remove user pthread 
            pthread_cond_signal( currRoom->cond ) ;
            pthread_mutex_unlock( currRoom->lock ) ;
            pthread_exit( NULL ) ;
        }
        else{
            strcat( buf, cmd ) ;
            pthread_mutex_lock( currRoom->lock ) ;
            dll_append( currRoom->messages, new_jval_s( strdup(buf) ) ) ;
            pthread_cond_signal( currRoom->cond ) ;
            pthread_mutex_unlock( currRoom->lock ) ;
        }
    }

    return NULL ;
}

// Handles threading for each room 
void *roomThread( void *v ){
    Dllist msgDLL ;
    Dllist clientDLL ;
    room   *currRoom ;
    client *currClient ;

    // Get the room and respective lock 
    currRoom = ( room* )v ;
    pthread_mutex_lock( currRoom->lock ) ;
    while(1){
        // Waits until ready to print messages 
        pthread_cond_wait( currRoom->cond, currRoom->lock ) ;

        // Outputs messages to every user in a room 
        dll_traverse( msgDLL, currRoom->messages ){
            if( ( dll_empty( currRoom->clients ) ) == 0 ){
                dll_traverse( clientDLL, currRoom->clients ){
                    currClient = ( client* ) clientDLL->val.v ;
                    if( fputs( msgDLL->val.s, currClient->fout ) == EOF ){
                        dll_delete_node( clientDLL ) ;
                        fclose( currClient->fout ) ;
                    }
                    if( fflush( currClient->fout ) == EOF ){
                        dll_delete_node( clientDLL ) ;
                        fclose( currClient->fout ) ;
                    }
                }
            }
        }

		// Deletes old message log and sets up a new one 
		free_dllist( currRoom->messages ) ;
		currRoom->messages = new_dllist() ;
	}
    return NULL ;
}

int main( int argc, char** argv ){
    int fd ;
    int socket ;
    room *currRoom ;
    client *currClient ;
    pthread_t *threadID ;

    // Request server setup/error checking 
    if( argc < 3 ){
        fprintf( stderr, "usage: chat_server host port\n" ) ;
        exit(1) ;
    }

    // Tree for all threads 
    threadTree = make_jrb() ;

    // Makes thread for each room and initializes everything 
    for( int i = 2; i < argc; i++ ){
        currRoom = ( room* ) malloc( sizeof(room) ) ;
        currRoom->clients = new_dllist() ;
        currRoom->messages = new_dllist() ;
        currRoom->lock = ( pthread_mutex_t* ) malloc( sizeof( pthread_mutex_t ) ) ;
        pthread_mutex_init( currRoom->lock, NULL ) ;
        currRoom->cond = ( pthread_cond_t* ) malloc( sizeof( pthread_cond_t ) ) ;
        pthread_cond_init( currRoom->cond, NULL ) ;
        currRoom->user = ( char* ) malloc( sizeof(char) * strlen( argv[i] ) ) ;
        currRoom->user = strdup( argv[i] ) ; 

        // Sets up room thread 
        threadID = ( pthread_t* ) malloc( sizeof(pthread_t) ) ;
        if( pthread_create( threadID, NULL, roomThread, currRoom ) != 0 ){
            exit(1) ;
        }
        pthread_detach( *threadID ) ;

        // Put room in thread 
        jrb_insert_str( threadTree, currRoom->user, new_jval_v( ( void* )currRoom ) ) ;
    }
    
    // Gets client connect 
    socket = serve_socket( atoi( argv[1] ) ) ;

    // Sets up connection for incoming users 
    while (1) {
        fd = accept_connection(socket) ;
        currClient = ( client* ) malloc( sizeof(client) ) ;
        currClient->user = NULL ;
        currClient->room = NULL ;
        currClient->fd = fd ;
        currClient->fin = NULL ;
        currClient->fout = NULL ;

        // Sets up client thread 
        threadID = ( pthread_t* ) malloc( sizeof(pthread_t) ) ;
        if( pthread_create( threadID, NULL, clientThread, currClient ) != 0){
            exit(1) ;
        }
        pthread_detach( *threadID ) ;
    }

    return 0 ;
}