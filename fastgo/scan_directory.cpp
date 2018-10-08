// Convert SGF files into trainable features and chunks.

#include "go_utils.h"

using namespace std;
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

#include <random>
std::random_device rd;     // only used once to initialise (seed) engine
std::mt19937 rng(rd());    // random-number engine used (Mersenne-Twister in this case)

using boost::iostreams::filtering_ostream;

#define HISTORY_LENGTH 3
//#define SELECT_JUST_ONE_MOVE false
#define SELECT_JUST_ONE_MOVE true
//#define SCORING
//#define RANK_THRESHOLD 4
#define RANK_THRESHOLD -100
#define SELF_PLAY

struct Move {
	Player who_moved;
	Coord xy;
	bool pass;

#ifdef SELF_PLAY
	bool is_random_self_play_move = false;
#endif
};

struct Game {
	string result_string = "???";
	Player who_won = Player::NOBODY;
	vector<Move> moves;
	int white_rank = -99;
	int black_rank = -99;
	float komi = 7.5;
};

std::unordered_map<string, int> rank_string_table{
	{"1d", 1}, {"2d", 2}, {"3d", 3},
	{"4d", 4}, {"5d", 5}, {"6d", 6},
	{"7d", 7}, {"8d", 8}, {"9d", 9},
};

//			cerr << "Unexpected EOF in " << path << endl; \

#define NOT_EOF(x) \
	do { \
		if ((x) == EOF) { \
			return false; \
		} \
	} while (0)

static void fill_in_move(Move& m, string& location) {
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
		cout << "Weird coordinates: " << x << " " << y << endl;
		assert(false);
	}
	m.xy = {x, y};
}

string slurp_file(string path) {
	ifstream in(path, ios_base::in | ios_base::binary);
	std::ostringstream ss{};
	ss << in.rdbuf();
	return ss.str();
//	FILE* fp = fopen(path.c_str(), "rb");
//	char buf[4096];
//	size_t total_read = fread(buf, 4095, 1, fp);
//	fclose(fp);
//	return string(buf, total_read);
}

