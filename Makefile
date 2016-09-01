CC=g++
CFLAGS= -Wall -lrt -pthread -lm -lbcm2835
DEPS=adcio.h

ibike: ibike.cpp
	@echo "Compiling..."
	$(CC) -o ibike ibike.cpp $(CFLAGS)
	@echo "Done."

dummy: dummy.cpp
	@echo "Compiling..."
	$(CC) -o dummy dummy.cpp $(CFLAGS)
	@echo "Done."

clean:
	@echo "Removing compiled objects and binaries..."
	@rm -f *.o ibike
	@echo "Done."

install:
	@echo "Terminating running instances..."
	@if pgrep ibike; then pkill -2 ibike; fi
	@if pgrep ibike; then pkill -9 ibike; fi
	@echo "Copying binary to /usr/local/bin/ibike ..."
	@cp ibike /usr/local/bin/ibike
	@echo "Setting ownership and permissions..."
	@chown root:root /usr/local/bin/ibike
	@chmod +x+s /usr/local/bin/ibike
	@echo "Done."

