#!/usr/bin/env python3
"""Independently import, compare, bake and record completed isolated MR trials.

The authoring runner may still be active. Each case is processed only after its
run.json is published. No authored tuning is changed; a separate comparison copy
sets the native physics frequency to the Crankwave renderer's required 20 kHz.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time


def write(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['runs','toolkit','adapter','crankwave','specification','out']:
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--harness-url',required=True)
    a=p.parse_args()
    tools=Path(__file__).resolve().parent
    root=a.out.resolve();root.mkdir(parents=True,exist_ok=False)
    for name in ['comparison','complete-bake','case-reports']:(root/name).mkdir()
    cases=json.loads(a.specification.read_text())['cases']
    toolkit=a.toolkit.resolve();runs=a.runs.resolve()

    def process(name):
        report={'case':name,'status':'processing','stages':[],'retuned_by_importer':False}
        report_file=root/'case-reports'/(name+'.json')
        comparison=root/'comparison'/name;comparison.mkdir()
        def run(stage,argv,cwd=None,timeout=1800):
            print(name+': '+stage,flush=True)
            with (comparison/(stage+'.log')).open('w') as log:
                r=subprocess.run(list(map(str,argv)),cwd=cwd,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
            report['stages'].append({'stage':stage,'exit_code':r.returncode})
            write(report_file,report)
            if r.returncode:raise RuntimeError(f'{stage} failed ({r.returncode}); see {comparison/(stage+".log")}')
        try:
            workspace=runs/name/'workspace'
            source=workspace/'main.mr'
            report['author_run']=json.loads((runs/name/'run.json').read_text())
            report['source_sha256']=sha(source)
            matches=[]
            for f in sorted(workspace.glob('audition-*/report.json')):
                r=json.loads(f.read_text())
                if r.get('native_exit')==0 and r.get('source_sha256')==report['source_sha256']:matches.append(f)
            if not matches:raise ValueError('Final authored MR has no matching successful native audition')
            report['author_audition']=str(matches[-1].parent)
            audition=json.loads(matches[-1].read_text())
            stages={s['name']:s for s in audition.get('stages',[])}
            if (stages.get('idle',{}).get('tail_median_rpm',0)<=200 or
                    stages.get('full-throttle',{}).get('peak_rpm',0)<=
                    1.2*stages.get('idle',{}).get('tail_median_rpm',0)):
                raise ValueError('Final native engine does not sustain idle and rev; refusing to package a stalled authoring result')
            # Keep the untouched authored artifact beside the separate rate copy.
            shutil.copy2(source,comparison/'authored.mr')
            original=source.read_text()
            rate_copy,count=re.subn(r'(simulation_frequency\s*:\s*)[^,\n]+',r'\g<1>20000',original)
            if count!=1:raise ValueError('Expected one explicit simulation_frequency; refusing ambiguous rewrite')
            (comparison/'main.mr').write_text(rate_copy)
            (comparison/'es').symlink_to(toolkit/'es',target_is_directory=True)
            report['comparison_source_sha256']=sha(comparison/'main.mr')
            report['only_source_change']='simulation_frequency set to 20000 for both comparison renderers'
            run('export',[sys.executable,tools/'export.py','--script',comparison/'main.mr','--adapter',a.adapter.resolve(),
                 '--library',toolkit/'es','--id',name+'-cleanroom','--out',comparison/'import','--diagnostic-loss-policy'])
            run('native-render',[sys.executable,toolkit/'audition.py','audition',comparison/'main.mr',
                 '--native',toolkit/'engine-sim-native','--frequency','20000','--out',comparison/'native'],comparison)
            run('native-limiter',[toolkit/'limiter-probe',comparison/'main.mr',comparison/'native/controls.txt',
                 comparison/'native/limiter-events.jsonl'],comparison)
            run('crankwave-render',[a.crankwave.resolve(),'render','--engine',comparison/'import/engine.json',
                 '--scenario',comparison/'import/scenario.json','--asset-root',comparison/'import',
                 '--output-directory',comparison/'crankwave','--result-format','json'])
            report['comparison_complete']=True;write(report_file,report)
            destination=root/'complete-bake'/name
            run('complete-bake',[sys.executable,tools/'bake.py','--import-dir',comparison/'import',
                 '--crankwave',a.crankwave.resolve(),'--out',destination,'--diagnostic-loss-policy'])
            runtime=json.loads((destination/'package/runtime.json').read_text())
            lifecycle=json.loads((destination/'package'/runtime['lifecycle_package_path']).read_text())
            longest_shutdown=max(lifecycle[role]['artifact']['frame_count'] for role in
                                 ['shutdown','shutdown_elevated'] if role in lifecycle)
            # Key-off is at 36s. Retain the whole authenticated shutdown tape and
            # one second of silence, rather than imposing a universal 4s rundown.
            seconds=max(40,36+math.ceil(longest_shutdown/192000)+1)
            run('harness-record',['node',tools/'record_harness.mjs',a.harness_url,destination/'complete.crankwave',
                                 destination/'harness','--seconds',str(seconds)])
            report.update(status='complete',carrier=str(destination/'complete.crankwave'),
                          harness_audio=str(destination/'harness/harness-48k.wav'))
        except Exception as error:
            report.update(status='failed',error=str(error))
        write(report_file,report)
        print(name+': '+report['status'],flush=True)
        return report

    pending=set(cases);futures=[];deadline=time.monotonic()+3600
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        while pending:
            for name in list(pending):
                if (runs/name/'run.json').is_file():
                    futures.append(pool.submit(process,name));pending.remove(name)
            if pending:
                if time.monotonic()>deadline:raise TimeoutError('Authoring runs did not finish: '+', '.join(sorted(pending)))
                time.sleep(2)
        reports=[f.result() for f in futures]
    write(root/'pipeline-report.json',reports)
    return 0 if all(r['status']=='complete' for r in reports) else 1


if __name__=='__main__':
    sys.exit(main())
