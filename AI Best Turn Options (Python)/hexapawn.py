'''
	File: hexapawn.py
	Author: Grace Willhoite
	Description: The main function hexapawn([boardstate],int,char ('b'/'w'),int) takes as input a representation of the 
	state of a hexapawn game (i.e., a board position), an integer representing the size of the board, an indication 
	as to which player is to move next, and an integer representing the number of moves to look ahead. This function 
	returns as output the best next move that the designated player can make from that given board position.
'''

import copy

def hexapawn(boardstate, boardsize, player, nummoves):
	'''
		Function: hexapawn(boardstate, boardsize, player, nummoves)
		Description: This is the main hexapawn function that generates a tree with all possible moves for a board move
		and applies the minimax function the tree. hexapawn() returns the next best possible move based on the evaluation
		of minimax. 
		There are 4 arguments. boardstate is an array of
		of strings representing a board state with black or white pawns. boardsize is an int representing
		the length of the rows and columns. player is the char that is either 'b' or 'w' for the player deciding
		on a move. nummoves is the depth of the exploration desired for deciding on a move.
	'''
	curboardstate = []

	#format input to array of char lists instead of array of strings
	for i in range(boardsize):
		row = []
		for j in range(boardsize):
			row.append(boardstate[i][j])
		curboardstate.append(row)

	# initialize root node/board
	movetree = Move(curboardstate, player)

	# create all moves for this boardstate up to a certain depth and add to tree
	populateMoveTree(movetree, boardsize, nummoves)

	if movetree.childrenmoves == []:
		print("No legal moves")
		return

	# apply minimax on the tree to get next best move
	nextmove = minimax(movetree, player, boardsize, nummoves)

	# format move to array of strings
	board = []
	for i in range(boardsize):
		stringrow = ""
		for j in range(boardsize):
			stringrow += nextmove.boardstate[i][j]
		board.append(stringrow)

	return board

# Class definition for Move Nodes for tree construction
class Move:
	def __init__(self, boardstate, color):
		# list of possible moves, initially empty
		self.childrenmoves = []
		# arbitrary move value before set when evaluated
		self.movevalue = -100
		# array of char arrays
		self.boardstate = boardstate
		# player that needs to play
		self.playercolor = color

def populateMoveTree(movenode, boardsize, depthremaining):
	'''
		Function: populateMoveTree(movenode, boardsize, depthremaining)
		Description: populateMoveTree gnerates all the possible moves for a given node (by calling a helper function:
		generateplayermoves()) and then recursively generates all the moves for its children as well after converting them
		to Move Objects. It has no return value.
		There are 3 arguments. movenode, is a Move Object reference. boardsize is the int representing
		the size of the board. depthremaining indicates how many moves are left before reaching the move limit.
	'''
	# terminate when at move limit
	if depthremaining == 0:
		return
	else:
		# generate the children moves for the parent movenode
		childrenmoves = generateplayermoves(movenode.boardstate, boardsize, movenode.playercolor)

		# assign the color of the childrens moves to be opposit of the parent
		if movenode.playercolor == 'b':
			childcolor = 'w'
		else:
			childcolor = 'b'

		for i in range(len(childrenmoves)):
			# populate children array for parent node
			movenode.childrenmoves.append(Move(childrenmoves[i], childcolor))

		for j in range(len(childrenmoves)):
			# generate moves for each children
			populateMoveTree(movenode.childrenmoves[j], boardsize, depthremaining-1)

def boardevaluation(boardstate, player, boardsize):
	'''
		Function: boardevaluation(boardstate, player, boardsize)
		Description: boardevaluation returns the static evaluation of a board (int) depending on
		the player color.
		There are 3 arguments. boardstate is the array form of the boardstate. player is the color
		of the player attempting to choose a best move in the main function call. boardsize is the int
		representing the size of the playing board
	'''
	# evaluate board depending on the player
	if player == 'w':
		return evaluationwhite(boardstate, boardsize)
	elif player == 'b':
		return evaluationblack(boardstate, boardsize)

