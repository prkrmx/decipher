#!/usr/bin/python3

import pyopencl as cl
import numpy as np
import sys
import time

mf = cl.mem_flags

#ctx = cl.create_some_context()
ctx = cl.create_some_context()
queue = cl.CommandQueue(ctx)

def revbits(x):
  return int(bin(x)[2:].zfill(64)[::-1], 2)


a = np.array([
        revbits(0x82649d956e0b4941),
        #0x094437620afdad7a,
	0x094437620afdad7a,
        0x0000000000000000,
        0x0000000000000000,
]
        , dtype=np.uint64)


a = np.zeros(4*64, dtype=np.uint64)

a[0] = revbits(0x82649d956e0b4941)
a[1] = 0x094437620afdad7a

#a[0] = revbits(0x4567b878c3ecf060)
#a[1] = 0xb4c691af9d9c32c3
#a[2] = revbits(0x4567b878c3ecf060)

 
x = time.time()
# create context buffers for a and b arrays
# for a (input), we need to specify that this buffer should be populated from a
a_dev = cl.Buffer(ctx, cl.mem_flags.READ_ONLY | cl.mem_flags.COPY_HOST_PTR, 
                    hostbuf=a) 

s = np.uint64(a.shape)

# compile the kernel
FILE_NAME=sys.argv[1]
f=open(FILE_NAME,"r")
SRC = ''.join(f.readlines())
f.close()

prg = cl.Program(ctx, SRC).build()

print("\n\nCracking the following data for a challenge:")

print("%X %X"%(a[0], a[1]))

# launch the kernel
event = prg.krak(queue, (1,), None, a_dev, s)
event.wait()
print("lag=%.3f"%(time.time()-x))
 
# copy the output from the context to the Python process
cl.enqueue_copy(queue, a, a_dev)
 
# if everything went fine, b should contain squares of integers
print("CL returned:")

print("%X %X %X %X"%(a[0], a[1], a[2], a[3]))
