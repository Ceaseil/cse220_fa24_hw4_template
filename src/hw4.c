#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024
#define MAX_BOARD_SIZE 20
#define PIECES_PER_PLAYER 5

typedef struct {
    int x, y;
    int hit;
} Shot;

typedef struct {
    Shot shots[400];  // Max shots for a 20x20 board
    int shot_count;
} GameHistory;

typedef struct {
    int width;
    int height;
    int shape[4][4];
} PieceType;

typedef struct {
    int board[MAX_BOARD_SIZE][MAX_BOARD_SIZE];
    int ships_remaining;
    int ready;
    int socket;
    GameHistory history;
} Player;

typedef struct {
    Player players[2];
    int board_size_x;
    int board_size_y;
    int current_turn;  // 1 or 2
    int game_started;
} GameState;

PieceType piece_types[7];

void init_piece_types() {
    // Type 0: 2x2 square
    piece_types[0].width = 2;
    piece_types[0].height = 2;
    int square[4][4] = {
        {1,1,0,0},
        {1,1,0,0},
        {0,0,0,0},
        {0,0,0,0}
    };
    memcpy(piece_types[0].shape, square, sizeof(square));

    // Type 1: 1x4 rectangle
    piece_types[1].width = 4;
    piece_types[1].height = 1;
    int rect[4][4] = {
        {1,1,1,1},
        {0,0,0,0},
        {0,0,0,0},
        {0,0,0,0}
    };
    memcpy(piece_types[1].shape, rect, sizeof(rect));

    // Type 2: S-shape
    piece_types[2].width = 3;
    piece_types[2].height = 2;
    int s_shape[4][4] = {
        {0,1,1,0},
        {1,1,0,0},
        {0,0,0,0},
        {0,0,0,0}
    };
    memcpy(piece_types[2].shape, s_shape, sizeof(s_shape));

    // Type 3: L-shape
    piece_types[3].width = 2;
    piece_types[3].height = 3;
    int l_shape[4][4] = {
        {1,0,0,0},
        {1,0,0,0},
        {1,1,0,0},
        {0,0,0,0}
    };
    memcpy(piece_types[3].shape, l_shape, sizeof(l_shape));

    // Type 4: Flipped S-shape
    piece_types[4].width = 3;
    piece_types[4].height = 2;
    int flipped_s[4][4] = {
        {1,1,0,0},
        {0,1,1,0},
        {0,0,0,0},        
        {0,0,0,0}
    };
    memcpy(piece_types[4].shape, flipped_s, sizeof(flipped_s));

    // Type 5: Flipped L-shape
    piece_types[5].width = 2;
    piece_types[5].height = 3;
    int flipped_l[4][4] = {
        {0,1,0,0},
        {0,1,0,0},
        {1,1,0,0},
        {0,0,0,0}
    };
    memcpy(piece_types[5].shape, flipped_l, sizeof(flipped_l));

    // Type 6: T-shape
    piece_types[6].width = 3;
    piece_types[6].height = 2;
    int t_shape[4][4] = {
        {1,1,1,0},
        {0,1,0,0},
        {0,0,0,0},
        {0,0,0,0}
    };
    memcpy(piece_types[6].shape, t_shape, sizeof(t_shape));
}

