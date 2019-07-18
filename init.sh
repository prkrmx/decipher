./paplon.py > paplon.log &

sleep 1

# we run a few delta lookups to achieve paralelism
./delta_client.py > delta_client1.log &
./delta_client.py > delta_client2.log &
./delta_client.py > delta_client3.log &

# and one or two ocl computations per GPU (because it takes ~200 ms for the GPU to process and then another ~30 ms for the host to create new GPU blob and we want to use the time inbetween, so we run two opencl queues
PYOPENCL_CTX=0 ./oclvankus.py > oclvankus1_1.log &
PYOPENCL_CTX=0 ./oclvankus.py > oclvankus1_2.log &
PYOPENCL_CTX=1 ./oclvankus.py > oclvankus2_1.log &
PYOPENCL_CTX=1 ./oclvankus.py > oclvankus2_2.log &
PYOPENCL_CTX=2 ./oclvankus.py > oclvankus3_1.log &
PYOPENCL_CTX=2 ./oclvankus.py > oclvankus3_2.log &

