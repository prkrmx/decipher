#!/usr/bin/python3

import socket
import sys
import delta
import time

from libdeka import *

import delta

from vankusconf import HOST, PORT

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

sock.connect((HOST, PORT))

delta.delta_init()

d = bytes()
y = bytearray()

while 1:

  sock.sendall(bytes("getdps\r\n", "ascii"))

  header = getline(sock)

  jobnum = int(header.split()[0])
  plen = int(header.split()[1])


  if plen == 0:
    time.sleep(1)
  else:
    print(header)
 
    d = getdata(sock, plen)

    # convert to mutable bytearray, some swig magic
    y = bytearray(d)

    x=time.time()

    # find blocks we will need and madvise them
    delta.ncq_submit(y)

    print("submit took: %f s"%(time.time()-x))
    x=time.time()

    print("process")

    # read the madvised blocks
    delta.ncq_read(y)

    print("process took: %f s"%(time.time()-x))

    sendascii(sock, "putstart %i %i\r\n"%(jobnum, plen))

    sendblob(sock, y)

