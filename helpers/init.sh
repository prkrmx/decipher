
#!/bin/bash
tmpf=$(tempfile)
sudo /home/mainuser/status.sh -r 10.255.255.255 13000 &
#sudo /home/mainuser/deka.sh -r &