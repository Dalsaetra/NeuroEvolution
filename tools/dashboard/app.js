const state = {
  schema: null,
  values: {},
  runs: [],
  jobs: [],
  build: { ready: false },
  selectedRun: null,
  runData: null,
  replayMode: "episode",
  pollBusy: false,
};

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];

function formatNumber(value, digits = 3) {
  if (value === null || value === undefined || value === "") return "—";
  const number = Number(value);
  if (!Number.isFinite(number)) return "—";
  if (Math.abs(number) >= 10000) return number.toLocaleString(undefined, { maximumFractionDigits: 0 });
  return number.toLocaleString(undefined, { maximumFractionDigits: digits });
}

function timestampName() {
  const d = new Date();
  const two = (v) => String(v).padStart(2, "0");
  return `run_${d.getFullYear()}${two(d.getMonth()+1)}${two(d.getDate())}_${two(d.getHours())}${two(d.getMinutes())}${two(d.getSeconds())}`;
}

function relativeTime(iso) {
  if (!iso) return "—";
  const seconds = Math.max(0, (Date.now() - new Date(iso).getTime()) / 1000);
  if (seconds < 60) return `${Math.floor(seconds)}s ago`;
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`;
  return new Date(iso).toLocaleDateString();
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json", ...(options.headers || {}) },
    ...options,
  });
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(payload.error || `${response.status} ${response.statusText}`);
  return payload;
}

function toast(message, type = "") {
  const node = document.createElement("div");
  node.className = `toast ${type}`;
  node.textContent = message;
  $("#toastRegion").append(node);
  setTimeout(() => node.remove(), 4800);
}

function defaultValues() {
  return Object.fromEntries(state.schema.parameters.map((parameter) => [parameter.id, parameter.default]));
}

function savedDraft() {
  try { return JSON.parse(localStorage.getItem("neuroevo-dashboard-draft") || "null"); }
  catch { return null; }
}

function saveDraft() {
  localStorage.setItem("neuroevo-dashboard-draft", JSON.stringify(state.values));
}

function isRelevant(spec) {
  if (!spec.relevantWhen) return true;
  return Object.entries(spec.relevantWhen).every(([key, value]) => state.values[key] === value);
}

function renderParameters() {
  const container = $("#parameterGroups");
  container.innerHTML = "";
  state.schema.groups.forEach((group, groupIndex) => {
    const specs = state.schema.parameters.filter((parameter) => parameter.group === group.id);
    const details = document.createElement("details");
    details.dataset.group = group.id;
    details.open = groupIndex < 4;
    details.innerHTML = `<summary><strong>${group.label}</strong><span>${specs.length} parameters</span></summary><p class="group-description">${group.description}</p><div class="parameter-list"></div>`;
    const list = details.querySelector(".parameter-list");
    specs.forEach((spec) => list.append(createParameterField(spec)));
    container.append(details);
  });
  updateParameterRelevance();
}

function createParameterField(spec) {
  const field = document.createElement("div");
  field.className = "parameter-field";
  field.dataset.parameter = spec.id;
  field.dataset.search = `${spec.label} ${spec.help} ${spec.id}`.toLowerCase();
  field.title = spec.help;

  const label = document.createElement("div");
  label.className = "parameter-label";
  label.innerHTML = `<label for="parameter-${spec.id}">${spec.label}</label><small>${spec.help}</small>`;
  const control = document.createElement("div");
  control.className = "parameter-control";
  let input;
  if (spec.type === "boolean") {
    const wrapper = document.createElement("label");
    wrapper.className = "switch";
    input = document.createElement("input");
    input.type = "checkbox";
    input.checked = Boolean(state.values[spec.id]);
    wrapper.append(input, document.createElement("i"));
    control.append(wrapper);
  } else if (spec.type === "enum") {
    input = document.createElement("select");
    spec.choices.forEach((choice) => {
      const option = document.createElement("option");
      option.value = choice;
      option.textContent = choice;
      input.append(option);
    });
    input.value = state.values[spec.id];
    control.append(input);
  } else {
    input = document.createElement("input");
    input.type = "number";
    if (spec.min !== undefined) input.min = spec.min;
    if (spec.max !== undefined) input.max = spec.max;
    if (spec.step !== undefined) input.step = spec.step;
    input.value = state.values[spec.id];
    control.append(input);
  }
  input.id = `parameter-${spec.id}`;
  input.dataset.id = spec.id;
  const commitValue = () => {
    if (spec.type === "boolean") state.values[spec.id] = input.checked;
    else if (spec.type === "integer") state.values[spec.id] = Number.parseInt(input.value, 10);
    else if (spec.type === "number") state.values[spec.id] = Number.parseFloat(input.value);
    else state.values[spec.id] = input.value;
    saveDraft();
    updateParameterRelevance();
    updateWorkEstimate();
  };
  input.addEventListener("input", commitValue);
  input.addEventListener("change", commitValue);
  field.append(label, control);
  return field;
}

function syncParameterControls() {
  state.schema.parameters.forEach((spec) => {
    const input = $(`#parameter-${CSS.escape(spec.id)}`);
    if (!input) return;
    if (spec.type === "boolean") input.checked = Boolean(state.values[spec.id]);
    else input.value = state.values[spec.id];
  });
  updateParameterRelevance();
  updateWorkEstimate();
}

