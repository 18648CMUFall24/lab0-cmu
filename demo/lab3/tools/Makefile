include ../app.mk

TEAM=14

APPS=\
	 fcalc \
	 sloppy \
	 holdopen \
	 openerrno \
	 seekerrno \
	 writeerrno \
	 chunkedcat \
	 concurrentreader \
	 count \
	 sleeploop \
	 busyloop \
	 ta_calc \
	 periodic \
	 papp \
	 rsvmgr 

#KERNEL_HEADERS=../archives-lab$(LAB)/team$(TEAM)/usr/include
#KERNEL_HEADERS=../../../team$(TEAM)/kernel/usr/include
KERNEL_HEADERS=../../../kernel/usr/include

kernel_headers:
	#if [ -z "$(LAB)" ]; then echo "Env var not set: LAB"; fi
	if [ -z "$(TEAM)" ]; then echo "Env var not set: TEAM"; fi
	test -d $(KERNEL_HEADERS)

all: $(APPS)

clean:
	@rm -rf *.o $(APPS)

lab1: 
	@make clean
	@LAB=1
	@make ta_calc
	@make fcalc
	@make holdopen
	@make openerrno
	@make writeerrno
	@make seekerrno
	@make chunkedcat
	@make concurrentreader
	@make sloppy

lab2:
	@make clean
	@LAB=2
	@make periodic
	@make busyloop
	@make sleeploop
	@make fcalc

lab3:
	@make clean
	@LAB=3
	@make rsvmgr
	@make busyloop
	@make count 
	@make fcalc
	@make sleeploop

lab4:
	@make clean
	@LAB=4
	@make busyloop
	@make fcalc
	@make rsvmgr


ta_calc: ta_calc.c
	$(CC) $(CFLAGS) -I$(KERNEL_HEADERS) -o $@ $< $(LDFLAGS) -lm

fcalc: fcalc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lm

sloppy: sloppy.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

holdopen: holdopen.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

openerrno: openerrno.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

writeerrno: writeerrno.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

seekerrno: seekerrno.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

chunkedcat: chunkedcat.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

concurrentreader: concurrentreader.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -pthread

papp: papp.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ $< $(LDFLAGS)

job: job.c
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ $< $(LDFLAGS)

rsvmgr: rsvmgr.c
	$(CC) $(CFLAGS) -I$(KERNEL_HEADERS) -D_GNU_SOURCE -o $@ $< $(LDFLAGS)

busyloop: busyloop.c
	$(CC) $(CFLAGS) -I$(KERNEL_HEADERS) -o $@ $< $(LDFLAGS)

busysig: busysig.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

sleeploop: sleeploop.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

periodic: periodic.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test: test.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

count: kernel_headers count.c
	$(CC) $(CFLAGS) -I$(KERNEL_HEADERS) -o $@ count.c $(LDFLAGS)
