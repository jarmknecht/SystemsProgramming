#
# Student makefile for DNS resolver lab
# Note: requires a 64-bit x86-64 system 
#
# Makefile for the CS:APP Shell Lab

BYUNETID = NOBODY
VERSION = 1
HANDINDIR = /users/faculty/snell/CS324/handin/Fall2018/DNSResolver

CC = gcc
CFLAGS = -g

all: resolver

resolver: resolver.c
	$(CC) $(CFLAGS) -o resolver resolver.c -lm 

#
# Clean the src dirctory
#
clean:
	rm -f resolver

##################
# Handin your work
##################
handin:
	cp resolver.c $(HANDINDIR)/$(BYUNETID)-$(VERSION)-resolver.c


