import os
import stageviz
target = os.environ['STORM_TRACE_PRIM']
session = stageviz.session()
session.setMask([target])
session.viewState().camera().frame(session.boundingBox([target]))
print('Isolated and framed', target)