function updateParameterRelevance() {
  state.schema.parameters.forEach((spec) => {
    const field = document.querySelector(`[data-parameter="${spec.id}"]`);
    if (field) field.classList.toggle("irrelevant", !isRelevant(spec));
  });
}

function filterParameters() {
  const query = $("#parameterSearch").value.trim().toLowerCase();
  $$(".parameter-field").forEach((field) => field.classList.toggle("search-hidden", query && !field.dataset.search.includes(query)));
  if (query) $$(".parameter-groups details").forEach((details) => { details.open = true; });
}

function updateWorkEstimate() {
  const generations = Number(state.values.generations || 0);
  const population = Number(state.values.population || 0);
  const trials = Number(state.values.trials || 0);
  const steps = Number(state.values.steps || 0);
  const multiplier = state.values["ea-mode"] === "neat-nsga2" ? 2 : 1;
  const evaluations = generations * population * trials * multiplier;
  const envSteps = evaluations * steps;
  $("#workEstimate").textContent = `≈ ${evaluations.toLocaleString()} genome trials · ${envSteps.toLocaleString()} environment steps`;
}

function presets() {
  try { return JSON.parse(localStorage.getItem("neuroevo-dashboard-presets") || "{}"); }
  catch { return {}; }
}

function renderPresets() {
  const select = $("#presetSelect");
  const selected = select.value;
  select.innerHTML = '<option value="">Saved presets</option>';
  Object.keys(presets()).sort().forEach((name) => {
    const option = document.createElement("option"); option.value = name; option.textContent = name; select.append(option);
  });
  if ([...select.options].some((option) => option.value === selected)) select.value = selected;
}

function savePreset() {
  const name = prompt("Preset name:");
  if (!name?.trim()) return;
  const all = presets(); all[name.trim()] = state.values;
  localStorage.setItem("neuroevo-dashboard-presets", JSON.stringify(all));
  renderPresets(); $("#presetSelect").value = name.trim(); toast(`Saved preset “${name.trim()}”.`, "success");
}

function loadPreset(name) {
  if (!name) return;
  const values = presets()[name];
  if (!values) return;
  state.values = { ...defaultValues(), ...values }; saveDraft(); syncParameterControls();
}

function deletePreset() {
  const name = $("#presetSelect").value;
  if (!name) return;
  const all = presets(); delete all[name]; localStorage.setItem("neuroevo-dashboard-presets", JSON.stringify(all)); renderPresets();
  toast(`Deleted preset “${name}”.`);
}

function renderBuildState() {
  const pill = $("#buildState");
  pill.className = `status-pill ${state.build.ready ? "ready" : "neutral"}`;
  pill.innerHTML = `<span></span>${state.build.ready ? "Simulator ready" : "Build required"}`;
}

function activeJob() {
  return state.jobs.find((job) => ["queued", "running", "stopping"].includes(job.status)) || state.jobs[0] || null;
}

function renderJob() {
  const job = activeJob();
  const panel = $("#jobPanel");
  if (!job) { panel.classList.add("hidden"); return; }
  panel.classList.remove("hidden");
  $("#jobKind").textContent = job.kind;
  $("#jobTitle").textContent = job.label;
  const status = $("#jobStatus"); status.className = `status-pill ${job.status}`; status.innerHTML = `<span></span>${job.status}`;
  $("#stopButton").classList.toggle("hidden", !["queued", "running", "stopping"].includes(job.status));
  $("#stopButton").dataset.jobId = job.id;
  const log = $("#jobLog"); log.textContent = (job.log || []).join("\n") || "Waiting for output…"; log.scrollTop = log.scrollHeight;
}

