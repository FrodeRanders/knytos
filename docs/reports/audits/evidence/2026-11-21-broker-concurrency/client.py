#!/usr/bin/env python3
"""Short exploratory end-to-end Kafka acknowledgement measurement."""
import concurrent.futures
import json
import math
import os
from pathlib import Path
import platform
import resource
import socket
import struct
import subprocess
import threading
import time
from kafka import KafkaProducer

ADDRESS = '127.0.0.1:19099'
PAYLOAD = b'knytos-perf-' + b'x' * 28  # exactly 40 bytes
assert len(PAYLOAD) == 40
RESULT = Path('/tmp/knytos-broker-concurrency-results.json')
result = {'bootstrap': ADDRESS, 'payload_bytes': len(PAYLOAD), 'acks': 1,
          'batch_size': 0, 'linger_ms': 0, 'max_in_flight_per_connection': 1,
          'api_version': 'negotiated', 'phase_seconds': 8, 'host': platform.platform(), 'phases': []}
producers = []


def qemu_cpu():
    try:
        pid = Path('/tmp/knytos-broker-perf-qemu.pid').read_text().strip()
        value = subprocess.check_output(['ps', '-p', pid, '-o', 'time='], text=True).strip()
        total = 0.0
        for part in value.split(':'):
            total = total * 60 + float(part)
        return total
    except (OSError, ValueError, subprocess.CalledProcessError):
        return None


def quantile(samples, fraction):
    if not samples:
        return None
    ordered = sorted(samples)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def phase(count, partition_mode):
    ready = threading.Barrier(count + 1)
    stopping = threading.Event()
    def worker(index):
        timings = []
        errors = []
        ready.wait()
        while not stopping.is_set():
            begin = time.perf_counter()
            try:
                producers[index].send('events', value=PAYLOAD,
                                      partition=index % 4 if partition_mode == 'spread' else 0).get(timeout=8)
                timings.append((time.perf_counter() - begin) * 1000)
            except Exception as error:
                errors.append(repr(error))
                stopping.set()
        return timings, errors
    with concurrent.futures.ThreadPoolExecutor(max_workers=count) as pool:
        futures = [pool.submit(worker, index) for index in range(count)]
        cpu_before = qemu_cpu()
        client_before = resource.getrusage(resource.RUSAGE_SELF)
        begin = time.perf_counter()
        ready.wait()
        stopping.wait(8)
        stopping.set()
        outcomes = [future.result(timeout=12) for future in futures]
        elapsed = time.perf_counter() - begin
        client_after = resource.getrusage(resource.RUSAGE_SELF)
        cpu_after = qemu_cpu()
    timings = [value for timing, _ in outcomes for value in timing]
    errors = [value for _, errors in outcomes for value in errors]
    entry = {'producers': count, 'partition_mode': partition_mode, 'records': len(timings),
             'elapsed_s': elapsed, 'records_per_s': len(timings) / elapsed,
             'ack_ms_p50': quantile(timings, .5), 'ack_ms_p95': quantile(timings, .95),
             'ack_ms_p99': quantile(timings, .99), 'errors': errors,
             'client_cpu_s': client_after.ru_utime + client_after.ru_stime - client_before.ru_utime - client_before.ru_stime,
             'qemu_cpu_s': None if cpu_before is None or cpu_after is None else cpu_after - cpu_before}
    result['phases'].append(entry)
    RESULT.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(entry), flush=True)
    if errors:
        raise RuntimeError('phase failed')


try:
    for index in range(4):
        producer = KafkaProducer(bootstrap_servers=[ADDRESS], client_id=f'knytos-perf-{index}',
                                 acks=1, batch_size=0, linger_ms=0,
                                 max_in_flight_requests_per_connection=1, retries=0,
                                 request_timeout_ms=5000, max_block_ms=5000)
        producers.append(producer)
        producer.send('events', value=PAYLOAD, partition=index % 4).get(timeout=8)
    print('Four producer connections warmed', flush=True)
    before = qemu_cpu(); idle_start = time.perf_counter(); time.sleep(3)
    after = qemu_cpu()
    result['idle'] = {'elapsed_s': time.perf_counter() - idle_start,
                      'qemu_cpu_s': None if before is None or after is None else after - before}
    with socket.create_connection(('127.0.0.1', 19099), timeout=5) as stalled:
        stalled.sendall(struct.pack('>i', 128))
        begin = time.perf_counter()
        producers[0].send('events', value=PAYLOAD, partition=0).get(timeout=8)
        result['ack_with_incomplete_other_frame_ms'] = (time.perf_counter() - begin) * 1000
    print('Incomplete-frame overlap check:', result['ack_with_incomplete_other_frame_ms'], 'ms', flush=True)
    for concurrency, mode in [(1, 'single'), (2, 'single'), (4, 'single'), (4, 'spread'), (1, 'single')]:
        phase(concurrency, mode)
except Exception as error:
    result['fatal_error'] = repr(error)
    raise
finally:
    RESULT.write_text(json.dumps(result, indent=2) + '\n')
    for producer in producers:
        producer.close(timeout=3)
