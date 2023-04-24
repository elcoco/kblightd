SRC := src
OBJ := obj
CFLAGS := -g -Wall 
LIBS   := -lX11 -lXtst -lpthread -lsystemd -lm
CC := cc

INSTALL_PATH?=/usr/local

$(shell mkdir -p $(OBJ))
NAME := $(shell basename $(shell pwd))

SOURCES := $(wildcard $(SRC)/*.c)
OBJECTS := $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(SOURCES))

all: $(OBJECTS)
	$(CC) $^ $(CFLAGS) $(LIBS) -o $@ -o $(NAME)

install:
	cp $(NAME) $(INSTALL_PATH)/bin

$(OBJ)/%.o: $(SRC)/%.c
	$(CC) -I$(SRC) $(CFLAGS) $(LIBS) -c $< -o $@
