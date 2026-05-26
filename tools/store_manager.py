"""
store_manager.py — CipherOS Store Manager GUI (v2)
Run: python3 tools/store_manager.py
Requires: pip install tkinter (usually pre-installed)
"""
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import os, threading, json, shutil
from store_core import (
    GameCatalog, FirmwareCatalog,
    calc_crc32, get_size, make_url, git_commit_push,
    scan_bins, scan_roms, is_git_repo
)

CONFIG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")
DEFAULT_REPO = "/home/abhiram/Projects/Project_Personal/ESP_32_Custom_Launcher/Store_Repo"

# ── Dark theme palette ───────────────────────────────────────────────────────
BG, BG2, BG3 = "#0d1117", "#161b22", "#21262d"
ACCENT, GREEN, RED = "#58a6ff", "#3fb950", "#f85149"
TEXT, DIM, BORDER = "#e6edf3", "#8b949e", "#30363d"
FONT, FONT_B, FONT_T, FONT_M = ("Inter", 10), ("Inter", 10, "bold"), ("Inter", 13, "bold"), ("Courier New", 9)

def styled_btn(parent, text, cmd, color=ACCENT, **kw):
    b = tk.Button(parent, text=text, command=cmd, bg=color, fg=BG, font=FONT_B,
                  relief="flat", bd=0, padx=12, pady=6, cursor="hand2", **kw)
    b.bind("<Enter>", lambda e: b.config(bg=TEXT))
    b.bind("<Leave>", lambda e: b.config(bg=color))
    return b

def lbl(parent, text, color=TEXT, font=FONT, **kw):
    return tk.Label(parent, text=text, bg=BG2, fg=color, font=font, **kw)

def entry_row(parent, label, row, default="", state="normal", width=38):
    lbl(parent, label, color=DIM).grid(row=row, column=0, sticky="e", padx=(0,8), pady=3)
    var = tk.StringVar(value=default)
    e = tk.Entry(parent, textvariable=var, width=width, bg=BG3, fg=TEXT, insertbackground=TEXT,
                 relief="flat", font=FONT, bd=4, highlightthickness=1, highlightcolor=ACCENT,
                 highlightbackground=BORDER, state=state)
    e.grid(row=row, column=1, columnspan=2, sticky="ew", pady=3)
    return var

