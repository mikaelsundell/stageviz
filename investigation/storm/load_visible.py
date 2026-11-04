import stageviz
assert stageviz.session().load(r'C:\Temp\usdtest\storm-investigation\run01\visible-wrapper.usda')
stageviz.session().viewState().setSceneMaterialsEnabled(False)
stageviz.session().viewState().setDoubleSidedMode(1)
print('Loading existing all-visible wrapper')
