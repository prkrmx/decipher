# DECIPHER
## GSM decipher based on the projects:
  - Deka - An OpenCL A5/1 cracker https://brmlab.cz/project/gsm/deka/start
  - Kraken - Decrypting GSM phone calls https://srlabs.de/bites/decrypting-gsm
  - Rainbow Tables https://opensource.srlabs.de/projects/a51-decrypt/files

### Prerequisites
The following prerequisites are required to build and run the project
- AMD CATALYST
- OPENCL
- AMD SDK
- CAL++
- PYRIT
```
sudo apt-get update
sudo apt-get install python3-dev swig3.0 python-pyopencl socket -y
```
### Configure and Build
Obtaining the Source
```
git clone https://github.com/prkrmx/decipher.git
```
Config the tables
```
cd decipher/TableConvert/
make
cp TableConvert ../indexes/ 
cd ../indexes/
cp tables.conf.sample tables.conf
```
Edit the `tables.conf` where to store tables raw. In my case, I'm going to put all 40 tables in the device `/dev/nvme1n1`
```
Device: /dev/nvme1n1 40
```
Start the indexing process. It will take a couple of hours depending on your computer. 
In my case, table source: `/media/mainuser/rbt_src`
```
sudo ./Behemoth.py /media/mainuser/rbt_src
```