function renderRuns() {
  const body = $("#runsBody"); body.innerHTML = "";
  $("#runsEmpty").classList.toggle("hidden", state.runs.length > 0);
  state.runs.forEach((run) => {
    const row = document.createElement("tr");
    row.classList.toggle("selected", run.name === state.selectedRun);
    const complete = run.generationsTarget > 0 && run.generationsCompleted >= run.generationsTarget;
    row.innerHTML = `
      <td>${run.name}</td><td>${run.eaMode || "—"}</td><td>${run.task || "—"}</td>
      <td class="${complete ? "completed" : "partial"}">${run.generationsCompleted}/${run.generationsTarget || "?"}</td>
      <td>${formatNumber(run.final.best_fitness)}</td><td>${formatNumber(run.final.best_foods_collected, 2)}</td><td>${relativeTime(run.modifiedIso)}</td>`;
    row.addEventListener("click", () => selectRun(run.name));
    body.append(row);
  });
}

async function selectRun(name, scrollToReplay = false) {
  state.selectedRun = name;
  renderRuns();
  await loadRunData();
  if (scrollToReplay) $("#replayPanel").scrollIntoView({ behavior: "smooth", block: "start" });
}

async function loadRunData() {
  if (!state.selectedRun) { state.runData = null; renderRunData(); return; }
  try {
    state.runData = await api(`/api/runs/${encodeURIComponent(state.selectedRun)}/data`);
    const fresh = state.runData.run;
    const index = state.runs.findIndex((run) => run.name === fresh.name);
    if (index >= 0) state.runs[index] = fresh;
    renderRunData(); renderRuns();
  } catch (error) { toast(error.message, "error"); }
}

function renderRunData() {
  const data = state.runData;
  if (!data) {
    $("#selectedRunTitle").textContent = "No run selected";
    $("#selectedRunSubtitle").textContent = "Build the simulator or select a previous experiment below.";
    updateMetrics(null); drawCharts([]); renderReplay(); return;
  }
  const run = data.run;
  $("#selectedRunTitle").textContent = run.name;
  $("#selectedRunSubtitle").textContent = `${run.eaMode || "Unknown mode"} · ${run.task || "Unknown task"} · ${run.generationsCompleted} generations recorded`;
  const target = Number(run.generationsTarget || 0);
  const completed = Number(run.generationsCompleted || 0);
  const percent = target ? Math.min(100, completed / target * 100) : 0;
  $("#progressBar").style.width = `${percent}%`;
  $("#progressPercent").textContent = `${Math.round(percent)}%`;
  $("#progressLabel").textContent = target ? `Generation ${completed} of ${target}` : `${completed} generations`;
  const latest = data.stats.at(-1) || null;
  updateMetrics(latest, data.stats);
  drawCharts(data.stats);
  renderReplay();
}

function updateMetrics(latest, rows = []) {
  const isNeat = state.runData?.run?.eaMode === "neat-nsga2";
  $("#bestFitness").textContent = formatNumber(latest?.best_fitness);
  $("#meanFitness").textContent = formatNumber(latest?.mean_fitness);
  $("#foodsCollected").textContent = formatNumber(latest?.best_foods_collected, 2);
  $("#topologyMetricLabel").textContent = isNeat ? "Species" : "Mean synapses";
  $("#speciesCount").textContent = formatNumber(isNeat ? latest?.species_count : latest?.mean_synapses, isNeat ? 0 : 1);
  $("#occludedFoods").textContent = latest ? `${formatNumber(latest.best_occluded_foods_collected, 2)} during occlusion` : "Best genome";
  $("#topologySummary").textContent = latest
    ? (isNeat
      ? `${formatNumber(latest.mean_neurons, 1)} neurons · ${formatNumber(latest.mean_enabled_synapses || latest.mean_synapses, 1)} enabled synapses`
      : "Fixed scalar topology")
    : "Population topology";
  if (rows.length > 1) {
    const first = Number(rows[0].best_fitness); const last = Number(latest.best_fitness); const delta = last - first;
    $("#fitnessDelta").textContent = `${delta >= 0 ? "+" : ""}${formatNumber(delta)} from generation 1`;
  } else $("#fitnessDelta").textContent = latest ? "Generation one" : "Awaiting data";
}

function drawCharts(rows) {
  const fitnessHasData = rows.length > 0;
  $("#fitnessEmpty").classList.toggle("hidden", fitnessHasData);
  $("#taskEmpty").classList.toggle("hidden", fitnessHasData);
  drawLineChart($("#fitnessChart"), rows, [
    { key: "best_fitness", color: "#6ff0bd" }, { key: "mean_fitness", color: "#75bcff" },
  ]);
  drawLineChart($("#taskChart"), rows, [
    { key: "best_foods_collected", color: "#6ff0bd" }, { key: "best_occluded_foods_collected", color: "#aa96ff" },
  ]);
}

