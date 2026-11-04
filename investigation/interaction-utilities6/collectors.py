"""Migrate append-only command notification lists to indexed collectors."""
from pathlib import Path
import re
for filename in ('commandproperty.cpp','commandvisibility.cpp','commandtransform.cpp','commandprim.cpp'):
    p=Path('sources/commands')/filename
    s=p.read_text(encoding='utf-8')
    names=set(re.findall(r'path::appendUnique\((\w+),',s))
    for name in names:
        pattern=rf'(?m)^(\s*)QList<SdfPath> {name};$'
        s,n=re.subn(pattern,rf'\1QList<SdfPath> {name};\1path::PathCollector {name}Collector({name});',s)
        if not n and name!='affected':
            raise AssertionError((filename,name))
        s=s.replace(f'path::appendUnique({name}, ',f'{name}Collector.append(')
    if filename=='commandprim.cpp':
        marker='        QList<DependencyPropertyState> states;'
        assert marker in s
        s=s.replace(marker,marker+'\n        path::PathCollector affectedCollector(affected);')
    p.write_text(s,encoding='utf-8')
p=Path('sources/usd/payloadutils.cpp');s=p.read_text(encoding='utf-8')
# All result lists here are local append-only outputs. Collectors are used only
# for their corresponding appendUnique sites; other lists keep their own logic.
s=s.replace('        QList<SdfPath> result;','        QList<SdfPath> result;\n        path::PathCollector resultCollector(result);')
s=s.replace('path::appendUnique(result, ','resultCollector.append(')
p.write_text(s,encoding='utf-8')
