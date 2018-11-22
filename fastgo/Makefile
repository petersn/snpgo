
CXXFLAGS=-std=c++17 -g3 -O3 -Wall -Wextra -fPIC
LIBS=-lboost_iostreams -lboost_filesystem -lboost_system

#all: feature_extraction.o

all: sgf_to_chunks

#all: libfastgo.so sgf_to_chunks scan_directory

libfastgo.so: sgf_to_chunks.o go_utils.o Makefile
	$(CXX) $(CXXFLAGS) -shared -Wl,-soname,$@ -o $@ sgf_to_chunks.o go_utils.o $(LIBS)

sgf_to_chunks: sgf_to_chunks.o go_utils.o Makefile
	$(CXX) $(CXXFLAGS) -o $@ sgf_to_chunks.o go_utils.o $(LIBS)

scan_directory: scan_directory.o go_utils.o Makefile
	$(CXX) $(CXXFLAGS) -o $@ scan_directory.o go_utils.o $(LIBS)

.PHONY: clean
clean:
	rm -f *.o libgo_utils.so

