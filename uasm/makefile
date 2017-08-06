OPTIONS = -std=gnu++11 -O3 -g -Wall
SRC=.
BUILD=.
OUT=uasm

all: assembler.o module.o importedmodule.o main.o
	g++ -o $(BUILD)/$(OUT) $^

assembler.o: $(SRC)/assembler.cpp
	g++ -c $(OPTIONS) $(BUILD)/$^

module.o: $(SRC)/module.cpp
	g++ -c $(OPTIONS) $(BUILD)/$^

importedmodule.o: $(SRC)/importedmodule.cpp
	g++ -c $(OPTIONS) $(BUILD)/$^

main.o: $(SRC)/main.cpp
	g++ -c $(OPTIONS) $(BUILD)/$^

clean:
	rm -f $(BUILD)/*.o
	rm -f $(BUILD)/$(OUT)

