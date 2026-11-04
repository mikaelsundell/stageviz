import stageviz
session = stageviz.session()
session.setMask([])
session.viewState().camera().frame(session.boundingBox())
print('Framed all')
