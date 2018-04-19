#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 3000;
int listenfd;

struct player {
    int fd;
    char name[MAXNAME];
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    char read_buf[MAXNAME + 2];
    int inbuf;// for read_buf in obtain_name
    int room; // for read_buf in obtain_name
    //other stuff undoubtedly needed here
    int hasname;
    struct player *next;
    int read_buf_overflowed;
};
struct player *playerlist = NULL;
struct player *current_player = NULL;


extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s);  /* you need to write this one */

struct player *accept_connection(int fd);
int obtain_name(struct player *candidate_player);
int valid_move(struct player *pl, int current_fd, fd_set *all_fds, int *move_pit);
int distribute_pebbles(int move_pit, struct player *pl);
int update_list(struct player *p, fd_set *all_fds);
int find_network_newline(const char *buf, int n);
void update_current_player(struct player **current_player);
void show_board();
void show_board_to_individual(int p_fd);
void announce(char *s);

int main(int argc, char **argv) {
    char msg[MAXMESSAGE];
    
    //set up
    parseargs(argc, argv); // for the optional -p hasname to listen on specific port
    makelistener(); // set up front-desk socket
    
    //set up all_fds
    int max_fd = listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);
    fd_set listen_fds = all_fds;
    //each loop finishes when a current player entered a valid move
    while (!game_is_over()) {
        //case 1: game hasn't started yet, we don't have any current player in the game
        while (current_player == NULL){
            //turn the hasname off so we can only proceed to the next player when the current player has completed his round, in which case the hasname will be set on
            //make a copy of all_fds
            listen_fds = all_fds;
            fflush(stdout);
            //do a select() on listen_fds
            int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
            if (nready == -1) {
                perror("server: select");
                exit(1);
            }
            fflush(stdout);
            //check if a new connection is ready
            if (FD_ISSET(listenfd, &listen_fds)) {
                printf("a new connection is being accepted\n");
                struct player *new_player = accept_connection(listenfd);
                
                FD_SET(new_player->fd, &all_fds);
                //update max_fd for next select() call
                if (new_player->fd > max_fd){
                    max_fd = new_player->fd;
                }
                //update playerlist if it is not established yet
                new_player->next = playerlist;
                playerlist = new_player;
            }
            fflush(stdout);
            //after select and checking for new connections, try to obtain name for unnamed potential players and warn in-game players not in their turn
            for (struct player *p = playerlist; p; p = p->next) {
                //this player is unamed/not in the game && the player inputted something
                if (p->hasname==0 &&FD_ISSET(p->fd, &listen_fds)){
                    int obtain_result = obtain_name(p); // attempt to obtain a valid name for player p
                    
                    if (obtain_result == 2){//the input is not yet complete
                        //CHECK FOR CORNER CASE
                        printf("user entered an incomplete buffer\n");
                        //we do nothing as next few rounds there should be enough string to complete this parsed input
                    }
                    else if (obtain_result == 1){//the input is a valid name and p's name is set accordingly
                        printf("the input is a valid name and %s's name is set accordingly\n", p->name);
                        
                        if (current_player==NULL){
                            current_player = p;
                        }
                        broadcast("we have a new player!\r\n");
                        printf("new player %s has finished typing his name\n", p->name);
                    }
                    else if (obtain_result == 0){//a player has disconnected
                        //restructure the linked list
                        update_list(p, &all_fds);
                        //announce the disconnection
                        printf("broadcasting the disconnection\n");
                        broadcast("Client %s disconnected\r\n");
                    }
                    else if (obtain_result == -1){//the input is not a valid name(1. duplicate; 2. name size overflow)
                        write(p->fd, "invalid name!\r\n", sizeof("invalid name!\r\n"));
                        printf("client %s entered an invalid name\n", p->name);
                    }
                }
            }
        }
        
        //case 2: game has started, we have a current player in the game
        while (current_player != NULL && (!game_is_over())){
            
            show_board();
            char temp_buf[MAXMESSAGE];
            if (sprintf(temp_buf, "It's %s's move\r\n", current_player->name)<0){
                perror("while (current_player != NULL: sprintf1");
            }
            announce(temp_buf);
            if (write(current_player->fd, "Your move?\r\n", sizeof("Your move?\r\n"))<0){
                perror("while (current_player != NULL: sprintf2");
            }
            
            int going_to_next_round = 0;
            while (!going_to_next_round){
                listen_fds = all_fds;
                //do a select() on listen_fds
                int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
                if (nready == -1) {
                    perror("server: select");
                    exit(1);
                }
                
                //check whether non-current players disconnected or warn non-current players in the game
                for (struct player *p = playerlist; p; p = p->next) {
                    if( (p->hasname == 1 && FD_ISSET(p->fd, &listen_fds)) && p!=current_player){
                        int move_pit;
                        int connection_result = valid_move(p, current_player->fd, &all_fds, &move_pit);
                        if (connection_result==3){// non-current_player has disconnected
                            //broadcast the disconnection
                            printf("Client %s disconnected\n", p->name);
                            char msg[MAXMESSAGE];
                            sprintf(msg, "Client %s disconnected\r\n", p->name);
                            
                            update_list(p, &all_fds);
                            
                            broadcast(msg);
                            show_board();
                            
                            
                            continue;
                        }
                        write(p->fd, "It is not your move.\r\n", sizeof("It is not your move.\r\n"));
                        printf("Client %s entered when not in his move\n", p->name);
                    }
                
                }
                
                
                //if current_player hasn't input his move, jump to the next ieration and check again
                if (!FD_ISSET(current_player->fd, &listen_fds)){continue;}
                int move_is_valid;
                int move_pit;
                //determine if the user disconnected, entered a valid move, or entered an invalid move
                move_is_valid = valid_move(current_player, current_player->fd, &all_fds, &move_pit);
                
                
                if (move_is_valid==2){// the current_player has disconnected
                    printf("Client %s disconnected\r\n",current_player->name);
                    fflush(stdout);
                    struct player *old_curr = current_player;
                    update_current_player(&current_player);
                    if (old_curr ==current_player){
                        current_player = NULL;
                    }
                    
                    char msg[MAXMESSAGE];
                    sprintf(msg, "Client %s disconnected\r\n", old_curr->name);
    
                    update_list(old_curr, &all_fds);
                    
                    broadcast(msg);
                    
                    fflush(stdout);
                    break;
                }
                if (move_is_valid==1){//the current player entered a valid move
                    //broadcast the move
                    char msg[MAXMESSAGE];
                    sprintf(msg, "player %s distributed pebbles in pit %d\n", current_player->name, move_pit);
                    broadcast(msg);
                    //move the pebbles according to the current_player
                    if (distribute_pebbles(move_pit, current_player) == 0){//no extra turn is generated
                        update_current_player(&current_player);//only this line should be different from the next case

                        printf("go to the next player %s's move\n", current_player->name);
                    }
                    else{//an extra turn for the current player, announce it here since we won't annouce it in this case otherwise
                        printf("generated extra round for the same player\n");
                    }
                    going_to_next_round = 1;
                    
                }
                else if (move_is_valid == -1){//the current player entered an invalid move
                    write(current_player->fd, "Invalid move.\r\n", sizeof("Invalid move.\r\n"));
                    printf("Current player %s entered an invalid move\n", current_player->name);
                }
            }
            
            //check if a new connection is ready
            if (FD_ISSET(listenfd, &listen_fds)) {
                printf("a new connection is being accepted\n");
                struct player *new_player = accept_connection(listenfd);
                
                FD_SET(new_player->fd, &all_fds);
                //update max_fd for next select() call
                if (new_player->fd > max_fd){
                    max_fd = new_player->fd;
                }
                //update playerlist if it is not established yet
                new_player->next = playerlist;
                playerlist = new_player;
            }
            
            
            for (struct player *p = playerlist; p; p = p->next) {
                //this player is unamed/not in the game && the player inputted something
                if (p->hasname==0 &&FD_ISSET(p->fd, &listen_fds)){
                    int obtain_result = obtain_name(p); // attempt to obtain a valid name for player p
                    
                    if (obtain_result == 2){//the input is not yet complete
                        //CHECK FOR CORNER CASE
                        printf("user entered an incomplete buffer\n");
                        //we do nothing as next few rounds there should be enough string to complete this parsed input
                    }
                    else if (obtain_result == 1){//the input is a valid name and p's name is set accordingly
                        printf("the input is a valid name and %s's name is set accordingly\n", p->name);
                        
                        if (current_player==NULL){
                            current_player = p;
                            broadcast("the input is a valid name and %s's name is set accordingly\r\n");
                        }
                        broadcast("we have a new player!\r\n");
                        printf("new player %s has finished typing his name\n", p->name);
                    }
                    else if (obtain_result == 0){//a player has disconnected
                        //restructure the linked list
                        update_list(p, &all_fds);
                        //announce the disconnection
                        printf("broadcasting the disconnection\n");
                        broadcast("Client %s disconnected\r\n");
                    }
                    else if (obtain_result == -1){//the input is not a valid name(1. duplicate; 2. name size overflow)
                        write(p->fd, "invalid name!\r\n", sizeof("invalid name!\r\n"));
                        printf("client %s entered an invalid name\n", p->name);
                    }
                }
            }
        }
        
  
        
        
    }
    //end of infinite while loop, indicating the end of game
    broadcast("Game over!\r\n");
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg);
    }

    return 0;
}


