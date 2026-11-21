import { useEffect, useRef, useState } from "react";
import { useApp } from "../state";
import type { SearchResponse } from "../lib/types";
import { formatBytes, formatCount, formatAgo, middleEllipsis } from "../lib/format";
import { categoryOf } from "../lib/colors";
import { Empty, ItemIcon, Loading, PageHead, RevealButton } from "../components/common";
import { IconArrowRight, IconSearch } from "../components/Icons";

export function SearchView() {
  const { api, generation, go, reveal, notify } = useApp();
  const [q, setQ] = useState("");
  const [res, setRes] = useState<SearchResponse | null>(null);
  const [busy, setBusy] = useState(false);
  const [tab, setTab] = useState<"files" | "dirs">("files");
  const input = useRef<HTMLInputElement>(null);

  useEffect(() => { input.current?.focus(); }, []);

  useEffect(() => {
    const term = q.trim();
    if (term.length < 2) { setRes(null); return; }
    let live = true;
    setBusy(true);
    const t = setTimeout(() => {
      api.search(term, 200)
        .then((r) => { if (live) { setRes(r); if (r.files.length === 0 && r.dirs.length > 0) setTab("dirs"); } })
        .catch((e) => notify(e.message, true))
        .finally(() => live && setBusy(false));
    }, 220);
    return () => { live = false; clearTimeout(t); };
  }, [q, api, generation, notify]);

  const list = res ? (tab === "files" ? res.files : res.dirs) : [];

  return (
    <>
      <PageHead title="Search" sub="Find files and folders by name in the current scan. Results are sorted by size, biggest first." />
      <div className="page">
        <div className="glass search-bar">
          <IconSearch style={{ width: 20, height: 20, color: "var(--text-3)" }} />
          <input
            ref={input}
            className="input"
            placeholder="Type part of a name, e.g. .iso, backup, steamapps"
            value={q}
            onChange={(e) => setQ(e.target.value)}
            spellCheck={false}
            aria-label="Search by name"
          />
          {busy && <div className="spinner" />}
        </div>

        {q.trim().length < 2 ? (
          <div className="glass"><Empty icon={<IconSearch />} title="Search by name">Type at least two characters. Matching ignores upper and lower case.</Empty></div>
        ) : !res ? <Loading text="Searching…" /> : (
          <section className="glass card">
            <div className="card-head">
              <div className="tabs" role="tablist">
                <button role="tab" aria-selected={tab === "files"} className={"tab" + (tab === "files" ? " active" : "")} onClick={() => setTab("files")}>
                  Files <span className="count">{formatCount(res.files.length)}</span>
                </button>
                <button role="tab" aria-selected={tab === "dirs"} className={"tab" + (tab === "dirs" ? " active" : "")} onClick={() => setTab("dirs")}>
                  Folders <span className="count">{formatCount(res.dirs.length)}</span>
                </button>
              </div>
              {(res.files.length >= 200 || res.dirs.length >= 200) && <span className="hint">Showing the 200 largest matches. Type more to narrow it down.</span>}
            </div>
            {list.length === 0 ? <Empty title="Nothing matches">Try a shorter or different part of the name.</Empty> : list.map((it) => (
              <div className="mini-row" key={it.path} style={{ gridTemplateColumns: "22px minmax(0,1fr) auto auto auto" }}>
                <span className="row-icon"><ItemIcon item={it} /></span>
                <span className="row-name">
                  <span className="primary"><Highlight text={it.name} term={q.trim()} /></span>
                  <span className="secondary" title={it.path}>{middleEllipsis(it.path, 90)}</span>
                </span>
                <span className="hint" style={{ whiteSpace: "nowrap" }}>
                  {it.type === "dir" ? `${formatCount(it.files)} files` : categoryOf(it.ext).label}
                  {it.modified ? ` · ${formatAgo(it.modified)}` : ""}
                </span>
                <span className="row-size" style={{ minWidth: 80 }}>{formatBytes(it.size)}</span>
                <span className="row-actions" style={{ opacity: 1 }}>
                  <RevealButton onReveal={() => reveal(it.path)} />
                  {it.type === "dir" && (
                    <button className="btn ghost small icon" title="Explore this folder" aria-label="Explore this folder" onClick={() => go("explore", it.path)}>
                      <IconArrowRight />
                    </button>
                  )}
                </span>
              </div>
            ))}
          </section>
        )}
      </div>
    </>
  );
}

function Highlight({ text, term }: { text: string; term: string }) {
  const i = text.toLowerCase().indexOf(term.toLowerCase());
  if (i < 0 || !term) return <>{text}</>;
  return (
    <>
      {text.slice(0, i)}
      <mark style={{ background: "rgba(255,197,61,0.25)", color: "var(--text)", padding: "0 1px" }}>
        {text.slice(i, i + term.length)}
      </mark>
      {text.slice(i + term.length)}
    </>
  );
}
