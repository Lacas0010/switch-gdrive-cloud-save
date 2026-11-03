CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O2 -I../../src -I../../source -I.
LDFLAGS ?= -lz

SRCS = test_main.cpp test_timestamp.cpp test_crc32_zip.cpp test_sync_decision.cpp test_rollback.cpp ../../source/ZipHelper.cpp ../../src/storage/zip_util.cpp ../../src/utils/time_util.cpp ../../src/utils/log.cpp
OBJS = $(SRCS:.cpp=.o)
TARGET = run_unit_tests

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf tests_tmp_zip tests_tmp_rollback
