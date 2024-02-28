/* vim: :se ai :se sw=4 :se ts=4 :se sts :se et */

/*H**********************************************************************
 *
 *    This is a skeleton to guide development of Othello engines that can be used
 *    with the Ingenious Framework and a Tournament Engine.
 *
 *    The communication with the referee is handled by an implementaiton of comms.h,
 *    All communication is performed at rank 0.
 *
 *    Board co-ordinates for moves start at the top left corner of the board i.e.
 *    if your engine wishes to place a piece at the top left corner,
 *    the "gen_move_master" function must return "00".
 *
 *    The match is played by making alternating calls to each engine's
 *    "gen_move_master" and "apply_opp_move" functions.
 *    The progression of a match is as follows:
 *        1. Call gen_move_master for black player
 *        2. Call apply_opp_move for white player, providing the black player's move
 *        3. Call gen move for white player
 *        4. Call apply_opp_move for black player, providing the white player's move
 *        .
 *        .
 *        .
 *        N. A player makes the final move and "game_over" is called for both players
 *
 *    IMPORTANT NOTE:
 *        Write any (debugging) output you would like to see to a file.
 *        	- This can be done using file fp, and fprintf()
 *        	- Don't forget to flush the stream
 *        	- Write a method to make this easier
 *        In a multiprocessor version
 *        	- each process should write debug info to its own file
 *H***********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <limits.h>
#include </opt/homebrew/Cellar/open-mpi/4.1.5/include/mpi.h>
#include <time.h>
#include <assert.h>
#include "comms.h"

const int EMPTY = 0;
const int BLACK = 1;
const int WHITE = 2;
const int ROOT = 0;
const int OUTER = 3;
const int ALLDIRECTIONS[8] = {-11, -10, -9, -1, 1, 9, 10, 11};
const int BOARDSIZE = 100;
const int LEGALMOVSBUFSIZE = 65;
const char piecenames[4] = {'.', 'b', 'w', '?'};

void run_master(int argc, char *argv[]);
int initialise_master(int argc, char *argv[], int *time_limit, int *my_colour, FILE **fp);
void apply_opp_move(char *move, int my_colour, FILE *fp, int *active_board);
void game_over();
void initialise_board();
void free_board();
void run_worker(int rank);
void gen_move_master(char *move, int my_colour, FILE *fp, int *active_board);
void legal_moves(int player, int *moves, FILE *fp, int *active_board);
int legalp(int move, int player, FILE *fp, int *active_board);
int validp(int move);
int would_flip(int move, int dir, int player, FILE *fp, int *active_board);
int opponent(int player, FILE *fp);
int find_bracket_piece(int square, int dir, int player, FILE *fp, int *active_board);
int bens_strategy(int my_colour, FILE *fp);
void make_move(int move, int player, FILE *fp, int *active_board);
void make_flips(int move, int dir, int player, FILE *fp, int *active_board);
int get_loc(char *movestring);
void get_move_string(int loc, char *ms);
void print_board(FILE *fp);
char nameof(int piece);
int count(int player, int *board);
int evaluate(int player, int *board);
int minimax(int *board, int player, int depth, int rank, int alpha, int beta);
int max(int value1, int value2);
int min(int value1, int value2);
void process_moves(int *moves, int amount_of_moves, int *array);
void writeToFile(char *filename, char *text);
int is_game_over_move(int *board);
int has_legal_moves(int *board, int player);

int *current_board; // gameboard
int MPI_SIZE;		// amount of processors
char bufferp[100];	// This defines a character array with a size of 100 that can hold the path of the file to write to.
char bufferm[100];	// This defines a character array with a size of 100 that can hold the text to write to the file.

/**
 * @brief Main function of the program that seperates the MPI Processes.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of strings containing the command-line arguments.
 * @return The exit status of the program.
 */
