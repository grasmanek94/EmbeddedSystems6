CC=/usr/local/xtools/arm-unknown-linux-uclibcgnueabi/bin/arm-unknown-linux-uclibcgnueabi-gcc
CFLAGS= -Wall -O2 -std=c99
LDFLAGS= 

PROG= ass2-app
OBJ= main.o

.PHONY: all clean install

all: $(PROG)

$(PROG): $(OBJ)
	 $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

%.o: %.c
	 $(CC)  $(CFLAGS) $(LDFLAGS) -c $< -o $@

clean:
	-rm -rf $(PROG) $(OBJ)

install: $(PROG)
	mkdir -p $(prefix)/sbin/
	cp $(PROG) $(prefix)/sbin/
