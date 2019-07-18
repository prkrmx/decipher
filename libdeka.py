# class generator like namedtuple, but not immutable
# https://stackoverflow.com/questions/3648442/python-how-to-define-a-structure-like-in-c/3648450#3648450
def Struct(name, fields):
  fields = fields.split()
  def init(self, *values):
    for field, value in zip(fields, values):
      self.__dict__[field] = value
  cls = type(name, (object,), {'__init__': init})
  return cls

def toascii(s):
  return s.decode('ascii', 'ignore')

def fromascii(s):
  return s.encode('ascii', 'ignore')

def sendascii(req, msg):
  req.sendall(fromascii(msg))

def sendblob(req, blob):
  req.sendall(blob)

def getline(req):
  a = ''
  i = 0
  while 1:
    if len(a) > 0:
      if a[-1] == '\n':
        break
    b = req.recv(1)
    if not b:
      return None
    a += toascii(b)

  return a

def getdata(req, remaining):
  d = b''

  while remaining > 0:
    r = req.recv(max(remaining, 4096))
    remaining -= len(r)
    d += r

  return d

def encodemsg(header, data):
  s = int(len(data)) + " " + header
  return s + data

