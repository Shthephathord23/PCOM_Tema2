CXX := g++
CPPFLAGS := -Iinclude
# Consider adding -Wconversion or other warnings if desired
CXXFLAGS := -Wall -Wextra -g -std=c++17 -fPIC
LDFLAGS := -lm # Math library needed for pow in server_udp.cpp

# Directories
SRC_DIR := src
LIB_DIR := lib
INC_DIR := include
# No OBJ_DIR needed

# Source Files
SOURCES_SERVER := \
	$(SRC_DIR)/server_main.cpp \
	$(SRC_DIR)/server_network.cpp \
	$(SRC_DIR)/server_connection.cpp \
	$(SRC_DIR)/server_udp.cpp \
	$(SRC_DIR)/server_client.cpp \
	$(SRC_DIR)/server_topic.cpp

SOURCES_SUBSCRIBER := \
	$(SRC_DIR)/subscriber_main.cpp \
	$(SRC_DIR)/subscriber_network.cpp \
	$(SRC_DIR)/subscriber_io.cpp

SOURCES_COMMON := \
	$(LIB_DIR)/common.cpp \
	$(LIB_DIR)/circular_buffer.cpp

# Object Files (derived from source files, placed in current directory)
# Use $(notdir ...) to get the basename, then change suffix
OBJECTS_SERVER := $(patsubst %.cpp,%.o,$(notdir $(SOURCES_SERVER)))
OBJECTS_SUBSCRIBER := $(patsubst %.cpp,%.o,$(notdir $(SOURCES_SUBSCRIBER)))
OBJECTS_COMMON := $(patsubst %.cpp,%.o,$(notdir $(SOURCES_COMMON)))

# Combined list for cleaning
ALL_OBJECTS := $(OBJECTS_SERVER) $(OBJECTS_SUBSCRIBER) $(OBJECTS_COMMON)

# Executables
SERVER_EXEC := server
SUBSCRIBER_EXEC := subscriber
BINARY := $(SERVER_EXEC) $(SUBSCRIBER_EXEC)

# VPATH tells make where to look for source files (.cpp)
VPATH := $(SRC_DIR):$(LIB_DIR)

# Default target: Build both executables
all: $(BINARY)

# --- Build Rules ---

# Link Server Executable
$(SERVER_EXEC): $(OBJECTS_SERVER) $(OBJECTS_COMMON)
	@echo "Linking $@..."
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# Link Subscriber Executable
$(SUBSCRIBER_EXEC): $(OBJECTS_SUBSCRIBER) $(OBJECTS_COMMON)
	@echo "Linking $@..."
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# Pattern Rule to Compile C++ Source Files into Object Files in Current Directory
# This rule finds the source file (.cpp) using VPATH based on the target (.o) pattern.
# It depends on the source file, relevant headers, and the Makefile itself.
%.o: %.cpp $(wildcard $(INC_DIR)/*.h) Makefile
	@echo "Compiling $< --> $@"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@


# --- Cleanup Rule ---
clean:
	@echo "Cleaning up..."
	rm -f $(ALL_OBJECTS) $(BINARY) core.* *~
	# No object directory to remove

# --- Phony Targets ---
# Declare targets that are not actual files
.PHONY: all clean