#!/usr/bin/python

import itertools, random

# Precompute all board positions. TODO: Variable board size?
valid_coords = set(itertools.product(range(19), repeat=2))

neighborhood_offsets = [(-1, 0), (1, 0), (0, -1), (0, 1)]

def add(a, b):
	return a[0] + b[0], a[1] + b[1]

# Precompute all adjacencies.
adjacent_coords = {}
for xy in valid_coords:
	s = adjacent_coords[xy] = set()
	for offset in neighborhood_offsets:
		neighbor = add(offset, xy)
		if neighbor in valid_coords:
			s.add(neighbor)

class Group:
	def __init__(self, xys):
		self.xys = set(xys)
		self.adjacent = reduce(lambda x, y: x | y, map(adjacent_coords.__getitem__, self.xyz))

	def add_stone(self, xy):
		self.xys.add(xy)
		if xy in self.adjacent:
			self.adjacent.remove(xy)
		self.adjacent |= adjacent_coords[xy]

	def add_group(self, group):
		for xy in group.xys:
			self.add_stone(xy)

def stones_of_color(board, color):
	return set(xy for xy, c in board.iteritems() if c == color)

def stones_to_groups(stones):
	groups = []
	which_group = {}
	group_counter = 1
	equivalencies = 
	# By sorting the set of stones as we iterate through we guarantee that adding a stone can only ever merge two groups.
	for xy in sorted(stones):
		for neighbor in adjacent_coords[xy]:
			if 
#		# Look for a group to pair this stone up with.
#		for group in groups:
#			if xy in group.adjacent:
#				group.add_stone(xy)
#				# Break to make sure we don't get put into multiple groups.
#				break
#		else:
#			# If we found no group for this stone, then make a new one.
#			groups.append(Group([xy]))

def board_to_groups(board):
	stones = [stones_of_color(board, color) for color in (0, 1)]

