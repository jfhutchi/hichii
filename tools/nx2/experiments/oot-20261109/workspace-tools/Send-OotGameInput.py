"""Navigation for the isolated reference's observed Items/System menu layout."""
import argparse
import json
from evaluate_oot import request, OUT

ap = argparse.ArgumentParser()
ap.add_argument('label')
ap.add_argument('action', choices=['reload', 'resume', 'menu', 'title-load', 'title-continue'])
ap.add_argument('--slot-index', type=int, choices=range(6), default=0)
a = ap.parse_args()
buttons = {'reload': [('Plus',48),('R',48),('Down',48),('A',108),('A',108),('Up',48),('A',300)],
           'resume': [('B',120)], 'menu':[('Plus',60)], 'title-load':[('A',108),('A',300)],
           'title-continue':[('A',108),('A',108),('A',300)]}[a.action]
if a.action == 'reload' and a.slot_index:
    buttons = buttons[:4] + [('Down',48)] * a.slot_index + buttons[4:]
steps = []
for button, wait in buttons:
    steps.extend([dict(frames=12,buttons=[button]),dict(frames=wait,buttons=[])])
result = request('input_sequence',dict(steps=steps,timeout_s=90))
doc = dict(label=a.label,action=a.action,slot_index=a.slot_index,steps=steps,result=result)
(OUT/(a.label+'-navigation.json')).write_text(json.dumps(doc,indent=2))
print(json.dumps(result))