def evaluationblack(boardstate, boardsize):
	'''
		Function: evaluationblack(boardstate, boardsize)
		Description: evaluationblack returns the int value of the board if black is the one evaluating the board.
		It returns -10 if white wins, returns 10 if black wins and in all other cases returns (total pawn count
		of black - total pawn count of white) such that boards in which black has more pawns will be more desired.
		There are 2 arguments. boardstate is the array form of the boardstate. boardsize is the int representing
		the size of the playing board
	'''
	if 'w' in boardstate[boardsize-1]:
		return -10
	elif 'b' in boardstate[0]:
		return 10
	else:
		blacktotal = 0
		whitetotal = 0
		for i in range(boardsize):
			for j in range(boardsize):
				if boardstate[i][j] == 'w':
					whitetotal += 1
				elif boardstate[i][j] == 'b':
					blacktotal += 1
		return blacktotal-whitetotal

def evaluationwhite(boardstate, boardsize):
	'''
		Function: evaluationwhite(boardstate, boardsize)
		Description: evaluationwhite returns the int value of the board if white is the one evaluating the board.
		It returns -10 if black wins, returns 10 if white wins and in all other cases returns (total pawn count
		of white - total pawn count of black) such that boards in which white has more pawns will be more desired.
		There are 2 arguments. boardstate is the array form of the boardstate. boardsize is the int representing
		the size of the playing board
	'''
	if 'w' in boardstate[boardsize-1]:
		return 10
	elif 'b' in boardstate[0]:
		return -10
	else:
		blacktotal = 0
		whitetotal = 0
		for i in range(boardsize):
			for j in range(boardsize):
				if boardstate[i][j] == 'w':
					whitetotal += 1
				elif boardstate[i][j] == 'b':
					blacktotal += 1
		return whitetotal-blacktotal

def minimax(movetree, decidingplayer, boardsize, depth):
	'''
		Function: minimax(movetree, decidingplayer, boardsize, depth)
		Description: minimax recursively assigns a move value to each Move node's boardstate depending on whether it is on a max layer,
		min layer, or a leaf/bottom of the move tree. The static board evaluation is called on Move nodes with no children nodes
		or those at the bottom of the move tree. The max value of children moves are assigned when the parent node has the same color
		as the deciding player node. Otherwise the min is assigned. The return value is the best possible move node for the parent.

		There are 4 arguments. movetree is a Move Object. decidingplayer is the char representation of the main player deciding on 
		the best move at the top of the tree. boardsize is an integer representation of the board size. depth is the current remaining depth
		of the tree.
	'''
	# reached bottom of tree or no children moves, do static board evaluation
	if depth == 0 or len(movetree.childrenmoves) == 0:
		if decidingplayer == 'b':
			movetree.movevalue = evaluationblack(movetree.boardstate, boardsize)
		else:
			movetree.movevalue = evaluationwhite(movetree.boardstate, boardsize)
	elif decidingplayer == movetree.playercolor:
		# max layer if current player is same as deciding player
		for child in movetree.childrenmoves:
			# call minimax on all children to set their move values and set value of parent
			minimax(child, decidingplayer, boardsize, depth - 1)

		# choose max value of all the childrens move values
		maxvalue = None
		bestmove = None
		for child in movetree.childrenmoves:
			if maxvalue == None:
				maxvalue = child.movevalue
				bestmove = child
			if child.movevalue > maxvalue:
				maxvalue = child.movevalue
				bestmove = child
		movetree.movevalue = maxvalue
		return bestmove
	else:
		# min layer if player is not the same as the deciding player
		for child in movetree.childrenmoves:
			# call minimax on all children to set their move values and set value of parent
			minimax(child, decidingplayer, boardsize, depth - 1)

		# choose min value of all the childrens move values
		minvalue = None
		bestmove = None
		for child in movetree.childrenmoves:
			if minvalue == None:
				minvalue = child.movevalue
				bestmove = child
			if child.movevalue < minvalue:
				minvalue = child.movevalue
				bestmove = child
		movetree.movevalue = minvalue
		return bestmove

