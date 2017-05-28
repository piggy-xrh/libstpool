.PHONE: all clean

all:demo

demo:demo.cpp
	g++ -o $@ $< -lstpool -lstpoolc++ -lmsglog -lpthread

clean:
	-@rm -f demo *.o
