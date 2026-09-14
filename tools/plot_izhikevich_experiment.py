"""Summarize native experiment CSVs and plot them (requires matplotlib)."""
import argparse
import csv
import json
from pathlib import Path
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument("directory", nargs="?", default="runs/izhikevich")
out = Path(parser.parse_args().directory)

def read(name):
    with (out / name).open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))

def dominant(row):
    a,b,c,d = (float(row[key]) for key in ("a_before","b_before","a_after","b_after"))
    return a>0 and d>0 and (a-b)/max(1,a+b)>=0.5 and (d-c)/max(1,d+c)>=0.5

profile, fi, trace, validation = (read(name) for name in
    ("profile.csv", "fi.csv", "candidate_trace.csv", "validation.csv"))
summary = {"profile": {}, "validation": {}}
for work in dict.fromkeys(row["workload"] for row in profile):
    stats = {}
    for model in ("lif20","lif1","izh_rs"):
        values = [float(row["wall_ms"]) for row in profile if row["workload"]==work and row["model"]==model]
        stats[model] = {"median_ms":statistics.median(values), "min_ms":min(values), "max_ms":max(values)}
    stats["izh_over_lif20"] = stats["izh_rs"]["median_ms"]/stats["lif20"]["median_ms"]
    stats["izh_over_lif1"] = stats["izh_rs"]["median_ms"]/stats["lif1"]["median_ms"]
    summary["profile"][work] = stats
for model in ("izh_rs","izh_low_adaptation","izh_fast"):
    summary["validation"][model] = {}
    for mode in dict.fromkeys(row["mode"] for row in validation):
        rows = [r for r in validation if r["model"]==model and r["mode"]==mode]
        summary["validation"][model][mode] = {"cases":len(rows),"selects_and_reverses":sum(map(dominant,rows))}
(out/"analysis.json").write_text(json.dumps(summary,indent=2),encoding="utf-8")

plt.rcParams.update({"font.size":10,"axes.spines.top":False,"axes.spines.right":False})
fig, axes = plt.subplots(2,2,figsize=(13,8.5),layout="constrained")
colors = {"lif20":"#929daa","lif1":"#b5b7c3","izh_rs":"#237ba1",
          "izh_low_adaptation":"#c38a35","izh_fast":"#278b69"}
names = {"lif20":"LIF 20 ms","lif1":"LIF 1 ms","izh_rs":"Izh RS",
         "izh_low_adaptation":"Izh less adaptation","izh_fast":"Izh fast recovery"}
ax=axes[0,0]
workloads=["brain_5","brain_32","brain_128","ecosystem_24_default_noise"]
x=np.arange(len(workloads))
for shift,model in ((-0.19,"lif1"),(0.19,"izh_rs")):
    values=[summary["profile"][w][model]["median_ms"]/summary["profile"][w]["lif20"]["median_ms"] for w in workloads]
    bars=ax.bar(x+shift,values,width=0.36,color=colors[model],label=names[model])
    ax.bar_label(bars,labels=[f"{v:.1f}x" for v in values],padding=3,fontsize=9)
ax.axhline(1,color="#707070",ls=":",lw=1)
ax.set(xticks=x,xticklabels=["5 hidden","32 hidden","128 hidden","24-creature\nworld"],
       ylabel="Runtime / LIF 20 ms runtime",title="A. Most neural overhead comes from 20x more updates")
ax.set_ylim(0,max(b.get_height() for b in ax.patches)*1.2)
ax.legend()

ax=axes[0,1]
for model in names:
    scale=4 if model.startswith("izh") else 10
    rows=[r for r in fi if r["model"]==model and float(r["current"])/scale<=3]
    ax.plot([float(r["current"])/scale for r in rows],[float(r["rate_hz"]) for r in rows],
            color=colors[model],ls="--" if model=="lif1" else "-",label=names[model])
ax.set(xlabel="Constant drive / approximate firing-onset current",
       ylabel="Output rate (Hz)",title="B. Recovery parameters change the rate response")
ax.legend(fontsize=8)

ax=axes[1,0]
rows=[r for r in trace if r["model"]=="izh_rs"]
for index,(field,label,color) in enumerate((("spike_a","Neuron A",colors["izh_rs"]),("spike_b","Neuron B",colors["izh_fast"]))):
    times=[float(r["time"]) for r in rows if r[field]=="1"]
    ax.eventplot(times,lineoffsets=1-index,linelengths=0.5,colors=color,linewidths=0.8)
ax.axvline(2,color="#bf724d",ls="--")
ax.text(2.05,1.45,"Input advantage switches to B",fontsize=9,color="#9b593b")
ax.set(yticks=[0,1],yticklabels=["Neuron B","Neuron A"],ylim=(-0.6,1.8),xlim=(0,4),
       xlabel="Time (s)",title="C. A selected RS pair follows a constant-input reversal")
ax.text(0.02,-0.42,"Self: weight 2, 10 ms   |   Inhibition: weight 1, 1 ms",fontsize=9)

ax=axes[1,1]
models=["izh_rs","izh_low_adaptation","izh_fast"]
modes=[("resolution","Resolution (12)","#237ba1"),
       ("weights_and_contrast","Weights / contrast (81)","#899ab8"),
       ("intrinsic_5pct","Intrinsic variation (100)","#278b69"),
       ("poisson_20_40","Random spike input (100)","#c38a35")]
for i,(mode,label,color) in enumerate(modes):
    values=[100*summary["validation"][m][mode]["selects_and_reverses"]/summary["validation"][m][mode]["cases"] for m in models]
    ax.bar(np.arange(3)+(i-1.5)*0.19,values,width=0.18,color=color,label=label)
ax.set(xticks=range(3),xticklabels=["Regular spiking","Less adaptation","Fast recovery"],
       ylim=(0,125),ylabel="Test cases selecting and reversing (%)",
       title="D. Success depends strongly on drive and parameters")
ax.legend(fontsize=8,ncol=2,loc="upper center")
fig.suptitle("Izhikevich neurons: implementation, cost, and two-neuron competition",fontsize=15,fontweight="bold")
fig.savefig(out/"izhikevich-results.png",dpi=160)
fig.savefig(out/"izhikevich-results.svg")
print(json.dumps(summary,indent=2))