def generateplayermoves(boardstate, boardsize, player):
	'''
		Function: generateplayermoves(boardstate, boardsize, player)
		Description: generateplayermoves returns the list of next board states possible for an initial board state for
		a certain player.
		There are 3 arguments. boardstate is the starting board state in array form. boardsize is an int represenation of the boardsize.
		player is the player that is moving ('b' or 'w' encoded).
	'''
	# each pawn can move forward if the space is empty or take a pawn if there is one diagonal from it
	moves = movePawnsForward(boardstate, boardsize, player) + capturePawn(boardstate, boardsize, player)

	# return all possible moves for this player
	return moves

def movePawnsForward(boardstate, boardsize, player):
	'''
		Function: movePawnsForward(boardstate, boardsize, player)
		Description: movePawnsForward generates and returns all moves in which pawns move forward for a particular player.
		There are 3 arguments. boardstate is the starting board state in array form. boardsize is an int represenation of the boardsize.
		player is the player that is moving ('b' or 'w' encoded).
	'''
	if player == 'b':
		return moveBlackPawnsForward(boardstate, boardsize)
	else:
		return moveWhitePawnsForward(boardstate, boardsize)

def capturePawn(boardstate, boardsize, player):
	'''
		Function: capturePawn(boardstate, boardsize, player)
		Description: capturePawn generates and returns all possible moves in which pawns are captured by a given player.
		There are 3 arguments. boardstate is the starting board state in array form. boardsize is an int represenation of the boardsize.
		player is the player that is moving ('b' or 'w' encoded).
	'''
	if player == 'b':
		return moveBlackPawnsDiagonal(boardstate, boardsize)
	else:
		return moveWhitePawnsDiagonal(boardstate, boardsize)

def moveBlackPawnsForward(boardstate, boardsize):
	'''
		Function: moveBlackPawnsForward(boardstate, boardsize)
		Description: moveBlackPawnsForward generates and returns a list of all boardstates in which black pawns can move forward.
		There are 2 arguments. boardstate is the starting board state in array form. boardsize is an int represenation of the boardsize.
	'''
	blackcount = 0
	whitecount = 0
	for i in range(0, boardsize):
		for j in range(0, boardsize):
			if boardstate[i][j] == 'b':
				blackcount += 1
			elif boardstate[i][j] == 'w':
				whitecount += 1

	# check if player has already won
	if 'w' in boardstate[boardsize-1] or 'b' in boardstate[0] or whitecount == 0 or blackcount == 0:
		return []

	newmoves = []
	for i in range(0, boardsize):
		for j in range(0, boardsize):
			if boardstate[i][j] == 'b':
				if i != 0:
					if boardstate[i-1][j] == '-':
						newmove = copy.deepcopy(boardstate)
						newmove[i][j] = '-'
						newmove[i-1][j] = 'b'
						newmoves.append(newmove)
	return newmoves

def moveWhitePawnsForward(boardstate, boardsize):
	'''
		Function: moveWhitePawnsForward(boardstate, boardsize)
		Description: moveWhitePawnsForward generates and returns a list of all boardstates in which white pawns can move forward.
		There are 2 arguments. boardstate is the starting board state in array form. boardsize is an int represenation of the boardsize.
	'''
	blackcount = 0
	whitecount = 0
	for i in range(0, boardsize):
		for j in range(0, boardsize):
			if boardstate[i][j] == 'b':
				blackcount += 1
			elif boardstate[i][j] == 'w':
				whitecount += 1

	# check if player has already won
	if 'w' in boardstate[boardsize-1] or 'b' in boardstate[0] or whitecount == 0 or blackcount == 0:
		return []

	newmoves = []
	for i in range(0, boardsize):
		for j in range(0, boardsize):
			if boardstate[i][j] == 'w':
				if i != boardsize-1:
					if boardstate[i+1][j] == '-':
						newmove = copy.deepcopy(boardstate)
						newmove[i][j] = '-'
						newmove[i+1][j] = 'w'
						newmoves.append(newmove)
	return newmoves

