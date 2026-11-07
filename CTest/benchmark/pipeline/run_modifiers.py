# SPDX-License-Identifier: BSD-3-Clause
"""Run ordered Shift-sweep/undo checks on the generated large mesh fixture."""
import argparse
from pathlib import Path
from run_namespace import run


def steps():
    result=[dict(name='load',action='load',exists=['/World']),dict(name='frame',action='frame')]
    a=[.4,.4,.6,.6];b=[.45,.42,.65,.62];full=[.02,.02,.98,.98]
    def sweep(name,rectangle,shift=False,**extra):
        result.append(dict(name=name,action='sweep',rectangle=rectangle,shift=shift,**extra))
    def history(name,action,expect):result.append(dict(name=name,action=action,expect=expect))
    sweep('sweep_a',a,save='a');sweep('sweep_b',b,save='b')
    history('undo_b','undo','a')
    sweep('shift_b',b,True,expect='a_x_b')
    history('undo_shift_b','undo','a');history('redo_shift_b','redo','a_x_b');history('restore_a','undo','a')
    sweep('shift_same_a',a,True,expect='empty')
    history('undo_same','undo','a');history('redo_same','redo','empty');history('restore_same','undo','a')
    sweep('sweep_full',full,save='full');history('undo_full','undo','a')
    sweep('shift_full',full,True,expect='a_x_full')
    history('undo_shift_full','undo','a');history('redo_shift_full','redo','a_x_full')
    # Preserve the current empty-hit behavior: even Shift clears selection.
    sweep('shift_empty',[.005,.005,.015,.015],True,expect='empty')
    history('undo_empty','undo','a_x_full')
    result.append(dict(name='cleanup',action='cleanup',exists=['/World'],absent=['/AuditTarget']))
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app',required=True);parser.add_argument('--fixture',required=True)
    parser.add_argument('--output',required=True);parser.add_argument('--label',required=True)
    args=parser.parse_args()
    args.root='/World';args.branch='/World/Batch_125';args.max_selection=250000
    args.selection_only=False;args.render_only=False;args.render_repeats=0;args.property_pane_width=0
    run(args,script=Path(__file__).with_name('modifiers_workflow.py'),steps=steps(),
        extra_config={'screenshotPhases':['sweep_a','shift_b','shift_full','shift_empty']})
