#!/usr/bin/env python3
"""
OmniRoute Model Health Check
Tests every model from OmniRoute and reports which ones work.
"""

import json
import subprocess
import sys
import time
from datetime import datetime

BASE_URL = "http://localhost:20128/v1"
TEST_PROMPT = "Say hi in 2 words"
MAX_TOKENS = 10
TIMEOUT = 45  # seconds per model


def get_models():
    """Fetch all models from OmniRoute."""
    result = subprocess.run(
        ["curl", "-s", f"{BASE_URL}/models"],
        capture_output=True, text=True, timeout=15
    )
    if result.returncode != 0:
        print("ERROR: Failed to fetch models from OmniRoute")
        sys.exit(1)

    data = json.loads(result.stdout)
    models = [m["id"] for m in data.get("data", [])]
    return sorted(models)


def test_model(model_id):
    """Test a single model. Returns (success, message)."""
    payload = json.dumps({
        "model": model_id,
        "messages": [{"role": "user", "content": TEST_PROMPT}],
        "max_tokens": MAX_TOKENS,
        "stream": False,
    })

    try:
        result = subprocess.run(
            [
                "curl", "-s", "-w", "\n%{http_code}",
                f"{BASE_URL}/chat/completions",
                "-H", "Content-Type: application/json",
                "-d", payload,
            ],
            capture_output=True, text=True, timeout=TIMEOUT,
        )

        output = result.stdout
        lines = output.strip().split("\n")
        http_code = lines[-1].strip() if lines else "000"
        body = "\n".join(lines[:-1]) if len(lines) > 1 else ""

        if http_code == "200":
            try:
                data = json.loads(body)
                content = data.get("choices", [{}])[0].get("message", {}).get("content", "")
                return True, content[:60]
            except (json.JSONDecodeError, KeyError, IndexError):
                provider = model_id.split("/")[0]
                return True, f"(response received, provider: {provider})"
        else:
            try:
                err = json.loads(body)
                msg = err.get("error", {}).get("message", body[:120])
            except json.JSONDecodeError:
                msg = body[:120] if body else "no response"
            return False, f"HTTP {http_code}: {msg[:120]}"

    except subprocess.TimeoutExpired:
        return False, "TIMEOUT"
    except Exception as e:
        return False, f"ERROR: {e}"


def main():
    print("=" * 70)
    print("  OmniRoute Model Health Check")
    print(f"  Started: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 70)

    print("\nFetching model list from OmniRoute...")
    all_models = get_models()
    print(f"  Found {len(all_models)} models\n")

    print(f"{'Provider':<20} {'Total':>6} {'Working':>8} {'Failed':>8} {'Rate':>6}")
    print("-" * 50)

    results = {}
    total_all = 0
    total_working = 0
    total_failed = 0

    # Group by provider
    groups = {}
    for m in all_models:
        p = m.split("/")[0]
        groups.setdefault(p, []).append(m)

    for provider in sorted(groups.keys()):
        models = groups[provider]
        working = 0
        failed = 0
        provider_results = []

        print(f"\n{'─' * 50}")
        print(f"  [{provider}] testing {len(models)} model(s)...")
        print(f"{'─' * 50}")

        for i, model_id in enumerate(models, 1):
            prefix = "├─" if i < len(models) else "└─"
            short = model_id[len(provider) + 1:] if model_id.startswith(provider + "/") else model_id
            print(f"  {prefix} ({i}/{len(models)}) {short[:50]}", end="", flush=True)

            success, msg = test_model(model_id)

            if success:
                print(f"  ✅  {msg[:60]}")
                working += 1
            else:
                print(f"  ❌  {msg[:60]}")
                failed += 1

            time.sleep(0.2)

        total = len(models)
        rate = f"{working/total*100:.0f}%" if total > 0 else "N/A"
        print(f"\n  -> {working}/{total} working ({rate})")
        total_all += total
        total_working += working
        total_failed += failed

    # Final summary
    print("\n" + "=" * 70)
    print("  FINAL SUMMARY")
    print("=" * 70)
    print(f"{'Total Models':<20} {'Working':>8} {'Failed':>8} {'Rate':>6}")
    print("-" * 50)
    overall_rate = f"{total_working/total_all*100:.0f}%" if total_all > 0 else "N/A"
    print(f"{total_all:<20} {total_working:>8} {total_failed:>8} {overall_rate:>6}")
    print("=" * 70)
    print(f"Completed: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")


if __name__ == "__main__":
    main()
