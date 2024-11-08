
CFLAGS := -g -I. $(shell pkg-config --cflags  glib-2.0) 

libs := $(shell pkg-config  --libs glib-2.0) \
	-lmosquitto

mqomstat_OBJS=main.o asciiutils.o tty.o glib_extra.o mqoms.o glib-mqtt.o omnistat.o  utils.o

mqomstat: $(mqomstat_OBJS)
	gcc -o $@ $(mqomstat_OBJS) $(libs)

clean:
	rm -f *.o mqomstat
