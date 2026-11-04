import stageviz
assert stageviz.session().load(r'C:\Temp\usdtest\TEMP_asm.usd')
stageviz.command.clear()
print('Requested original dataset reload; no dataset saved')
