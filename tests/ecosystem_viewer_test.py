from __future__ import annotations

import importlib.util
import gzip
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ecosystem_viewer", ROOT / "tools" / "view_ecosystem.py")
assert SPEC and SPEC.loader
VIEWER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VIEWER)


class EcosystemReplayTests(unittest.TestCase):
    def test_statistics_csv_numeric_columns_and_missing_values(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            self.write_recording(directory, [self.metadata(), {"type": "frame", "time": 0}])
            (directory / "ecosystem_stats.csv").write_text(
                "time,population,net_energy,weather\n2,3,-4,calm\n0,1,nan,storm\n", encoding="utf-8")
            rows = VIEWER.read_replay(directory)["stats"]
            self.assertEqual(rows, [{"time": 0, "population": 1},
                                    {"time": 2, "population": 3, "net_energy": -4}])

    def write_recording(self, directory: Path, records: list[dict]) -> Path:
        path = directory / "ecosystem.jsonl"
        path.write_text("\n".join(json.dumps(record) for record in records), encoding="utf-8")
        return path

    def metadata(self) -> dict:
        return {"type": "metadata", "version": 1, "width": 3, "height": 2}

    def test_offline_cli_handles_empty_population_and_optional_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            self.write_recording(directory, [self.metadata(), {"type": "frame", "time": 0}, {"type": "frame", "time": 1}])
            subprocess.run([sys.executable, str(ROOT / "tools" / "view_ecosystem.py"), str(directory)], check=True, capture_output=True)
            document = (directory / "ecosystem.html").read_text(encoding="utf-8")
            embedded = re.search(r'<script id="replay-data" type="application/json">(.*?)</script>', document, re.S)
            self.assertIsNotNone(embedded)
            data = json.loads(embedded.group(1))
            self.assertEqual(data["metadata"]["terrain"], [0] * 6)
            self.assertEqual(data["frames"][0]["creatures"], [])
            self.assertNotRegex(document, r'<script[^>]+src=|<link[^>]+href="https?://|fetch\(')

    def test_recorded_text_cannot_close_script_or_inject_markup(self) -> None:
        marker = '</script><img src=x onerror="alert(1)">\u2028&__REPLAY_PAYLOAD____REPLAY_TITLE__'
        payload = {"name": marker, "metadata": self.metadata(), "frames": [{"time": 0, "events": [{"type": marker}]}]}
        document = VIEWER.render_html(payload)
        self.assertNotIn('<img src=x', document)
        embedded = re.search(r'<script id="replay-data" type="application/json">(.*?)</script>', document, re.S)
        self.assertIsNotNone(embedded)
        self.assertEqual(json.loads(embedded.group(1)), payload)

    def test_gzip_source_replaces_jsonl_and_remains_readable(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            source = self.write_recording(directory, [self.metadata(), {"type": "frame", "time": 0}])
            subprocess.run([sys.executable, str(ROOT / "tools" / "view_ecosystem.py"),
                            str(directory), "--gzip-source"], check=True, capture_output=True)
            self.assertFalse(source.exists())
            compressed = directory / "ecosystem.jsonl.gz"
            self.assertTrue(compressed.exists())
            self.assertEqual(VIEWER.read_replay(directory)["frames"][0]["time"], 0)
            with gzip.open(compressed, "rt", encoding="utf-8") as handle:
                self.assertEqual(json.loads(handle.readline())["type"], "metadata")

    def test_streaming_compactor_samples_and_removes_heavy_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            creature = {"id": 1, "observation": [1, 2], "brain": {"potentials": [1], "neurons": [{"x": 0}]}}
            frames = [{"type": "frame", "time": i, "creatures": [creature], "events": [
                {"type": "ingestion"}, {"type": "birth"}]} for i in range(5)]
            self.write_recording(directory, [self.metadata(), *frames])
            subprocess.run([sys.executable, str(ROOT / "tools" / "compact_replay.py"),
                            str(directory), "--every", "3"], check=True, capture_output=True)
            payload = VIEWER.read_replay(directory / "ecosystem_compact.jsonl.gz")
            self.assertEqual([frame["time"] for frame in payload["frames"]], [0, 3, 4])
            compact_creature = payload["frames"][0]["creatures"][0]
            self.assertNotIn("observation", compact_creature)
            self.assertNotIn("brain", compact_creature)
            self.assertEqual([event["type"] for event in payload["frames"][0]["events"]], ["birth"])

    def test_invalid_recordings_have_actionable_errors(self) -> None:
        cases = [
            ([{"type": "frame", "time": 0}], "metadata"),
            ([self.metadata()], "no frames"),
            ([{**self.metadata(), "terrain": [0]}, {"type": "frame", "time": 0}], "Terrain"),
            ([self.metadata(), {"type": "frame", "time": 2}, {"type": "frame", "time": 1}], "chronological"),
            ([self.metadata(), {"type": "frame", "time": float("nan")}], "non-finite"),
            ([self.metadata(), {"type": "frame", "time": 0, "creatures": [3]}], "creatures"),
            ([{**self.metadata(), "width": -1}, {"type": "frame", "time": 0}], "width"),
        ]
        with tempfile.TemporaryDirectory() as temp:
            for records, message in cases:
                with self.subTest(message=message):
                    path = self.write_recording(Path(temp), records)
                    with self.assertRaisesRegex(ValueError, message):
                        VIEWER.read_replay(path)

    @unittest.skipUnless(shutil.which("node"), "Node is optional; needed to exercise replay JavaScript")
    def test_playback_handles_births_deaths_and_missing_brain_activity(self) -> None:
        metadata = {**self.metadata(), "terrain": [0] * 6, "resources": [], "brains": [], "input_labels": [],
                    "predation": True, "attack_range": 0.8, "attack_degrees": 60}
        creature = {"id": 1, "x": 1, "y": 1, "energy": 50, "age": 0,
                    "mass": 1.5, "carnivory": 0.3, "health": 20, "max_health": 30, "attack": 0.5}
        child = {**creature, "id": 2, "parent": 0, "generation": 0,
                 "origin": "archive-clone", "source_id": 1, "genome_id": 1, "brain": {
            "inputs": 1, "outputs": 1,
            "neurons": [{"x": 0, "y": 0, "threshold": 1}, {"x": 1, "y": 1, "threshold": 1}],
            "synapses": [{"pre": 0, "post": 1, "weight": 1}],
            "potentials": [0, 0.5], "spiked": [0],
        }, "observation": [0.25]}
        frames = [{"time": i, "creatures": creatures, "resources": [], "events": [], "totals": {}}
                  for i, creatures in enumerate(([creature], [child], []))]
        frames[1]["resources"] = [{"id": 99, "kind": "meat", "x": 1.5, "y": 1, "stock": 1, "capacity": 2, "value": 20}]
        frames[1]["totals"] = {"immigrants": 1, "immigrant_clones": 1, "immigrant_energy": 90}
        frames[1]["establishment"] = {"enabled": True, "active": True, "floor": 2, "next_check": 5,
                                      "archive": [{"genome_id": 1, "niche": "fruit-a", "score": 3.5,
                                                   "trials": 2, "mean_food_energy": 10}]}
        frames[2]["establishment"] = {"enabled": True, "active": True, "floor": 2, "next_check": 5, "archive": []}
        payload = {"name": "lifecycle test", "metadata": metadata, "frames": frames,
                   "stats": [{"time": 0, "population": 1, "net_energy": -3},
                             {"time": 2, "population": 0, "net_energy": 2}]}
        # Execute the real rendering code against a minimal DOM/canvas model. This
        # checks lifecycle and optional-diagnostic failures without a browser package.
        harness = r'''
const assert=require('node:assert/strict'),vm=require('node:vm');
const nodes=new Map();
const context=new Proxy({measureText:t=>({width:String(t).length*6})},{get:(o,k)=>k in o?o[k]:(()=>{})});
function element(tag='div') {return {tagName:tag.toUpperCase(),style:{},dataset:{},children:[],listeners:{},checked:false,value:0,width:0,height:0,hidden:false,textContent:'',
classList:{add(){},remove(){}},getContext:()=>context,getBoundingClientRect:()=>({left:0,top:0,width:400,height:220}),
add(child){this.children.push(child)},append(...children){this.children.push(...children)},replaceChildren(...children){this.children=children},setAttribute(){},
addEventListener(name,fn){this.listeners[name]=fn}};}
const document={getElementById(id){if(!nodes.has(id))nodes.set(id,element());return nodes.get(id)},createElement:element,addEventListener(){}};
document.getElementById('replay-data').textContent=PAYLOAD;
document.getElementById('speed').value='5';
const scope={document,Option:function(text,value){return {...element('option'),textContent:text,value}},window:{devicePixelRatio:1,innerWidth:1200,innerHeight:900,addEventListener(){}},performance:{now:()=>0},requestAnimationFrame(){}};
vm.runInNewContext(SOURCE,scope);
assert.equal(nodes.get('bodyTraits').hidden,false);
assert.match(nodes.get('bodyTraits').textContent,/Mass 1.5/);
assert.equal(nodes.get('actions').children.length,6);
assert.equal(nodes.get('dietLabels').children.length,5);
assert.equal(nodes.get('statSelect').children.length,2);
nodes.get('statSelect').value='net_energy';
nodes.get('statSelect').listeners.change();
assert.match(nodes.get('statCaption').textContent,/ecosystem_stats.csv/);
vm.runInNewContext(`
 const sample={id:99,brain:{inputs:3,outputs:1,neurons:Array.from({length:5},()=>({threshold:1})),synapses:[{pre:2,post:3,weight:1},{pre:3,post:4,weight:1}],spiked:[2]},observation:[.1,.2,.3]};
 drawBrain(sample);
 if(brainPoints.map(p=>p.i).join(',')!=='2,3,4')throw Error('Filtered neuron indices changed');
 drawSensors(sample);
`,scope);
assert.equal(nodes.get('sensors').children.length,1);
vm.runInNewContext('render()',scope);
assert.equal(String(nodes.get('population').textContent),'1');
assert.equal(nodes.get('brainEmpty').style.display,'block');
assert.equal(nodes.get('establishmentPanel').hidden,true);
assert.equal(nodes.get('neuronDetails').children.length,0);
nodes.get('stepForward').listeners.click();
assert.equal(nodes.get('selectedContent').hidden,true);
assert.equal(nodes.get('establishmentPanel').hidden,false);
assert.equal(String(nodes.get('immigrants').textContent),'1');
assert.equal(nodes.get('supportState').textContent,'active');
assert.match(nodes.get('archiveEntries').children[0].textContent,/Genome 1/);
nodes.get('creatureSelect').listeners.change({target:{value:'2'}});
assert.equal(nodes.get('agentTitle').textContent,'Creature 2');
assert.equal(nodes.get('agentParent').textContent,'archive clone · source 1');
assert.equal(nodes.get('brainEmpty').style.display,'none');
vm.runInNewContext(`
 const edges=brains.get(2).synapses;
 edges.push({pre:1,post:0,weight:-0.125},{pre:0,post:0,weight:0},{pre:0,post:1,weight:0.00000001});
 drawBrain(F[index].creatures[0]);
 const p=brainPoints.find(p=>p.i===0);
 brain.listeners.click({clientX:p.x,clientY:p.y});
 if(selectedNeuron!==0)throw Error('Click did not select neuron zero');
`,scope);
const lists=nodes.get('neuronDetails').children[1].children;
assert.equal(lists[0].children[0].textContent,'Incoming synapses (2)');
assert.equal(lists[1].children[0].textContent,'Outgoing synapses (3)');
const rows=lists[0].children[1].children[0].children[0].children;
assert.equal(rows[0].children[1].textContent,'-0.125');
assert.equal(rows[1].children[1].textContent,'0');
vm.runInNewContext('render(); if(selectedNeuron!==0)throw Error("Selection lost during playback")',scope);
nodes.get('neuronSelect').listeners.change({target:{value:'1'}});
assert.match(nodes.get('neuronDetails').children[0].textContent,/#1/);
assert.equal(nodes.get('potentialHistory').hidden,false);
assert.match(nodes.get('potentialTitle').textContent,/Potential V history/);
assert.match(nodes.get('potentialCaption').textContent,/Current V: 0.5/);
vm.runInNewContext(`
 const history=potentialSamples(2,1);
 if(JSON.stringify(history.map(p=>p.value))!=='[null,0.5,null]')throw Error('History must retain missing samples and creature identity');
 selectNeuron(0);
 if(potentialCache.samples[1].value!==0)throw Error('Zero potential was lost');
 if(potentialCache.samples[1].threshold!==1||potentialCache.samples[1].spiked!==true)throw Error('Threshold or spike missing');
 if(potentialCache.samples[0].threshold!==null||potentialCache.samples[0].spiked!==null)throw Error('Absent creature has spike data');
 if(potentialSamples(2,1)[1].spiked!==false)throw Error('Non-spiking neuron marked as firing');
 M.calibrated_io=true;F[1].creatures[0].brain.neurons[0].threshold=2;
 if(potentialSamples(2,0)[1].threshold!==1)throw Error('Calibrated input must fire at phase 1');
 M.calibrated_io=false;F[1].creatures[0].brain.neurons[0].threshold=1;

 const saved=F[1].creatures[0].brain.potentials;
 F[1].creatures[0].brain.potentials=[null,NaN];
 if(potentialSamples(2,0).some(p=>p.value!==null)||potentialSamples(2,1).some(p=>p.value!==null))throw Error('Invalid samples were plotted');
 potentialCache={};drawPotentialHistory(brainData(F[1].creatures[0]));
 F[1].creatures[0].brain.potentials=saved;potentialCache={};
`,scope);
assert.match(nodes.get('potentialCaption').textContent,/was not recorded/);
nodes.get('clearNeuron').listeners.click();
assert.equal(nodes.get('neuronDetails').children.length,0);
assert.equal(nodes.get('potentialHistory').hidden,true);
vm.runInNewContext('selectNeuron(0)',scope);

nodes.get('sensorsTab').listeners.click();
assert.equal(nodes.get('sensors').children.length,1);
nodes.get('stepForward').listeners.click();
assert.equal(String(nodes.get('population').textContent),'0');
assert.equal(nodes.get('noCreature').hidden,false);
assert.match(nodes.get('worldStatus').textContent,/immigration remains active/);
assert.match(nodes.get('archiveEntries').children[0].textContent,/Waiting for food successes/);
nodes.get('stepBack').listeners.click();
assert.equal(nodes.get('agentTitle').textContent,'Creature 2');
'''
        source = VIEWER.TEMPLATE.split("<script>", 1)[1].split("</script>", 1)[0]
        harness = "const PAYLOAD=" + json.dumps(json.dumps(payload)) + ";\nconst SOURCE=" + json.dumps(source) + ";\n" + harness
        with tempfile.TemporaryDirectory() as temp:
            script = Path(temp) / "replay_test.js"
            script.write_text(harness, encoding="utf-8")
            result = subprocess.run([shutil.which("node"), str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
