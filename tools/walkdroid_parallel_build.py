#!/usr/bin/env python3
"""Schedule source builds after their locally produced libraries."""
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
import os
import subprocess


def default_jobs(limit):
    try:
        cpus = len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        cpus = os.cpu_count() or 1
    return max(1, min(cpus, limit))


def run_compiles(plans, env, jobs):
    """Compile independent outputs concurrently, after their local libraries."""
    produced = {plan['output'] for plan in plans}
    pending = list(plans)
    finished = set()
    with ThreadPoolExecutor(max_workers=jobs) as executor:
        active = {}
        while pending or active:
            while len(active) < jobs:
                ready = next((plan for plan in pending
                              if (set(plan['libraries']) & produced) <= finished), None)
                if ready is None:
                    break
                pending.remove(ready)
                future = executor.submit(subprocess.run, ready['command'], env=env, check=True)
                active[future] = ready['output']
            if not active:
                raise RuntimeError('Local build library dependencies cannot be ordered')
            completed, _ = wait(active, return_when=FIRST_COMPLETED)
            for future in completed:
                name = active.pop(future)
                future.result()
                finished.add(name)
