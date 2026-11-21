import { expect, test, type Page } from "@playwright/test";

const fixture = process.env.E2E_FIXTURE!;

async function scanFixture(page: Page) {
  await page.goto("");
  await expect(page.getByRole("heading", { name: "Your storage" })).toBeVisible();
  await page.getByLabel("Folder path").fill(fixture);
  await page.getByRole("button", { name: "Scan folder" }).click();
  // Finishing a scan opens the overview
  await expect(page.getByRole("heading", { name: /^Overview of / })).toBeVisible({ timeout: 30_000 });
}

test.describe.configure({ mode: "serial" });

test("home lists drives and connects to the engine", async ({ page }) => {
  await page.goto("");
  await expect(page.getByRole("heading", { name: "Your storage" })).toBeVisible();
  await expect(page.getByText("Engine not responding")).toHaveCount(0);
  // At least one drive card with a scan button
  await expect(page.getByRole("button", { name: /^Scan [A-Z]:$/ }).first()).toBeVisible();
  // Result pages stay disabled until something is scanned
  await expect(page.getByRole("button", { name: "Overview" })).toBeDisabled();
});

test("home offers a whole-PC scan and folder quick picks", async ({ page }) => {
  await page.goto("");
  await expect(page.getByRole("button", { name: "Scan whole PC" })).toBeEnabled();
  // Clicking a quick pick fills the folder path with that folder
  const pick = page.locator(".places .place").first();
  await expect(pick).toBeVisible();
  const path = await pick.getAttribute("title");
  await pick.click();
  await expect(page.getByLabel("Folder path")).toHaveValue(path!);
  await expect(pick).toHaveClass(/active/);
});

test("scanning a folder shows the ranked overview", async ({ page }) => {
  await scanFixture(page);
  // 4,000,000+1,000,000+3,000,000+20,000+600,000+4,000+2,000,000+1,000 = 10,625,000 bytes
  await expect(page.locator(".stat").filter({ hasText: "Total size" })).toContainText("10.1 MB");
  await expect(page.locator(".stat").filter({ hasText: "Files" }).first()).toContainText("8");
  const rows = page.locator(".rows .row");
  await expect(rows.nth(0)).toContainText("Videos");
  await expect(rows.nth(1)).toContainText("Games");
  await expect(rows.nth(2)).toContainText("Downloads");
  await expect(page.getByText("holiday-2025.mp4", { exact: true })).toBeVisible();
  await expect(page.getByText("Videos", { exact: true }).last()).toBeVisible(); // file type category
  // A folder scan shows its drive's space: the folder, the rest, and what's free
  const space = page.locator("section").filter({ has: page.getByRole("heading", { name: "Drive space" }) });
  await expect(space).toContainText("This folder");
  await expect(space).toContainText("Rest of the drive");
  await expect(space).toContainText("Free");
  await expect(page.locator(".foot-free")).toContainText("free");
  await expect(page.locator(".stat").filter({ hasText: "Space left" })).toContainText("free of");
});

test("explore drills into folders and stays in sync", async ({ page }) => {
  await scanFixture(page);
  await page.getByRole("button", { name: "Explore" }).first().click();
  await expect(page.getByRole("heading", { name: "Explore" })).toBeVisible();
  await expect(page.locator(".sunburst path.arc").first()).toBeVisible();

  // Hovering a list row highlights its arcs
  const gamesRow = page.locator(".list-card .row", { hasText: "Games" });
  await gamesRow.hover();
  await expect(page.locator(".sunburst")).toHaveClass(/has-hot/);
  await expect(page.locator(".center-label")).toContainText("Games");

  // Clicking drills in; breadcrumb and list follow
  await gamesRow.click();
  await expect(page.locator(".crumb.current")).toHaveText("Games");
  await expect(page.locator(".list-card .row").first()).toContainText("big-world");

  // Up one level returns to the scan root
  await page.getByRole("button", { name: "Up one level" }).click();
  await expect(page.locator(".list-card .row").first()).toContainText("Videos");
});

test("path flow draws columns and re-roots on click", async ({ page }) => {
  await scanFixture(page);
  await page.getByRole("button", { name: "Path flow" }).first().click();
  await expect(page.getByRole("heading", { name: "Path flow" })).toBeVisible();
  const cells = page.locator(".icicle-cell");
  await expect(cells.first()).toBeVisible();
  await page.locator(".icicle-cell", { hasText: "Videos" }).first().click();
  await expect(page.locator(".crumb.current")).toHaveText("Videos");
  await expect(page.locator(".icicle-cell", { hasText: "holiday-2025.mp4" })).toBeVisible();
});

test("search finds files and folders by name", async ({ page }) => {
  await scanFixture(page);
  await page.getByRole("button", { name: "Search" }).click();
  await page.getByLabel("Search by name").fill("report");
  await expect(page.locator(".mini-row", { hasText: "report-final.pdf" })).toBeVisible();
  await page.getByLabel("Search by name").fill("big-world");
  await expect(page.getByRole("tab", { name: /Folders/ })).toContainText("1");
  await expect(page.locator(".mini-row", { hasText: "big-world" })).toBeVisible();
});

test("snapshots save, compare and delete", async ({ page }) => {
  await scanFixture(page);
  await page.getByRole("button", { name: "Snapshots" }).click();
  await page.getByLabel("Snapshot label").fill("E2E baseline");
  await page.getByRole("button", { name: "Save snapshot" }).click();
  const row = page.locator(".snap-row", { hasText: "E2E baseline" });
  await expect(row).toBeVisible();
  await expect(row).toContainText("Same folder as current scan");

  await row.getByRole("button", { name: /Compare/ }).click();
  await expect(page.getByRole("heading", { name: /What changed since E2E baseline/ })).toBeVisible();
  await expect(page.locator(".stat").filter({ hasText: "Net change" })).toContainText("0 B");

  await row.getByRole("button", { name: "Delete snapshot" }).click();
  await page.getByRole("dialog").getByRole("button", { name: "Delete snapshot" }).click();
  await expect(row).toHaveCount(0);
});

test("a bad path shows a clear error", async ({ page }) => {
  await page.goto("");
  await page.getByLabel("Folder path").fill("Z:\\definitely\\not\\here");
  await page.getByRole("button", { name: "Scan folder" }).click();
  await expect(page.locator(".toast.error")).toBeVisible();
});