void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() { 
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}

/* Accept a connection. Note that a new file descriptor is created for
 * communication with the client. The initial socket descriptor is used
 * to accept connections, but the new socket is used to communicate.
 * Return the new client's file descriptor or -1 on error.
 */
struct player *accept_connection(int fd){
    //accept the client's request to connect, keep track of the fd for the communication socket
    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }
    
    //send greeting message to new client, ask for their name input
    char *greeting = "Welcome to Mancala. What is your name?\r\n";
    if (write(client_fd, greeting, strlen(greeting))==-1){
        perror("accpet_connection: write");
        exit(1);
    }
    
    //initialize a new struct player to store information about the new client
    struct player *new_player = malloc(sizeof(struct player));
    
    //set new_player's fd
    new_player->fd = client_fd;
    //set new_player's pits with pebbles
    int avg_num_pebbles = compute_average_pebbles();///
    for (int i=0; i<NPITS; i++){
        (new_player->pits)[i] = avg_num_pebbles;
    }
    (new_player->pits)[NPITS] = 0;
    memset(new_player->name, '\0', sizeof(new_player->pits));
    memset(new_player->read_buf, '\0', sizeof(new_player->read_buf));
    //set new_player's hasname to 0 to indicate the player has not entered a valid name
    new_player->hasname = 0;
    //set new_player's next pointer to NULL
    new_player->next = NULL;
    new_player->inbuf = 0;// for read_buf in obtain_name
    new_player->room = MAXNAME+2; // for read_buf in obtain_name
    new_player->read_buf_overflowed = 0;//turned to 1 when there is an overflow of read_buf, indicating no read on the next round of select and turned back to 0 after skipping the next round
    
    return new_player;
}

