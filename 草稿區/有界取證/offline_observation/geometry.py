"""Decode bounded V2 observations and conditionally clip their ordered triangles.

Never adopts upload content, current state, GPU coverage or pixel contribution.
"""
import hashlib, math, struct
from projection import clip_points
DOMAIN=b'GEHD_INDEXED_ATTRIBUTES_V2\0'
FIELDS=((0,0,2,12),(5,0,1,8),(10,0,4,4),(10,1,4,4),(3,0,2,12))
CAP=8192

def decode(receipt):
 if not receipt.get('attempted') or not receipt.get('complete') or receipt.get('reason')!='NONE':raise ValueError('incomplete_receipt')
 if receipt.get('schema')!='GEHD_SELECTED_CANONICAL_V1':raise ValueError('receipt_schema')
 n=receipt.get('bytes');text=receipt.get('hex','')
 if not isinstance(n,int) or not 1<=n<=CAP or not isinstance(text,str) or len(text)!=2*n:raise ValueError('byte_bounds')
 try:raw=bytes.fromhex(text)
 except ValueError:raise ValueError('hex')
 if len(raw)!=n or hashlib.sha256(raw).hexdigest().upper()!=receipt.get('sha256'):raise ValueError('identity')
 if not raw.startswith(DOMAIN) or len(raw)<len(DOMAIN)+8:raise ValueError('domain')
 mask,count=struct.unpack_from('<II',raw,len(DOMAIN))
 if mask&~31 or mask&3!=3 or mask&8 and not mask&4:raise ValueError('field_mask')
 if not 3<=count<=4096 or count%3:raise ValueError('triangle_count')
 fields=[(i,v) for i,v in enumerate(FIELDS) if mask&(1<<i)]
 offset=len(DOMAIN)+8;stride=sum(v[3] for _,v in fields)
 if len(raw)!=offset+4*len(fields)+count*stride:raise ValueError('layout_length')
 for _,v in fields:
  if raw[offset:offset+4]!=bytes(v):raise ValueError('field_descriptor')
  offset+=4
 vertices=[]
 for _ in range(count):
  row={}
  for i,(_,_,_,size) in fields:
   val=struct.unpack_from('<'+('I' if i in (2,3) else 'f'*(size//4)),raw,offset);offset+=size
   if i not in (2,3) and not all(math.isfinite(x) for x in val):raise ValueError('nonfinite_attribute')
   row[i]=val
  vertices.append(row)
 return dict(raw=raw,fieldMask=mask,indexCount=count,vertices=vertices,positions=[r[0] for r in vertices])

def distances(v):
 x,y,z,w=v
 return (w+x,w-x,w+y,w-y,z,w-z)

def clip_triangle(points):
 if len(points)!=3 or any(len(v)!=4 or not all(math.isfinite(x) for x in v) for v in points):raise ValueError('clip_triangle_input')
 scale=max(1.0,max(abs(x) for v in points for x in v));epsilon=scale*1e-6
 polygon=[list(v) for v in points]
 for plane in range(6):
  if not polygon:break
  output=[];a=polygon[-1];da=distances(a)[plane]
  for b in polygon:
   db=distances(b)[plane]
   if (da>=0)!=(db>=0):
    t=da/(da-db);output.append([a[i]+t*(b[i]-a[i]) for i in range(4)])
   if db>=0:output.append(b)
   a,da=b,db
  polygon=output
  if len(polygon)>16:raise ValueError('polygon_cap')
 ndc=None;area=None
 if not polygon:kind='outside'
 elif any(v[3]<=epsilon for v in polygon):kind='singular_or_near_eye'
 else:
  ndc=[[v[i]/v[3] for i in range(3)] for v in polygon]
  area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(ndc,ndc[1:]+ndc[:1]))/2
  kind='degenerate_or_subprecision' if abs(area)<=1e-10 else 'intersects'
 return dict(classification=kind,clipPolygon=polygon,ndcPolygon=ndc,signedNdcArea=area,
  tolerance=epsilon,nearInputPlane=any(abs(x)<=epsilon for v in points for x in distances(v)))

def project(receipt,shader,constants):
 data=decode(receipt);clip=clip_points(shader,constants,data['positions'])
 triangles=[dict(ordinal=i//3,**clip_triangle(clip[i:i+3])) for i in range(0,len(clip),3)]
 counts={kind:sum(t['classification']==kind for t in triangles) for kind in ('outside','intersects','singular_or_near_eye','degenerate_or_subprecision')}
 return dict(triangleCount=len(triangles),counts=counts,triangles=triangles,
  arithmetic='FLOAT64_FROM_FLOAT32_RECEIPTS_NOT_BIT_EXACT_GPU',geometryAdopted=False,currentStateProven=False,pixelContributionProven=False,
  limits=['Clipping assumes the supplied VS, constants and ordered source geometry are effective.',
   'Clip-space intersection does not prove covered samples, depth acceptance, final surface or texture contribution.',
   'No viewport state, culling, user clip planes, sample grid, scissor, depth/stencil or shader discard is simulated.'])
