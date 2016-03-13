#!/usr/bin/python

import sys
import engine

sys.stderr = open("/tmp/snpgo_stderr", "w")

command_list = ["name", "protocol_version", "version", "list_commands", "play", "genmove"]
command_list.extend(["known_command", "quit", "boardsize", "clear_board", "komi"])
valid_ranks = "abcdefghjklmnopqrst"
assert len(valid_ranks) == 19 and sorted(valid_ranks) == list(valid_ranks)

def color_code_to_player_number(s):
	return {"b": 0, "w": 1}[s.lower()[0]]

def code_to_xy(code):
	code = code.lower()
	letter = code[0]
	return valid_ranks.index(letter), int(code[1:]) - 1

def xy_to_code(xy):
	x, y = xy
	return valid_ranks[x] + str(y + 1)

class DebugStream:
	def __init__(self, debug_path):
		self.do_debug = debug_path != None
		if self.do_debug:
			self.debug_file = open(debug_path, "w")

	def write(self, s):
		if not self.do_debug:
			return
		self.debug_file.write(s)
		self.debug_file.flush()

	def flush(self, s):
		self.debug_file.flush()

class GTPHandler:
	def __init__(self, engine_factory, debug_path=None):
		self.engine_factory = engine_factory
		self.debug_stream = DebugStream(debug_path)
		self.komi = 0.0
		self.board_size = 19
		self.reset()

	def reset(self):
		self.engine = self.engine_factory(self.board_size)
		self.engine.debug_stream = self.debug_stream

	def send(self, s):
		print >>self.debug_stream, ">>> " + s
		sys.stdout.write(s)
		sys.stdout.flush()

	def handle(self):
		while True:
			line = sys.stdin.readline()
			print >>self.debug_stream, line
			line = line.strip()
			if not line:
				continue
			args = line.split(" ")
			if args[0] == "name":
				self.send("= snpgo\n\n")
			elif args[0] == "protocol_version":
				self.send("= 2\n\n")
			elif args[0] == "version":
				self.send("= 0.01\n\n")
			elif args[0] == "list_commands":
				self.send("= %s\n\n" % "\n".join(command_list))
			elif args[0] == "boardsize":
				board_size = int(args[1])
				self.reset()
				self.send("=\n\n")
			elif args[0] == "komi":
				komi = float(args[1])
				self.send("=\n\n")
			elif args[0] == "clear_board":
				self.reset()
				self.send("=\n\n")
			elif args[0] == "quit":
				self.send("=\n\n")
				exit()
			elif args[0] == "play":
				player = color_code_to_player_number(args[1])
				if args[2].lower() == "pass":
					self.engine.player_passes(player)
				elif args[2].lower() == "resign":
					# Huzzah, we win!
					print >>self.debug_stream, "We win by resignation!"
				else:
					xy = code_to_xy(args[2])
					self.engine.play(player, xy)
				self.send("=\n\n")
			elif args[0] == "genmove":
				player = color_code_to_player_number(args[1])
				xy = self.engine.genmove(player)
				# Check if the location is actually "pass" or "resign"
				if isinstance(xy, str):
					self.send("= %s\n\n" % xy)
				else:
					self.send("= %s\n\n" % xy_to_code(xy))

handler = GTPHandler(engine.Engine, debug_path="/home/snp/proj/snpgo/dump.txt")
handler.handle()

