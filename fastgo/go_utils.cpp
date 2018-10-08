// Utilities for Go.

#include "go_utils.h"
#include <iostream>
#include <cassert>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
using std::cout;
using std::endl;
using std::vector;
using std::pair;
using std::tuple;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::unique_ptr;

// ===== Go board =====

std::ostream& operator <<(std::ostream& os, const Coord& xy) {
	os << "(" << xy.first << " " << xy.second << ")";
	return os;
}

std::ostream& operator <<(std::ostream& os, const GoBoard::Group& group) {
	os << "<";
	for (auto xy : group.stones)
		os << xy << " ";
	os << "| ";
	for (auto xy : group.liberties)
		os << xy << " ";
	os << ">";
	return os;
}

std::ostream& operator <<(std::ostream& os, const GoBoard& board) {
	for (int y = 0; y < BOARD_SIZE; y++) {
		for (int x = 0; x < BOARD_SIZE; x++) {
			cout << unordered_map<Cell, string>{{0, "."}, {1, "#"}, {2, "o"}}[piece_at(board, {x, y})] << " ";
		}
		cout << endl;
	}
	return os;
}

template <>
struct merge_trait<GoBoard::Group> {
	static void merge_values(GoBoard::Group& g1, GoBoard::Group& g2) {
		assert(g1.owner == g2.owner);
		g1.stones.insert(g2.stones.begin(), g2.stones.end());
		g1.liberties.insert(g2.liberties.begin(), g2.liberties.end());
		// Assert disjointness of liberties and stones.
		for (auto xy : g1.liberties) {
			assert(g1.stones.count(xy) == 0);
		}
	}
};

void GoBoard::remove_group(DisjointSet<Coord, Group>::DisjointSetNode* node) {
	node = groups.find(node);
	groups.root_nodes.erase(node);
	// First erase all of our key_to_node entries.
	for (const Coord& xy : node->value.stones) {
		groups.key_to_node.erase(xy);
		Cell& cell = piece_at(cells, xy);
		assert(cell == (int)node->value.owner);
		cell = 0;
	}
	// Then find all of our neighbors and increment their libery counts.
	for (const Coord& xy : node->value.stones) {
		for (auto neighbor_xy : NEIGHBORS_INIT_LIST(xy)) {
			if (not coord_in_bounds(neighbor_xy))
				continue;
			// Look for a neighboring group at neighbor_xy.
			auto it = groups.key_to_node.find(neighbor_xy);
			if (it != groups.key_to_node.end()) {
				auto other_node = (*it).second;
				// At this point it should be impossible for a neigbhor to be of the same color, as we would have been in the same group.
				assert(other_node->value.owner != node->value.owner);
				groups.find(other_node)->value.liberties.insert(xy);
			}
		}
	}
}

void GoBoard::eliminate_dead_stones_of(Player color) {
	vector<DisjointSet<Coord, Group>::DisjointSetNode*> to_remove;
	for (DisjointSet<Coord, Group>::DisjointSetNode* node : groups.root_nodes) {
		if (node->value.owner == color and node->value.liberties.size() == 0)
			to_remove.push_back(node);
	}
	for (auto node : to_remove)
		remove_group(node);
}

void GoBoard::place_stone(Player color, Coord xy) {
	// First, check that the location is free.
	Cell& cell = piece_at(cells, xy);
	assert(cell == (int)Player::NOBODY);

	cell = (int)color;

	// Make a group for the node.
	DisjointSet<Coord, Group>::DisjointSetNode* node = groups.make_node(xy, {color, {xy}, {}});

	// Try to merge with neighbors, and also update liberties for us and neighbors.
	for (auto neighbor_xy : NEIGHBORS_INIT_LIST(xy)) {
		if (not coord_in_bounds(neighbor_xy))
			continue;
		// Look for a neighboring group at neighbor_xy.
		auto it = groups.key_to_node.find(neighbor_xy);
		if (it != groups.key_to_node.end()) {
			auto other_node = groups.find((*it).second);
			// Remove us as a liberty from them.
			other_node->value.liberties.erase(xy);
			// Additionally, if they're of our color then merge with them.
			if (other_node->value.owner == color)
				groups.union_nodes(node, (*it).second);
		} else {
			// If there's no one there then add a liberty for this group.
			node = groups.find(node);
			node->value.liberties.insert(neighbor_xy);
		}
	}

	// Eliminate enemy groups with zero liberties.
	eliminate_dead_stones_of(opponent_of(color));
	eliminate_dead_stones_of(color);
}

int GoBoard::liberty_count(Coord xy) {
	assert(coord_in_bounds(xy));
	auto it = groups.key_to_node.find(xy);
	if (it == groups.key_to_node.end())
		return 0;
	return groups.find((*it).second)->value.liberties.size();
}

int GoBoard::group_size(Coord xy) {
	assert(coord_in_bounds(xy));
	auto it = groups.key_to_node.find(xy);
	if (it == groups.key_to_node.end())
		return 0;
	return groups.find((*it).second)->value.stones.size();
}

