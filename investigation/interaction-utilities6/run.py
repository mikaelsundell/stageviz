"""Run the existing selection/transform benchmark in the isolated sixth archive."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'performance'))
import run

run.SCRIPT=Path(__file__).resolve().parents[2] / 'CTest/benchmark/benchmark_selection_transform.py'
run.ROOT=Path(r'C:\git\3rdparty\packages\OpenUSD-25.11\stageviz-investigation\test6-interaction-utilities')
run.main()