int main(int argc, char *argv[])
{
	int rank;
	int size;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size); // amount of prosesses
	MPI_Comm_rank(MPI_COMM_WORLD, &rank); // ID of prosses
	MPI_SIZE = size;

	initialise_board();  // initilises the starting gameboard

	if (rank == 0)
	{
		run_master(argc, argv);
	}
	else
	{
		run_worker(rank);
	}

	MPI_Barrier(MPI_COMM_WORLD); // Waits for all ranks before finalisation
	game_over();
}
/**
 * @brief Function executed by the master process which controls all refree functions and move
 * 		  generation. Feeding process to run_worker().
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of strings containing the command-line arguments.
 */
void run_master(int argc, char *argv[]) 
{
	char cmd[CMDBUFSIZE];			 // command buffer
	char my_move[MOVEBUFSIZE];		 // move buffer
	char opponent_move[MOVEBUFSIZE]; // opponents move buffer
	int time_limit;
	int my_colour;	 				 // current player
	int running = 0; 				 // state of game
	FILE *fp = NULL; 		

	if (initialise_master(argc, argv, &time_limit, &my_colour, &fp) != FAILURE) // Initalises Ref functions and Comms
	{
		running = 1;
	}
	if (my_colour == EMPTY)
	{
		my_colour = BLACK; 
	}

	MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD); // Broadcast my_colour

	while (running == 1)
	{
		/* Receive next command from referee */
		if (comms_get_cmd(cmd, opponent_move) == FAILURE)
		{
			fprintf(fp, "Error getting cmd\n");
			fflush(fp);
			running = 0;
			break;
		}

		/* Received game_over message */
		if (strcmp(cmd, "game_over") == 0)
		{
			running = 0;
			fprintf(fp, "Game over\n");
			fflush(fp);
			break;
		}
		/* Received gen_move message */
		else if (strcmp(cmd, "gen_move") == 0)
		{
			MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD);				 // Broadcast running
			MPI_Bcast(current_board, BOARDSIZE, MPI_INT, 0, MPI_COMM_WORLD); // Broadcast board

			gen_move_master(my_move, my_colour, fp, current_board); 		 // Generates a move for my_player
			print_board(fp);

			if (comms_send_move(my_move) == FAILURE)
			{
				running = 0;
				fprintf(fp, "Move send failed\n");
				fflush(fp);
				break;
			}
		}
		/* Received opponent's move (play_move mesage) */
		else if (strcmp(cmd, "play_move") == 0)
		{
			apply_opp_move(opponent_move, my_colour, fp, current_board);
			print_board(fp);
		}
		/* Received unknown message */
		else
		{
			fprintf(fp, "Received unknown command from referee\n");
		}
	}
	MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD); // Broadcast running (DONE)
}
/**
 * @brief Initializes the master process and sets up communication.
 *
 * @param argc The number of command-line arguments.
 * @param argv The command-line arguments.
 * @param time_limit The time limit for the game.
 * @param my_colour Pointer to the player's color.
 * @param fp Pointer to the file pointer for logging.
 * @return int The result of initialization (SUCCESS or FAILURE).
 */
int initialise_master(int argc, char *argv[], int *time_limit, int *my_colour, FILE **fp)
{
	int result = FAILURE;

	if (argc == 5)
	{
		unsigned long ip = inet_addr(argv[1]);
		int port = atoi(argv[2]);
		*time_limit = atoi(argv[3]);

		*fp = fopen(argv[4], "w");
		if (*fp != NULL)
		{
			fprintf(*fp, "Initialise communication and get player colour \n");
			if (comms_init_network(my_colour, ip, port) != FAILURE)
			{
				result = SUCCESS;
			}
			fflush(*fp);
		}
		else
		{
			fprintf(stderr, "File %s could not be opened", argv[4]);
		}
	}
	else
	{
		fprintf(*fp, "Arguments: <ip> <port> <time_limit> <filename> \n");
	}

	return result;
}
/**
 * @brief Initilizes the Gameboard
 *
 */
