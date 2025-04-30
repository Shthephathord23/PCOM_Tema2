CXX := g++
CPPFLAGS := -Iinclude
CXXFLAGS := -Wall -Wextra -g -std=c++11 -fPIC
LDFLAGS := -lm

SRC_DIR := src
LIB_DIR := lib
INC_DIR := include

SOURCES_SERVER := $(SRC_DIR)/server.cpp
SOURCES_SUBSCRIBER := $(SRC_DIR)/subscriber.cpp
SOURCES_COMMON := $(LIB_DIR)/common.cpp

OBJECTS_SERVER := $(notdir $(SOURCES_SERVER:.cpp=.o))
OBJECTS_SUBSCRIBER := $(notdir $(SOURCES_SUBSCRIBER:.cpp=.o))
OBJECTS_COMMON := $(notdir $(SOURCES_COMMON:.cpp=.o))

ALL_OBJECTS := $(OBJECTS_SERVER) $(OBJECTS_SUBSCRIBER) $(OBJECTS_COMMON)

SERVER_EXEC := server
SUBSCRIBER_EXEC := subscriber
BINARY := $(SERVER_EXEC) $(SUBSCRIBER_EXEC)

VPATH := $(SRC_DIR):$(LIB_DIR)

all: $(BINARY)

$(SERVER_EXEC): $(OBJECTS_SERVER) $(OBJECTS_COMMON)
	@echo "Linking $@..."
	$(CXX) $^ -o $@ $(LDFLAGS)  # Use CXX, $^ includes both prerequisites

$(SUBSCRIBER_EXEC): $(OBJECTS_SUBSCRIBER) $(OBJECTS_COMMON)
	@echo "Linking $@..."
	$(CXX) $^ -o $@ $(LDFLAGS) # Use CXX, $^ includes both prerequisites

%.o: %.cpp $(INC_DIR)/* Makefile
	@echo "Compiling $< (found via VPATH) --> $@"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@ # $< is the prerequisite (.cpp)

clean:
	@echo "Cleaning up..."
	rm -f $(ALL_OBJECTS) $(BINARY) core.* *~

.PHONY: all clean