//this helper reads the rest of the data from pipe readend to prevent the pipe from overflow
int decongest(int fd){
    char buf1[MAXNAME+2];
    int read_result1;
    //read each int from pipe's readfd
    while ((read_result1 = read(fd, &buf1, MAXNAME+2)) != 0) {
        //check if read() call failed
        if (read_result1 == -1){
            return 1;
        }
    }
    
    return 0;
}


/*
* Search the first n characters of buf for a network newline (\r\n) or (\n).
* Return the index of the '\n' of the first network newline,
* or -1 if no network newline is found.
* Definitely do not use strchr or other string functions to search here. (Why not?)
*/
int find_network_newline(const char *buf, int n) {
    for (int i=0; i<n; i++){
        if (buf[i] == '\r' || buf[i]=='\n'){
            return i;
        }
    }
    return -1;
}

//broadcast the message to every player
void broadcast(char *s){
    for (struct player *p = playerlist; p; p = p->next) {
        write(p->fd, s, strlen(s));
    }
}

//announce the message to every non-current player
void announce(char *s){
    for (struct player *p = playerlist; p; p = p->next) {
        if (p != current_player){
            if (write(p->fd, s, strlen(s))==-1){
                perror("announce: write");
            }
        }
    }
}

//return 2 if the input is not yet complete;
//return 1 if a valid name;
//return 0 if the player disconnected;
//return -1 if an invalid name;
int obtain_name(struct player *pl){
    //check if previous read resulted in a buffer overflow
    if (pl->read_buf_overflowed == 1){
        //clean up the communication chanel & buffer for later read and return accordingly
        int read_result = read(pl->fd, &((pl->read_buf)[pl->inbuf]), pl->room);
        //error checking
        if (read_result == -1){
            perror("obtain_name: read");
            exit(1);
        }
        if (find_network_newline(pl->read_buf, read_result)==-1){
            pl->read_buf_overflowed = 1;
            return 2;
        }
        else{
            pl->read_buf_overflowed = 0;
            return -1;
        }
        //clean up the buffer
        memset(pl->read_buf, '\0', sizeof(pl->read_buf));
        pl->inbuf = 0;
        pl->room = MAXNAME+2;
    }
    
    //read input at pl's fd
    //fflush(stdout);
    int read_result = read(pl->fd, &((pl->read_buf)[pl->inbuf]), pl->room);
    //error checking
    if (read_result == -1){
        perror("obtain_name: read");
        exit(1);
    }
    //case 0: 0 bytes is read, a client has disconnected
    if (read_result == 0){
        return 0;
    }
    //update inbuf and room
    (pl->inbuf) += read_result;
    (pl->room) -= read_result;
    //case 1: not a complete name. check if a network newline is received, and null-terminate the string at the network newline \r\n or \n
    int null_index= find_network_newline(pl->read_buf, pl->inbuf);
    if (null_index == -1){//a network newline isn't found, two cases here: case2.1: the input string exceeds the requirement. case2.2: The user didn't finish typing the message
        if (pl->room == 0){//the buffer is full and we still haven't received a network newline
            memset(pl->read_buf, '\0', sizeof(pl->read_buf));
            pl->inbuf = 0;
            pl->room = MAXNAME+2;
            
            pl->read_buf_overflowed = 1;
        }
        return 2;
    }
    //case 2: an input >0 bytes is received
    (pl->read_buf)[null_index] = '\0';
    
    //case 3: check for duplicate name
    for (struct player *each_player = playerlist; each_player; each_player = each_player->next) {
        if(each_player->hasname == 1 && strcmp(pl->read_buf, each_player->name) == 0){
            memset(pl->read_buf, '\0', sizeof(pl->read_buf));
            pl->inbuf = 0;
            pl->room = MAXNAME + 2;
            return -1;
        }
    }//end of for loop, we have eliminated the possibility of a duplicate name
    //case 4: the input name is a valid username
    strncpy(pl->name, pl->read_buf, sizeof(pl->name));
    pl->hasname =1;
    return 1;
}

