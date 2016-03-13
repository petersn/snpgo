#!/usr/bin/python

import itertools, random
import utilities

class Engine:
	def __init__(self, board_size):
		self.board_size = board_size
		self.board = {}

	def player_passes(self, color):
		pass

	def play(self, color, xy):
		self.board[xy] = color

	def genmove(self, color):
		spots = [xy for xy in valid_coords if xy not in self.board.viewkeys()]
		if spots:
			choice = random.choice(spots)
			self.board[choice] = color
			return choice
		return "pass"

