import glob,os,sys,time

# Usage:
#  - go to kraken/TableConvert
#  - make sure you have 3.5 GB of free space there
#  - run install.py /path/to/directory/containing/the/.dlt/files /dev/device_to_install

disk=sys.argv[2]
print("I am going to overwrite all data on your %s in 10 seconds!"%disk)
time.sleep(10)

files = glob.glob(sys.argv[1]+'/*.dlt')
files.sort()
ofs = 0
for f in files:
  num = f[-7:-4]
  cmd = "./TableConvert di %s %s:%i %s.idx"%(f, disk, ofs, num)
  print("running %s"%cmd)
  print("FLAG %s %i"%(num, ofs))
  if ofs>-1:
    #print("r")
    os.system(cmd)
  s = os.lstat("%s.idx"%num)
  size = (s.st_size/8)-1
  ofs += size
