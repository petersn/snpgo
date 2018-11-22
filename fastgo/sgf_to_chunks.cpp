// Convert SGF files into trainable features and chunks.

#include "go_utils.h"
#include "feature_extraction.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
#include <iterator>
#include <exception>
#include <boost/filesystem.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
using boost::iostreams::filtering_ostream;

constexpr int RANK_THRESHOLD = -100;

class RoundRobinWriter {
	// Here we use lists instead of vectors because filtering_ostream deletes copy.
	std::list<std::ofstream> files;
	std::list<filtering_ostream> streams;
	std::list<filtering_ostream>::iterator here;

	int count;
public:

	int index = 0;
	RoundRobinWriter(std::string base_path, int count) : count(count) {
		for (int i = 0; i < count; i++) {
			files.emplace_back(base_path + "_" + std::to_string(i), std::ios_base::out | std::ios_base::binary);
			streams.emplace_back();
			streams.back().push(boost::iostreams::zlib_compressor());
			streams.back().push(files.back());
		}
		here = streams.begin();
	}

	void advance() {
		here++;
		if (here == streams.end())
			here = streams.begin();
		index++;
		index %= count;
	}

	filtering_ostream& get_filtering_ostream() {
		return *here;
	}

	void write(const char* data, std::streamsize length) {
		get_filtering_ostream().write(data, length);
	}
};

struct Move {
	Player who_moved;
	Coord xy;
	bool pass;
};

struct Game {
	std::string result_string = "???";
	Player who_won = Player::NOBODY;
	std::vector<Move> moves;
	int white_rank = -99;
	int black_rank = -99;
	float komi = 7.5;
};

std::unordered_map<std::string, int> rank_string_table{
	{"1d", 1}, {"2d", 2}, {"3d", 3},
	{"4d", 4}, {"5d", 5}, {"6d", 6},
	{"7d", 7}, {"8d", 8}, {"9d", 9},
	{"1p",  9}, {"2p",  9}, {"3p",  9},
	{"4p", 10}, {"5p", 11}, {"6p", 12},
	{"7p", 13}, {"8p", 14}, {"9p", 15},

	// For Fox Go Server data.
	{"1段", 1}, {"2段", 2}, {"3段", 3},
	{"4段", 4}, {"5段", 5}, {"6段", 6},
	{"7段", 7}, {"8段", 8}, {"9段", 9},
	{"P1段",  9}, {"P2段",  9}, {"P3段",  9},
	{"P4段", 10}, {"P5段", 11}, {"P6段", 12},
	{"P7段", 13}, {"P8段", 14}, {"P9段", 15},
};

#define NOT_EOF(x) \
	do { \
		if ((x) == EOF) { \
			return false; \
		} \
	} while (0)

static void fill_in_move(Move& m, std::string& location) {
	if (location.size() == 0) {
		m.pass = true;
		return;
	}
	int x = ((int)location[0]) - 'a';
	int y = ((int)location[1]) - 'a';
	if (not ((0 <= x and x < 19 and 0 <= y and y < 19) or (x == 20 and y == 20))) {
		// A move at [tt] (or (19, 19), right off the corner) is considered a pass.
		if (x == 19 and y == 19) {
			m.pass = true;
			m.xy = {-1, -1};
			return;
		}
		std::cout << "Weird coordinates: " << x << " " << y << std::endl;
		assert(false);
	}
	m.xy = {x, y};
}

std::string slurp_file(std::string path) {
	std::ifstream in(path, std::ios_base::in | std::ios_base::binary);
	std::ostringstream ss{};
	ss << in.rdbuf();
	return ss.str();
}

