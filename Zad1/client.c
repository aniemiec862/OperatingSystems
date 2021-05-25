#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MESSAGE_LEN 256

int server_socket;
int is_client_O;
char message[MESSAGE_LEN + 1];
char *username;

char *command, *arg;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

// 0 - empty  1 - O   2 - X
typedef struct{
    int move;
    int symbols[9];
} Board;

int move(Board *board, int field){
    if (field < 0 || field > 9 || board->symbols[field] != 0)
        return 0;
    board->symbols[field] = board->move ? 1 : 2;
    board->move = !board->move;
    return 1;
}

int checkWinner(Board *board){
    int column = 0;
    for (int x = 0; x < 3; x++){
        int first = board->symbols[x];
        int second = board->symbols[x + 3];
        int third = board->symbols[x + 6];
        if (first == second && first == third && first != 0)
            column = first;
    }

    if(column != 0)
        return column;

    int row = 0;
    for(int y = 0; y < 3; y++){
        int first = board->symbols[3 * y];
        int second = board->symbols[3 * y + 1];
        int third = board->symbols[3 * y + 2];
        if (first == second && first == third && first != 0)
            row = first;
    }

    if(row != 0)
        return row;

    int lower_diagonal = 0;

    int first = board->symbols[0];
    int second = board->symbols[4];
    int third = board->symbols[8];

    if(first == second && first == third && first != 0)
        lower_diagonal = first;

    if(lower_diagonal != 0)
        return lower_diagonal;

    int upper_diagonal = 0;
    first = board->symbols[2];
    second = board->symbols[4];
    third = board->symbols[6];
    if(first == second && first == third && first != 0)
        upper_diagonal = first;
    return upper_diagonal;
}
Board board;

typedef enum{
    START,
    WAITING_FOR_OPPONENT,
    WAITING_FOR_MOVE,
    MOVE_OPPONENT,
    MOVING,
    DISCONNECT
} Game_status;

Game_status status = START;

void disconnect(){
    char message[MESSAGE_LEN + 1];
    sprintf(message, "disconnect: :%s", username);
    send(server_socket, message, MESSAGE_LEN, 0);
    exit(0);
}

void checkForGameResult()
{
    int win = 0;
    int winner = checkWinner(&board);
    if(winner != 0){
        if ((is_client_O && winner == 1) || (!is_client_O && winner == 2))
            printf("You have WON!\n");
        else printf("You have LOST!\n");

        win = 1;
    }

    int draw = 1;
    for(int i = 0; i < 9; i++){
        if(board.symbols[i] == 0){
            draw = 0;
            break;
        }
    }

    if(draw && !win)
        printf("That's a DRAW\n");

    if(win || draw)
        status = DISCONNECT;

}

Board newBoard(){
    Board board = {1, {0}};
    return board;
}

void drawBoard(){
    char symbol;
    for(int y = 0; y < 3; y++){
        for(int x = 0; x < 3; x++){
            if (board.symbols[y * 3 + x] == 0)
                symbol = y * 3 + x + 1 + '0';
            
            else if (board.symbols[y * 3 + x] == 1)
                symbol = 'O';
            
            else symbol = 'X';
            printf("  %c  ", symbol);
        }
        printf("\n_________________________\n");
    }
}

void startGame(){
    while(1){
        if(status == START){
            if(strcmp(arg, "username_taken") == 0){
                printf("Username is taken!\n");
                exit(1);
            }
            else if(strcmp(arg, "no_opponent") == 0){
                printf("Waiting for opponent\n");
                status = WAITING_FOR_OPPONENT;
            }
            else{
                board = newBoard();
                is_client_O = arg[0] == 'O';
                status = is_client_O ? MOVING : WAITING_FOR_MOVE;
            }
        }
        else if(status == WAITING_FOR_OPPONENT){
            pthread_mutex_lock(&mutex);
            while (status != START && status != DISCONNECT)
                pthread_cond_wait(&cond, &mutex);
            
            pthread_mutex_unlock(&mutex);

            board = newBoard();
            is_client_O = arg[0] == 'O';
            status = is_client_O ? MOVING : WAITING_FOR_MOVE;
        } else if(status == WAITING_FOR_MOVE){
            printf("Waiting for opponents move\n");

            pthread_mutex_lock(&mutex);
            while (status != MOVE_OPPONENT && status != DISCONNECT)
                pthread_cond_wait(&cond, &mutex);

            pthread_mutex_unlock(&mutex);
        } else if(status == MOVE_OPPONENT){
            move(&board, atoi(arg));
            checkForGameResult();
            if (status != DISCONNECT)
                status = MOVING;
        } else if(status == MOVING){
            drawBoard();
            int pos;
            do{
                printf("Next move (%c): ", is_client_O ? 'O' : 'X');
                scanf("%d", &pos);
                pos--;
            } while(!move(&board, pos));

            drawBoard();

            char message[MESSAGE_LEN + 1];
            sprintf(message, "move:%d:%s", pos, username);
            send(server_socket, message, MESSAGE_LEN, 0);

            checkForGameResult();

            if (status != DISCONNECT)
                status = WAITING_FOR_MOVE;
                
        } else if (status == DISCONNECT)
            disconnect();
    }
}

void connectToServer(char *connectionType, char *address){
    if(strcmp(connectionType, "local") == 0){
        server_socket = socket(AF_UNIX, SOCK_STREAM, 0);

        struct sockaddr_un local_sockaddr;
        memset(&local_sockaddr, 0, sizeof(struct sockaddr_un));
        local_sockaddr.sun_family = AF_UNIX;
        strcpy(local_sockaddr.sun_path, address);

        connect(server_socket, (struct sockaddr *)&local_sockaddr, sizeof(struct sockaddr_un));
    } else{
        struct addrinfo *info;

        struct addrinfo hints;
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        getaddrinfo("localhost", address, &hints, &info);
        server_socket = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        connect(server_socket, info->ai_addr, info->ai_addrlen);

        freeaddrinfo(info);
    }
}

void checkServerMessages(){
    int gameStarted = 0;
    while (1){
        recv(server_socket, message, MESSAGE_LEN, 0);
        command = strtok(message, ":");
        arg = strtok(NULL, ":");

        pthread_mutex_lock(&mutex);
        if(strcmp(command, "set_symbol") == 0){
            status = START;
            if (!gameStarted){
                pthread_t game_thread;
                pthread_create(&game_thread, NULL, (void *(*)(void *))startGame, NULL);
                gameStarted = 1;
            }
        }
        else if(strcmp(command, "move") == 0){
            status = MOVE_OPPONENT;
        }
        else if(strcmp(command, "disconnect") == 0){
            status = DISCONNECT;
            exit(0);
        }
        else if(strcmp(command, "ping") == 0){
            sprintf(message, "pong: :%s", username);
            send(server_socket, message, MESSAGE_LEN, 0);
        }
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
    }
}

int main(int argc, char *argv[]){
    if(argc != 4)
        exit(1);

    username = argv[1];

    signal(SIGINT, disconnect);

    connectToServer(argv[2], argv[3]);

    char message[MESSAGE_LEN + 1];
    sprintf(message, "connect: :%s", username);
    send(server_socket, message, MESSAGE_LEN, 0);

    checkServerMessages();
    return 0;
}