def moveBlackPawnsDiagonal(boardstate, boardsize):
	'''
		Function: moveBlackPawnsDiagonal(boardstate, boardsize)
		Description: moveBlackPawnsDiagonal generates and returns a list of all boardstates in which black pawns can capture a white pawn.
		There are 2 arguments. boardstate is the starting board state in array form. boardsize is an int represenation of the boardsize.
	'''
	blackcount = 0
	whitecount = 0
	for i in range(0, boardsize):
		for j in range(0, boardsize):
			if boardstate[i][j] == 'b':
				blackcount += 1
			elif boardstate[i][j] == 'w':
				whitecount += 1

	# check if player has already won
	if 'w' in boardstate[boardsize-1] or 'b' in boardstate[0] or whitecount == 0 or blackcount == 0:
		return []

	newmoves = []
	for i in range(0, boardsize):
		for j in range(0, boardsize):
			if boardstate[i][j] == 'b':
				# check if there are w's in right diagonal and left diagonal
				if i != 0:
					if j == 0:
						# only check right diagonal
						if boardstate[i-1][j+1] == 'w':
							newmove = copy.deepcopy(boardstate)
							newmove[i][j] = '-'
							newmove[i-1][j+1] = 'b'
							newmoves.append(newmove)
					elif j == boardsize - 1:
						# only check left diagonal
						if boardstate[i-1][j-1] == 'w':
							newmove = copy.deepcopy(boardstate)
							newmove[i][j] = '-'
							newmove[i-1][j-1] = 'b'
							newmoves.append(newmove)
					else:
						# check both left and right
						if boardstate[i-1][j-1] == 'w':
							newmove = copy.deepcopy(boardstate)
							newmove[i][j] = '-'
							newmove[i-1][j-1] = 'b'
							newmoves.append(newmove)

						if boardstate[i-1][j+1] == 'w':
							newmove = copy.deepcopy(boardstate)
							newmove[i][j] = '-'
							newmove[i-1][j+1] = 'b'
							newmoves.append(newmove)

	return newmoves

def moveWhitePawnsDiagonal(boardstate, boardsize):
	'''
		Function: moveWhitePawnsDiagonal(boardstate, boardsize)
		Description: moveWhitePawnsDiagonal generates and returns a list of all boardstates in which white pawns can capture a black pawn.
		There are 2 arguments. boardstate is the starting board state in array form. boardsize is an int represenation of the boardsize.
	'''
	blackcount = 0
	whitecount = 0
	for i in range(0, boardsize):
		for j in range(0, boardsize):
			if boardstate[i][j] == 'b':
				blackcount += 1
			elif boardstate[i][j] == 'w':
				whitecount += 1

	# check if player has already won
	if 'w' in boardstate[boardsize-1] or 'b' in boardstate[0] or whitecount == 0 or blackcount == 0:
		return []


	newmoves = []
	for i in range(0, boardsize):
		for j in range(0, boardsize):
			if boardstate[i][j] == 'w':
				# check if there are w's in right diagonal and left diagonal
				if i != boardsize-1:
					if j == 0:
						# only check right diagonal
						if boardstate[i+1][j+1] == 'b':
							newmove = copy.deepcopy(boardstate)
							newmove[i][j] = '-'
							newmove[i+1][j+1] = 'w'
							newmoves.append(newmove)
					elif j == boardsize - 1:
						# only check left diagonal
						if boardstate[i+1][j-1] == 'b':
							newmove = copy.deepcopy(boardstate)
							newmove[i][j] = '-'
							newmove[i+1][j-1] = 'w'
							newmoves.append(newmove)
					else:
						# check both left and right
						if boardstate[i+1][j-1] == 'b':
							newmove = copy.deepcopy(boardstate)
							newmove[i][j] = '-'
							newmove[i+1][j-1] = 'w'
							newmoves.append(newmove)

						if boardstate[i+1][j+1] == 'b':
							newmove = copy.deepcopy(boardstate)
							newmove[i][j] = '-'
							newmove[i+1][j+1] = 'w'
							newmoves.append(newmove)
	return newmoves