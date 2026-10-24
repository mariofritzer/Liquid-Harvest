// Erzeugt die Laien-Anleitung als PDF – mit aktuellen Screenshots aus der Demo-App.
// Aufruf: node docs/anleitung/build.mjs <ausgabe.pdf>
// Braucht: playwright (Chromium). Version aus src/version.h, "Neu in dieser Version" aus ENV CHANGES
// oder der letzten Commit-Nachricht.
import { chromium } from "playwright";
import fs from "node:fs";
import path from "node:path";
import { execSync } from "node:child_process";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "../..");
const out = path.resolve(process.argv[2] || "anleitung.pdf");
const version = (fs.readFileSync(path.join(root, "src/version.h"), "utf8").match(/FW_VERSION\s+"([^"]+)"/) || [])[1] || "?";
let changes = process.env.CHANGES;
if (!changes) {
  try { changes = execSync("git log -1 --format=%B", { cwd: root }).toString(); } catch { changes = ""; }
}
changes = changes.split("\n").filter(l => l.trim() && !/^(Co-Authored-By|Claude-Session|Signed-off-by):/i.test(l)).join("\n").trim() || "–";
const esc = s => s.replace(/[&<>]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
const date = new Date().toLocaleDateString("de-AT", { day: "2-digit", month: "2-digit", year: "numeric" });

const browser = await chromium.launch();
const shots = {};
{
  const p = await browser.newPage({ viewport: { width: 390, height: 844 }, deviceScaleFactor: 2 });
  await p.goto(pathToFileURL(path.join(root, "web/index.html")).href + "?demo");
  await p.evaluate(() => { try { localStorage.clear(); } catch (e) {} });
  await p.reload();
  await p.waitForTimeout(1500);
  await p.addStyleTag({ content: "#toast{display:none!important}" });
  await p.evaluate(() => { SIM.upd.new = null; });                    // kein Update-Hinweis auf den Bildern
  await p.waitForTimeout(400);
  const shot = async (name, clipSel) => {
    let buf;
    if (clipSel) { const el = await p.$(clipSel); buf = await el.screenshot(); }
    else buf = await p.screenshot();
    shots[name] = "data:image/png;base64," + buf.toString("base64");
  };
  const drawer = async (tab, prep) => {
    await p.evaluate(t => { curTab = t; openDrawer(); }, tab); await p.waitForTimeout(400);
    if (prep) { await prep(); await p.waitForTimeout(400); }
  };
  const close = async () => { await p.evaluate(() => { document.querySelector("#drawer").classList.remove("show"); closeSheet(); window.scrollTo(0,0); }); await p.waitForTimeout(300); };

  await shot("main");
  await p.evaluate(() => openSheet(0)); await p.waitForTimeout(500); await shot("sheet"); await close();
  await p.evaluate(() => send({ c: "party", on: true })); await p.waitForTimeout(600); await shot("party");
  await p.evaluate(() => send({ c: "party", on: false })); await p.waitForTimeout(400);
  await p.evaluate(() => send({ c: "roulMode", on: true })); await p.waitForTimeout(600);
  await p.evaluate(() => document.querySelector("#roulCard").scrollIntoView({ block: "center" })); await p.waitForTimeout(200);
  await shot("roul");
  await p.evaluate(() => { send({ c: "roulMode", on: false }); window.scrollTo(0, 0); }); await p.waitForTimeout(400);
  await drawer("scales"); await shot("scales");
  await drawer("servo"); await shot("teach"); await close();
  await drawer("pump"); await shot("pump");
  await p.evaluate(() => document.querySelector("#cleanCard").scrollIntoView({ block: "start" })); await p.waitForTimeout(200); await shot("clean"); await close();
  await drawer("sys");
  await p.evaluate(() => document.querySelector("#updCard").scrollIntoView({ block: "start" })); await p.waitForTimeout(200); await shot("update"); await close();
}

let html = fs.readFileSync(path.join(here, "anleitung.html"), "utf8")
  .replaceAll("{{VERSION}}", "v" + version.replace(/^v/, ""))
  .replaceAll("{{DATE}}", date)
  .replaceAll("{{CHANGES}}", esc(changes))
  .replace(/src="shot:([a-z]+)"/g, (_, n) => `src="${shots[n] || ""}"`);

const page = await browser.newPage();
await page.setContent(html, { waitUntil: "load" });
await page.pdf({
  path: out, format: "A4", printBackground: true, displayHeaderFooter: true,
  headerTemplate: "<span></span>",
  footerTemplate: `<div style="width:100%;font-size:8px;color:#888;padding:0 15mm;display:flex;justify-content:space-between;font-family:Arial,sans-serif"><span>Liquid Harvest – Anleitung · v${version}</span><span><span class="pageNumber"></span> / <span class="totalPages"></span></span></div>`,
  margin: { top: "16mm", bottom: "18mm", left: "15mm", right: "15mm" },
});
await browser.close();
console.log(`Anleitung v${version}: ${out} (${Math.round(fs.statSync(out).size / 1024)} kB)`);