static bool parse_sgf(string path, Game& game) {
	string file_contents = slurp_file(path);
	//std::ifstream f(path);
	std::stringstream f{file_contents};

	// Move up to the first open paren.
	f >> std::ws;
	if (f.get() != '(') {
		cerr << "Expected '(' in " << path << endl;
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
			cerr << "Expected ';' in " << path << endl;
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
			string property_name, property_first_contents;
			std::getline(f, property_name, '[');
			std::getline(f, property_first_contents, ']');
			if (property_name == "SZ") {
				if (property_first_contents != "19") {
//					cerr << "Bad size: " << property_first_contents << " in " << path << endl;
					return false;
				}
			} else if (property_name == "HA") {
				if (property_first_contents != "0") {
//					cerr << "Bad handicap: " << property_first_contents << " in " << path << endl;
					return false;
				}
			} else if (property_name == "AW" or property_name == "AB") {
//				cerr << "Contains AW/BW, which is currently not supported." << endl;
				return false;
			} /* else if (property_name == "TM") { // or property_name == "OT") {
				int value = 0;
				try {
					value = std::stoi(property_first_contents);
				} catch (exception& e) {
					cout << "Bad integer attempt in: " << path << " with " << e.what() << " at value: " << property_first_contents << " -- skipping!" << endl;
					return false;
				}
				if (value < 600)
					return false;
//				cerr << "Contains TM/OT, which we currently drop: " << property_name << " = " << property_first_contents << endl;
//				return false;
			} */ else if (property_name == "RE") {
				game.result_string = property_first_contents;
			} else if (property_name == "WR") {
				if (rank_string_table.count(property_first_contents) > 0)
					game.white_rank = rank_string_table[property_first_contents];
//				else
//					cerr << "Weird white rank: " << property_first_contents << endl;
			} else if (property_name == "BR") {
				if (rank_string_table.count(property_first_contents) > 0)
					game.black_rank = rank_string_table[property_first_contents];
//				else
//					cerr << "Weird black rank: " << property_first_contents << endl;
			} else if (property_name == "KM") {
				try {
					game.komi = std::stof(property_first_contents);
				} catch (exception& e) {
					cerr << "Bad komi: " << property_first_contents << endl;
					return false;
				}
				if (game.komi >= 8.5 or game.komi <= -0.5) {
//					cerr << "Bizarre komi: " << game.komi << " in " << path << endl;
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
				cerr << "Expected move ';' in " << path << endl;
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
				string property_name, property_first_contents;
				std::getline(f, property_name, '[');
				std::getline(f, property_first_contents, ']');
				if (property_name == "B" or property_name == "W") {
					m.who_moved = property_name == "B" ? Player::BLACK : Player::WHITE;
					fill_in_move(m, property_first_contents);
				} else if (property_name == "AW" or property_name == "BW") {
					cerr << "Contains move AW/BW, which is currently not supported." << endl;
					return false;
				}
#ifdef SELF_PLAY
				else if (property_name == "C" and property_first_contents == "rand") {
					m.is_random_self_play_move = true;
				}
#endif
			}
			if (m.who_moved == Player::NOBODY) {
				cerr << "Bad empty move in:" << path << endl;
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
//		cout << "                                                      Unknown result: " << game.result_string << endl;
	}

	if (game.who_won == Player::NOBODY)
		return false;

	if (game.komi != 6.5)
		return false;

	return true;
}

enum FeatureKind {
	FEAT_ONES_PLANE,
	FEAT_EMPTY_LOCATIONS_PLANE,
	FEAT_P1_STONES,
	FEAT_P2_STONES,
	FEAT_P1_LIBERTY1,
	FEAT_P1_LIBERTY2,
	FEAT_P1_LIBERTY3,
	FEAT_P1_LIBERTY4PLUS,
	FEAT_P2_LIBERTY1,
	FEAT_P2_LIBERTY2,
	FEAT_P2_LIBERTY3,
	FEAT_P2_LIBERTY4PLUS,
	FEAT_P1_PLAY_CAUSES_CAPTURE1,
//	FEAT_P1_PLAY_CAUSES_CAPTURE2,
//	FEAT_P1_PLAY_CAUSES_CAPTURE3,
	FEAT_P1_PLAY_CAUSES_CAPTURE2PLUS,
	FEAT_P2_PLAY_CAUSES_CAPTURE1,
//	FEAT_P2_PLAY_CAUSES_CAPTURE2,
//	FEAT_P2_PLAY_CAUSES_CAPTURE3,
	FEAT_P2_PLAY_CAUSES_CAPTURE2PLUS,
	FEAT_IS_WHITE,
	FEATURE_COUNT,
};

void write_features(filtering_ostream& features_out, GoBoard& board, Player perspective_player) {
	uint8_t feature_buffer[BOARD_SIZE * BOARD_SIZE * FEATURE_COUNT] = {};
	for (int y = 0; y < BOARD_SIZE; y++) {
		for (int x = 0; x < BOARD_SIZE; x++) {
			Cell piece = piece_at(board, {x, y});
			assert(piece == 0 or piece == 1 or piece == 2);

#define FEATURE(k) (feature_buffer[(BOARD_SIZE * BOARD_SIZE * (k)) + (x) + (y) * BOARD_SIZE])
			FEATURE(FEAT_ONES_PLANE)            = 1;
			FEATURE(FEAT_EMPTY_LOCATIONS_PLANE) = piece == 0;
			FEATURE(FEAT_P1_STONES)             = piece == (int)perspective_player;
			FEATURE(FEAT_P2_STONES)             = piece == (int)opponent_of(perspective_player);

			// If the piece is non-zero then lookup the liberty count, and  write out feature maps.
			int liberties = (piece != 0) ? board.liberty_count({x, y}) : 0;
			if (piece == (int)perspective_player) {
				FEATURE(FEAT_P1_LIBERTY1)           = liberties == 1;
				FEATURE(FEAT_P1_LIBERTY2)           = liberties == 2;
				FEATURE(FEAT_P1_LIBERTY3)           = liberties == 3;
				FEATURE(FEAT_P1_LIBERTY4PLUS)       = liberties >= 4;
			} else {
				FEATURE(FEAT_P2_LIBERTY1)           = liberties == 1;
				FEATURE(FEAT_P2_LIBERTY2)           = liberties == 2;
				FEATURE(FEAT_P2_LIBERTY3)           = liberties == 3;
				FEATURE(FEAT_P2_LIBERTY4PLUS)       = liberties >= 4;
			}

			for (auto layer_and_player : {
				pair<FeatureKind, Player>{FEAT_P1_PLAY_CAUSES_CAPTURE1, perspective_player},
				                         {FEAT_P2_PLAY_CAUSES_CAPTURE1, opponent_of(perspective_player)},
			}) {
				FeatureKind feature_layer = layer_and_player.first;
				Player player = layer_and_player.second;
				FEATURE(feature_layer) = 0;
				// Check for a neighbor we might capture.
				if (piece_at(board, {x, y}) == 0) {
					Coord left  {x - 1, y};
					Coord right {x + 1, y};
					Coord above {x, y - 1};
					Coord below {x, y + 1};
					// Look for an adjacent group owned by the other player with exactly one liberty.
					// If such a group exists, then playing here causes a capture.
					int total_captured_stones = 0;
					for (auto direction : {left, right, above, below}) {
						if (coord_in_bounds(direction) and piece_at(board, direction) == (int)opponent_of(player)) {
							if (board.liberty_count(direction) == 1) {
								total_captured_stones += board.group_size(direction);
							}
						}
					}
					if (total_captured_stones > 2)
						total_captured_stones = 2;
					if (total_captured_stones > 0)
						FEATURE(feature_layer + (total_captured_stones - 1)) = 1;
				}
			}

			FEATURE(FEAT_IS_WHITE) = perspective_player == Player::WHITE;
#undef FEATURE
		}
	}
	features_out.write(reinterpret_cast<const char*>(feature_buffer), BOARD_SIZE * BOARD_SIZE * FEATURE_COUNT);
}

extern "C" uint8_t* fastgo_extract_features(uint8_t* raw_board, int* output_length, int perspective_player) {
	assert(perspective_player == 1 or perspective_player == 2);
	// Copy all of the moves into a board.
	GoBoard board;
	for (int y = 0; y < BOARD_SIZE; y++) {
		for (int x = 0; x < BOARD_SIZE; x++) {
//			uint8_t piece = piece_at(*reinterpret_cast<std::array<Cell, BOARD_SIZE * BOARD_SIZE>*>(raw_board), {x, y});
			uint8_t piece = raw_board[x + y * BOARD_SIZE];
			assert(piece == 0 or piece == 1 or piece == 2);
			if (piece == 1 or piece == 2)
				board.place_stone((Player)piece, {x, y});
		}
	}
	// Get features out.
	string main_feature_block;
	filtering_ostream features_out(std::back_inserter(main_feature_block));
	write_features(features_out, board, (Player)perspective_player);
	features_out.flush();
	// Copy into a C-style array for returning.
	uint8_t* result = new uint8_t[main_feature_block.size()];
	std::copy(main_feature_block.begin(), main_feature_block.end(), result);
	*output_length = main_feature_block.size();
	return result;
}

extern "C" void fastgo_free_features(uint8_t* features) {
	delete[] features;
}

#ifdef SCORING
void write_all_samples(
	filtering_ostream& features_out,
	filtering_ostream& targets_out,
	filtering_ostream& winners_out,
	filtering_ostream& territory_out,
	string path
) {
#else
void write_all_samples(filtering_ostream& features_out, filtering_ostream& targets_out, filtering_ostream& winners_out, string path) {
#endif
	// Read in the SGF file.
	Game game;
	if (not parse_sgf(path, game))
		return;

	// If both players are too low rank then skip.
	if (game.white_rank < RANK_THRESHOLD and game.black_rank < RANK_THRESHOLD) {
//		cerr << "Both players too low rank: " << game.white_rank << " " << game.black_rank << endl;
		return;
	}

	// If just one player is too low rank then print a warning.
//	if (game.white_rank < RANK_THRESHOLD)
//		cerr << "White too low rank: " << game.white_rank << " " << game.black_rank << endl;
//	if (game.black_rank < RANK_THRESHOLD)
//		cerr << "Black too low rank: " << game.white_rank << " " << game.black_rank << endl;

#ifdef SCORING
	string final_territory_black = slurp_file(path + "-FINAL");
	assert(final_territory_black.size() == 361);

	string final_territory_white = final_territory_black;
	std::replace(final_territory_white.begin(), final_territory_white.end(), '\1', 'x');
	std::replace(final_territory_white.begin(), final_territory_white.end(), '\xff', '\1');
	std::replace(final_territory_white.begin(), final_territory_white.end(), 'x', '\xff');
#endif

	// Keep move histories in a pair of circular buffers.
	vector<vector<std::array<uint8_t, BOARD_SIZE * BOARD_SIZE>>> histories{{}, {}};
	vector<int> history_rotations{0, 0};
	std::array<uint8_t, BOARD_SIZE * BOARD_SIZE> blank_board{};
	for (int which_history = 0; which_history < 2; which_history++) {
		for (int i = 0; i < HISTORY_LENGTH; i++) {
			histories[which_history].push_back(blank_board);
			histories[which_history].push_back(blank_board);
		}
	}

	GoBoard board;
	std::array<uint8_t, BOARD_SIZE * BOARD_SIZE> one_hot_winning_move = {};

	std::uniform_int_distribution<int> uni(0, game.moves.size() - 1);
	int random_integer1 = uni(rng);
	int random_integer2 = uni(rng);

	for (int move_index = 0; move_index < game.moves.size(); move_index++) {
		Move& m = game.moves[move_index];

		bool do_write_this_move = true;
#ifdef SELECT_JUST_ONE_MOVE
		do_write_this_move = (move_index == random_integer1) or (move_index == random_integer2);
#endif
#ifdef SELF_PLAY
		do_write_this_move = (move_index > 0) and game.moves[move_index - 1].is_random_self_play_move;
#endif

		// Disable writing this move if the player is too low rank.
		if (m.who_moved == Player::BLACK and game.black_rank < RANK_THRESHOLD)
			do_write_this_move = false;
		if (m.who_moved == Player::WHITE and game.white_rank < RANK_THRESHOLD)
			do_write_this_move = false;

		// Currently we generate no samples on a pass.
		if (m.pass)
			continue;

		// Get out features for the board right BEFORE the move.
		if (do_write_this_move)
			write_features(features_out, board, m.who_moved);

		// If the current perspective player is WHITE then we grab the histories swapped.
		int who_moved_as_index = m.who_moved == Player::BLACK ? 0 : 1;

		// Order the histories to have the perspective player first.
		for (int which_history = 0; which_history < 2; which_history++) {
			auto& history = histories[(which_history + who_moved_as_index) % 2];
			int history_rotation = history_rotations[(which_history + who_moved_as_index) % 2];
			for (int i = 0; i < HISTORY_LENGTH; i++) {
				std::array<uint8_t, BOARD_SIZE * BOARD_SIZE>& entry = history[(i + history_rotation) % HISTORY_LENGTH];
				if (do_write_this_move)
					features_out.write(reinterpret_cast<const char*>(&entry[0]), BOARD_SIZE * BOARD_SIZE);
			}
		}

		// Write the winning move out.
		Cell& winning_move_cell = piece_at(one_hot_winning_move, m.xy);
		winning_move_cell = 1;
		if (do_write_this_move)
			targets_out.write(reinterpret_cast<const char*>(&one_hot_winning_move[0]), BOARD_SIZE * BOARD_SIZE);
		winning_move_cell = 0;

		// Write the winner of the game out.
		int our_komi = std::round(game.komi * 2);
		if (m.who_moved == Player::BLACK)
			our_komi = -our_komi;
		char game_winner[] = {0, 0, our_komi};
		if (game.who_won == m.who_moved) //Player::BLACK)
			game_winner[0] = 1;
		if (game.who_won == opponent_of(m.who_moved))//Player::WHITE)
			game_winner[1] = 1;
		if (do_write_this_move)
			winners_out.write(game_winner, 2);

#ifdef SCORING
		// Write a copy of this game's final territory out.
//		territory_out.write(final_territory, final_territory.size());
		if (m.who_moved == Player::BLACK) {
			territory_out << final_territory_black;
		} else if (m.who_moved == Player::WHITE) {
			territory_out << final_territory_white;
		} else {
			assert(false);
		}
#endif

		// Update the board.
		board.place_stone(m.who_moved, m.xy);

		// Insert into the history.
		int& history_rotation = history_rotations[who_moved_as_index];
		std::array<uint8_t, 361>& this_history_entry = histories[who_moved_as_index][history_rotation];
		this_history_entry = blank_board;
		piece_at(this_history_entry, m.xy) = 1;
		// Increment the rotation of the appropriate history entry.
		history_rotation = (history_rotation + 1) % HISTORY_LENGTH;
	}
}

int main(int argc, char** argv) {
	if (argc != 7) {
		cerr << "Usage: sgf_to_chunks root_directory features_chunk.z targets_chunk.z winners_chunk.z start_index stop_index" << endl;
		cerr << endl;
		cerr << "Finds all SGF files under the root directory, sorts them asciibetically processes those in [start_index, stop_index), and outputs to the chunk files." << endl;
		return 1;
	}

	string root_directory_path = argv[1];
	string features_chunk_path = argv[2];
	string targets_chunk_path  = argv[3];
	string winners_chunk_path  = argv[4];
	int start_index       = stoi(argv[5]);
	int stop_index        = stoi(argv[6]);

	vector<string> paths;

	for (auto& entry : boost::filesystem::directory_iterator(root_directory_path)) {
		if (boost::filesystem::is_directory(entry)) {
			for (auto& leaf : boost::filesystem::directory_iterator(entry)) {
				if (leaf.path().string().find("-scored") == std::string::npos)
					continue;
				paths.push_back(leaf.path().string());
			}
		}
	}

	cout << "Found: " << paths.size() << endl;

	int total_length = 0;
	int files_read = 0;
	for (string& path : paths) {
		string answer = slurp_file(path);
		total_length += answer.size();
		if (++files_read % 10000 == 0)
			cout << "Read: " << files_read << endl;
	}

	cout << "Total length: " << total_length << endl;

	return 43;

	boost::filesystem::recursive_directory_iterator dir(root_directory_path);
	for (auto entry : dir) {
		if (boost::filesystem::extension(entry) == ".sgf") {
#ifdef SCORING
			if (!boost::filesystem::exists(entry.path().string() + "-FINAL"))
				continue;
#endif
#ifdef SELF_PLAY
			if (entry.path().string().find("-scored") == std::string::npos)
				continue;
#endif
			paths.push_back(entry.path().string());
		}
	}

	// Put the data in a deterministic but random order.
	std::sort(paths.begin(), paths.end());
	std::minstd_rand0 generator(12345);
	std::shuffle(paths.begin(), paths.end(), generator);

	stop_index = std::min(stop_index, (int)paths.size());

	cout << "Found " << paths.size() << " SGF files." << endl;

	// Open the output files for writing.
	ofstream features_f(features_chunk_path, ios_base::out | ios_base::binary);
	filtering_ostream features_out;
	features_out.push(boost::iostreams::zlib_compressor());
	features_out.push(features_f);

	ofstream targets_f(targets_chunk_path, ios_base::out | ios_base::binary);
	filtering_ostream targets_out;
	targets_out.push(boost::iostreams::zlib_compressor());
	targets_out.push(targets_f);

	ofstream winners_f(winners_chunk_path, ios_base::out | ios_base::binary);
	filtering_ostream winners_out;
	winners_out.push(boost::iostreams::zlib_compressor());
	winners_out.push(winners_f);

#ifdef SCORING
	string territory_chunk_path = boost::replace_all_copy(winners_chunk_path, "winners", "territory");
	assert(territory_chunk_path != winners_chunk_path);
	ofstream territory_f(territory_chunk_path, ios_base::out | ios_base::binary);
	filtering_ostream territory_out;
	territory_out.push(boost::iostreams::zlib_compressor());
	territory_out.push(territory_f);
#endif

	for (int index = start_index; index < stop_index; index++) {
		string& path = paths[index];
		if ((index + 1) % 10000 == 0)
			printf("Processing %5i [%5i/%5i] %s\n", index, (index - start_index + 1), (stop_index - start_index), path.c_str());
//		cout << path << endl;
#ifdef SCORING
		write_all_samples(features_out, targets_out, winners_out, territory_out, path);
#else
		write_all_samples(features_out, targets_out, winners_out, path);
#endif
	}
}

