# tables we have
# yeah it's in this silly order
mytables = [380, 220, 100,108,116,124,132,140,148,156,164,172,180,188,196,204,212,230,238,250,260,268,276,292,324,332,340,348,356,364,372,388,396,404,412,420,428,436,492,500]

# server host and port
HOST, PORT = "localhost", 6666

# how many kernels to run in parallel
kernels = 8191
# XXX 4095
# slices per kernel
slices = 32

# dump computed bursts to files for later analysis - useful for bug hunting
DEBUGDUMP = False
