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
    @unittest.skipUnless(shutil.which("node"), "Node required for compressed replay decoding")
    def test_lossless_compressed_tail_and_random_access(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            records = [self.metadata()] + [{"type": "frame", "time": t / 10,
                "creatures": [{"id": 1, "brain": {"potentials": [t / 17, -0.0],
                    "spiked": [t % 2]}, "observation": [t / 19]}],
                "resources": ([{"id": 2, "stock": t / 13, "kind": "meat", "x": 1}] if t % 3 else [])
                    + [{"id": 1, "stock": 2.0, **({"label": "</script>\u2028&"} if t % 2 else {})}],
                "events": [{"type": "birth", "time": t / 10}]} for t in range(21)]
            source = self.write_recording(directory, records)
            source.write_text(source.read_text().replace('-0.0', '-0'), encoding='utf-8')
            expected = VIEWER.read_replay(source)
            packed = VIEWER.read_replay(source, pack_resources=True)
            document = VIEWER.render_html(packed, compress=True)
            envelope = re.search(r'<script id="replay-data" type="application/json">(.*?)</script>', document, re.S).group(1)
            script = directory / "decode.js"
            script.write_text(VIEWER.REPLAY_LOADER + "\n(async()=>{\n"
                + "const replay=await decodeReplay({textContent:" + json.dumps(envelope) + "});\n"
                + "const expected=" + json.dumps(expected) + ";\n"
                + "const assert=require('node:assert/strict');\n"
                + "for(const i of [20,0,12,1,20,0])assert.deepEqual(replay.frames[i],expected.frames[i]);\n"
                + "assert.deepEqual(replay,expected);\n"
                + "})().catch(e=>{console.error(e);process.exitCode=1;});\n", encoding="utf-8")
            result = subprocess.run([shutil.which("node"), str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

            self.assertEqual(len(packed["frames"]), 21)
            self.assertNotIn('</script>\u2028&', document)

    def test_byte_budget_samples_tail_but_preserves_events_and_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            records = [self.metadata()] + [{"type": "frame", "time": t,
                "creatures": [{"id": 1, "brain": {"potentials": [t] * 300}, "observation": [t]}],
                "events": [{"type": "birth", "time": t}]} for t in range(103)]
            source = self.write_recording(directory, records)
            original = source.read_bytes()
            with self.assertWarnsRegex(UserWarning, "sampled"):
                payload = VIEWER.read_replay(source, max_frame_bytes=12000)
            frames = payload["frames"]
            self.assertLess(len(frames), 20)
            self.assertEqual((frames[0]["time"], frames[-1]["time"]), (0, 102))
            self.assertEqual([e["time"] for f in frames for e in f["events"]], list(range(103)))
            for frame in frames:
                self.assertEqual(frame["creatures"][0]["brain"]["potentials"], [frame["time"]] * 300)
                self.assertEqual(frame["creatures"][0]["observation"], [frame["time"]])
            self.assertEqual(source.read_bytes(), original)
            self.assertIn('replay-data', VIEWER.render_html(payload))
            self.assertEqual(len(VIEWER.read_replay(source, max_frame_bytes=1000000)["frames"]), 103)

    def test_bounded_overview_preserves_endpoints_events_and_drops_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            metadata = self.metadata()
            metadata["brains"] = [{"id": 1, "neurons": [{"threshold": 1}]}]
            records = [metadata] + [{"type": "frame", "time": t,
                "creatures": [{"id": 1, "brain": {"potentials": [t]}, "observation": [1]}],
                "resources": [{"id": t, "kind": "meat", "stock": 1}],
                "events": [{"type": "birth", "time": t}, {"type": "ingestion", "time": t}]}
                for t in range(103)]
            source = self.write_recording(directory, records)
            payload = VIEWER.read_replay(source, overview=True, max_frames=7)
            frames = payload["frames"]
            self.assertLessEqual(len(frames), 7)
            self.assertEqual((frames[0]["time"], frames[-1]["time"]), (0, 102))
            self.assertEqual(payload["source_frames"], 103)
            self.assertEqual(payload["metadata"]["brains"], [])
            self.assertEqual([e["time"] for f in frames for e in f["events"]], list(range(103)))
            self.assertTrue(all("brain" not in c and "observation" not in c for f in frames for c in f["creatures"]))
            self.assertEqual(frames[-1]["resources"][0]["id"], 102)
            detailed = VIEWER.read_replay(source)
            self.assertEqual(len(detailed["frames"]), 103)
            self.assertIn("potentials", detailed["frames"][-1]["creatures"][0]["brain"])
            records[7]["time"] = 0
            self.write_recording(directory, records)
            with self.assertRaisesRegex(ValueError, "chronological"):
                VIEWER.read_replay(source, overview=True, max_frames=7)

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell required for rebuild wrapper")
    def test_rebuild_wrapper_preserves_sources_and_handles_both_recordings(self) -> None:
        with tempfile.TemporaryDirectory(prefix="rebuild ecosystem ") as temp:
            directory = Path(temp)
            source = self.write_recording(directory, [self.metadata(), {"type": "frame", "time": 0}])
            original = source.read_bytes()
            tail = directory / "ecosystem_tail.jsonl.gz"
            with gzip.open(tail, "wb") as f:
                f.write(original)
            tail_original = tail.read_bytes()
            command = [shutil.which("pwsh"), "-NoProfile", "-File",
                       str(ROOT / "scripts" / "rebuild-ecosystem.ps1"), "-RunDir", str(directory)]
            subprocess.run(command, capture_output=True, text=True, check=True, timeout=30)
            main_html = (directory / "ecosystem.html").read_bytes()
            self.assertIn(b'replay-data', main_html)
            self.assertIn(b'replay-data', (directory / "ecosystem_tail.html").read_bytes())
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(tail.read_bytes(), tail_original)
            source.write_bytes(b'broken\n')
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((directory / "ecosystem.html").read_bytes(), main_html)
            self.assertFalse(list(directory.glob('*.tmp.html')))

    def test_recovery_only_skips_incomplete_final_json_record(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            source = self.write_recording(directory, [self.metadata(), {"type": "frame", "time": 0}])
            complete = source.read_bytes()
            broken = complete + b'\n{"type":"frame","time":1,"creatures":['
            source.write_bytes(broken)
            with self.assertRaises(ValueError):
                VIEWER.read_replay(source)
            with self.assertWarnsRegex(UserWarning, "incomplete final"):
                recovered = VIEWER.read_replay(source, recover_truncated=True)
            self.assertEqual(len(recovered["frames"]), 1)
            self.assertEqual(source.read_bytes(), broken)
            compressed = directory / "ecosystem_tail.jsonl.gz"
            with gzip.open(compressed, "wb") as f:
                f.write(broken)
            with self.assertWarns(UserWarning):
                self.assertEqual(len(VIEWER.read_replay(compressed, recover_truncated=True)["frames"]), 1)
            source.write_bytes(broken + b'\n{"type":"frame","time":2}\n')
            with self.assertRaises(ValueError):
                VIEWER.read_replay(source, recover_truncated=True)
            source.write_bytes(complete + b'\n{"type":"frame","time":NaN}')
            with self.assertRaisesRegex(ValueError, "non-finite"):
                VIEWER.read_replay(source, recover_truncated=True)

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
        metadata["food_sources"] = [
            {"id": 1, "kind": "field", "x": 1, "y": 1, "radius": .4, "phase": 0},
            {"id": 2, "kind": "fruit-tree", "x": 2, "y": 1, "radius": .3, "phase": 1},
            {"id": 3, "kind": "pod-tree", "x": 2.5, "y": 1, "radius": .2, "phase": 2},
        ]
        metadata["field_spacing"] = 1.5
        metadata["resources"] = [
            {"id": 10, "source_id": 1, "kind": "graze", "x": 1, "y": 1, "capacity": 2},
            {"id": 11, "source_id": 2, "kind": "fruit-a", "x": 2, "y": 1, "capacity": 4},
            {"id": 12, "source_id": 3, "kind": "pod", "x": 2.5, "y": 1, "capacity": 10},
        ]
        creature = {"id": 1, "x": 1, "y": 1, "energy": 50, "age": 0,
                    "mass": 1.5, "carnivory": 0.3, "health": 20, "max_health": 30, "attack": 0.5}
        child = {**creature, "id": 2, "parent": 1, "generation": 1,
                 "origin": "birth", "genome_id": 1, "brain": {
            "inputs": 1, "outputs": 1,
            "neurons": [{"x": 0, "y": 0, "threshold": 1}, {"x": 1, "y": 1, "threshold": 1}],
            "synapses": [{"pre": 0, "post": 1, "weight": 1}],
            "potentials": [0, 0.5], "spiked": [0],
        }, "observation": [0.25]}
        frames = [{"time": i, "creatures": creatures, "resources": [], "events": [], "totals": {}}
                  for i, creatures in enumerate(([creature], [child], []))]
        frames[1]["resources"] = [{"id": 99, "kind": "meat", "x": 1.5, "y": 1, "stock": 1, "capacity": 2, "value": 20}]
        for frame in frames:
            frame["resources"] += [
                {"id": 10, "stock": .5, "ripening_remaining": -1},
                {"id": 11, "stock": 0, "ripening_remaining": 30},
                {"id": 12, "stock": 2, "state": "refilling"},
            ]
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
assert.equal(nodes.get('neuronDetails').children.length,0);
nodes.get('stepForward').listeners.click();
assert.equal(nodes.get('selectedContent').hidden,true);
nodes.get('creatureSelect').listeners.change({target:{value:'2'}});
assert.equal(nodes.get('agentTitle').textContent,'Creature 2');
assert.equal(nodes.get('agentParent').textContent,'parent 1');
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
 const originalInput=F[1].creatures[0].observation;
 F[1].creatures[0].observation=[0];
 F[1].creatures[0].brain.potentials[0]=1;
 potentialCache={};drawPotentialHistory(brainData(F[1].creatures[0]));
 if(potentialCache.samples[1].input!==0||potentialCache.samples[1].value!==1)throw Error('Sensor input was confused with retained phase');
 if(!$('potentialCaption').textContent.includes('Sensor input: 0 (off)'))throw Error('Inactive sensor not identified');
 F[1].creatures[0].observation=[1];
 potentialCache={};drawPotentialHistory(brainData(F[1].creatures[0]));
 if(!$('potentialCaption').textContent.includes('Sensor input: 1'))throw Error('Reactivated sensor did not refresh');
 F[1].creatures[0].observation=undefined;
 potentialCache={};drawPotentialHistory(brainData(F[1].creatures[0]));
 if(!$('potentialCaption').textContent.includes('Sensor input: not recorded'))throw Error('Missing input shown as zero');
 F[1].creatures[0].observation=originalInput;F[1].creatures[0].brain.potentials[0]=0;
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
assert.match(nodes.get('worldStatus').textContent,/Extinct/);
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
            # Exercise the actual asynchronous compressed-page startup and its
            # interactive brain controls with the same DOM/canvas fixture.
            compressed = VIEWER.render_html(payload, compress=True)
            compressed_source = compressed.split("<script>", 1)[1].split("</script>", 1)[0]
            compressed_data = re.search(r'<script id="replay-data" type="application/json">(.*?)</script>', compressed, re.S).group(1)
            startup = harness.split("vm.runInNewContext(SOURCE,scope);", 1)[0]
            startup += "\nObject.assign(scope,{DecompressionStream,Response,Blob,atob});\n"
            startup += "document.getElementById('replay-data').textContent=" + json.dumps(compressed_data) + ";\n"
            startup += "(async()=>{await vm.runInNewContext(" + json.dumps(compressed_source) + ",scope);\n"
            startup += "assert.equal(String(nodes.get('population').textContent),'1');\n"
            startup += "nodes.get('stepForward').listeners.click();\n"
            startup += "nodes.get('creatureSelect').listeners.change({target:{value:'2'}});\n"
            startup += "assert.equal(nodes.get('brainEmpty').style.display,'none');\n"
            startup += "nodes.get('neuronSelect').listeners.change({target:{value:'1'}});\n"
            startup += "assert.equal(nodes.get('potentialHistory').hidden,false);\n"
            startup += "assert.match(nodes.get('potentialCaption').textContent,/Current V: 0.5/);\n"
            startup += "})().catch(e=>{console.error(e);process.exitCode=1});\n"
            script.write_text(startup, encoding="utf-8")
            result = subprocess.run([shutil.which("node"), str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
