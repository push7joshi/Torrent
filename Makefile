CC=g++
CPFLAGS=-g -Wall -std=gnu++0x
LDFLAGS= -lcrypto


SRC= bencode.cpp  bt_client.cpp bt_lib.cpp bt_setup.cpp 
OBJ=$(SRC:.cpp=.o)
BIN=bt_client

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CPFLAGS)  -o $(BIN) $(OBJ) $(LDFLAGS)


%.o:%.cpp
	$(CC) -c $(CPFLAGS) -o $@ $<  

$(SRC):

clean:
	rm -rf $(OBJ) $(BIN)
