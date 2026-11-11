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
# include <stdio.h>       // FILE* streams including fdopen, fgets, fputs, fflush, fclose; also printf/fprintf for server logging
# include <stdlib.h>      // malloc (client/room structs), exit, atoi (turns the port argument "8005" into the number 8005)
# include <string.h>      // String helpers including strcspn (cutting off the \r\n that fgets keeps), strcpy, strcat (building "Alice: hi"), strdup (copying names and messages onto the heap)
# include <pthread.h>     // Threads and synchronization including pthread_create/detach/exit, mutexes (the room's lock), condition variables (the room's doorbell)

# include "sockettome.h"  // Plank's socket helpers, in sockettome.c: serve_socket (set up the listening socket) and accept_connection (wait for the next user)
# include "jrb.h"         // libfdr red-black tree including JRB, make_jrb, jrb_insert_str, jrb_find_str, jrb_traverse (threadTree, the room directory)
# include "dllist.h"      // libfdr doubly-linked list including Dllist, new_dllist, dll_append, dll_traverse, dll_delete_node, free_dllist (clients and messages). Also pulls in jval.h for new_jval_v/new_jval_s

// The client struct contains the chat server's notes on each user. 
typedef struct {
    int fd ;            // File descriptor: an integer (e.g., 5) the operating system gives the server for this one user's connection
    FILE *fin  ;        // Pointer to the stream where the machine reads what the user types. Essentially, whatever "Alice" types in her window will show up in this file for the server to read
    FILE *fout ;        // Pointer to the stream where the server writes what the user should see. Essentially, whatever the server writes here is what Alice will see
    char* user ;        // Their username (e.g., "Alice"). It's a pointer to the address in memory where the row of letters starts, because C has no string types
} client ;

// The room struct contains the chat server's notes on each room. 
typedef struct {
    char* name ;            // The name of the room, e.g., "Bridge"
    Dllist clients  ;       // A doubly-linked list of the clients in the room 
    Dllist messages ;       // The room's message queue: messages posted in the room that the room thread hasn't delivered to everyone's windows yet
    pthread_cond_t  *cond ; // The doorbell (a condition variable, not a thread): client threads signal it after adding a message; the room thread waits on it
    pthread_mutex_t *lock ; // The lock to the room. A thread can't change the list of the users or the messages without holding it. 
} room ;

JRB threadTree ;            // Red-Black tree that contains the rooms

