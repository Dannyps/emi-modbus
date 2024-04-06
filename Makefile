CFLAGS = -O2 -Wall

main.o: build emi-read.c build/light-modbus.o build/light-modbus-rtu.o 
	$(CC) $(CFLAGS) emi-read.c build/light-modbus.o build/light-modbus-rtu.o -lpaho-mqtt3c -lsystemd -lm -o build/emi-read

build/light-modbus.o: build light-modbus/light-modbus.c light-modbus/light-modbus.h
	$(CC) $(CFLAGS) -c light-modbus/light-modbus.c -o build/light-modbus.o

build/light-modbus-rtu.o: build light-modbus/light-modbus-rtu.c light-modbus/light-modbus-rtu.h
	$(CC) $(CFLAGS) -c light-modbus/light-modbus-rtu.c -o build/light-modbus-rtu.o

build: 
	mkdir build

.PHONY: clean

clean:
	rm -rf build