void rotate_piece(int piece[4][4], int rotation) {
    int temp[4][4];
    for (int r = 0; r < rotation; r++) {
        memcpy(temp, piece, sizeof(temp));
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                piece[j][3-i] = temp[i][j];
            }
        }
    }
}
int validate_ship_placement(char* msg, GameState* game, int player_num) {
    char* token = strtok(msg, " ");
    if (!token || strcmp(token, "I") != 0) {
        send(game->players[player_num-1].socket, "E 201", 5, 0);
        return 0;
    }

    int pieces_count = 0;
    while ((token = strtok(NULL, " ")) != NULL && pieces_count < PIECES_PER_PLAYER) {
        int type = atoi(token);
        int rotation = atoi(strtok(NULL, " "));
        int x = atoi(strtok(NULL, " "));
        int y = atoi(strtok(NULL, " "));

        if (type < 0 || type >= 7) {
            send(game->players[player_num-1].socket, "E 300", 5, 0);
            return 0;
        }
        if (rotation < 0 || rotation > 3) {
            send(game->players[player_num-1].socket, "E 301", 5, 0);
            return 0;
        }

        int piece[4][4];
        memcpy(piece, piece_types[type].shape, sizeof(piece));
        rotate_piece(piece, rotation);

        // Check boundaries and overlap
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (piece[i][j]) {
                    if (x+i >= game->board_size_x || y+j >= game->board_size_y) {
                        send(game->players[player_num-1].socket, "E 302", 5, 0);
                        return 0;
                    }
                    if (game->players[player_num-1].board[x+i][y+j]) {
                        send(game->players[player_num-1].socket, "E 303", 5, 0);
                        return 0;
                    }
                }
            }
        }

        // Place the piece
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                if (piece[i][j]) {
                    game->players[player_num-1].board[x+i][y+j] = 1;
                    game->players[player_num-1].ships_remaining++;
                }
            }
        }
        pieces_count++;
    }

    if (pieces_count != PIECES_PER_PLAYER) {
        send(game->players[player_num-1].socket, "E 201", 5, 0);
        return 0;
    }

    game->players[player_num-1].ready = 1;
    return 1;
}

void send_game_history(GameState* game, int player_num) {
    char response[4096] = {0};
    char temp[32];
    
    sprintf(response, "G %d", game->players[player_num-1].ships_remaining);
    
    GameHistory* hist = &game->players[player_num-1].history;
    for (int i = 0; i < hist->shot_count; i++) {
        sprintf(temp, " %c %d %d", 
                hist->shots[i].hit ? 'H' : 'M',
                hist->shots[i].x,
                hist->shots[i].y);
        strcat(response, temp);
    }
    
    send(game->players[player_num-1].socket, response, strlen(response), 0);
}

void process_attack(GameState* game, int player_num, int x, int y) {
    int target_player = (player_num == 1) ? 1 : 0;
    GameHistory* hist = &game->players[player_num-1].history;
    
    // Check if cell was already attacked
    for (int i = 0; i < hist->shot_count; i++) {
        if (hist->shots[i].x == x && hist->shots[i].y == y) {
            send(game->players[player_num-1].socket, "E 401", 5, 0);
            return;
        }
    }
    
    // Record shot
    hist->shots[hist->shot_count].x = x;
    hist->shots[hist->shot_count].y = y;
    
    char response[32];
    // Check if hit
    if (game->players[target_player].board[x][y]) {
        hist->shots[hist->shot_count].hit = 1;
        game->players[target_player].board[x][y] = 0;
        game->players[target_player].ships_remaining--;
        
        sprintf(response, "%d H", game->players[target_player].ships_remaining);
        send(game->players[player_num-1].socket, response, strlen(response), 0);
        
        // Check for win condition
        if (game->players[target_player].ships_remaining == 0) {
            send(game->players[player_num-1].socket, "H 1", 3, 0);
            send(game->players[target_player].socket, "H 0", 3, 0);
            close(game->players[0].socket);
            close(game->players[1].socket);
            game->players[0].socket = 0;
            game->players[1].socket = 0;
            return;
        }
    } else {
        hist->shots[hist->shot_count].hit = 0;
        sprintf(response, "%d M", game->players[target_player].ships_remaining);
        send(game->players[player_num-1].socket, response, strlen(response), 0);
    }
    
    hist->shot_count++;
    game->current_turn = (player_num == 1) ? 2 : 1;
}

void handle_forfeit(GameState* game, int player_num) {
    int winner = (player_num == 1) ? 2 : 1;
    send(game->players[winner-1].socket, "H 1", 3, 0);
    send(game->players[player_num-1].socket, "H 0", 3, 0);
    close(game->players[0].socket);
    close(game->players[1].socket);
    game->players[0].socket = 0;
    game->players[1].socket = 0;
}

