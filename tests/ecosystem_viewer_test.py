from __future__ import annotations

import importlib.util
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
        metadata = {**self.metadata(), "terrain": [0] * 6, "resources": [], "brains": [], "input_labels": []}
        creature = {"id": 1, "x": 1, "y": 1, "energy": 50, "age": 0}
        child = {**creature, "id": 2, "parent": 1, "generation": 1, "brain": {
            "inputs": 1, "outputs": 1,
            "neurons": [{"x": 0, "y": 0, "threshold": 1}, {"x": 1, "y": 1, "threshold": 1}],
            "synapses": [{"pre": 0, "post": 1, "weight": 1}],
            "potentials": [0, 0.5], "spiked": [0],
        }, "observation": [0.25]}
        frames = [{"time": i, "creatures": creatures, "resources": [], "events": [], "totals": {}}
                  for i, creatures in enumerate(([creature], [child], []))]
        payload = {"name": "lifecycle test", "metadata": metadata, "frames": frames}
        # Execute the real rendering code against a minimal DOM/canvas model. This
        # checks lifecycle and optional-diagnostic failures without a browser package.
        harness = r'''
const assert=require('node:assert/strict'),vm=require('node:vm');
const nodes=new Map();
const context=new Proxy({measureText:t=>({width:String(t).length*6})},{get:(o,k)=>k in o?o[k]:(()=>{})});
function element(tag='div') {return {tagName:tag.toUpperCase(),style:{},children:[],listeners:{},checked:false,value:0,width:0,height:0,hidden:false,textContent:'',
classList:{add(){},remove(){}},getContext:()=>context,getBoundingClientRect:()=>({width:400,height:220}),
append(...children){this.children.push(...children)},replaceChildren(...children){this.children=children},setAttribute(){},
addEventListener(name,fn){this.listeners[name]=fn}};}
const document={getElementById(id){if(!nodes.has(id))nodes.set(id,element());return nodes.get(id)},createElement:element,addEventListener(){}};
document.getElementById('replay-data').textContent=PAYLOAD;
document.getElementById('speed').value='5';
const scope={document,window:{devicePixelRatio:1,innerWidth:1200,innerHeight:900,addEventListener(){}},performance:{now:()=>0},requestAnimationFrame(){}};
vm.runInNewContext(SOURCE,scope);
assert.equal(String(nodes.get('population').textContent),'1');
assert.equal(nodes.get('brainEmpty').style.display,'block');
nodes.get('stepForward').listeners.click();
assert.equal(nodes.get('selectedContent').hidden,true);
nodes.get('creatureSelect').listeners.change({target:{value:'2'}});
assert.equal(nodes.get('agentTitle').textContent,'Creature 2');
assert.equal(nodes.get('brainEmpty').style.display,'none');
nodes.get('sensorsTab').listeners.click();
assert.equal(nodes.get('sensors').children.length,1);
nodes.get('stepForward').listeners.click();
assert.equal(String(nodes.get('population').textContent),'0');
assert.equal(nodes.get('noCreature').hidden,false);
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
