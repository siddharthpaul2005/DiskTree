import { hierarchy, partition, type HierarchyRectangularNode } from "d3-hierarchy";
import type { DirItem, NodeResponse, TreeItem } from "./types";
import { OTHER_COLOR, rankColor, shade } from "./colors";

export type PNode = HierarchyRectangularNode<TreeItem>;

/** Unique key for an item within one response ("other" shares its parent's path). */
export function itemKey(item: TreeItem): string {
  return `${item.type}:${item.path}`;
}

/**
 * Partition layout of an engine `node` response. Each leaf is weighted by its
 * size; folders whose children were not loaded (depth limit) are leaves too.
 */
export function layoutPartition(root: NodeResponse, width: number, height: number): PNode {
  const h = hierarchy<TreeItem>(root as DirItem, (d) => (d.type === "dir" && d.children?.length ? d.children : null))
    .sum((d) => (d.type === "dir" && d.children?.length ? 0 : Math.max(0, d.size)));
  return partition<TreeItem>().size([width, height]).padding(0)(h);
}

/** The ancestor at depth 1 (the top-level item this node belongs to). */
export function topAncestor(n: PNode): PNode {
  let cur = n;
  while (cur.depth > 1 && cur.parent) cur = cur.parent;
  return cur;
}

/** Colour by top-level branch: same rank colour as the list, darker when deeper. */
export function nodeColor(n: PNode): string {
  if (n.data.type === "other") return OTHER_COLOR;
  const top = topAncestor(n);
  const idx = top.parent?.children?.indexOf(top) ?? 0;
  const base = top.data.type === "other" ? OTHER_COLOR : rankColor(idx);
  const depth = n.depth - 1 + (n.data.type === "file" && n.depth > 1 ? 1 : 0);
  return shade(base, depth);
}

/** Colour for a direct child shown in the synced list. */
export function childColor(item: TreeItem, index: number): string {
  return item.type === "other" ? OTHER_COLOR : rankColor(index);
}
