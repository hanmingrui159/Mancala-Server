# Mancala-Server
A server used to play the customized multiple-player version of the Mancala game through TCP sockets

SET-UP:
To compile the game, open the bash terminal, cd into the directory containing Makefile and mancsrv.c, and enter "make" on the command line
To play the game, run the server ./mancsrv in one terminal window, and each player uses the command "nc 127.0.0.1 3000" in a new bash window to connect to the server. 

RULES:
-Newly Connected players need to enter the player's name first to be added to the game
-The current player enters his pit # to distribute the pebbles
-During the distribution of a pit, the pebbles in the pit is distributed sequentially to the pits that follows, and can be distributed the next players' pits, end-pits of other players' are skipped
-If the last pebble during distribution landed on the player's own end pit, he gets an extra round
-The game ends when a player has emptied all the non-end pits

The board is displayed in the format 
  mwc:  [0]5 [1]1 [2]6 [3]7 [4]6 [5]5  [end pit]0
	jpc:  [0]4 [1]4 [2]0 [3]5 [4]5 [5]0  [end pit]2
That is, one user per line, saying the number of pebbles in each pit (e.g., mwc's are 5, 1, 6, 7, 6, and 5, respectively), and identifying the pits by index numbers (starting from zero) for the use of players in making their moves.
