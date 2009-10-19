#!/usr/bin/python
# -*- coding: utf-8 -*-

import sys, time, os, struct

FILE_MAGIC = 6893732045233840104
FMT = "=QIQQ"

def read_stamps(start):
    fn = "%016x.stamps" % start
    stat = os.stat(fn)
    filesize = stat.st_size
    headersize = struct.calcsize(FMT)
    header = open(fn).read(headersize)
    assert len(header) == headersize
    #print repr(header), len(header)
    magic, rate, interval, start2 = struct.unpack(FMT, header)
    #print "rate:",rate
    #print "interval:",interval
    #print "start:", start
    #print "start2:", start2
    assert magic == FILE_MAGIC
    assert start == start2
    length = ((filesize-headersize)/8 -1) * interval
    print "length:",length*1e-6
    return (rate, interval, length)

def get_frame(start, offset):
    fn = "%016x.stamps" % start
    offset = int(offset)
    if offset == 0: return 0
    fd = open(fn)
    headersize = struct.calcsize(FMT)
    fd.seek(headersize+offset*8)
    ret = fd.read(8)
    ret, = struct.unpack("Q", ret)
    return ret;
    
def sec_to_mp3splt(sec):
    min = int(sec / 60)
    s   = int(sec) % 60
    hun = int(round(sec%1, 2)*100)
    return "%d.%d.%d" % (min, s, hun)

def main(start, duration):
    print "looking for recording starting %d and lasting %fs" %(start,duration*1e-6)
    starttimes = [int(f.split(".")[0], 16) for f in os.listdir(".")
                  if f.endswith(".stamps")]
    starttimes = [s for s in starttimes
                  if s <= start]
    starttimes.sort()
    print "got starttimes",starttimes
    for s in starttimes:
        (rate, interval, length) = read_stamps(s)
        if length+s > start:
            offset = start-s
            print s,"contains start-time at offset", offset*1e-6
            clipped = False
            if length-offset < duration:
                print "duration clipped to", (length-offset)*1e-6
                duration = length-offset
                clipped = True
            startframe = get_frame(s, offset/interval);
            rest = offset % interval
            startframe += int((rest*rate)/1e6)
            print "audio starts after frame",startframe,float(startframe)/rate
            
            endframe = get_frame(s, (offset+duration)/interval)
            rest = (offset+duration)%interval
            endframe += int((rest*rate)/1e6);
            print "audio ends after frame",endframe, float(endframe)/rate
            
            startstr = sec_to_mp3splt(float(startframe)/rate)
            if clipped:
                endstr = "EOF"
            else:
                endstr = sec_to_mp3splt(float(endframe)/rate)
            print
            print "******************************"
            print "The command below should give you what you want"
            print
            print "mp3splt %016x.mp3 %s %s" % (s, startstr, endstr)
            
            return
            
def usage():
    print "Usage: %s YYYY-mm-dd-HH:MM:SS[.µµµµµµ] s[.µs]" % sys.argv[0]
    print "example for getting 1 hour after 10:00:"
    print "%s 2009-10-19-10:00 3600" % sys.argv[0]

if __name__ == "__main__":
    if len(sys.argv) != 3:
        usage()
        sys.exit(1);
    split = sys.argv[1].split(".")
    if len(split) == 1:
        date = split[0]
        start = 0
    else:
        date, us = split
        start = long(float("0."+us)*1000000)
    date = time.strptime(date, "%Y-%m-%d-%H:%M:%S")
    s = time.mktime(date)
    start += long(s)*1000000

    split = sys.argv[2].split(".")
    if len(split) == 1:
        s = int(split[0])
        duration = s*1000000;
    else:
        s, us = split
        duration = long((float("0."+us)+int(s))*1000000)
    main(start, duration);
