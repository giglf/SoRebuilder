# This project develop platform have been changed to linux.
# Because of some strange issuses with g++ parameter '-m32' under windows.

CPP_SOURCES = $(wildcard src/*.cpp)
HEADERS = $(wildcard src/*.h)

OBJS = ${CPP_SOURCES:.cpp=.o}

TARGET = sb

CC = g++
CFLAGS = -g -std=c++11 -m32


$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.o : %.cpp $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@


# make clean cannot work under windows powershell or cmd
# it report
# # process_begin: CreateProcess(NULL, rm sb.exe src/main.o src/ELFReader.o, ...) failed.
# # make (e=2): 系统找不到指定的文件。
# # Makefile:23: recipe for target 'clean' failed
# # make: *** [clean] Error 2
# not because of rm under powershell need ',' to seperate each item.
# I have tried that it didn't work yet.
# 
# Thank to my friend help. I have try add double quote to the rm command.
# But it shows
# # "rm sb.exe src/main.o src/ELFReader.o"
# # /usr/bin/bash: rm sb.exe src/main.o src/ELFReader.o: No such file or directory
# # Makefile:30: recipe for target 'clean' failed
# # make: *** [clean] Error 127
# Don't know why it would enter bash from powershell. That mean wrong directory.
# Google say it was because the PATH of "git/bin/bash"
# I tried delete bash in git/bin. It show "make (e=2)" again.
#
# My suggestion is, just leave it...Don't use windows...(╯°□°)╯︵ ┻━┻

clean:
	rm $(TARGET) $(OBJS)
