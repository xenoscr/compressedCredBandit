BOFNAME := compressedCredBandit
COMINCLUDE := -I ./include
LIBINCLUDE := 
CC_x64 := x86_64-w64-mingw32-gcc
CC=x86_64-w64-mingw32-clang

all:
	$(CC_x64) -o $(BOFNAME).x64.o $(COMINCLUDE) -Os -c $(BOFNAME).c -DBOF -masm=intel
	mv $(BOFNAME)*.o ../$(BOFNAME)

test:
	$(CC_x64) $(BOFNAME).c -g $(COMINCLUDE) $(LIBINCLUDE)  -o $(BOFNAME).x64.exe -masm=intel

scanbuild:
	$(CC) $(BOFNAME).c -o $(BOFNAME).scanbuild.exe $(COMINCLUDE) $(LIBINCLUDE) -masm=intel

check:
	cppcheck --enable=all $(COMINCLUDE) --platform=win64 $(BOFNAME).c

clean:
	rm $(BOFNAME).*.exe
