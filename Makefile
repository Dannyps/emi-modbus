main.o: build emi-read.c build/light-modbus.o build/light-modbus-rtu.o 
	gcc -g emi-read.c build/light-modbus.o build/light-modbus-rtu.o -lpaho-mqtt3c -lm -o build/emi-read

build/light-modbus.o: build light-modbus/light-modbus.c light-modbus/light-modbus.h
	gcc -g -c light-modbus/light-modbus.c -o build/light-modbus.o

build/light-modbus-rtu.o: build light-modbus/light-modbus-rtu.c light-modbus/light-modbus-rtu.h
	gcc -g -c light-modbus/light-modbus-rtu.c -o build/light-modbus-rtu.o

build: 
	mkdir build

.PHONY: clean

clean:
	rm -rf build