static bool parse_sgf(std::string path, Game& game) {
	std::string file_contents = slurp_file(path);
	std::stringstream f{file_contents};

	// Move up to the first open paren.
	f >> std::ws;
	if (f.get() != '(') {
		std::cerr << "Expected '(' in " << path << std::endl;
		return false;
	}
	// Begin consuming nodes.
	while (1) {
		f >> std::ws;
		// End parsing if we've hit the end of file or ')'
		int c = f.get();
		if (c == EOF or c == ')')
			break;
		// Begin the first node.
		if (c != ';') {
			std::cerr << "Expected ';' in " << path << std::endl;
			return false;
		}
		// Parse the Properties for the master header node.
		while (1) {
			f >> std::ws;
			int next = f.peek();
			NOT_EOF(next);
			// If we hit a ; then we're starting the moves.
			if (next == ';')
				break;
			// Parse an entry in the header node.
			std::string property_name, property_first_contents;
			std::getline(f, property_name, '[');
			std::getline(f, property_first_contents, ']');
			if (property_name == "SZ") {
				if (property_first_contents != "19") {
//					std::cerr << "Bad size: " << property_first_contents << " in " << path << std::endl;
					return false;
				}
			} else if (property_name == "HA") {
				if (property_first_contents != "0") {
//					std::cerr << "Bad handicap: " << property_first_contents << " in " << path << std::endl;
					return false;
				}
			} else if (property_name == "AW" or property_name == "AB") {
//				std::cerr << "Contains AW/BW, which is currently not supported." << std::endl;
				return false;
			} /* else if (property_name == "TM") { // or property_name == "OT") {
				int value = 0;
				try {
					value = std::stoi(property_first_contents);
				} catch (std::exception& e) {
					std::cout << "Bad integer attempt in: " << path << " with " << e.what() << " at value: " << property_first_contents << " -- skipping!" << std::endl;
					return false;
				}
				if (value < 600)
					return false;
//				std::cerr << "Contains TM/OT, which we currently drop: " << property_name << " = " << property_first_contents << std::endl;
//				return false;
			} */ else if (property_name == "RE") {
				game.result_string = property_first_contents;
			} else if (property_name == "WR") {
				if (rank_string_table.count(property_first_contents) > 0)
					game.white_rank = rank_string_table[property_first_contents];
//				else
//					std::cerr << "Weird white rank: " << property_first_contents << std::endl;
			} else if (property_name == "BR") {
				if (rank_string_table.count(property_first_contents) > 0)
					game.black_rank = rank_string_table[property_first_contents];
//				else
//					std::cerr << "Weird black rank: " << property_first_contents << std::endl;
			} else if (property_name == "KM") {
				try {
					game.komi = std::stof(property_first_contents);
				} catch (std::exception& e) {
//					std::cerr << "Bad komi: " << property_first_contents << std::endl;
//					return false;
				}
				if (game.komi >= 8.5 or game.komi <= -0.5) {
//					std::cerr << "Bizarre komi: " << game.komi << " in " << path << std::endl;
					return false;
				}
//				assert(-6 < game.komi);
//				assert(game.komi < 12);
			}
		}
		// Parse the sequence of move nodes.
		while (1) {
			f >> std::ws;
			int c = f.get();
			NOT_EOF(c);
			// Check if we're done with all of the moves.
			if (c == ')')
				break;
			// If not, then there better be a move here.
			if (c != ';') {
				std::cerr << "Expected move ';' in " << path << std::endl;
				return false;
			}
			game.moves.push_back({Player::NOBODY, {0, 0}, false});
			Move& m = game.moves.back();

			// Parse all of the properties inside of the move node.
			while (1) {
				f >> std::ws;
				int next = f.peek();
				NOT_EOF(next);
				// If we hit a ; then we're done with this move.
				if (next == ';' or next == ')')
					break;
				std::string property_name, property_first_contents;
				std::getline(f, property_name, '[');
				std::getline(f, property_first_contents, ']');
				if (property_name == "B" or property_name == "W") {
					m.who_moved = property_name == "B" ? Player::BLACK : Player::WHITE;
					fill_in_move(m, property_first_contents);
				} else if (property_name == "AW" or property_name == "AB" or property_name == "AE") {
//					std::cerr << "Contains move AW/BW/AE, which is currently not supported: " << path << std::endl;
					return false;
				} else if (property_name == "HA") {
//					std::cerr << "Contains unsupported handicap encoded in move: " << path << std::endl;
					return false;
				}
			}
			if (m.who_moved == Player::NOBODY) {
//				std::cerr << "Bad empty move in:" << path << std::endl;
				game.moves.pop_back();
			}
		}
	}

	// Parse who won.
	if (boost::starts_with(game.result_string, "B+")) {
		game.who_won = Player::BLACK;
	} else if (boost::starts_with(game.result_string, "W+")) {
		game.who_won = Player::WHITE;
	} else {
		game.who_won = Player::NOBODY;
//		std::cout << "                                                      Unknown result: " << game.result_string << std::endl;
	}

	if (game.who_won == Player::NOBODY)
		return false;

	return true;
}