function drawLineChart(canvas, rows, series) {
  const rect = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(rect.width * dpr)); canvas.height = Math.floor(280 * dpr);
  const ctx = canvas.getContext("2d"); ctx.scale(dpr, dpr);
  const width = rect.width; const height = 280; const pad = { l: 49, r: 16, t: 17, b: 30 };
  ctx.clearRect(0, 0, width, height);
  if (!rows.length) return;
  const values = series.flatMap((line) => rows
    .map((row) => row[line.key])
    .filter((value) => value !== null && value !== undefined && value !== "")
    .map(Number)
    .filter(Number.isFinite));
  if (!values.length) return;
  let min = Math.min(...values); let max = Math.max(...values);
  if (min === max) { min -= Math.max(1, Math.abs(min) * .1); max += Math.max(1, Math.abs(max) * .1); }
  const margin = (max - min) * .08; min -= margin; max += margin;
  ctx.font = "9px Inter, sans-serif"; ctx.fillStyle = "#61746f"; ctx.strokeStyle = "rgba(188,224,214,.09)"; ctx.lineWidth = 1;
  for (let i = 0; i <= 4; i++) {
    const y = pad.t + (height - pad.t - pad.b) * i / 4;
    ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(width - pad.r, y); ctx.stroke();
    const value = max - (max - min) * i / 4; ctx.fillText(formatNumber(value, 2), 2, y + 3);
  }
  const xAt = (index) => pad.l + (width - pad.l - pad.r) * (rows.length === 1 ? .5 : index / (rows.length - 1));
  const yAt = (value) => pad.t + (max - value) / (max - min) * (height - pad.t - pad.b);
  series.forEach((line) => {
    ctx.beginPath(); ctx.strokeStyle = line.color; ctx.lineWidth = 2; ctx.lineJoin = "round"; ctx.lineCap = "round";
    let started = false;
    rows.forEach((row, index) => {
      const raw = row[line.key];
      if (raw === null || raw === undefined || raw === "") return;
      const value = Number(raw); if (!Number.isFinite(value)) return;
      const x = xAt(index), y = yAt(value); if (!started) { ctx.moveTo(x, y); started = true; } else ctx.lineTo(x, y);
    });
    ctx.stroke();
  });
  ctx.fillStyle = "#61746f"; ctx.fillText("1", pad.l, height - 8); ctx.fillText(String(rows.length), width - pad.r - 12, height - 8);
  ctx.fillText("generation", Math.max(pad.l, width / 2 - 22), height - 8);
}

function replayUrl() {
  const run = state.runData?.run;
  if (!run) return null;
  if (state.replayMode === "pareto") return run.hasParetoViewer ? `/artifacts/${encodeURIComponent(run.name)}/pareto_front.html` : null;
  return run.hasViewer ? `/artifacts/${encodeURIComponent(run.name)}/viewer.html` : null;
}

function renderReplay(forceReload = false) {
  const url = replayUrl(); const iframe = $("#replayFrame"); const placeholder = $("#replayPlaceholder");
  $("#episodeTab").classList.toggle("active", state.replayMode === "episode");
  $("#paretoTab").classList.toggle("active", state.replayMode === "pareto");
  $("#paretoTab").disabled = !state.runData?.run?.hasPareto;
  if (url) {
    placeholder.classList.add("hidden"); iframe.classList.remove("hidden");
    const desired = `${url}${forceReload ? `?v=${Date.now()}` : ""}`;
    if (forceReload || !iframe.src.includes(url)) iframe.src = desired;
    $("#openViewerButton").href = url; $("#openViewerButton").classList.remove("disabled");
  } else {
    iframe.classList.add("hidden"); iframe.removeAttribute("src"); placeholder.classList.remove("hidden");
    const title = placeholder.querySelector("h4"); const copy = placeholder.querySelector("p");
    if (state.runData?.run?.hasTrajectory) { title.textContent = "Visualization not generated yet"; copy.textContent = "Generate views to create the interactive episode and brain replay."; }
    else if (state.selectedRun) { title.textContent = "Episode still running"; copy.textContent = "A replay becomes available after final trajectory recording completes."; }
    else { title.textContent = "Select a completed run"; copy.textContent = "The interactive replay combines creature movement, motor output, neuron activity, and synaptic events."; }
    $("#openViewerButton").classList.add("disabled"); $("#openViewerButton").removeAttribute("href");
  }
}