//valid_move takes a struct player that has input ready and determine if that input is valid
//return  3 if a non-current player disconnected;
//return  2 if the current player disconnected;
//return  1 if the current player entered a valid move;
//return -1 if the current player entered an invalid move or
//return -2 if it's not his turn
int valid_move(struct player *pl, int current_fd, fd_set *all_fds, int *move_pit){
    char next_move[MAXMESSAGE];
    int read_result = read(pl->fd, next_move, MAXMESSAGE);
    //error checking
    if (read_result ==-1){
        perror("read() in valid_move()");
    }
    if (next_move[0]=='\n' ||next_move[0]=='\r'){
        if (pl==current_player){
            return -1;
        }
        else{
            return -2;
        }
    }
    
    next_move[read_result] = '\0';
    //case 1: this is the current player who entered some input
    if (pl->fd == current_fd){
        if (read_result ==0){//player has disconnected
            return 2;
        }
        //convert the input move to a number
        int pit_num = strtol(next_move,NULL,10);
        if (0<=pit_num && pit_num<=5){//player has entered a valid move
            *move_pit = pit_num;//offset user_input by one
            //check if the pit is empty
            if ((pl->pits)[pit_num]==0){
                return -1;
            }
            return 1;
        }
        else{//player has entered an invalid move
            return -1;
        }
    }
    //case 2: this is the other players not supposed to enter any input
    else{
        if (read_result ==0){//non-current player has disconnected
            return 3;
        }
        return -2;
    }
    return 0;
}

