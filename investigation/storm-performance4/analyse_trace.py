"""Summarize the text trace; inclusive nesting is preserved, never summed."""
from pathlib import Path
import json
import re
import sys

source=Path(sys.argv[1])
rows=[]
for number,line in enumerate(source.read_text().splitlines(),1):
    match=re.match(r'\s*([\d.]+) ms\s+([\d.]+) ms\s+(\d+) samples\s+(.*)',line)
    if match and '|' in match[4]:
        rows.append({'inclusive_ms':float(match[1]),'exclusive_ms':float(match[2]),'calls':int(match[3]),'scope':match[4],'line':number})
result={'largestExclusiveScopes':sorted(rows,key=lambda row:row['exclusive_ms'],reverse=True)[:35],
        'largestInclusiveScopes':sorted(rows,key=lambda row:row['inclusive_ms'],reverse=True)[:35]}
destination=source.with_name(source.stem+'-summary.json')
destination.write_text(json.dumps(result,indent=2))
for key,rows in result.items():
    print(key)
    for row in rows[:18]:print(json.dumps(row))
