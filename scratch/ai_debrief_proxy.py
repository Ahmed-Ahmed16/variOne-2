#!/usr/bin/env python3
"""
VariOne AI Debrief — LAN proxy (local-AI path).

The ESP32 cannot comfortably do TLS to Google (only ~31KB contiguous heap when
PSRAM is off). This tiny proxy runs on a PC on the SAME WiFi: the device POSTs
plain HTTP {"prompt": "..."} here, and the PC does the real HTTPS call to Gemini
(or a local Ollama) and returns {"text": "..."}.

On the device: set aiEndpoint (AI Setup web form, or config) to
    http://<THIS-PC-LAN-IP>:8000/
Leave aiEndpoint empty to use the on-device cloud path instead.

Run:
    GEMINI_API_KEY=AQ...   python3 ai_debrief_proxy.py                    # forward to Gemini
    BACKEND=groq GROQ_API_KEY=gsk_...  python3 ai_debrief_proxy.py        # forward to Groq
    BACKEND=ollama OLLAMA_MODEL=llama3.2  python3 ai_debrief_proxy.py     # local Ollama

Needs: pip install flask requests
"""
import os
from flask import Flask, request, jsonify
import requests

app = Flask(__name__)

BACKEND = os.environ.get("BACKEND", "gemini")  # "gemini" | "groq" | "ollama"
GEMINI_KEY = os.environ.get("GEMINI_API_KEY", "")
GEMINI_MODEL = os.environ.get("GEMINI_MODEL", "gemini-3.5-flash")
GROQ_KEY = os.environ.get("GROQ_API_KEY", "")
GROQ_MODEL = os.environ.get("GROQ_MODEL", "llama-3.3-70b-versatile")
OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://127.0.0.1:11434/api/generate")
OLLAMA_MODEL = os.environ.get("OLLAMA_MODEL", "llama3.2")


@app.route("/", methods=["POST"])
def generate():
    data = request.get_json(force=True, silent=True) or {}
    prompt = data.get("prompt", "")
    if not prompt:
        return jsonify({"text": ""}), 400

    if BACKEND == "groq":
        r = requests.post(
            "https://api.groq.com/openai/v1/chat/completions",
            headers={"Authorization": f"Bearer {GROQ_KEY}"},
            json={"model": GROQ_MODEL, "messages": [{"role": "user", "content": prompt}]},
            timeout=60,
        )
        r.raise_for_status()
        return jsonify({"text": r.json()["choices"][0]["message"]["content"]})

    if BACKEND == "ollama":
        r = requests.post(
            OLLAMA_URL,
            json={"model": OLLAMA_MODEL, "prompt": prompt, "stream": False},
            timeout=120,
        )
        r.raise_for_status()
        return jsonify({"text": r.json().get("response", "")})

    # default: Gemini. New "auth keys" require the x-goog-api-key header
    # (the legacy ?key= query param is being retired).
    url = (
        f"https://generativelanguage.googleapis.com/v1beta/models/"
        f"{GEMINI_MODEL}:generateContent"
    )
    r = requests.post(
        url,
        headers={"x-goog-api-key": GEMINI_KEY},
        json={"contents": [{"parts": [{"text": prompt}]}]},
        timeout=60,
    )
    r.raise_for_status()
    j = r.json()
    text = j["candidates"][0]["content"]["parts"][0]["text"]
    return jsonify({"text": text})


if __name__ == "__main__":
    # 0.0.0.0 so the ESP32 on the LAN can reach it. Pick any free port.
    app.run(host="0.0.0.0", port=int(os.environ.get("PORT", "8000")))
