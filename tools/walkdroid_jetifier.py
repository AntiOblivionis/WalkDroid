"""Repair stale references after reverse Jetifier transforms an AndroidX class.

Some support-library source files already use the old package, so Jetifier
leaves those class files unchanged even when referenced classes are renamed.
Only the outputs compiled from source are modified.
"""
from collections import defaultdict
from io import BytesIO
from pathlib import Path
import re,struct,zipfile

def classes_in(path):
 with zipfile.ZipFile(path) as a:
  data=a.read('classes.jar') if path.suffix=='.aar' else path.read_bytes()
 with zipfile.ZipFile(BytesIO(data)) as jar:
  return {n[:-6] for n in jar.namelist() if n.endswith('.class')}

def renamed_classes(pairs):
 mappings={}
 ambiguous=set()
 for original,converted in pairs:
  before=classes_in(original);after=classes_in(converted)
  byname=defaultdict(list)
  for n in after:byname[n.rsplit('/',1)[-1]].append(n)
  for name in before:
   options=byname[name.rsplit('/',1)[-1]]
   if len(options)!=1 or name==options[0]:continue
   value=options[0]
   if name in mappings and mappings[name]!=value:ambiguous.add(name)
   else:mappings[name]=value
 for n in ambiguous:mappings.pop(n,None)
 return mappings

def change_class(data,replace_value):
 if data[:4]!=b'\xca\xfe\xba\xbe':return data
 count=struct.unpack_from('>H',data,8)[0]
 out=bytearray(data[:10]);offset=10;i=1
 while i<count:
  tag=data[offset];out.append(tag);offset+=1
  if tag==1:
   n=struct.unpack_from('>H',data,offset)[0];offset+=2
   value=data[offset:offset+n];offset+=n
   value=replace_value(value)
   out.extend(struct.pack('>H',len(value)));out.extend(value)
  else:
   sizes={3:4,4:4,5:8,6:8,7:2,8:2,9:4,10:4,11:4,12:4,15:3,16:2,17:4,18:4,19:2,20:2}
   size=sizes[tag];out.extend(data[offset:offset+size]);offset+=size
   if tag in (5,6):i+=1
  i+=1
 out.extend(data[offset:])
 return bytes(out)

def rewrite_zip(data,replace_value,outer=False):
 out=BytesIO()
 with zipfile.ZipFile(BytesIO(data)) as source,zipfile.ZipFile(out,'w') as target:
  for info in source.infolist():
   item=source.read(info.filename)
   if info.filename.endswith('.class'):item=change_class(item,replace_value)
   if outer and info.filename=='classes.jar':item=rewrite_zip(item,replace_value)
   target.writestr(info,item)
 return out.getvalue()

def repair(pairs):
 mappings=renamed_classes(pairs)
 rules={}
 for old,new in mappings.items():
  for a,b in ((old,new),(old.replace('/','.'),new.replace('/','.'))):
   rules[a.encode()]=b.encode()
 pattern=re.compile(rb'androidx[/.][A-Za-z0-9_/$\.]+')
 def replace_value(value):
  def replace(match):
   key=match.group()
   if key in rules:return rules[key]
   if b'$' in key:
    base,suffix=key.split(b'$',1)
    if base in rules:return rules[base]+b'$'+suffix
   return key
  return pattern.sub(replace,value)
 for _,out in pairs:out.write_bytes(rewrite_zip(out.read_bytes(),replace_value,out.suffix=='.aar'))
 print('Repaired references for',len(mappings),'renamed classes in',len(pairs),'generated libraries',flush=True)
