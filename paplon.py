#!/usr/bin/python3

import struct
import time
import sys
import queue
import os
import math
import socket
import select
import re
import threading

from collections import namedtuple

from libdeka import *

from vankusconf import HOST, PORT, DEBUGDUMP

import socketserver

import pickle

jobstages = [ "submitted",   # user submitted keystream
              "dpsearch",    # worker is searching for distinguished points
              "endpoints",   # worker submitted computed endpoints
              "startsearch", # worker is looking startpoints up in tables
              "startpoints", # worker submitted startpoints
              "collsearch",  # worker is recomputing chain from the beginning, 
                             #  trying to find collisions
              "finished"     # everything done
            ]

jobptr = 0

lock = threading.Lock()

reportqs=[]

def saveblob(fname, blob):
  '''
  save burst to file for debugging
  '''

  if not DEBUGDUMP:
    return

  o=open("bursts/%s.pkl"%fname, 'wb')
  pickle.dump(blob, o, pickle.HIGHEST_PROTOCOL)
  o.close()

def report_thr(msgq, sock):
  """
  Reporting thread sending messages to client
  """

  while 1:
    # blocking queue get
    s = msgq.get()
    sendascii(sock, s)

JobT = Struct("Job", "num time stage keystream blob plaintext")
def Job(stime=time.time(), stage="submitted", keystream="", blob=bytes(), plaintext=[]):
  global jobptr
  lock.acquire()
  jobptr += 1
  myjob = jobptr
  stime = time.time()
  lock.release()
  return JobT(myjob, stime, stage, keystream, blob, plaintext)

jobs = {}


def getfjob(stage):
  """
  Return first job in the specified stage
  """
  global jobs

  for job in jobs.keys():
    if jobs[job].stage == stage:
      return job
  return None

def rq_crack(req, header):
  """
  Create new job from the crack command
  """
  global jobs

  keystream = header.split()[1]
  if not re.search("^[01]{114}$", keystream):
    sendascii(req, "Keystream must be exactly one GSM burst, i.e. 114 bits\r\n")
    return

  job = Job(keystream = keystream)

  sendascii(req, "Cracking #%i %s\r\n"%(job.num, job.keystream))

  if re.search("^[0]{114}$", keystream):
    sendascii(req, "crack #%i took 0 msec\n"%job.num)
    return

  lock.acquire()
  jobs[job.num] = job
  lock.release()

def rq_crackadd(req):
  """
  Create a reporting thread for the user that submitted a crack command
  """
  global reportqs

  q = queue.Queue()

  t = threading.Thread(target=report_thr, args=(q,req))
  t.start()

  reportqs.append(q)

def rq_getkeystream(req, header):
  """
  Return keystream for endpoint computation
  """

  lock.acquire()
  jobn = getfjob("submitted")

  if jobn == None:
    lock.release()
    sendascii(req, "-1 0\r\n")
  else:
    job = jobs[jobn]
    jobs[jobn].stage = "dpsearch"
    lock.release()
    sendascii(req, "%i %s\r\n"%(job.num, job.keystream))

def rq_putdps(req, header):
  """
  Receive computed endpoints
  """
  jobnum = int(header.split()[1])
  plen = int(header.split()[2])

  payload = getdata(req, plen)

  saveblob("%i-dps"%jobnum, payload)

  lock.acquire()
  jobs[jobnum].blob = payload
  jobs[jobnum].stage = "endpoints"
  lock.release()

def rq_getdps(req, header):
  """
  Send computed endpoints for table lookup
  """
  lock.acquire()
  jobn = getfjob("endpoints")

  if jobn == None:
    lock.release()
    sendascii(req, "-1 0\r\n")
  else:
    job = jobs[jobn]
    jobs[jobn].stage = "startsearch"
    lock.release()
    sendascii(req, "%i %i\r\n"%(job.num, len(job.blob)))
    sendblob(req, job.blob)

def rq_putstart(req, header):
  """
  Receive startpoints from tables
  """
  jobnum = int(header.split()[1])
  plen = int(header.split()[2])

  payload = getdata(req, plen)

  saveblob("%i-start"%jobnum, payload)

  lock.acquire()
  jobs[jobnum].blob = payload
  jobs[jobnum].stage = "startpoints"
  lock.release()

def rq_getstart(req, header):
  """
  Send startpoints for chain recovery
  """

  lock.acquire()
  jobn = getfjob("startpoints")

  if jobn == None:
    lock.release()
    sendascii(req, "-1 0\r\n")
  else:
    job = jobs[jobn]
    jobs[jobn].stage = "collsearch"
    lock.release()
    sendascii(req, "%i %s %i\r\n"%(job.num, job.keystream, len(job.blob)))
    sendblob(req, job.blob)

def rq_putkey(req, header):
  """
  Receive cracked key
  """
  jobnum = int(header.split()[1])
  keyinfo = ' '.join(header.split()[2:])

  for q in reportqs:
    q.put(keyinfo + "\r\n")

def rq_finished(req, header):
  """
  Receive message that a job has been finished
  """
  jobnum = int(header.split()[1])
  jobs[jobnum].stage = "finished"

  for q in reportqs:
    q.put("crack #%i took %i msec\r\n"%(jobnum, (time.time() - jobs[jobnum].time) * 1000))

  lock.acquire()
  del(jobs[jobnum])
  lock.release()

def rq_stats(req, header):
  """
  Print server performance info
  """
  global jobs

  lock.acquire()
  cnts = {}
  for stage in jobstages:
    cnts[stage] = 0

  for job in jobs:
    cnts[jobs[job].stage] += 1

  lock.release()

  for stage in jobstages:
    sendascii(req, "%s: %i\r\n"%(stage, cnts[stage]))

def rq_unknown(req, header):
  """
  Command was not understood
  """
  cmd = ""
  if len(header.split()) > 0:
    cmd = header.split()[0]
  sendascii(req, "Unknown command %s\r\n"%cmd)

class ThreadedTCPRequestHandler(socketserver.BaseRequestHandler):

  """
  TCP server implementation from https://docs.python.org/3/library/socketserver.html example
  """

  def handle(self):
    """
    New thread for each client
    """
    print("Connect from %s:%i"%self.request.getpeername())

    crackadded = 0

    # just process requests from client infinitely
    while 1:

      # read request header
      header = getline(self.request)

      if not header:
        print("Disconnect %s:%i"%self.request.getpeername())
        self.request.close()
        break

      # decide what type it is and process accordingly
      rtype = ""
      if len(header.split()) > 0:
        rtype = header.split()[0]

      #print("DEBUG "+header)
      if rtype == "crack":
        rq_crack(self.request, header)
        if crackadded == 0:
          rq_crackadd(self.request)
        crackadded = 1
      elif rtype == "getkeystream":
        rq_getkeystream(self.request, header)
      elif rtype == "putdps":
        rq_putdps(self.request, header)
      elif rtype == "getdps":
        rq_getdps(self.request, header)
      elif rtype == "putstart":
        rq_putstart(self.request, header)
      elif rtype == "getstart":
        rq_getstart(self.request, header)
      elif rtype == "putkey":
        rq_putkey(self.request, header)
      elif rtype == "finished":
        rq_finished(self.request, header)
      elif rtype == "stats":
        rq_stats(self.request, header)
      else:
        rq_unknown(self.request, header)

class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
  # some weird "address already in use" after unclean shutdown
  allow_reuse_address = True

# bind to socket and start accepting clients
server = ThreadedTCPServer((HOST, PORT), ThreadedTCPRequestHandler)
server.serve_forever()