void write_all_samples(RoundRobinWriter& features_writer, RoundRobinWriter& targets_writer, RoundRobinWriter& winners_writer, std::string path) {
	// Read in the SGF file.
	Game game;
	if (not parse_sgf(path, game))
		return;

	// If both players are too low rank then skip.
	if (game.white_rank < RANK_THRESHOLD and game.black_rank < RANK_THRESHOLD) {
//		std::cerr << "Both players too low rank: " << game.white_rank << " " << game.black_rank << std::endl;
		return;
	}

	// If just one player is too low rank then print a warning.
//	if (game.white_rank < RANK_THRESHOLD)
//		std::cerr << "White too low rank: " << game.white_rank << " " << game.black_rank << std::endl;
//	if (game.black_rank < RANK_THRESHOLD)
//		std::cerr << "Black too low rank: " << game.white_rank << " " << game.black_rank << std::endl;

	GoBoard board;
	FeatureExtractor feature_extractor;
	std::array<uint8_t, BOARD_SIZE * BOARD_SIZE> one_hot_winning_move = {};

	for (int move_index = 0; move_index < game.moves.size(); move_index++) {
		Move& m = game.moves[move_index];

		bool do_write_this_move = true;

		// Disable writing this move if the player is too low rank.
		if (m.who_moved == Player::BLACK and game.black_rank < RANK_THRESHOLD)
			do_write_this_move = false;
		if (m.who_moved == Player::WHITE and game.white_rank < RANK_THRESHOLD)
			do_write_this_move = false;

		// Currently we generate no samples on a pass.
		if (m.pass) {
			// Insert a dummy move to the history, so that the network can rely on
			// particular positions in the history being moves by particular players.
			feature_extractor.add_move_to_history({-1, -1});
			continue;
		}

		// Get out features for the board right BEFORE the move.
		if (do_write_this_move) {
			uint8_t features_buffer[TOTAL_FEATURES];
			feature_extractor.fill_features(features_buffer, board, m.who_moved);
			features_writer.write(reinterpret_cast<const char*>(features_buffer), TOTAL_FEATURES);
		}

		// Write the winning move out.
		Cell& winning_move_cell = piece_at(one_hot_winning_move, m.xy);
		winning_move_cell = 1;
		if (do_write_this_move)
			targets_writer.write(reinterpret_cast<const char*>(&one_hot_winning_move[0]), BOARD_SIZE * BOARD_SIZE);
		winning_move_cell = 0;

		// Write the winner of the game out.
		int our_komi = std::round(game.komi * 2);
		if (m.who_moved == Player::BLACK)
			our_komi = -our_komi;
		char game_winner[] = {0, 0};
		if (game.who_won == m.who_moved) //Player::BLACK)
			game_winner[0] = 1;
		if (game.who_won == opponent_of(m.who_moved))//Player::WHITE)
			game_winner[1] = 1;
		if (do_write_this_move)
			winners_writer.write(game_winner, 2);

		// Update the board and feature extractor.
		board.place_stone(m.who_moved, m.xy);
		feature_extractor.add_move_to_history(m.xy);

		// Advance each RoundRobinWriter. It is CRITICAL that we advance all of them together so they remain synced up!
		assert(features_writer.index == targets_writer.index and targets_writer.index == winners_writer.index);
		features_writer.advance();
		targets_writer.advance();
		winners_writer.advance();
	}
}

int main(int argc, char** argv) {
	if (argc != 8) {
		std::cerr << "Usage: sgf_to_chunks root_directory features_chunk.z targets_chunk.z winners_chunk.z start_index stop_index round_robin_count" << std::endl;
		std::cerr << std::endl;
		std::cerr << "Finds all SGF files under the root directory, sorts them asciibetically processes those in [start_index, stop_index), and outputs to the chunk files." << std::endl;
		return 1;
	}

	std::string root_directory_path = argv[1];
	std::string features_chunk_path = argv[2];
	std::string targets_chunk_path  = argv[3];
	std::string winners_chunk_path  = argv[4];
	int start_index       = std::stoi(argv[5]);
	int stop_index        = std::stoi(argv[6]);
	int round_robin_count = std::stoi(argv[7]);

	std::vector<std::string> paths;

	boost::filesystem::recursive_directory_iterator dir(root_directory_path);
	for (auto entry : dir) {
		if (boost::filesystem::extension(entry) == ".sgf") {
			paths.push_back(entry.path().string());
		}
	}

	// Put the data in a deterministic but shuffled order.
	std::sort(paths.begin(), paths.end());
	std::minstd_rand0 generator(12345);
	std::shuffle(paths.begin(), paths.end(), generator);

	stop_index = std::min(stop_index, (int)paths.size());

	std::cout << "Found " << paths.size() << " SGF files." << std::endl;

	// Open the output files for writing.
	RoundRobinWriter features_writer(features_chunk_path, round_robin_count);
	RoundRobinWriter targets_writer (targets_chunk_path,  round_robin_count);
	RoundRobinWriter winners_writer (winners_chunk_path,  round_robin_count);

	for (int index = start_index; index < stop_index; index++) {
		std::string& path = paths[index];
		if ((index + 1) % 10000 == 0)
			printf("Processing %5i [%5i/%5i] %s\n", index, (index - start_index + 1), (stop_index - start_index), path.c_str());
		write_all_samples(features_writer, targets_writer, winners_writer, path);
	}
}

