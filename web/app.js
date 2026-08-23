const statusEl = document.getElementById("status");
const sqlEl = document.getElementById("sql");
const runBtn = document.getElementById("run");
const resultEl = document.getElementById("result");
const tablesEl = document.getElementById("tables");
const form = document.getElementById("query-form");

let engine;

function renderTables() {
  const tables = JSON.parse(engine.listTables());
  tablesEl.innerHTML = "";
  for (const t of tables) {
    const li = document.createElement("li");
    li.innerHTML = `<span class="name">${t.name}</span> ` +
      `<span class="meta">(${t.row_count} row${t.row_count === 1 ? "" : "s"}: ${t.columns.join(", ")})</span>`;
    tablesEl.appendChild(li);
  }
}

function renderResult(res) {
  if (!res.ok) {
    resultEl.innerHTML = `<span class="msg error">${escapeHtml(res.message)}</span>`;
    return;
  }
  if (!res.has_rows) {
    resultEl.innerHTML = `<span class="msg">${escapeHtml(res.message)}</span>`;
    return;
  }
  if (res.rows.length === 0) {
    resultEl.innerHTML = `<span class="msg">(0 rows)</span>`;
    return;
  }
  const thead = `<tr>${res.columns.map((c) => `<th>${escapeHtml(c)}</th>`).join("")}</tr>`;
  const tbody = res.rows
    .map((row) => `<tr>${row.map((v) => `<td>${escapeHtml(v)}</td>`).join("")}</tr>`)
    .join("");
  resultEl.innerHTML = `<table><thead>${thead}</thead><tbody>${tbody}</tbody></table>` +
    `<p class="msg">${escapeHtml(res.message)}</p>`;
}

function escapeHtml(s) {
  const div = document.createElement("div");
  div.textContent = s;
  return div.innerHTML;
}

function runQuery(sql) {
  if (sql === ".tables") {
    renderTables();
    resultEl.innerHTML = `<span class="msg">See Tables section below.</span>`;
    return;
  }
  const res = JSON.parse(engine.execute(sql));
  renderResult(res);
  renderTables();
}

form.addEventListener("submit", (e) => {
  e.preventDefault();
  const sql = sqlEl.value.trim();
  if (!sql) return;
  runQuery(sql);
});

function autoGrow() {
  sqlEl.style.height = "auto";
  sqlEl.style.height = sqlEl.scrollHeight + "px";
}
sqlEl.addEventListener("input", autoGrow);

document.querySelectorAll("code[data-fill]").forEach((el) => {
  el.addEventListener("click", () => {
    sqlEl.value = el.dataset.fill;
    sqlEl.focus();
    autoGrow();
  });
});

createTachyonModule().then((mod) => {
  engine = new mod.Engine();
  statusEl.textContent = "Engine ready.";
  sqlEl.disabled = false;
  runBtn.disabled = false;
  renderTables();
});