int process_message(GameState* game, int player_num, char* msg) {
    if (msg[0] == 'D') {
        if (player_num != 1) {
            send(game->players[player_num-1].socket, "E 100", 5, 0);
            return 0;
        }
        int x, y;
        if (sscanf(msg, "D %d %d", &x, &y) != 2 || x > 20 || y > 20 || x < 1 || y < 1) {
            send(game->players[player_num-1].socket, "E 200", 5, 0);
            return 0;
        }
        game->board_size_x = x;
        game->board_size_y = y;
        return 1;
    }
    
    if (msg[0] == 'I') {
        if (!game->board_size_x) {
            send(game->players[player_num-1].socket, "E 101", 5, 0);
            return 0;
        }
        return validate_ship_placement(msg, game, player_num);
    }
    
    // Go to implementation
    if (!game->game_started) {
        if (game->players[0].ready && game->players[1].ready) {
            game->game_started = 1;
            game->current_turn = 1;
        } else {
            send(game->players[player_num-1].socket, "E 102", 5, 0);
            return 0;
        }
    }

    // go to implementation
    if (game->current_turn != player_num) {
        send(game->players[player_num-1].socket, "E 102", 5, 0);
        return 0;
    }
    
    switch(msg[0]) {
        case 'S': {
            int x, y;
            if (sscanf(msg, "S %d %d", &x, &y) != 2) {
                send(game->players[player_num-1].socket, "E 202", 5, 0);
                return 0;
            }
            if (x < 0 || x >= game->board_size_x || y < 0 || y >= game->board_size_y) {
                send(game->players[player_num-1].socket, "E 400", 5, 0);
                return 0;
            }
            process_attack(game, player_num, x, y);
            return 1;
        }
        case 'Q':
            send_game_history(game, player_num);
            return 1;
        case 'F':
            handle_forfeit(game, player_num);
            return 0;
        default:
            send(game->players[player_num-1].socket, "E 100", 5, 0);
            return 0;
    }
}

int main() {
    int server_fd1, server_fd2, max_sd;
    struct sockaddr_in address1, address2;
    fd_set readfds;
    GameState game = {0};
    
    init_piece_types();

    // Create and setup both server sockets
    if ((server_fd1 = socket(AF_INET, SOCK_STREAM, 0)) == 0 ||
        (server_fd2 = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd1, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ||
        setsockopt(server_fd2, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address1.sin_family = AF_INET;
    address1.sin_addr.s_addr = INADDR_ANY;
    address1.sin_port = htons(PORT1);

    address2.sin_family = AF_INET;
    address2.sin_addr.s_addr = INADDR_ANY;
    address2.sin_port = htons(PORT2);

    if (bind(server_fd1, (struct sockaddr *)&address1, sizeof(address1)) < 0 ||
        bind(server_fd2, (struct sockaddr *)&address2, sizeof(address2)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd1, 1) < 0 || listen(server_fd2, 1) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server is running...\n");

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_fd1, &readfds);
        FD_SET(server_fd2, &readfds);
        max_sd = server_fd1 > server_fd2 ? server_fd1 : server_fd2;

        // Add active client sockets to set
        for (int i = 0; i < 2; i++) {
            if (game.players[i].socket > 0) {
                FD_SET(game.players[i].socket, &readfds);
                if (game.players[i].socket > max_sd)
                    max_sd = game.players[i].socket;
            }
        }

        // Wait for activity on any socket
        if (select(max_sd + 1, &readfds, NULL, NULL, NULL) < 0) {
            perror("select error");
            continue;
        }

        // Check for new connections on server sockets
        if (FD_ISSET(server_fd1, &readfds) && game.players[0].socket == 0) {
            int addrlen = sizeof(address1);
            game.players[0].socket = accept(server_fd1, (struct sockaddr *)&address1, (socklen_t*)&addrlen);
            printf("Player 1 connected\n");
        }

        if (FD_ISSET(server_fd2, &readfds) && game.players[1].socket == 0) {
            int addrlen = sizeof(address2);
            game.players[1].socket = accept(server_fd2, (struct sockaddr *)&address2, (socklen_t*)&addrlen);
            printf("Player 2 connected\n");
        }

        // Handle client messages
        for (int i = 0; i < 2; i++) {
            if (game.players[i].socket > 0 && FD_ISSET(game.players[i].socket, &readfds)) {
                char buffer[BUFFER_SIZE] = {0};
                int valread = read(game.players[i].socket, buffer, BUFFER_SIZE);
                
                if (valread == 0) {
                    // Client disconnected
                    handle_forfeit(&game, i+1);
                    printf("Player %d disconnected\n", i+1);
                } else {
                    process_message(&game, i+1, buffer);
                }
            }
        }
    }

    return 0;
}