#!/usr/bin/env python3
"""Compare exact simulation outputs and host CPU cost of two executables."""
import argparse, json, resource, subprocess, statistics, hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path)
p.add_argument('--out',type=Path,default=Path('out/optimization.json'));p.add_argument('--days',type=int,default=30)
a=p.parse_args();rows=[]
for seed in (42,91,777):
    pair=[]
    for label,binary in (('before',a.before),('after',a.after)):
        start=resource.getrusage(resource.RUSAGE_CHILDREN)
        r=subprocess.run([str(binary.resolve()),'benchmark','--days',str(a.days),'--seed',str(seed),'--insects','0.25','--weather','data/weather/newcastle_2023_may_aug.csv'],capture_output=True,text=True,check=True)
        end=resource.getrusage(resource.RUSAGE_CHILDREN);row=json.loads(r.stdout)
        row.update(label=label,cpu_seconds=end.ru_utime+end.ru_stime-start.ru_utime-start.ru_stime,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        pair.append(row);rows.append(row);print(json.dumps(row),flush=True)
    if pair[0]['hash']!=pair[1]['hash']:raise RuntimeError('Optimization changed the world at seed '+str(seed))
result={'scope':'host CPU seconds; full core, identical state hashes, three seeds','runs':rows,
        'median_cpu_ratio_after_over_before':statistics.median(rows[i+1]['cpu_seconds']/rows[i]['cpu_seconds'] for i in range(0,len(rows),2))}
a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
