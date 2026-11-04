# Host-Tests (PC, g++): NMEA-Parser, CRC32, Wear-Leveling Ringpuffer mit simuliertem M95P08
# Aufruf:  make -C tests/host
FW   := ../../firmware/ChainCounter
SRC  := build/recordlog.cpp build/crc32.cpp build/nmea.cpp

test: build/test
	./build/test

build/test: test_main.cpp mock/*.h $(FW)/*.cpp $(FW)/*.h
	mkdir -p build
	cp $(FW)/recordlog.* $(FW)/crc32.* $(FW)/nmea.* $(FW)/records.h build/
	cp mock/*.h build/
	g++ -std=gnu++17 -Wall -Ibuild -o $@ test_main.cpp $(SRC)

clean:
	rm -rf build