async function startBuild() {
  try { const result = await api("/api/build", { method: "POST", body: "{}" }); state.jobs.unshift(result.job); renderJob(); toast("Build started.", "success"); }
  catch (error) { toast(error.message, "error"); }
}

async function startRun() {
  const runName = $("#runName").value.trim();
  if (!runName) { toast("Choose a run name.", "error"); return; }
  try {
    const result = await api("/api/run", { method: "POST", body: JSON.stringify({ runName, parameters: state.values }) });
    state.jobs.unshift(result.job); state.selectedRun = result.runName; state.runData = null; renderJob(); renderRunData(); toast(`Started ${runName}.`, "success");
  } catch (error) { toast(error.message, "error"); }
}

async function visualizeSelected() {
  if (!state.selectedRun) { toast("Select a completed run first.", "error"); return; }
  try { const result = await api("/api/visualize", { method: "POST", body: JSON.stringify({ runName: state.selectedRun }) }); state.jobs.unshift(result.job); renderJob(); toast("Generating replay views.", "success"); }
  catch (error) { toast(error.message, "error"); }
}

async function stopJob() {
  const jobId = $("#stopButton").dataset.jobId; if (!jobId) return;
  try { await api("/api/stop", { method: "POST", body: JSON.stringify({ jobId }) }); toast("Stop requested."); }
  catch (error) { toast(error.message, "error"); }
}

async function visualizeLatest() {
  if (!state.runs.length) { toast("No simulation runs are available yet.", "error"); return; }
  await selectRun(state.runs[0].name, true);
  if (state.runData?.run?.hasTrajectory && !state.runData.run.hasViewer) await visualizeSelected();
}

async function poll() {
  if (state.pollBusy) return;
  state.pollBusy = true;
  try {
    const latest = await api("/api/state");
    const priorJob = state.jobs[0];
    state.runs = latest.runs; state.jobs = latest.jobs; state.build = latest.build;
    renderBuildState(); renderJob(); renderRuns();
    if (state.selectedRun) await loadRunData();
    const currentJob = state.jobs[0];
    if (priorJob && currentJob && priorJob.id === currentJob.id && priorJob.status !== currentJob.status && currentJob.status === "succeeded") {
      toast(`${currentJob.label} completed.`, "success");
      if (currentJob.runName) { state.selectedRun = currentJob.runName; await loadRunData(); renderReplay(true); }
    }
  } catch (error) { console.warn(error); }
  finally { state.pollBusy = false; }
}

function bindEvents() {
  $("#parameterSearch").addEventListener("input", filterParameters);
  $("#resetButton").addEventListener("click", () => { state.values = defaultValues(); saveDraft(); syncParameterControls(); toast("Parameters reset to simulator defaults."); });
  $("#refreshNameButton").addEventListener("click", () => { $("#runName").value = timestampName(); });
  $("#buildButton").addEventListener("click", startBuild);
  $("#latestButton").addEventListener("click", visualizeLatest);
  $("#runButton").addEventListener("click", startRun);
  $("#sidebarRunButton").addEventListener("click", startRun);
  $("#stopButton").addEventListener("click", stopJob);
  $("#refreshRunsButton").addEventListener("click", poll);
  $("#generateViewerButton").addEventListener("click", visualizeSelected);
  $("#episodeTab").addEventListener("click", () => { state.replayMode = "episode"; renderReplay(); });
  $("#paretoTab").addEventListener("click", () => { state.replayMode = "pareto"; renderReplay(); });
  $("#savePresetButton").addEventListener("click", savePreset);
  $("#deletePresetButton").addEventListener("click", deletePreset);
  $("#presetSelect").addEventListener("change", (event) => loadPreset(event.target.value));
  window.addEventListener("resize", () => drawCharts(state.runData?.stats || []));
}

async function initialize() {
  try {
    const bootstrap = await api("/api/bootstrap");
    state.schema = bootstrap.schema; state.runs = bootstrap.runs; state.jobs = bootstrap.jobs; state.build = bootstrap.build;
    state.values = { ...defaultValues(), ...(savedDraft() || {}) };
    $("#runName").value = timestampName();
    renderParameters(); renderPresets(); updateWorkEstimate(); renderBuildState(); renderJob(); renderRuns(); bindEvents();
    if (state.runs.length) await selectRun(state.runs[0].name);
    else renderRunData();
    setInterval(poll, 1200);
  } catch (error) {
    document.body.innerHTML = `<main style="padding:40px;color:#ff9b88"><h1>Dashboard failed to start</h1><pre>${error.message}</pre></main>`;
  }
}

initialize();
