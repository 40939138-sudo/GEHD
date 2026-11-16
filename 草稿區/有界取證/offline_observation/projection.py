"""Conditional position-only evaluation of the two identity-bound saved VS bodies.

This evaluates supplied receipts; it does not assert current-state validity, geometry
adoption, rasterization coverage, depth visibility, or a visible surface mapping.
"""
import hashlib,math,struct
SHADERS={
 '4C4B12F2E960B4646ED2F62903777DF95E338E2F27A4D806D39DEAAE9CBFE03A':'PTCCSG',
 'A4E3EA3047F474A285219C9E3D4B0B9A67E368AC13AF581AEC80DC50B1950104':'PNTCCSG',
}
REGISTERS=(0,1,2,3,8,9,10)
def clip_points(shader,constants,points):
 if not shader or shader.get('isNull') or shader.get('sha256') not in SHADERS:raise ValueError('unknown_shader')
 raw=bytes.fromhex(shader['bytesHex'])
 if len(raw)!=shader['length'] or hashlib.sha256(raw).hexdigest().upper()!=shader['sha256']:raise ValueError('shader_identity')
 if not 1<=len(points)<=4096:raise ValueError('point_bounds')
 c={}
 for k in REGISTERS:
  if str(k) not in constants:raise ValueError('missing_register:'+str(k))
  v=constants[str(k)]['values']
  if len(v)!=4 or not all(math.isfinite(x) for x in v):raise ValueError('nonfinite_register')
  raw=bytes.fromhex(constants[str(k)]['rawBits'])
  if len(raw)!=16 or tuple(v)!=struct.unpack('<4f',raw):raise ValueError('register_bits')
  c[k]=v
 out=[]
 for xyz in points:
  if len(xyz)!=3 or not all(math.isfinite(x) for x in xyz):raise ValueError('position')
  pos=[*xyz,1.0]
  # Both saved instruction bodies: three dp4 World rows, w=1, four dp4 ViewProj rows.
  world=[sum(pos[j]*c[k][j] for j in range(4)) for k in (8,9,10)]+[1.0]
  clip=[sum(world[j]*c[k][j] for j in range(4)) for k in range(4)]
  if not all(math.isfinite(x) for x in clip):raise ValueError('nonfinite_clip')
  out.append(clip)
 return out

def box_candidate(shader,constants,minimum,maximum):
 if len(minimum)!=3 or len(maximum)!=3 or any(a>b for a,b in zip(minimum,maximum)):raise ValueError('box')
 points=[[maximum[j] if mask&(1<<j) else minimum[j] for j in range(3)] for mask in range(8)]
 clip=clip_points(shader,constants,points)
 distances=[[w+x,w-x,w+y,w-y,z,w-z] for x,y,z,w in clip]
 scale=max(1.0,max(abs(x) for row in clip for x in row));epsilon=scale*1e-6
 outside=[name for i,name in enumerate(['left','right','bottom','top','near','far']) if all(row[i]<-epsilon for row in distances)]
 positive_w=all(v[3]>epsilon for v in clip)
 ndc=[[v[i]/v[3] for i in range(3)] for v in clip] if positive_w else None
 return dict(shaderModel=SHADERS[shader['sha256']],clipCorners=clip,
  whollyOutsidePlanes=outside,positiveWForAllCorners=positive_w,
  ndcMinimum=[min(v[i] for v in ndc) for i in range(3)] if ndc else None,
  ndcMaximum=[max(v[i] for v in ndc) for i in range(3)] if ndc else None,
  arithmetic='FLOAT64_EVALUATION_OF_FLOAT32_RECEIPTS_NOT_BIT_EXACT_GPU',
  planeTolerance=epsilon,currentStateProven=False,geometryAdopted=False,pixelContributionProven=False)
