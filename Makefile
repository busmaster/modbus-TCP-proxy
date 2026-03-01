

CC = gcc
CFLAGS = -Wall -g
TARGET = modbus-proxy
LIBS =
all: $(TARGET)

$(TARGET): modbus-proxy.cpp
	$(CC) $(CFLAGS) -o $(TARGET) modbus-proxy.cpp $(LIBS)

# Aufräumen: Erstellte Dateien löschen
clean:
	rm -f $(TARGET)