// clientThread: The function each user's thread runs, from the moment they
//      connect until they disconnect. One copy runs per connected user.
//
//      Flow: Open fin/fout on the socket -> list rooms and who's in them ->
//      ask for a name -> ask for a room -> join it (under the room's lock) ->
//      loop: read a line with fgets, queue "name: line" in the room, signal
//      the room thread. On disconnect: queue "name has left", remove self
//      from the room's clients list, exit the thread.
//
//      Why the signature is "void *clientThread( void *arg )":
//      This is a function, not a class (C has no classes). pthread_create
//      only accepts functions shaped exactly like  void *f( void * ),
//      because it is generic and doesn't know what data a thread needs.
//        - void *arg: "a pointer to anything." main passes this user's
//          malloc'd client struct (pthread_create's 4th argument), and we
//          cast it back to client* below.
//        - void * return: what the thread hands back when it finishes,
//          which pthread_join could collect. Nothing joins these threads
//          (they're detached), so it always returns or exits with NULL.
void *clientThread( void *arg ){
    JRB tmpTree ;           // Cursor into threadTree that points to one room's node at a time by listing rooms and then finding the chosen one
    char cmd[1000] ;        // What Alice just typed, including her name, her room name, and each chat message. Reused once to build "Alice has joined".
    char buf[1000] ;        // The outgoing line being built: "Alice: " plus what she typed, which then gets queued for the room
    room *currRoom ;        // Pointer to the room this thread’s user chose
    client *tmpClient  ;    // Pointer to the client at tmpDLL's current node, used to read other users' names and compare when leaving
    client *currClient ;    // Pointer to this user's client struct passed in by main via pthread_create. Each connection gets its own thread and its own struct, so Alice's thread points to Alice's struct.
    Dllist tmpDLL ;         // Cursor that points to one node of a room's clients list at a time, used by the "who's online" and "find myself" loops.
    Dllist cleanDLL ;       // Alice’s own node in the room’s clients list, found when she disconnects so it can be deleted. It’s NULL if she isn’t found

    // Sets up the client's thread by taking in Alice’s card and getting her connection ready for reading and writing
    currClient = ( client* ) arg ;                         // arg is the pointer main passed to pthread_create, or Alice’s client struct. This line stores it in the thread’s local variable, so from here on currClient means “my user.”
    currClient->fin = fdopen( currClient->fd, "r" ) ;       // fdopen takes a raw file descriptor (Alice’s socket, say fd 5) and wraps it in a FILE* stream so you can use fgets on it. The second argument is the mode: "r" for read, "w" for write.
    if( currClient->fin == NULL ){                          // fdopen returns NULL if it fails, for example if the system is out of memory. Bug: exit(1) ends the whole server, so one user's setup failure disconnects everyone.
        exit(1) ;
    }
    currClient->fout = fdopen( currClient->fd, "w" ) ;      // Same thing for the write stream ("w"), on the same fd. Same exit(1) problem as above.
    if( currClient->fout == NULL ){ 
        exit(1) ; 
    }

    // This block sends Alice the first line of the welcome screen, and if that fails, ends her thread.
    fputs( "Chat Rooms:\n\n", currClient->fout ) ;
    
    // fflush empties the outbox. It calls write() on Alice’s socket, and the text goes over TCP to her netcat window.
    //      If sending failed, Alice is gone, so the thread shuts itself down. 
    if( fflush( currClient->fout ) == EOF ){
        fclose( currClient->fin ) ;             // Closes the read stream and the shared fd underneath it.
        fclose( currClient->fout ) ;            // Closes the write stream. (This fails bc the prior line already closes the shared fd)
        pthread_exit( NULL ) ;                  // Ends only Alice's thread. 
    }

    // This is the start of the “who’s online” listing. For each room it prints the room’s name and a colon. 
    //  The inner loop that follows adds the names of the people in that room, and then a newline.
    //  jrb_traverse is a macro from jrb.h. It expands to an ordinary for loop:
    //  for( tmpTree = jrb_first(threadTree); tmpTree != threadTree; tmpTree = jrb_next(tmpTree) )
    //  tmpTree is the cursor, cycling through the threadTree one room at a time. 
    jrb_traverse( tmpTree, threadTree ) {
        currRoom = ( room* ) tmpTree->val.v ;                   // currRoom is the pointer to whatever room is being listed 
        
        // The following four blocks run once per room, inside the jrb_traverse loop. Each run prints one room’s label, such as Bridge:
        if( fputs( currRoom->name, currClient->fout ) == EOF){  // Puts "Bridge" in Alice's outbox (fout's buffer), to be sent to her
            fclose( currClient->fin ) ;                         // Frees the read stream and closes fd 5, Alice’s connection.
            fclose( currClient->fout ) ;                        // Frees the write stream and incorrectly tries closing fd 5 again 
            pthread_exit( NULL ) ;                              // Ends Alice’s thread only. The rest of the server keeps running.
        }
        if( fflush( currClient->fout ) == EOF){                 // Pushes "Bridge" in the outbox to Alice's window
            fclose( currClient->fin ) ;                         // Same cleanup as above
            fclose( currClient->fout ) ;
            pthread_exit( NULL ) ;
        }
        if( fputs( ":", currClient->fout ) == EOF){             // Pushes ":" to the outbox
            fclose( currClient->fin ) ;
            fclose( currClient->fout ) ;
            pthread_exit( NULL ) ;
        }
        if( fflush( currClient->fout ) == EOF){                 // Pushes ":" to Alice's window
            fclose( currClient->fin ) ;
            fclose( currClient->fout ) ;
            pthread_exit( NULL ) ;
        }

        // The following four blocks do the same things as above, but to list the clients in each room.
        // BUG: this reads currRoom->clients without taking currRoom->lock. If someone leaves mid-loop, their node is freed and tmpDLL dangles (use-after-free).
        dll_traverse( tmpDLL, currRoom->clients ){              // Uses a list instead of a tree to walk the room’s guest list
            if( fputs( " ", currClient->fout ) == EOF){         // Puts a space between each user's name in the outbox 
                fclose( currClient->fin ) ;
                fclose( currClient->fout ) ;
                pthread_exit( NULL ) ;
            }
            if( fflush( currClient->fout ) == EOF ){            // Pushes the space in the outbox to the user window
                fclose( currClient->fin ) ;
                fclose( currClient->fout ) ;
                pthread_exit( NULL ) ;
            }
            
            tmpClient = ( client* ) tmpDLL->val.v ;                 // tmpClient reads the other users' cards (Bob’s, Carol’s), while currClient is still the new arrival who’s reading this list

            if( fputs( tmpClient->user, currClient->fout ) == EOF){ // Writes the other user's name to Alice's outbox 
                fclose( currClient->fin ) ;
                fclose( currClient->fout ) ;
                pthread_exit( NULL ) ;
            }
            if ( fflush( currClient->fout ) == EOF ){               // Sends the other user's name from the outbox to Alice's window
                fclose( currClient->fin ) ;
                fclose( currClient->fout ) ;
                pthread_exit( NULL ) ;
            }
        }

		if( fputs( "\n", currClient->fout ) == EOF ){           // Put a newline in the outbox after printing the last user in a particular room 
			fclose( currClient->fin ) ;
			fclose( currClient->fout ) ;
			pthread_exit( NULL ) ;
		}
        fflush( currClient->fout ) ;                            // Print the newline in the outbox to the window 
    }
	
    // After all the rooms are listed, these two blocks send one more newline (a blank line) to the outbox and then to the user's window
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

    // Same pattern as above, but this time to get the user's name 
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

    // Gets the user's username and copies it into cmd
    if( fgets( cmd, sizeof(cmd), currClient->fin ) == NULL ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }
    
    cmd[ strcspn( cmd, "\r\n" ) ] = '\0' ;          // After fgets, cmd holds Alice\n. strcspn finds the first \r or \n (or the end, if there's none) and this cuts the string there. Safe for \r\n, overlong lines, and empty input
	printf( "Client name entered: %s\n", cmd ) ;    // Prints the user's name for our debugging purposes
	currClient->user = strdup( cmd ) ;              // strdup allocates new heap memory just big enough (strlen + 1), copies Alice\0 into it, and returns the address, which goes on her card.

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

    // Gets the user's room input name and copies it into cmd
    if( fgets( cmd, sizeof(cmd), currClient->fin ) == NULL ){
        fclose( currClient->fin ) ;
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;
    }
	
    cmd[ strcspn( cmd, "\r\n" ) ] = '\0' ;          // Same as above, cuts the room name at its first \r or \n
	printf( "Room name entered: %s\n", cmd ) ;      // Prints their input for debugging purposes

    tmpTree = jrb_find_str( threadTree, cmd ) ;     // jrb_find_str looks up the room name Alice typed in threadTree; tmpTree holds the node it finds, or NULL
    
    // If the room doesn't exist, tell this user (with the name they typed) and close only their connection.
    //      (This used to print the wrong room's name on the server's terminal and call exit(1), which shut down the whole server.)
    if( tmpTree == NULL ){
        fprintf( currClient->fout, "Chatroom %s does not exist\n", cmd ) ;   // Goes to the user's window, not the server's stderr
        fflush( currClient->fout ) ;
        fclose( currClient->fin ) ;                                         // Same cleanup as everywhere else in this function
        fclose( currClient->fout ) ;
        pthread_exit( NULL ) ;                                              // Ends only this user's thread; everyone else keeps chatting
    }

    // If it does, the server enables the user thread to join         
    else{
        currRoom = ( room* ) tmpTree->val.v ;       // Takes the room struct out of the node jrb_find_str returned
        pthread_mutex_lock( currRoom->lock ) ;      // Takes Bridge's lock before changing its clients and messages lists
        
        dll_append( currRoom->clients, new_jval_v( ( void* ) currClient ) ) ;   // Add the user to the list of users in the room 
        strcpy( cmd, currClient->user ) ;                                       // Copy the new user's name into cmd
        strcat( cmd, " has joined\n" ) ;                                        // Append " has joined\n" to build the announcement
        dll_append( currRoom->messages, new_jval_s( strdup(cmd) ) ) ;           // Queues a heap copy of "Alice has joined" on the room's message queue; the room thread sends it to everyone

        pthread_cond_signal( currRoom->cond ) ;                                 // Wakes the thread sleeping on Bridge’s condition variable (the room thread) to tell it there are new messages in the queue
        pthread_mutex_unlock( currRoom->lock ) ;                                // Releases Bridge’s lock, which Alice’s thread took before adding herself to the guest list.
    }

    // This loop runs while Alice is connected 
    while(1){
        strcpy( buf, currClient->user ) ;   // Copies the current user's name into a buffer
        strcat( buf, ": " ) ;               // Adds a colon after their name to precede their message 

        // This branch runs when the user ends their session by leaving (fgets returns NULL)
        if( fgets( cmd, sizeof(cmd), currClient->fin ) == NULL ){
            fclose( currClient->fin ) ;             // Closes the read stream AND the shared fd underneath it

            // Outputs when client leaves
            strcpy( buf, currClient->user ) ;                               // Copy the user's name into the buf
            strcat( buf, " has left\n" ) ;                                  // Append the message that they have left 
            pthread_mutex_lock( currRoom->lock ) ;                          // Takes Bridge's lock before changing its lists
            dll_append( currRoom->messages, new_jval_s( strdup(buf) ) ) ;   // Queues "Alice has left" on the room's message queue

            // Removes the user from the DLList
            if( currClient->fout != NULL ){                                 // Checks that fout isn't NULL
                fclose( currClient->fout ) ;                                // Closes the write stream. The fd was already closed above, so this is the double close
                cleanDLL = NULL ;                                           // Resets the "found" marker. cleanDLL will hold the Alice's node if it finds it 
                dll_traverse( tmpDLL, currRoom->clients ){                  // Walks the room's guest list using tmpDLL as the cursor to find Alice
                    tmpClient = ( client* ) tmpDLL->val.v ;
                    if( tmpClient == currClient ){                          // Same card (same address), not just the same name, so duplicate names can't remove each other
                        cleanDLL = tmpDLL ;                                 // Remember Alice's node...
                        break ;                                             // ...and stop: there's exactly one. Deleting still happens after the loop
                    }
                }
                if( cleanDLL != NULL ){                                     // If Alice's node was found, delete it from the guest list
                    dll_delete_node(cleanDLL) ;
                }
            }

            // Remove user pthread 
            pthread_cond_signal( currRoom->cond ) ;                         // The room thread wakes up and delivers the queued “Alice has left” to everyone still in the room.
            pthread_mutex_unlock( currRoom->lock ) ;                        // The room thread, already awake from the signal, takes Bridge's key and delivers the message.
            pthread_exit( NULL ) ;                                          // Ends Alice's thread 
        }
        
        // If Alice hasn't left, this branch runs and handles her message
        else{
            strcat( buf, cmd ) ;                                            // Concatenates "Alice: " and <her message>
            pthread_mutex_lock( currRoom->lock ) ;                          // Takes Bridge’s key, because the next line changes messages.
            dll_append( currRoom->messages, new_jval_s( strdup(buf) ) ) ;   // Queues a heap copy of her message on the room's message queue
            pthread_cond_signal( currRoom->cond ) ;                         // The room thread wakes...
            pthread_mutex_unlock( currRoom->lock ) ;                        // ...takes the lock and delivers "Alice: hi Bob\n" to everyone in Bridge, including Alice.
        }
    }
}

