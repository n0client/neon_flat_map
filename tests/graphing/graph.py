import matplotlib.pyplot as plt
import numpy as np
import subprocess
import os


def get_runtime_data(file, trials, bound, mult1, mult2, O3_on):

    args = ["clang++", "-o", file, f"{file}.cpp",
                           f"-DBOUND={bound}ull", f"-DMULT1={mult1}", f"-DMULT2={mult2}"]

    if "absl" in file:
        args.extend(["-labsl_raw_logging_internal", "-labsl_log_severity", "-labsl_base",
                              "-labsl_raw_hash_set",
                              "-labsl_hashtablez_sampler", "-labsl_hash"])
    if O3_on:
        args.append("-O3")

    subprocess.run(args)

    insert_time = 0
    find_time = 0
    erase_time = 0
    mem = 0
    for _ in range(trials):
        args = ["/usr/bin/time", "-l", f"./{file}"]
        res = subprocess.run(args, capture_output=True, text=True)
        out = res.stdout.split('\n')[:-1]
        assert(len(out) == 3)
        times = [x.split(' ')[-2] for x in out]
        mem += int(res.stderr.split('\n')[-2].split()[0])

        insert_time += int(times[0])
        find_time += int(times[1])
        erase_time += int(times[2])

    os.remove(file)
    total_time = insert_time + find_time + erase_time
    return [total_time / trials, insert_time / trials, find_time / trials, erase_time / trials, mem / trials]


use_O3 = True
trials = 10
bounds = [1_000_000, 10_000_000, 50_000_000]
files = ["test_absl", "test_ankerl", "test_fhm", "test_um"]
data = np.zeros((len(bounds), len(files), 5))

for i, bound in enumerate(bounds):
    for j, file in enumerate(files):
        loc_data = get_runtime_data(file, trials, bound, 2, 3, use_O3)
        data[i, j, :] = loc_data

colors = ['#2ca02c', '#1f77b4', '#d62728']
labels = ['Insert', 'Find', 'Erase']

for i, bound in enumerate(bounds):
    fig, ax = plt.subplots(figsize=(10, 8))

    names = ["Absl Flat Hashmap", "Ankerl Unordered Dense", "Neon flat hashmap", "std::unordered_map"]
    totals = data[i, :, 0]
    ins = data[i, :, 1]
    fnd = data[i, :, 2]
    ers = data[i, :, 3]
    mem = data[i, :, 4]

    ax.bar(names, ins, color=colors[0], label=labels[0])
    ax.bar(names, fnd, bottom=ins, color=colors[1], label=labels[1])
    ax.bar(names, ers, bottom=ins+fnd, color=colors[2], label=labels[2])

    for j in range(len(files)):
        # A. Labels inside the segments (Specific Times)
        if ins[j] > totals[j] * 0.05:
            ax.text(j, ins[j]/2, f'{int(ins[j])}', ha='center', va='center', color='white', fontweight='bold', fontsize=9)
        if fnd[j] > totals[j] * 0.05:
            ax.text(j, ins[j] + fnd[j]/2, f'{int(fnd[j])}', ha='center', va='center', color='white', fontweight='bold', fontsize=9)
        if ers[j] > totals[j] * 0.05:
            ax.text(j, ins[j] + fnd[j] + ers[j]/2, f'{int(ers[j])}', ha='center', va='center', color='white', fontweight='bold', fontsize=9)

        # B. Total Time on top
        ax.text(j, totals[j], f'{int(totals[j])} ms', ha='center', va='bottom', fontsize=10, fontweight='bold')

        # C. Statistics below the X-axis
        y_min, y_max = ax.get_ylim()
        offset = (y_max - y_min) * 0.12 # dynamic spacing based on chart height

        # Memory Line
        mb = round(int(mem[j]) / 1e6, 2)
        ax.text(j, -offset, f"Avg Peak Mem: {mb}MB", ha='center', color='purple', fontsize=9, fontweight='bold')

        # Throughput Line (Ops/sec)
        throughput = (bound * 3) / (totals[j] / 1000) if totals[j] > 0 else 0
        ax.text(j, -offset * 1.6, f"{throughput/1e6:.1f}M ops/s", ha='center', color='black', fontsize=9)

    # Formatting
    o3_is_used = "With " + ("-O3" if use_O3 else "-O0")
    ax.set_title(f"Elements used = {bound:,}, num trials = {trials:,}, {o3_is_used}", fontsize=16, pad=25)
    ax.set_ylabel("Runtime (ms)", fontsize=12)
    ax.grid(axis='y', linestyle='--', alpha=0.3)

    # Legend - handle duplicate labels
    handles, lbls = ax.get_legend_handles_labels()
    ax.legend(handles[:3], lbls[:3], loc='upper left')

    plt.savefig(f"./images/{bound}.png", bbox_inches='tight', dpi=300)
    plt.subplots_adjust(bottom=0.2)
    plt.show()

