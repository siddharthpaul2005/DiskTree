// Small line icons (24px grid, 1.8 stroke) so the bundle has no icon font.
import type { SVGProps } from "react";

type P = SVGProps<SVGSVGElement>;
const base = (p: P) => ({
  viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 1.8,
  strokeLinecap: "round" as const, strokeLinejoin: "round" as const, ...p,
});

export const IconHome = (p: P) => (<svg {...base(p)}><path d="M3 10.5 12 3l9 7.5" /><path d="M5 9.5V21h14V9.5" /><path d="M10 21v-6h4v6" /></svg>);
export const IconOverview = (p: P) => (<svg {...base(p)}><path d="M4 6h10" /><path d="M4 12h16" /><path d="M4 18h7" /></svg>);
export const IconSunburst = (p: P) => (<svg {...base(p)}><circle cx="12" cy="12" r="3" /><path d="M12 5a7 7 0 0 1 7 7" /><path d="M19 12a7 7 0 0 1-11 5.7" /><path d="M12 1.5A10.5 10.5 0 0 1 22.5 12" opacity=".6" /><path d="M5.5 16A7 7 0 0 1 12 5" opacity=".6" /></svg>);
export const IconFlow = (p: P) => (<svg {...base(p)}><rect x="3" y="3" width="5" height="18" rx="1.5" /><rect x="10" y="3" width="5" height="11" rx="1.5" /><rect x="10" y="16" width="5" height="5" rx="1.5" /><rect x="17" y="3" width="4" height="6" rx="1.5" /><rect x="17" y="11" width="4" height="3" rx="1" /></svg>);
export const IconSearch = (p: P) => (<svg {...base(p)}><circle cx="11" cy="11" r="7" /><path d="m20 20-3.5-3.5" /></svg>);
export const IconCamera = (p: P) => (<svg {...base(p)}><path d="M4 8h3l2-3h6l2 3h3v11H4z" /><circle cx="12" cy="13" r="3.5" /></svg>);
export const IconDrive = (p: P) => (<svg {...base(p)}><rect x="3" y="13" width="18" height="7" rx="2" /><path d="M5 13 7.5 5h9L19 13" /><path d="M16.5 16.5h.01" strokeWidth="2.6" /></svg>);
export const IconUsb = (p: P) => (<svg {...base(p)}><rect x="7" y="9" width="10" height="12" rx="2" /><path d="M9 9V3h6v6" /><path d="M11 5.5h.01M13 5.5h.01" strokeWidth="2.4" /></svg>);
export const IconFolder = (p: P) => (<svg {...base(p)}><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z" /></svg>);
export const IconFile = (p: P) => (<svg {...base(p)}><path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z" /><path d="M14 3v5h5" /></svg>);
export const IconStack = (p: P) => (<svg {...base(p)}><path d="M12 3 3 8l9 5 9-5z" /><path d="m3 13 9 5 9-5" opacity=".6" /></svg>);
export const IconReveal = (p: P) => (<svg {...base(p)}><path d="M14 4h6v6" /><path d="M20 4 11 13" /><path d="M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5" /></svg>);
export const IconArrowRight = (p: P) => (<svg {...base(p)}><path d="M5 12h14" /><path d="m13 6 6 6-6 6" /></svg>);
export const IconUp = (p: P) => (<svg {...base(p)}><path d="m6 15 6-6 6 6" /></svg>);
export const IconChevron = (p: P) => (<svg {...base(p)}><path d="m9 6 6 6-6 6" /></svg>);
export const IconRefresh = (p: P) => (<svg {...base(p)}><path d="M20 11a8 8 0 1 0-2.3 5.7" /><path d="M20 4v7h-7" /></svg>);
export const IconTrash = (p: P) => (<svg {...base(p)}><path d="M4 7h16" /><path d="M10 11v6M14 11v6" /><path d="M6 7l1 13h10l1-13" /><path d="M9 7V4h6v3" /></svg>);
export const IconCompare = (p: P) => (<svg {...base(p)}><path d="M7 4v16" /><path d="m3 8 4-4 4 4" /><path d="M17 20V4" /><path d="m13 16 4 4 4-4" /></svg>);
export const IconClose = (p: P) => (<svg {...base(p)}><path d="M6 6l12 12M18 6 6 18" /></svg>);
export const IconSpark = (p: P) => (<svg {...base(p)}><path d="M12 3v4M12 17v4M3 12h4M17 12h4M5.6 5.6l2.8 2.8M15.6 15.6l2.8 2.8M5.6 18.4l2.8-2.8M15.6 8.4l2.8-2.8" /></svg>);
export const IconWarn = (p: P) => (<svg {...base(p)}><path d="M12 3 2 20h20z" /><path d="M12 10v4" /><path d="M12 17h.01" strokeWidth="2.6" /></svg>);
export const IconDots = (p: P) => (<svg {...base(p)}><circle cx="5" cy="12" r="1" /><circle cx="12" cy="12" r="1" /><circle cx="19" cy="12" r="1" /></svg>);
export const IconShield = (p: P) => (<svg {...base(p)}><path d="M12 3l7 3v5c0 4.5-3 8.3-7 10-4-1.7-7-5.5-7-10V6l7-3z" /><path d="m9 12 2 2 4-4" /></svg>);