// roomThread: the delivery worker for one room (the "consumer"). main starts
//      one per room at startup, and it runs for the life of the server.
//      Signature is void *f( void * ) for pthread_create, like clientThread;
//      here v is this thread's room*.
//
//      It takes the room's lock once and holds it at all times except while
//      asleep in pthread_cond_wait. Loop: sleep until a client thread signals
//      cond -> write every pending message to every client in the room
//      (fputs + fflush on each fout), dropping clients whose write fails ->
//      free the message list and start an empty one -> sleep again.
//      It is the only code that sends chat messages over the network.
void *roomThread( void *v ){
    Dllist msgDLL ;      // Cursor over the room's pending messages; msgDLL->val.s is the message text ("Alice: hi\n")
    Dllist clientDLL ;   // Cursor over the room's clients list, for the recipient currently being written to
    room   *currRoom ;   // This thread's room, passed in by main as v. Set once, never changes
    client *currClient ; // The recipient currently being written to (NOT "my user" as in clientThread)

    // Get the room and respective lock 
    currRoom = ( room* ) v ;
    pthread_mutex_lock( currRoom->lock ) ;
    
    // Runs while the program is active 
    while(1){
        // pthread_cond_wait does three things, in order:
        // 1. Releases the room’s lock, so client threads can take it to post messages.
        // 2. Puts this thread to sleep until a client thread calls pthread_cond_signal on cond. (Steps 1 and 2 happen as one atomic step, so a signal can't slip in between.)
        // 3. Takes the lock back before returning. When this line finishes, the room thread holds the key again.
        pthread_cond_wait( currRoom->cond, currRoom->lock ) ; 

        // This loops over the pending messages in the room's message queue
        dll_traverse( msgDLL, currRoom->messages ){
            if( ( dll_empty( currRoom->clients ) ) == 0 ){                  // If the room is empty, the program skips this loop 
                dll_traverse( clientDLL, currRoom->clients ){               // This loop walks the guest list. currClient is the recipient, or whose window this particular message is going to.
                    currClient = ( client* ) clientDLL->val.v ;
                    if( fputs( msgDLL->val.s, currClient->fout ) == EOF ){  // Copies the message into this recipient’s outbox. No network traffic yet.
                        dll_delete_node( clientDLL ) ;                      // If the write to Bob fails, the code removes him from the guest list... 
                        fclose( currClient->fout ) ;                        // ...and closes his fout.
                    }
                    if( fflush( currClient->fout ) == EOF ){                // Sends the message in the outbox using a write() system call to the recipient’s socket, and from there over TCP to their nc window.
                        dll_delete_node( clientDLL ) ;                      // See above. 
                        fclose( currClient->fout ) ;
                    }
                }
            }
        }

		// Deletes old message log and sets up a new one. The while loop goes back to sleep, and waits for the signal that there are new messages
		free_dllist( currRoom->messages ) ;
		currRoom->messages = new_dllist() ;
	}
}

