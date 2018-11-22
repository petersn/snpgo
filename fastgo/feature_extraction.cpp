// Feature extraction.

#include "feature_extraction.h"

void FeatureExtractor::add_move_to_history(Coord location) {
	move_history.push_front(location);
	if (move_history.size() > AGE_LAYERS)
		move_history.pop_back();
}

void FeatureExtractor::fill_features(uint8_t* feature_buffer, GoBoard& board, Player perspective_player) {
#define FEATURE(k) (feature_buffer[(BOARD_SIZE * BOARD_SIZE * (k)) + (x) + (y) * BOARD_SIZE])
	std::fill(feature_buffer, feature_buffer + TOTAL_FEATURES, 0);

	// Fill in the history features.
	int moves_ago = 0;
	for (Coord xy : move_history) {
		// The special move {-1, -1} is a dummy that we ignore.
		if (xy == {-1, -1})
			continue;
		int x = xy.first, y = xy.second;
		FEATURE(FEAT_HISTORY1 + moves_ago) = 1;
		moves_ago++;
	}

	// Fill in all the other features.
	for (int y = 0; y < BOARD_SIZE; y++) {
		for (int x = 0; x < BOARD_SIZE; x++) {
			Cell piece = piece_at(board, {x, y});
			assert(piece == 0 or piece == 1 or piece == 2);

			FEATURE(FEAT_ONES_PLANE)            = 1;
			FEATURE(FEAT_EMPTY_LOCATIONS_PLANE) = piece == 0;
			FEATURE(FEAT_P1_STONES)             = piece == (int)perspective_player;
			FEATURE(FEAT_P2_STONES)             = piece == (int)opponent_of(perspective_player);

			// If the piece is non-zero then lookup the liberty count, and write out feature maps.
			int liberties = (piece != 0) ? board.liberty_count({x, y}) : 0;
			assert(liberties > 0);
			liberties = std::min(liberties, MAX_LIBERTIES_FEATURE);
			FEATURE(FEAT_LIBERTIES1 + (liberties - 1)) = 1;

			for (auto layer_and_player : {
				std::pair<FeatureKind, Player>
				{FEAT_P1_PLAY_CAUSES_CAPTURE1, perspective_player},
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
					total_captured_stones = std::min(total_captured_stones, MAX_CAPTURES_FEATURE);
					if (total_captured_stones > 0)
						FEATURE(feature_layer + (total_captured_stones - 1)) = 1;
				}
			}

		} // loop over x
	} // loop over y

#undef FEATURE
}


