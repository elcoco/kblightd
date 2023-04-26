SRC := src
OBJ := obj
CFLAGS := -g -Wall 
LIBS   := -lpthread -lm
CC := cc

SYSTEMD_UNIT_DIR?=/etc/systemd/system
INSTALL_PATH?=/usr/local
ASSETS_DIR?=assets

$(shell mkdir -p $(OBJ))
NAME := $(shell basename $(shell pwd))

SOURCES := $(wildcard $(SRC)/*.c)
OBJECTS := $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(SOURCES))

all: $(OBJECTS)
	$(CC) $^ $(CFLAGS) $(LIBS) -o $@ -o $(NAME)

install:
	cp $(NAME) $(INSTALL_PATH)/bin
	cp $(ASSETS_DIR)/$(NAME).service $(SYSTEMD_UNIT_DIR)
	sudo systemctl daemon-reload

$(OBJ)/%.o: $(SRC)/%.c
	$(CC) -I$(SRC) $(CFLAGS) $(LIBS) -c $< -o $@