// main does three jobs:
// 1. Check the arguments,
// 2. Set up every room (each with its own thread),
// 3. Then accept connections forever, starting a thread for each one.
// argc is how many command-line words there are, and argv is the words themselves. 
// For ./chat_server 8005 Football Bridge Politics, argc is 5, with ./chat_server being argv[0], 8005 argv[1], and so on
int main( int argc, char** argv ){
    int fd ;                 // The connection fd for the newest client, from accept
    int socket ;             // The listening socket (this name hides the socket() function)
    room *currRoom ;         // The room being set up in the startup loop
    client *currClient ;     // The card for the newest client; reused for each connection
    pthread_t *threadID ;    // Where pthread_create writes the new thread's ID

    // Request server setup/error checking 
    if( argc < 3 ){
        fprintf( stderr, "usage: chat_server host port\n" ) ;
        exit(1) ;
    }

    // Creates the empty red-black tree. It’s a global, so every thread can reach it.
    threadTree = make_jrb() ;

    // Makes a thread for each room and initializes everything 
    for( int i = 2; i < argc; i++ ){
        currRoom = ( room* ) malloc( sizeof(room) ) ;   // Allocates a blank room struct on the heap.
        currRoom->clients = new_dllist() ;              // Creates an empty guest list
        currRoom->messages = new_dllist() ;             // Creates an empty message queue
        currRoom->lock = ( pthread_mutex_t* ) malloc( sizeof( pthread_mutex_t ) ) ; // Allocates the room’s mutex...
        
        pthread_mutex_init( currRoom->lock, NULL ) ;                                // ...and initializes it. NULL means "default settings," not a value.
        currRoom->cond = ( pthread_cond_t* ) malloc( sizeof( pthread_cond_t ) ) ;   // Does the same for the condition variable (the doorbell).
        
        pthread_cond_init( currRoom->cond, NULL ) ;
    
        currRoom->name = strdup( argv[i] ) ;            // Sets the room’s name.

        // Allocates space for the new thread's ID (never freed; a local pthread_t would do)
        threadID = ( pthread_t* ) malloc( sizeof(pthread_t) ) ;
        
        // Starts the room thread. The four arguments:
        // threadID: Where to store the new thread’s ID
        // NULL: Default thread settings
        // roomThread: The function the new thread runs
        // currRoom: The data handed to it, which arrives as v
        // It should return zero if this setup succeeded. If not, we close the program. 
        if( pthread_create( threadID, NULL, roomThread, currRoom ) != 0 ){
            exit(1) ;
        }
        pthread_detach( *threadID ) ;   // Nobody will join this thread, so the system cleans it up automatically if it ends

        // Add the room to the room directory (threadTree)
        jrb_insert_str( threadTree, currRoom->name, new_jval_v( ( void* )currRoom ) ) ;
    }
    
    // Gets client connect by turning the string of the port into an int, creates a listening socket on that port, and returns -1 if it fails, e.g. the port is in use (not checked here)
    socket = serve_socket( atoi( argv[1] ) ) ;

    // Waits until someone runs nc localhost 8005, then returns a new fd for that one connection. main spends almost all its time asleep on this line.
    while (1) {
        fd = accept_connection(socket) ;    // Accepts a connection if someone joins the socket. 
        currClient = ( client* ) malloc( sizeof(client) ) ; // Allocates a fresh client struct (card) for this user,
        currClient->user = NULL ;                           // and sets every field except fd to NULL (room is left unset, since it's unused)
        currClient->fd = fd ;
        currClient->fin = NULL ;
        currClient->fout = NULL ;

        // Sets up client thread for the new user 
        threadID = ( pthread_t* ) malloc( sizeof(pthread_t) ) ;
        if( pthread_create( threadID, NULL, clientThread, currClient ) != 0){
            exit(1) ;
        }
        pthread_detach( *threadID ) ;
    }
}