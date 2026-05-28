"""
store_core.py — CipherOS Store Manager: Backend Logic (v2)
Handles JSON catalog management, CRC32, file scanning, and git operations.
"""
import json, os, binascii, subprocess, re

# ── Field limits (from ESP32 C structs — DO NOT EXCEED) ─────────────────────
GAME_LIMITS = {"id":32,"name":48,"version":12,"genre":20,"description":128,"url":192}
FIRM_LIMITS = {"id":32,"name":32,"version":16,"description":128,"url":256}

GAMES_CATALOG = "games_catalog.json"
FIRMS_CATALOG = "firmware_catalog.json"

# ── Git Helpers ───────────────────────────────────────────────────────────────
def is_git_repo(path: str) -> bool:
    return os.path.isdir(os.path.join(path, ".git"))

def get_raw_base(repo_path: str) -> str | None:
    """
    Parse git remote 'origin' and return the raw.githubusercontent.com base.
    Handles both HTTPS (https://github.com/USER/REPO.git)
    and SSH     (git@github.com:USER/REPO.git) remotes.
    Returns None if remote is not a GitHub URL or not set.
    """
    try:
        r = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            cwd=repo_path, capture_output=True, text=True, timeout=5
        )
        remote = r.stdout.strip()
        # SSH: git@github.com:USER/REPO.git
        m = re.match(r"git@github\.com:([^/]+)/(.+?)(?:\.git)?$", remote)
        if m:
            return f"https://raw.githubusercontent.com/{m.group(1)}/{m.group(2)}/main"
        # HTTPS: https://github.com/USER/REPO[.git]
        m = re.match(r"https://github\.com/([^/]+)/(.+?)(?:\.git)?$", remote)
        if m:
            return f"https://raw.githubusercontent.com/{m.group(1)}/{m.group(2)}/main"
    except Exception:
        pass
    return None

def url_is_valid(url: str) -> bool:
    """Return True only if url is a full HTTP/HTTPS URL (not empty/relative)."""
    return bool(url) and (url.startswith("http://") or url.startswith("https://"))

def make_url(repo_path: str, rel_path: str) -> str:
    """
    Return a raw.githubusercontent.com download URL for rel_path.

    Raises RuntimeError if the git remote 'origin' is not set or is not a
    GitHub remote — so callers can never silently get an empty string that
    would later crash the ESP32 HTTP client.
    """
    base = get_raw_base(repo_path)
    if not base:
        raise RuntimeError(
            "Cannot build a download URL: git remote 'origin' is not set or is not a "
            "GitHub remote.\n"
            "Run: git remote add origin https://github.com/USER/REPO.git"
        )
    return f"{base}/{rel_path}"

def git_commit_push(repo_path: str, message: str) -> tuple[bool, str]:
    """Stage all, commit, push. Returns (success, output_text)."""
    def run(cmd):
        return subprocess.run(cmd, cwd=repo_path, capture_output=True,
                              text=True, timeout=120)
    run(["git", "add", "."])
    r = run(["git", "commit", "-m", message])
    if r.returncode != 0 and "nothing to commit" not in r.stdout:
        return False, (r.stderr or r.stdout).strip()
    r = run(["git", "push", "-u", "origin", "HEAD"])
    return r.returncode == 0, (r.stdout + r.stderr).strip()

# ── File Analysis ─────────────────────────────────────────────────────────────
def calc_crc32(filepath: str) -> str:
    """CRC32 as uppercase hex, no 0x prefix — matches ESP32 game_store.c."""
    crc = 0
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            crc = binascii.crc32(chunk, crc)
    return format(crc & 0xFFFFFFFF, "08X")

def get_size(filepath: str) -> int:
    return os.path.getsize(filepath)

def slug_from_filename(filename: str) -> str:
    """Convert 'Blink.ino.bin' → 'blink_ino', 'Super Mario Bros 3.nes' → 'super_mario_bros_3'"""
    name = os.path.splitext(filename)[0]
    name = re.sub(r"[^a-zA-Z0-9]+", "_", name).strip("_").lower()
    return name[:31]  # Stay within id[32]

# ── Disk Scanners ─────────────────────────────────────────────────────────────
def scan_bins(repo_path: str) -> list[dict]:
    """Return metadata for every .bin in firmwares/ subfolder."""
    result = []
    d = os.path.join(repo_path, "firmwares")
    if not os.path.isdir(d):
        return result
    for fname in sorted(os.listdir(d)):
        if not fname.endswith(".bin"):
            continue
        abs_path = os.path.join(d, fname)
        result.append({
            "filename": fname,
            "abs_path": abs_path,
            "size":     get_size(abs_path),
            "rel_path": f"firmwares/{fname}",
            "id":       slug_from_filename(fname),
        })
    return result

def scan_roms(repo_path: str) -> list[dict]:
    """Return metadata for every .nes in roms/ subfolder."""
    result = []
    d = os.path.join(repo_path, "roms")
    if not os.path.isdir(d):
        return result
    for fname in sorted(os.listdir(d)):
        if not fname.endswith(".nes"):
            continue
        abs_path = os.path.join(d, fname)
        result.append({
            "filename": fname,
            "abs_path": abs_path,
            "size":     get_size(abs_path),
            "rel_path": f"roms/{fname}",
            "id":       slug_from_filename(fname),
        })
    return result