class StoreManager(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("CipherOS Store Manager")
        self.configure(bg=BG)
        self.resizable(True, True)
        self.minsize(860, 680)

        self.repo_path = tk.StringVar()
        self.game_cat = None
        self.firm_cat = None

        self._build_ui()
        self._apply_style()

        if os.path.exists(CONFIG_FILE):
            try:
                with open(CONFIG_FILE, "r") as f:
                    last = json.load(f).get("last_repo", "")
                    if last and os.path.isdir(last):
                        self.repo_path.set(last)
            except Exception: pass

        if not self.repo_path.get():
            self.repo_path.set(DEFAULT_REPO)
            os.makedirs(DEFAULT_REPO, exist_ok=True)
            
        # Automatically load the repo path if it exists
        if os.path.isdir(self.repo_path.get()):
            self._load_repo()

    def _apply_style(self):
        s = ttk.Style(self)
        s.theme_use("clam")
        s.configure("TNotebook", background=BG, borderwidth=0)
        s.configure("TNotebook.Tab", background=BG3, foreground=DIM, padding=[16, 8], font=FONT_B)
        s.map("TNotebook.Tab", background=[("selected", BG2)], foreground=[("selected", ACCENT)])
        s.configure("Treeview", background=BG3, foreground=TEXT, fieldbackground=BG3, rowheight=26, font=FONT)
        s.configure("Treeview.Heading", background=BG, foreground=DIM, font=FONT_B, relief="flat")
        s.map("Treeview", background=[("selected", ACCENT)], foreground=[("selected", BG)])

    def _build_ui(self):
        hdr = tk.Frame(self, bg=BG, pady=16)
        hdr.pack(fill="x", padx=24)
        tk.Label(hdr, text="⚡  CipherOS Store Manager", bg=BG, fg=ACCENT, font=("Inter", 16, "bold")).pack(side="left")
        
        rf = tk.Frame(self, bg=BG2, pady=10, padx=20)
        rf.pack(fill="x", padx=24, pady=(0,4))
        tk.Label(rf, text="Git Repo Path:", bg=BG2, fg=DIM, font=FONT).pack(side="left")
        tk.Entry(rf, textvariable=self.repo_path, width=52, bg=BG3, fg=TEXT, insertbackground=TEXT,
                 relief="flat", font=FONT, bd=4, highlightthickness=1, highlightcolor=ACCENT).pack(side="left", padx=8)
        styled_btn(rf, "Browse", self._browse_repo).pack(side="left", padx=4)
        styled_btn(rf, "Load", self._load_repo, color=GREEN).pack(side="left", padx=4)

        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=24, pady=8)

        self.game_tab = tk.Frame(self.nb, bg=BG2)
        self.firm_tab = tk.Frame(self.nb, bg=BG2)
        self.nb.add(self.game_tab, text="🎮  NES Game Store")
        self.nb.add(self.firm_tab, text="💀  HackStore Firmware")

        self._build_game_tab()
        self._build_firm_tab()

        self.status = tk.StringVar(value="Load a git repository to get started.")
        tk.Label(self, textvariable=self.status, bg=BG, fg=DIM, font=FONT_M, anchor="w").pack(fill="x", padx=24, pady=(0,8))

    def _browse_repo(self):
        d = filedialog.askdirectory(title="Select CipherOS Store Git Repository")
        if d: self.repo_path.set(d)

    def _load_repo(self):
        p = self.repo_path.get().strip()
        if not os.path.isdir(p):
            messagebox.showerror("Error", "Directory does not exist."); return
        
        if not is_git_repo(p):
            try:
                import subprocess
                subprocess.run(["git", "init"], cwd=p, check=True, capture_output=True)
                self.status.set(f"Initialized empty Git repository in {p}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to initialize Git repository in {p}:\n{e}")
                return
            
        # Ensure standard folders exist
        os.makedirs(os.path.join(p, "firmwares"), exist_ok=True)
        os.makedirs(os.path.join(p, "roms"), exist_ok=True)

        self.game_cat = GameCatalog(p)
        self.firm_cat = FirmwareCatalog(p)
        self._sync_auto_discovery()
        self._refresh_game_list()
        self._refresh_firm_list()
        self.status.set(f"✅  Loaded repo: {p}")
        try:
            with open(CONFIG_FILE, "w") as f: json.dump({"last_repo": p}, f)
        except Exception: pass

    def _require_repo(self) -> bool:
        if not self.game_cat:
            messagebox.showwarning("No Repo", "Please load a Git repository first.")
            return False
        return True

    def _sync_auto_discovery(self):
        """Auto-add bins/roms found in folders but missing from JSON."""
        p = self.repo_path.get()
        new_f, new_g = 0, 0
        
        known_f = self.firm_cat.ids()
        for f in scan_bins(p):
            if f["id"] not in known_f:
                self.firm_cat.add_or_update({
                    "id": f["id"], "name": f["filename"], "version": "v1.0",
                    "description": "Auto-discovered binary", "size_bytes": f["size"],
                    "url": make_url(p, f["rel_path"])
                })
                new_f += 1
                
        known_g = self.game_cat.ids()
        for g in scan_roms(p):
            if g["id"] not in known_g:
                self.game_cat.add_or_update({
                    "id": g["id"], "name": g["filename"], "version": "1.0",
                    "genre": "Unknown", "description": "Auto-discovered ROM",
                    "size_bytes": g["size"], "crc32": calc_crc32(g["abs_path"]),
                    "mapper": 0, "url": make_url(p, g["rel_path"])
                })
                new_g += 1
                
        if new_f or new_g:
            self.game_cat.reload(); self.firm_cat.reload()
            messagebox.showinfo("Auto Discovery", f"Found & added:\n{new_f} new firmwares\n{new_g} new ROMs")

    # ── GAME TAB ─────────────────────────────────────────────────────────────────
    def _build_game_tab(self):
        p = self.game_tab
        cols = ("id","name","version","genre","size_bytes")
        self.game_tree = ttk.Treeview(p, columns=cols, show="headings", height=8)
        for c, w in zip(cols, [100,160,70,90,90]):
            self.game_tree.heading(c, text=c.upper()); self.game_tree.column(c, width=w, anchor="w")
        self.game_tree.pack(fill="x", padx=16, pady=(12,4))
        self.game_tree.bind("<<TreeviewSelect>>", self._on_game_select)

        tb = tk.Frame(p, bg=BG2)
        tb.pack(fill="x", padx=16, pady=4)
        styled_btn(tb, "+ Import ROM", self._import_nes).pack(side="left", padx=4)
        styled_btn(tb, "✏ Edit", self._edit_game, color=BG3).pack(side="left", padx=4)
        styled_btn(tb, "🗑 Delete", self._delete_game, color=RED).pack(side="left", padx=4)
        styled_btn(tb, "⬆ Commit & Push", lambda: self._push("game"), color=GREEN).pack(side="right", padx=4)

        tk.Frame(p, bg=BORDER, height=1).pack(fill="x", padx=16, pady=8)
        tk.Label(p, text="Game Details", bg=BG2, fg=ACCENT, font=FONT_T).pack(anchor="w", padx=16)

        f = tk.Frame(p, bg=BG2); f.pack(fill="x", padx=16, pady=4); f.columnconfigure(1, weight=1)
        self.g_id   = entry_row(f, "ID (slug)*", 0, state="readonly")
        self.g_name = entry_row(f, "Name*", 1)
        self.g_ver  = entry_row(f, "Version", 2)
        self.g_gen  = entry_row(f, "Genre", 3)
        self.g_desc = entry_row(f, "Description", 4)
        self.g_map  = entry_row(f, "Mapper (iNES)", 5)
        self.g_url  = entry_row(f, "Cloud URL", 6, state="readonly")
        
        self.g_info = tk.Label(f, text="Select a game to edit.", bg=BG2, fg=DIM, font=FONT_M)
        self.g_info.grid(row=7, column=1, sticky="w", pady=4)
        styled_btn(p, "Save Changes", self._save_game, color=ACCENT).pack(pady=8)
        self._g_size, self._g_crc = 0, ""

    def _refresh_game_list(self):
        self.game_tree.delete(*self.game_tree.get_children())
        for g in self.game_cat.entries():
            self.game_tree.insert("", "end", iid=g["id"], values=(g["id"], g["name"], g.get("version",""), g.get("genre",""), g.get("size_bytes",0)))

    def _on_game_select(self, _=None):
        sel = self.game_tree.selection()
        if not sel: return
        g = self.game_cat.get(sel[0])
        if g:
            self.g_id.set(g["id"]); self.g_name.set(g["name"]); self.g_ver.set(g.get("version","1.0"))
            self.g_gen.set(g.get("genre","")); self.g_desc.set(g.get("description",""))
            self.g_map.set(str(g.get("mapper",0))); self.g_url.set(g.get("url",""))
            self._g_size, self._g_crc = g.get("size_bytes",0), g.get("crc32","")
            self.g_info.config(text=f"Size: {self._g_size} bytes  |  CRC32: {self._g_crc}")

    def _edit_game(self): self._on_game_select()

    def _delete_game(self):
        if not self._require_repo(): return
        sel = self.game_tree.selection()
        if not sel: return
        if messagebox.askyesno("Delete", f"Delete game '{sel[0]}' from catalog?"):
            self.game_cat.delete(sel[0]); self._refresh_game_list()

    def _import_nes(self):
        if not self._require_repo(): return
        path = filedialog.askopenfilename(filetypes=[("NES ROM","*.nes")])
        if not path: return
        dest = os.path.join(self.repo_path.get(), "roms", os.path.basename(path))
        shutil.copy2(path, dest)
        self._sync_auto_discovery()
        self._refresh_game_list()
        self.status.set(f"📥 Imported ROM: {os.path.basename(path)}")

    def _save_game(self):
        if not self._require_repo() or not self.g_id.get(): return
        entry = {
            "id": self.g_id.get(), "name": self.g_name.get().strip(),
            "version": self.g_ver.get().strip(), "genre": self.g_gen.get().strip(),
            "description": self.g_desc.get().strip(), "size_bytes": self._g_size,
            "crc32": self._g_crc, "mapper": int(self.g_map.get() or 0), "url": self.g_url.get()
        }
        err = self.game_cat.add_or_update(entry)
        if err: messagebox.showerror("Error", err)
        else: self._refresh_game_list(); self.status.set(f"✅ Saved game: {entry['id']}")

    # ── FIRMWARE TAB ─────────────────────────────────────────────────────────────
    def _build_firm_tab(self):
        p = self.firm_tab
        cols = ("id","name","version","size_bytes")
        self.firm_tree = ttk.Treeview(p, columns=cols, show="headings", height=8)
        for c, w in zip(cols, [120,180,80,110]):
            self.firm_tree.heading(c, text=c.upper()); self.firm_tree.column(c, width=w, anchor="w")
        self.firm_tree.pack(fill="x", padx=16, pady=(12,4))
        self.firm_tree.bind("<<TreeviewSelect>>", self._on_firm_select)

        tb = tk.Frame(p, bg=BG2)
        tb.pack(fill="x", padx=16, pady=4)
        styled_btn(tb, "+ Import BIN", self._import_bin).pack(side="left", padx=4)
        styled_btn(tb, "✏ Edit", self._edit_firm, color=BG3).pack(side="left", padx=4)
        styled_btn(tb, "🗑 Delete", self._delete_firm, color=RED).pack(side="left", padx=4)
        styled_btn(tb, "⬆ Commit & Push", lambda: self._push("firm"), color=GREEN).pack(side="right", padx=4)

        tk.Frame(p, bg=BORDER, height=1).pack(fill="x", padx=16, pady=8)
        tk.Label(p, text="Firmware Details", bg=BG2, fg=ACCENT, font=FONT_T).pack(anchor="w", padx=16)

        f = tk.Frame(p, bg=BG2); f.pack(fill="x", padx=16, pady=4); f.columnconfigure(1, weight=1)
        self.f_id   = entry_row(f, "ID (slug)*", 0, state="readonly")
        self.f_name = entry_row(f, "Name*", 1)
        self.f_ver  = entry_row(f, "Version", 2)
        self.f_desc = entry_row(f, "Description", 3)
        self.f_url  = entry_row(f, "Cloud URL", 4, state="readonly")
        
        self.f_info = tk.Label(f, text="Select a firmware to edit.", bg=BG2, fg=DIM, font=FONT_M)
        self.f_info.grid(row=5, column=1, sticky="w", pady=4)
        styled_btn(p, "Save Changes", self._save_firm, color=ACCENT).pack(pady=8)
        self._f_size = 0

    def _refresh_firm_list(self):
        self.firm_tree.delete(*self.firm_tree.get_children())
        for f in self.firm_cat.entries():
            self.firm_tree.insert("", "end", iid=f["id"], values=(f["id"], f["name"], f.get("version",""), f.get("size_bytes",0)))

    def _on_firm_select(self, _=None):
        sel = self.firm_tree.selection()
        if not sel: return
        f = self.firm_cat.get(sel[0])
        if f:
            self.f_id.set(f["id"]); self.f_name.set(f["name"]); self.f_ver.set(f.get("version","v1.0"))
            self.f_desc.set(f.get("description","")); self.f_url.set(f.get("url",""))
            self._f_size = f.get("size_bytes",0)
            self.f_info.config(text=f"Size: {self._f_size} bytes")

    def _edit_firm(self): self._on_firm_select()

    def _delete_firm(self):
        if not self._require_repo(): return
        sel = self.firm_tree.selection()
        if not sel: return
        if messagebox.askyesno("Delete", f"Delete firmware '{sel[0]}' from catalog?"):
            self.firm_cat.delete(sel[0]); self._refresh_firm_list()

    def _import_bin(self):
        if not self._require_repo(): return
        path = filedialog.askopenfilename(filetypes=[("Binary","*.bin")])
        if not path: return
        dest = os.path.join(self.repo_path.get(), "firmwares", os.path.basename(path))
        shutil.copy2(path, dest)
        self._sync_auto_discovery()
        self._refresh_firm_list()
        self.status.set(f"📥 Imported BIN: {os.path.basename(path)}")

    def _save_firm(self):
        if not self._require_repo() or not self.f_id.get(): return
        entry = {
            "id": self.f_id.get(), "name": self.f_name.get().strip(),
            "version": self.f_ver.get().strip(), "description": self.f_desc.get().strip(),
            "size_bytes": self._f_size, "url": self.f_url.get()
        }
        err = self.firm_cat.add_or_update(entry)
        if err: messagebox.showerror("Error", err)
        else: self._refresh_firm_list(); self.status.set(f"✅ Saved firmware: {entry['id']}")

    # ── Git Push ─────────────────────────────────────────────────────────────────
    def _push(self, tab: str):
        if not self._require_repo(): return
        msg = f"chore: update {'games' if tab=='game' else 'firmwares'} catalog"
        self.status.set("⏳  Committing and pushing...")
        def do():
            ok, out = git_commit_push(self.repo_path.get(), msg)
            if ok:
                self.after(0, lambda: self.status.set("✅  Pushed successfully!"))
                self.after(0, lambda: messagebox.showinfo("Success", "Successfully pushed to GitHub!"))
            else:
                self.after(0, lambda: self.status.set(f"⚠️  Git Error"))
                self.after(0, lambda: messagebox.showerror("Git Error", out))
        threading.Thread(target=do, daemon=True).start()

if __name__ == "__main__":
    app = StoreManager()
    app.mainloop()
