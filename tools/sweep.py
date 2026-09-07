#!/usr/bin/env python3
"""Reproducible full-core colony trials; every simulated ant movement is executed."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import time

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--days',type=float,default=180)
    p.add_argument('--seeds',type=int,default=8)
    p.add_argument('--start-seed',type=int,default=1000)
    p.add_argument('--founders',type=int,nargs='+',default=[20,30,40])
    p.add_argument('--years',type=int,nargs='+',default=[2023])
    p.add_argument('--jobs',type=int,default=3)
    p.add_argument('--nectar',type=float,default=1)
    p.add_argument('--insects',type=float,default=.25)
    p.add_argument('--out',type=Path,default=ROOT/'out'/'sweep')
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    if args.seeds<1 or args.jobs<1 or args.days<=0:p.error('days, seeds and jobs must be positive')
    binary=ROOT/'build'/'antfarm'
    manifest={'started_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
              'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
              'mode':'full_core_unrendered','requested_days':args.days,'options':vars(args).copy()}
    manifest['options']['out']=str(args.out)
    (args.out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Each batch runs an immutable binary, even if development continues elsewhere.
    frozen=args.out.resolve()/'antfarm-trial'
    shutil.copy2(binary,frozen)
    trials=[(n,s,y) for n in args.founders for y in args.years for s in range(args.start_seed,args.start_seed+args.seeds)]
    def run(t):
        n,s,y=t;name=f'n{n}-seed{s}-weather{y}'
        weather=ROOT/'data/weather'/f'newcastle_{y}_may_aug.csv'
        cmd=[str(frozen),'run','--days',str(args.days),'--founders',str(n),'--seed',str(s),
             '--weather',str(weather),'--csv',str(args.out/(name+'.csv')),
             '--nectar',str(args.nectar),'--insects',str(args.insects),'--stop-on-queen-loss','1']
        result=subprocess.run(cmd,capture_output=True,text=True,cwd=ROOT)
        (args.out/(name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:return {'founders':n,'seed':s,'weather_year':y,'error':result.stderr.strip()}
        data=json.loads(result.stdout.strip().splitlines()[-1]);data['weather_year']=y
        data['reached_horizon']=data['day']>=args.days-1e-5
        data['viable']=data['queen'] and data['workers']>0 and data['reached_horizon']
        return data
    results=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for result in pool.map(run,trials):
            results.append(result);print(json.dumps(result),flush=True)
            (args.out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    groups=[]
    for n in args.founders:
        for y in args.years:
            group=[r for r in results if r['founders']==n and r['weather_year']==y]
            passed=sum(bool(r.get('viable')) for r in group);total=len(group)
            # Wilson 95% upper bound for failure rate: zero failures is not proof of zero risk.
            z=1.95996398454;f=(total-passed)/total
            upper=(f+z*z/(2*total)+z*((f*(1-f)/total+z*z/(4*total*total))**.5))/(1+z*z/total)
            groups.append({'founders':n,'weather_year':y,'trials':total,'survivors':passed,
                           'failure_rate_95pct_upper_bound':upper,
                           'median_workers':sorted(r.get('workers',0) for r in group)[total//2]})
    (args.out/'summary.json').write_text(json.dumps(groups,indent=2)+'\n')
    print(json.dumps({'summary':groups}),flush=True)
if __name__=='__main__':main()
