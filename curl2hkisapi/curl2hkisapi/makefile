
all:
	gcc -O3 main.c Thermometry.c -lcurl -lssl -lcrypto -lpthread

gm:
	arm-unknown-linux-uclibcgnueabi-gcc -O3 main.c Thermometry.c -lcurl -lcares -lpthread
	mv ./a.out ~/nfs_szy/gm8136netaudio/

clean:
	rm *.out
