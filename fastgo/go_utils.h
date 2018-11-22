// Utilities for Go.

#ifndef _SNPGO_GO_UTILS_H
#define _SNPGO_GO_UTILS_H

#include <cstdint>
#include <cassert>
#include <memory>
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>

constexpr int BOARD_SIZE = 19;

typedef uint8_t Cell;
typedef std::pair<int, int> Coord;

// Unclear if the following code invokes undefined behavior, as specializing on default types is disallowed.
// TODO: Double check.
namespace std {
	template <>
	class hash<Coord> {
	public:
		size_t operator ()(const Coord& x) const {
			size_t h1 = std::hash<int>()(x.first);
			size_t h2 = std::hash<int>()(x.second);
			return h1 + 0x9e3779b9 + (h2 << 6) + (h2 >> 2);
		}
	};
}

enum class Player {
	NOBODY = 0,
	BLACK = 1,
	WHITE = 2,
};

static inline Player opponent_of(Player p) {
	assert(p == Player::BLACK or p == Player::WHITE);
	return p == Player::BLACK ? Player::WHITE : Player::BLACK;
}

template <typename T>
struct merge_trait {
	static void merge_values(T& a, T& b);
};

template <typename Key, typename Value>
struct DisjointSet {
	struct DisjointSetNode {
		DisjointSetNode* parent;
		int rank;
		Key key;
		Value value;
	};

	std::vector<std::unique_ptr<DisjointSetNode>> nodes;
	std::unordered_map<Key, DisjointSetNode*> key_to_node;
	std::unordered_set<DisjointSetNode*> root_nodes;

	DisjointSetNode* make_node(Key key, Value value) {
		auto it = key_to_node.find(key);
		assert(it == key_to_node.end());
//		if (it != key_to_node.end())
//			return (*it).second;
		DisjointSetNode* node = new DisjointSetNode{nullptr, 0, key, value};
		node->parent = node;
		// Add a unique_ptr into nodes to handle destruction.
//		std::unique_ptr<DisjointSetNode> p(node);
//		nodes.push_back(std::move(p));
		nodes.emplace_back(node);
		// Add the appropriate mapping.
		key_to_node[key] = node;
		root_nodes.insert(node);
		return node;
	}

	inline bool contains(Key key) {
		return key_to_node.count(key) != 0;
	}

	DisjointSetNode* find(Key key) {
		return find(key_to_node.at(key));
	}

	DisjointSetNode* find(DisjointSetNode* x) {
		if (x->parent != x)
			x->parent = find(x->parent);
		return x->parent;
	}

	void union_nodes(Key k1, Key k2) {
		union_nodes(key_to_node.at(k1), key_to_node.at(k2));
	}

	void union_nodes(DisjointSetNode* x, DisjointSetNode* y) {
		auto x_root = find(x);
		auto y_root = find(y);
		if (x_root == y_root)
			return;
		if (x_root->rank < y_root->rank) {
			root_nodes.erase(x_root);
			x_root->parent = y_root;
			merge_trait<Value>::merge_values(y_root->value, x_root->value);
		} else {
			root_nodes.erase(y_root);
			y_root->parent = x_root;
			merge_trait<Value>::merge_values(x_root->value, y_root->value);
			// Increment x_root's rank to indicate that y_root was just rerooted to it.
			if (x_root->rank == y_root->rank)
				x_root->rank++;
		}
	}

	Value& get_value(Key key) {
		return find(key)->value;
	}
};

struct GoBoard {
	struct Group {
		Player owner;
		std::unordered_set<Coord> stones;
		std::unordered_set<Coord> liberties;
	};

	DisjointSet<Coord, Group> groups;
	std::array<Cell, BOARD_SIZE * BOARD_SIZE> cells = {};

	void remove_group(DisjointSet<Coord, Group>::DisjointSetNode* group);
	void eliminate_dead_stones_of(Player color);
	void place_stone(Player who, Coord xy);
	int liberty_count(Coord xy);
	int group_size(Coord xy);
};

static inline bool coord_in_bounds(Coord xy) {
	return 0 <= xy.first and xy.first < BOARD_SIZE and 0 <= xy.second and xy.second < BOARD_SIZE;
}

static inline Cell& piece_at(std::array<Cell, BOARD_SIZE * BOARD_SIZE>& board, Coord xy) {
	assert(coord_in_bounds(xy));
	return board[xy.first + xy.second * BOARD_SIZE];
}

static inline const Cell& piece_at(const std::array<Cell, BOARD_SIZE * BOARD_SIZE>& board, Coord xy) {
	assert(coord_in_bounds(xy));
	return board[xy.first + xy.second * BOARD_SIZE];
}

static inline Cell& piece_at(GoBoard& board, Coord xy) {
	return piece_at(board.cells, xy);
}

static inline const Cell& piece_at(const GoBoard& board, Coord xy) {
	return piece_at(board.cells, xy);
}

#define NEIGHBORS_INIT_LIST(xy) { \
	Coord{(xy).first - 1, (xy).second}, \
	Coord{(xy).first + 1, (xy).second}, \
	Coord{(xy).first, (xy).second - 1}, \
	Coord{(xy).first, (xy).second + 1} \
}

std::ostream& operator <<(std::ostream& os, const Coord& xy);
std::ostream& operator <<(std::ostream& os, const GoBoard::Group& group);
std::ostream& operator <<(std::ostream& os, const GoBoard& group);

#endif