# ── JSON Catalog Base ─────────────────────────────────────────────────────────
def _load(path: str, key: str) -> dict:
    if os.path.exists(path):
        try:
            with open(path, "r") as f:
                return json.load(f)
        except Exception:
            pass
    return {"version": 1, key: []}

def _save(path: str, data: dict):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)

# ── Game Catalog ──────────────────────────────────────────────────────────────
class GameCatalog:
    def __init__(self, repo_path: str):
        self.repo  = repo_path
        self.path  = os.path.join(repo_path, GAMES_CATALOG)
        self._data = _load(self.path, "games")

    def entries(self) -> list:
        return self._data.get("games", [])

    def ids(self) -> set:
        return {g["id"] for g in self.entries()}

    def get(self, game_id: str) -> dict | None:
        return next((g for g in self.entries() if g["id"] == game_id), None)

    def _truncate(self, entry: dict):
        for k, limit in GAME_LIMITS.items():
            if k in entry and isinstance(entry[k], str) and len(entry[k]) >= limit:
                entry[k] = entry[k][:limit-1]

    def add_or_update(self, entry: dict) -> str | None:
        """Save entry after validation. Returns error string on failure, None on success."""
        self._truncate(entry)
        url = entry.get("url", "")
        if not url_is_valid(url):
            return (
                f"Invalid URL for game '{entry.get('id', '?')}': '{url}'\n"
                "URL must start with http:// or https://\n"
                "Make sure git remote 'origin' is set to a GitHub remote, "
                "then re-import the ROM."
            )
        games = self._data.setdefault("games", [])
        for i, g in enumerate(games):
            if g["id"] == entry["id"]:
                games[i] = entry
                _save(self.path, self._data)
                return None
        games.append(entry)
        _save(self.path, self._data)
        return None

    def heal_urls(self, repo_path: str) -> list[str]:
        """
        Scan every entry and regenerate any missing or invalid URL from its filename.
        Returns a list of IDs that were fixed.
        """
        fixed = []
        try:
            base = get_raw_base(repo_path)
        except Exception:
            base = None
        if not base:
            return fixed
        for entry in self._data.get("games", []):
            if not url_is_valid(entry.get("url", "")):
                filename = entry.get("name", "")
                if filename:
                    entry["url"] = f"{base}/roms/{filename}"
                    fixed.append(entry["id"])
        if fixed:
            _save(self.path, self._data)
        return fixed

    def delete(self, game_id: str):
        self._data["games"] = [g for g in self.entries() if g["id"] != game_id]
        _save(self.path, self._data)

    def reload(self):
        self._data = _load(self.path, "games")

# ── Firmware Catalog ──────────────────────────────────────────────────────────
class FirmwareCatalog:
    def __init__(self, repo_path: str):
        self.repo  = repo_path
        self.path  = os.path.join(repo_path, FIRMS_CATALOG)
        self._data = _load(self.path, "firmwares")

    def entries(self) -> list:
        return self._data.get("firmwares", [])

    def ids(self) -> set:
        return {f["id"] for f in self.entries()}

    def get(self, firm_id: str) -> dict | None:
        return next((f for f in self.entries() if f["id"] == firm_id), None)

    def _truncate(self, entry: dict):
        for k, limit in FIRM_LIMITS.items():
            if k in entry and isinstance(entry[k], str) and len(entry[k]) >= limit:
                entry[k] = entry[k][:limit-1]

    def add_or_update(self, entry: dict) -> str | None:
        """Save entry after validation. Returns error string on failure, None on success."""
        self._truncate(entry)
        url = entry.get("url", "")
        if not url_is_valid(url):
            return (
                f"Invalid URL for firmware '{entry.get('id', '?')}': '{url}'\n"
                "URL must start with http:// or https://\n"
                "Make sure git remote 'origin' is set to a GitHub remote, "
                "then re-import the binary."
            )
        firms = self._data.setdefault("firmwares", [])
        for i, f in enumerate(firms):
            if f["id"] == entry["id"]:
                firms[i] = entry
                _save(self.path, self._data)
                return None
        firms.append(entry)
        _save(self.path, self._data)
        return None

    def heal_urls(self, repo_path: str) -> list[str]:
        """
        Scan every entry and regenerate any missing or invalid URL from its filename.
        Returns a list of IDs that were fixed.
        """
        fixed = []
        try:
            base = get_raw_base(repo_path)
        except Exception:
            base = None
        if not base:
            return fixed
        for entry in self._data.get("firmwares", []):
            if not url_is_valid(entry.get("url", "")):
                filename = entry.get("name", "")
                if filename:
                    entry["url"] = f"{base}/firmwares/{filename}"
                    fixed.append(entry["id"])
        if fixed:
            _save(self.path, self._data)
        return fixed

    def delete(self, firm_id: str):
        self._data["firmwares"] = [f for f in self.entries() if f["id"] != firm_id]
        _save(self.path, self._data)

    def reload(self):
        self._data = _load(self.path, "firmwares")