void initialise_board()
{
	int i;
	current_board = (int *)malloc(BOARDSIZE * sizeof(int));
	for (i = 0; i <= 9; i++)
		current_board[i] = OUTER;
	for (i = 10; i <= 89; i++)
	{
		if (i % 10 >= 1 && i % 10 <= 8)
			current_board[i] = EMPTY;
		else
			current_board[i] = OUTER;
	}
	for (i = 90; i <= 99; i++)
		current_board[i] = OUTER;
	current_board[44] = WHITE;
	current_board[45] = BLACK;
	current_board[54] = BLACK;
	current_board[55] = WHITE;
}
/**
 * @brief Free's the memory allocated for the game board.
 *
 * @param active_board The pointer to the game board.
 */
void free_board(int *active_board)
{
	free(active_board);
}
/**
 * @brief The entry point for worker processes. Worker processes dynamiclly recieve a set amount of moves which
 * 		  are then run through a MiniMax algorithm with Alpha/Beta Pruning and eventually use MPI to send the results 
 * 		  and best moves back to the master process
 *
 * @param rank The rank of the process.
 */
void run_worker(int rank)
{
	int running = 0;
	int my_colour = 0;
	int depth = 6;

	MPI_Bcast(&my_colour, 1, MPI_INT, 0, MPI_COMM_WORLD); // Broadcast colour
	MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD);	  // Broadcast running

	while (running == 1)
	{

		MPI_Bcast(current_board, BOARDSIZE, MPI_INT, 0, MPI_COMM_WORLD); // Broadcast board

		int num_moves;
		MPI_Recv(&num_moves, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // sets num_moves to how many moves for that rank

		if (num_moves != 0)
		{
			int *ranks_moves = (int *)malloc(num_moves * sizeof(int)); // allocate space for recieving moves
			memset(ranks_moves, 0, num_moves);
			MPI_Recv(ranks_moves, num_moves, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // populates rank_moves[] with its set of moves

			/* call minimax function to get score for each move */

			int score;				  
			int best_move = -1;		 
			int best_score = INT_MIN; 
			int alpha = INT_MIN;
			int beta = INT_MAX;
			for (int i = 0; i < num_moves; i++)  	// Goes through all possible moves
			{ 
				int *temp_board = (int *)malloc(BOARDSIZE * sizeof(int));
				for (int j = 0; j < BOARDSIZE; j++)
				{
					temp_board[j] = current_board[j]; 	// Creates new temp board for each move thats the same as the current board
				}

				make_move(ranks_moves[i], my_colour, NULL, temp_board);		// makes the ith move on the temp board
				score = minimax(temp_board, opponent(my_colour, NULL), depth - 1, rank, alpha, beta); 	// plays minimax on all the possible moves																				  /* update the best score and best move */

				if (score > best_score)
				{
					best_score = score;
					best_move = ranks_moves[i];   // Retrives the best score and correlating best move
				}
				free_board(temp_board);  
			}

			MPI_Send(&best_move, 1, MPI_INT, 0, 0, MPI_COMM_WORLD); // Each process sends it's best move to Master Process
		}
		else
		{
			int best_move = -1;
			MPI_Send(&best_move, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);  // Returns -1 for "pass" move 
		}

		MPI_Bcast(&running, 1, MPI_INT, 0, MPI_COMM_WORLD); // Broadcasts running
	}
}
/**
 * @brief The minimax algorithm for determining the best move.
 *
 * @param board The temp game board.
 * @param player The current player.
 * @param depth The depth of the search tree (depth = 6).
 * @param rank The rank of the process.
 * @param alpha The alpha value for alpha-beta pruning.
 * @param beta The beta value for alpha-beta pruning.
 * @return int The evaluation score for the current board position.
 */
int minimax(int *board, int player, int depth, int rank, int alpha, int beta)
{

	if (depth == 0 || is_game_over_move(board)) // if depth reached or move is a game over move
	{
		return evaluate(player, board);  // Evaluates the position on the board
	}

	if (player)  // Maximising Player
	{
		int maxEval = INT_MIN;
		int size;
		int *future_moves = (int *)malloc(LEGALMOVSBUFSIZE * sizeof(int));
		int *moves = (int *)malloc(LEGALMOVSBUFSIZE * sizeof(int));
		legal_moves(player, future_moves, NULL, board);
		size = future_moves[0];
		process_moves(future_moves, size, moves);
		free(future_moves);
		for (int i = 0; i < size; i++)
		{
			make_move(moves[i], player, NULL, board);
			int eval = minimax(board, opponent(player, NULL), depth - 1, rank, alpha, beta);
			maxEval = max(maxEval, eval); // Finds highest evaluation of every move
			alpha = max(alpha, eval);     // Adjusts Alpha value
			if (beta <= alpha)			  // Prunes if needed 
			{
				break;
			}
		}
		return maxEval;
	}
	else  // Minimising Player
	{
		int minEval = INT_MAX;
		int sizes;
		int *future_moves = (int *)malloc(LEGALMOVSBUFSIZE * sizeof(int));
		int *moves = (int *)malloc(LEGALMOVSBUFSIZE * sizeof(int));
		legal_moves(player, future_moves, NULL, board);
		sizes = future_moves[0];
		process_moves(future_moves, sizes, moves);
		free(future_moves);
		for (int i = 0; i < sizes; i++)
		{
			make_move(moves[i], player, NULL, board);
			int eval = minimax(board, opponent(player, NULL), depth - 1, rank, alpha, beta);
			minEval = min(minEval, eval);   // Finds lowest evaluation of every move
			beta = min(beta, eval);			// Adjust Beta values
			if (beta <= alpha)				// Prunes if needed
			{
				break;
			}
		}
		return minEval;
	}
}
/**
 * @brief if the game is over based on the current board state.
 *
 * @param board The game board represented as an array.
 * @return 1 if the game is over, 0 otherwise.
 */
int is_game_over_move(int *board)
{
	/* Check if both players have no legal moves */
	if (!has_legal_moves(board, BLACK) && !has_legal_moves(board, WHITE))
	{
		return 1; // Game over
	}
	return 0; // Game not over
}
/**
 * @brief if the given player has any legal moves on the current board.
 *
 * @param board The temp game board.
 * @param player The player to check for legal moves.
 * @return 1 if the player has legal moves, 0 otherwise.
 */
int has_legal_moves(int *board, int player)
{
	int *future_moves = (int *)malloc(LEGALMOVSBUFSIZE * sizeof(int));
	legal_moves(player, future_moves, NULL, board);
	int size = future_moves[0];
	return size > 0;
}

/**
 * @brief the remaining elements of the input array to the output array without the size at moves[0].
 *
 * @param moves The input array containing moves and size.
 * @param amount_of_moves The number of moves in the input array.
 * @param array The output array to store the copied moves.
 */
void process_moves(int *moves, int amount_of_moves, int *array)
{
	for (int i = 0; i < amount_of_moves; i++)
	{
		array[i] = moves[i + 1];
	}
}
/**
 * @brief the maximum value between two integers.
 *
 * @param value1 The first value.
 * @param value2 The second value.
 * @return The maximum value.
 */
int max(int value1, int value2)
{
	// Max Function
	if (value1 > value2)
	{
		return value1;
	}
	else
	{
		return value2;
	}
}
/**
 * @brief the minimum value between two integers.
 *
 * @param value1 The first value.
 * @param value2 The second value.
 * @return The minimum value.
 */
int min(int value1, int value2)
{
	// Min Function
	if (value1 > value2)
	{
		return value2;
	}
	else
	{
		return value1;
	}
}
/**
 * @brief Called when the next move should be generated.
 *
 * @param move The output string to store the generated move.
 * @param my_colour The color of the player executing the move.
 * @param fp The file pointer for logging and printing.
 * @param active_board The current game board.
 */
void gen_move_master(char *move, int my_colour, FILE *fp, int *active_board)
{
	int loc;

	loc = bens_strategy(my_colour, fp); // Genrates the best possible move using minimax

	if (loc == -1) // if move is a pass
	{
		strncpy(move, "pass\n", MOVEBUFSIZE);
	}
	else
	{
		/* apply move to gameboard */
		get_move_string(loc, move);
		make_move(loc, my_colour, fp, active_board);
	}
}
/**
 * @brief The opponent's move to the game board.
 *
 * @param move The move string representing the opponent's move.
 * @param my_colour The color of the player.
 * @param fp The file pointer for logging.
 * @param active_board The game board represented as an array.
 */
void apply_opp_move(char *move, int my_colour, FILE *fp, int *active_board)
{
	int loc;
	if (strcmp(move, "pass\n") == -1) // changed to -1 from 0
	{
		return;
	}
	loc = get_loc(move);
	make_move(loc, opponent(my_colour, fp), fp, active_board);
}
/**
 * @brief Necessary cleanup and finalize the game.
 */
void game_over()
{
	free_board(current_board);
	MPI_Finalize();
}
/**
 * @brief Sets a location on the game board to its corresponding move string.
 *
 * @param loc The location on the game board.
 * @param ms The output string to store the move string.
 */
void get_move_string(int loc, char *ms)
{
	int row, col, new_loc;
	new_loc = loc - (9 + 2 * (loc / 10));
	row = new_loc / 8;
	col = new_loc % 8;
	ms[0] = row + '0';
	ms[1] = col + '0';
	ms[2] = '\n';
	ms[3] = 0;
}
/**
 * @brief Converts a move string to its corresponding location on the game board.
 *
 * @param movestring The move string.
 * @return The location on the game board.
 */
int get_loc(char *movestring)
{
	int row, col;
	/* movestring of form "xy", x = row and y = column */
	row = movestring[0] - '0';
	col = movestring[1] - '0';
	return (10 * (row + 1)) + col + 1;
}
/**
 * @brief Generate an array of legal moves for the given player.
 *
 * @param player The player for whom to generate legal moves.
 * @param moves The output array to store the legal moves.
 * @param fp The file pointer for logging.
 * @param active_board The temp game board .
 */
void legal_moves(int player, int *moves, FILE *fp, int *active_board)
{
	int move, i;
	moves[0] = 0;
	i = 0;
	for (move = 11; move <= 88; move++)
	{
		if (legalp(move, player, fp, active_board))
		{
			i++;
			moves[i] = move;
		}
	}
	moves[0] = i;
}
/**
 * @brief Check if a move is legal for a player.
 *
 * @param move The move to check.
 * @param player The player making the move.
 * @param fp The file pointer.
 * @param active_board The active game board.
 * @return Returns 1 if the move is legal, 0 otherwise.
 */
int legalp(int move, int player, FILE *fp, int *active_board)
{
	int i;

	if (!validp(move))
		return 0;
	if (active_board[move] == EMPTY)
	{

		i = 0;
		while (i <= 7 && !would_flip(move, ALLDIRECTIONS[i], player, fp, active_board))
			i++;
		if (i == 8)
			return 0;
		else
			return 1;
	}
	else
		return 0;
}
/**
 * @brief Check if a move is valid.
 *
 * @param move The move to check.
 * @return Returns 1 if the move is valid, 0 otherwise.
 */
int validp(int move)
{
	if ((move >= 11) && (move <= 88) && (move % 10 >= 1) && (move % 10 <= 8))
		return 1;
	else
		return 0;
}
/**
 * @brief Check if a move would cause a piece to flip.
 *
 * @param move The move to check.
 * @param dir The direction to check for flipping.
 * @param player The player making the move.
 * @param fp The file pointer.
 * @param active_board The active game board.
 * @return Returns 1 if the move would cause a flip, 0 otherwise.
 */
int would_flip(int move, int dir, int player, FILE *fp, int *active_board)
{
	int c;
	c = move + dir;
	if (active_board[c] == opponent(player, fp))
		return find_bracket_piece(c + dir, dir, player, fp, active_board);
	else
		return 0;
}
/**
 * @brief Find the bracketing piece for a move.
 *
 * @param square The square to start searching from.
 * @param dir The direction to search.
 * @param player The player making the move.
 * @param fp The file pointer.
 * @param active_board The active game board.
 * @return Returns the bracketing piece if found, 0 otherwise.
 */
int find_bracket_piece(int square, int dir, int player, FILE *fp, int *active_board)
{
	while (validp(square) && active_board[square] == opponent(player, fp))
		square = square + dir;
	if (validp(square) && active_board[square] == player)
		return square;
	else
		return 0;
}
/**
 * @brief Get the opponent player.
 *
 * @param player The player.
 * @param fp The file pointer.
 * @return Returns the opponent player.
 */
int opponent(int player, FILE *fp)
{
	if (player == BLACK)
		return WHITE;
	if (player == WHITE)
		return BLACK;
	fprintf(fp, "illegal player\n");
	return EMPTY;
}
/**
 * @brief Strategy for making a move but calculating the legal moves in a position, dynamiclly dividing it 
 * 		  up and sending it between processors using MPI. It then Recieves the optimal positions and preforms another 
 * 		  MiniMax search on them to find the best piece.
 *
 * @param my_colour The color of the player.
 * @param fp The file pointer.
 * @return Returns the best move.
 */
int bens_strategy(int my_colour, FILE *fp)
{
	int *moves = (int *)malloc(LEGALMOVSBUFSIZE * sizeof(int));
	memset(moves, 0, LEGALMOVSBUFSIZE);
	legal_moves(my_colour, moves, fp, current_board); // populates moves[] with ALL moves possible

	int total_legal_moves = moves[0]; // amount of possible moves

	if (total_legal_moves < MPI_SIZE - 1)  // If the amount of moves are LESS than the amount of processors avalible
	{
		int *output_moves = (int *)malloc(total_legal_moves * sizeof(int));
		memset(output_moves, 0, total_legal_moves);
		process_moves(moves, total_legal_moves, output_moves);  // populates output_moves array with all moves

		for (int j = 1; j < MPI_SIZE; j++)  // Sends to Worker Process
		{
			MPI_Send(&total_legal_moves, 1, MPI_INT, j, 0, MPI_COMM_WORLD);
			MPI_Send(output_moves, total_legal_moves, MPI_INT, j, 0, MPI_COMM_WORLD);
		}
	}
	else if ((total_legal_moves >= (MPI_SIZE - 1)))  // If the amount of moves are MORE than the amount of processors avalible
	{

		int moves_per_process = total_legal_moves / (MPI_SIZE - 1); // Divide up
		int remainder_moves = total_legal_moves % (MPI_SIZE - 1);	// Remander goes to rank 3
		int current_index = 1;
		for (int i = 1; i < MPI_SIZE; i++)
		{
			int num_moves = moves_per_process;
			if (i == MPI_SIZE - 1)
			{
				num_moves = num_moves + remainder_moves;
			}
			MPI_Send(&num_moves, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
			MPI_Send(&moves[current_index], num_moves, MPI_INT, i, 0, MPI_COMM_WORLD); // Dynamiclly sends moves to each process
			current_index += num_moves;
		}
	}

	int *best_moves = (int *)malloc((MPI_SIZE - 1) * sizeof(int));
	memset(best_moves, 0, (MPI_SIZE - 1));
	for (int i = 1; i < MPI_SIZE; i++)
	{
		MPI_Recv(&best_moves[i - 1], 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // Recovers each processes best move
	}

	int score;
	int rank = 0;
	int best_score = INT_MIN;
	int best_move = -1;
	int alpha = INT_MIN;
	int beta = INT_MAX;
	for (int i = 0; i < MPI_SIZE - 1; i++)
	{ 
		int *temp_board = (int *)malloc(BOARDSIZE * sizeof(int));
		for (int j = 0; j < BOARDSIZE; j++)
		{
			temp_board[j] = current_board[j];
		}

		if (best_moves[i] != -1)
		{
			make_move(best_moves[i], my_colour, NULL, temp_board);
			score = minimax(temp_board, opponent(my_colour, NULL), 1, rank, alpha, beta); // Uses Minimax to get the best move possible
			best_move = best_moves[i];
		}
		else
		{
			score == INT_MIN + 1;
		}
		/* update the best score and best move */
		if (score > best_score)
		{
			best_score = score;
			best_move = best_moves[i];
		}
	}

	if (total_legal_moves == 0)
	{
		return -1;  // if Move is a Pass
	}
	else
	{
		return best_move; // Returns Best Move possible
	}
}
/**
Writes text to a file.

- @param filename The name of the file to write to.
- @param text The text to be written to the file.
*/
void writeToFile(char *filename, char *text)
{
	FILE *dfp;
	dfp = fopen(filename, "a");
	fprintf(dfp, "%s", text);
	fclose(dfp);
}
/**
Makes a move on the active game board.

- @param move The move to be made.
- @param player The player making the move.
- @param fp The file pointer for output.
- @param active_board The game board array.
*/
void make_move(int move, int player, FILE *fp, int *active_board)
{
	int i;
	active_board[move] = player;
	for (i = 0; i <= 7; i++)
	{
		make_flips(move, ALLDIRECTIONS[i], player, fp, active_board);
	}
}
/**
Makes flips on the game board.

- @param move The move that triggered the flips.
- @param dir The direction of the flips.
- @param player The player making the flips.
- @param fp The file pointer for output.
- @param active_board The game board array.
*/
void make_flips(int move, int dir, int player, FILE *fp, int *active_board)
{
	int bracketer, c;
	bracketer = would_flip(move, dir, player, fp, active_board);
	if (bracketer)
	{
		c = move + dir;
		do
		{
			active_board[c] = player;
			c = c + dir;
		} while (c != bracketer);
	}
}
/**
* @brief Prints the game board to a file.
*
* @param fp The file pointer for output.
*/
void print_board(FILE *fp)
{
	int row, col;
	fprintf(fp, "   1 2 3 4 5 6 7 8 [%c=%d %c=%d]\n",
			nameof(BLACK), count(BLACK, current_board), nameof(WHITE), count(WHITE, current_board));
	for (row = 1; row <= 8; row++)
	{
		fprintf(fp, "%d  ", row);
		for (col = 1; col <= 8; col++)
			fprintf(fp, "%c ", nameof(current_board[col + (10 * row)]));
		fprintf(fp, "\n");
	}
	fflush(fp);
}
/**
* @brief Returns the name of a game piece.
*
* @param piece The game piece identifier.
* @return The name of the game piece.
*/
char nameof(int piece)
{
	assert(0 <= piece && piece < 5);
	return (piecenames[piece]);
}
/**
* @brief Counts the number of game pieces for a player on the game board.
*
* @param player The player identifier.
* @param active_board The game board array.
* @return The count of game pieces for the player.
*/
int count(int player, int *active_board)
{
	int i, cnt;
	cnt = 0;
	for (i = 1; i <= 88; i++)
		if (active_board[i] == player)
			cnt++;
	return cnt;
}
/**
* @brief Evaluates the game board for a player using a weighted gameboard with higher weights being more
*        benifitial points on the board.
*
* @param player The player identifier.
* @param active_board The game board array.
* @return The score for the player.
*/
int evaluate(int player, int *active_board)
{
	int weights[100] = {    
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 5, -3, 2, 2, 2, 2, -3, 5, 0,
		0, -3, -4, -1, -1, -1, -1, -4, -3, 0,  // Weighed Board 
		0, 2, -1, 1, 0, 0, 1, -1, 2, 0,
		0, 2, -1, 0, 1, 1, 0, -1, 2, 0,
		0, 2, -1, 0, 1, 1, 0, -1, 2, 0,
		0, 2, -1, 1, 0, 0, 1, -1, 2, 0,
		0, -3, -4, -1, -1, -1, -1, -4, -3, 0,
		0, 5, -3, 2, 2, 2, 2, -3, 5, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	int score = 0;
	for (int i = 11; i <= 88; i++)
	{
		if (active_board[i] == player)
		{
			score += weights[i]; // Adds Weight 
		}
		else if (active_board[i] == opponent(player, NULL))
		{
			score -= weights[i]; // Subtracts Weight
		}
	}
	return score;
}
