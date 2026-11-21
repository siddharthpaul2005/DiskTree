import { Fragment } from "react";
import { IconChevron } from "./Icons";

export function Breadcrumb({ crumbs, onGo }: { crumbs: { name: string; path: string }[]; onGo: (path: string) => void }) {
  return (
    <nav className="crumbs" aria-label="Folder path">
      {crumbs.map((c, i) => {
        const last = i === crumbs.length - 1;
        return (
          <Fragment key={c.path}>
            {i > 0 && <IconChevron className="crumb-sep" style={{ width: 12, height: 12, color: "var(--text-3)" }} />}
            <button
              className={"crumb" + (last ? " current" : "")}
              onClick={() => !last && onGo(c.path)}
              aria-current={last ? "location" : undefined}
              title={c.path}
            >
              {c.name || c.path}
            </button>
          </Fragment>
        );
      })}
    </nav>
  );
}
