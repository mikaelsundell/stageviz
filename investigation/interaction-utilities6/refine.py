from pathlib import Path
import re
# Keep collectors adjacent to their list declaration, on a separate line.
for p in Path('sources/commands').glob('*.cpp'):
    s=p.read_text(encoding='utf-8')
    s=re.sub(r'(?m)^( *)(QList<SdfPath> \w+;) +path::PathCollector',r'\1\2\n\1path::PathCollector',s)
    # Lists passed to applyPreparedTransforms already have a collector there;
    # don't construct an unused index for unrelated notification lists.
    s=re.sub(r'^ *path::PathCollector (\w+)Collector\(\1\);\n',lambda m: m.group(0) if f'{m.group(1)}Collector.append(' in s else '',s,flags=re.M)
    p.write_text(s,encoding='utf-8')
p=Path('sources/usd/payloadutils.cpp');s=p.read_text(encoding='utf-8')
# Remove collectors in functions that already use another index or append once.
chunks=re.split(r'(?=    QList<SdfPath> \w+\()',s)
for i,chunk in enumerate(chunks):
    if 'resultCollector.append(' not in chunk:
        chunks[i]=chunk.replace('        path::PathCollector resultCollector(result);\n','')
p.write_text(''.join(chunks),encoding='utf-8')
p=Path('sources/commands/commandtransform.cpp');s=p.read_text(encoding='utf-8')
s=s.replace('#include <memory>','#include <memory>\n#include <unordered_map>')
old='''    QList<bool> assigned(operation.targetCount, false);
    QList<GfMatrix4d> targetDeltas(operation.targetCount, GfMatrix4d(1));'''
new=old+'''
    const path::PathIndex targetRoots(operation.paths.mid(0, operation.targetCount));
    std::unordered_map<SdfPath, qsizetype, SdfPath::Hash> targetIndices;
    targetIndices.reserve(static_cast<size_t>(operation.targetCount));
    for (qsizetype i = 0; i < operation.targetCount; ++i)
        targetIndices.emplace(operation.paths.at(i), i);'''
assert old in s;s=s.replace(old,new)
start=s.index('        qsizetype best = -1;',s.index('resolveTransformInput('))
end=s.index('        if (best < 0)',start)
s=s[:start]+'''        const auto target = targetIndices.find(targetRoots.nearestAncestor(selected));
        const qsizetype best = target == targetIndices.end() ? -1 : target->second;
'''+s[end:]
p.write_text(s,encoding='utf-8')
p=Path('sources/usd/usdutils.cpp');s=p.read_text(encoding='utf-8')
old='''        for (const TfToken& token : tokens) {
            order.erase(std::remove(order.begin(), order.end(), token), order.end());
        }'''
new='''        const std::unordered_set<TfToken, TfToken::HashFunctor> removed(tokens.begin(), tokens.end());
        order.erase(std::remove_if(order.begin(), order.end(), [&](const TfToken& token) {
            return removed.find(token) != removed.end();
        }), order.end());'''
assert old in s;s=s.replace(old,new);p.write_text(s,encoding='utf-8')