//distribute pebbles in the according pit.
//return 0 if no extra turn is generated
//return 1 if an extra turn is generated
int distribute_pebbles(int move_pit, struct player *pl){
    int loop_index = move_pit+1;
    int npebbles = (pl->pits)[move_pit];
    struct player *curr_pl = pl;
    pl->pits[move_pit] = 0;
    while (npebbles>0){
        //initialize which slot to start putting pebbles in a player's board (lowerbound is initally user's input and gets updated to 0 by the end of the first iteration)
        //distribute as many as possible pebbles to curr_pl
        while (loop_index<NPITS+1 && npebbles>0){//we will also put a pebble in the end pit
            if (loop_index == NPITS && pl != curr_pl){//don't put pebbles in other players' end pits
                update_current_player(&curr_pl);
                loop_index =0;
                continue;
            }
            //put a pebble in the according pit
            (curr_pl->pits)[loop_index] += 1;
            npebbles-=1;
            loop_index += 1;
        }
        //check if finished in special case
        if (npebbles==0 && loop_index==NPITS+1){
            if (pl == curr_pl){
                return 1;
            }
        }
        //go to the next valid player
        update_current_player(&curr_pl);
        loop_index =0;
    }//after the while loop, check if the player finished in his own pit i.e. another turn
    
    //no extra turn is generated
    return 0;
}


//updates the list to delete p from the list
//and make sure the list is linked
//return 1 to indicate a normal delete, return 0 to indicate no player is left so main should set current_player to NULL in which case;
int update_list(struct player *p, fd_set *all_fds){
    //clear p->fd from all_fds
    FD_CLR(p->fd, all_fds);
    
    //case 1: top/head is the node to delete
    if (strcmp(playerlist->name, p->name) == 0){
        struct player *temp = playerlist;
        playerlist = playerlist->next;
        free(temp);
        return 0;
    }
    //case 2: non-top is the node to delete i.e. we have more than one node in the list
    else{
        struct player *last = playerlist;
        struct player *curr = playerlist->next;
        while (strcmp(curr->name, p->name)!=0){
            last=curr;
            curr = curr->next;
        }
        last->next = curr->next;
        free(curr);
        
        return 1;
    }
}

//update the current_player to point to the next player with a name
void update_current_player(struct player **curr_pl){
    if ( (*curr_pl)==NULL){
        exit(1);
    }
    if ((*curr_pl)->next == NULL){
	(*curr_pl) = playerlist;
    }
    else{
    	(*curr_pl) = (*curr_pl)->next;
    }
    while ((*curr_pl)->hasname != 1){
        if ( (*curr_pl)->next){
            (*curr_pl) = (*curr_pl)->next;
        }
        else{//we have reached the end of the linked list, go back to the top of the linked list
            (*curr_pl) = playerlist;
        }
    }
}

//show the current game board to everyone
void show_board(){
    for (struct player *p = playerlist; p; p = p->next) {
        char game_stat[1024];
        if (p->hasname){
            sprintf(game_stat, "%s:  [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d  [end pit]%d\r\n",p->name, (p->pits)[0], (p->pits)[1], (p->pits)[2],(p->pits)[3],(p->pits)[4],(p->pits)[5],(p->pits)[6] );
            broadcast(game_stat);
        }
    }
}

//show the current game board to an individual who just joined game
void show_board_to_individual(int p_fd){
    for (struct player *p = playerlist; p; p = p->next) {
        char game_stat[1024];
        sprintf(game_stat, "%s:  [0]%d [1]%d [2]%d [3]%d [4]%d [5]%d  [end pit]%d\r\n",p->name, (p->pits)[0], (p->pits)[1], (p->pits)[2],(p->pits)[3],(p->pits)[4],(p->pits)[5],(p->pits)[6] );
        write(p_fd ,game_stat, strlen(game_stat));
    }
}

