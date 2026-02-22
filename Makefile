all: httpd client
LIBS = -lpthread #-lsocket
httpd: httpd_linux.c                    # 修改成自主文件httpd_linux.c
	gcc -g -W -Wall $(LIBS) -o $@ $<

client: simpleclient.c
	gcc -W -Wall -o $@ $<
clean:
	rm -f httpd client
