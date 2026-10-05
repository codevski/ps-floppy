"""A stand-in Floppy for `make check`. Answers the calls sync makes.

Shapes follow replies from a live Floppy, trimmed. Writes follow
Floppy's source at cbdd97d, including the traps: the track call reads a bare
`progress` number as hours, it never checks for an existing entry, and a
PATCH can be applied and still answer an error.

The search text picks special cases (BOOM, HUGE, CUT, CHUNKED). The test
sets how the next write misbehaves with POST /mock/mode, and starts clean
with POST /mock/reset.
Usage: python3 tests/mock_floppy.py <port>
"""
import json
import re
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

TOKEN = "good-token"

# Titles for the IGDB IDs the mock knows.
TITLES = {
    "347180": "Silent Hill f: Deluxe Edition",
    "284716": "Ratchet & Clank: Rift Apart",
    "7": "CHUNKED",
    "555": "Fresh Game",
    "556": "Second Game",
}

IGDB = {
    "SILENT HILL f": [
        {"media_id": 222343, "source": "igdb", "title": "Silent Hill f",
         "platforms": ["PC (Microsoft Windows)", "PlayStation 5"]},
        {"media_id": 347180, "source": "igdb",
         "title": "Silent Hill f: Deluxe Edition",
         "platforms": ["PlayStation 5"]},
    ],
    "FRESH GAME": [
        {"media_id": 555, "source": "igdb", "title": "Fresh Game",
         "platforms": ["PlayStation 5"]},
    ],
}

state = {}


def reset():
    state["entries"] = [entry("347180", 42), entry("284716", 5),
                        entry("7", 3)]
    state["mode"] = "normal"
    state["next_id"] = 1


def entry(media_id, progress, status="In progress"):
    return {"id": 0, "item": {"media_id": media_id, "source": "igdb",
                              "title": TITLES[media_id],
                              "platforms": ["PlayStation 5"]},
            "item_id": "game/igdb/%s" % media_id, "tracked": True,
            "status": status, "progress": progress,
            "progress_unit": "minutes", "source": "PlayStation"}


def page(results):
    return {"pagination": {"total": len(results), "limit": 10, "offset": 0,
                           "next": None, "previous": None},
            "results": results}


def duration_minutes(value):
    """Floppy's CustomDurationField, for the forms the tracker might send."""
    text = str(value).strip().lower()
    if re.fullmatch(r"\d+(\.\d+)?", text):
        return int(float(text) * 60)  # a bare number is hours
    m = re.fullmatch(r"(\d+)\s*(minutes?|mins?|min)", text)
    if m:
        return int(m.group(1))
    raise ValueError(text)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def send(self, status, body, chunked=False):
        data = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        if chunked:
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(data), 100):
                part = data[i:i + 100]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        self.close_connection = True

    def body(self):
        length = int(self.headers.get("Content-Length", 0))
        return json.loads(self.rfile.read(length) or b"{}")

    def authorised(self):
        if self.headers.get("Authorization") != "Bearer " + TOKEN:
            self.send(401, {"detail": "Invalid token."})
            return False
        return True

    def misbehave(self, reply_status, reply_body):
        """Finish a write that has already been applied, per the mode."""
        mode, state["mode"] = state["mode"], "normal"
        if mode == "drop":  # applied, then the connection dies
            self.close_connection = True
            return
        if mode == "fail_after":  # applied, then the metadata fetch fails
            return self.send(500, {"detail": "Internal Server Error."})
        return self.send(reply_status, reply_body)

    def do_GET(self):
        url = urlsplit(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        if not self.authorised():
            return
        if q.get("limit") != "10":
            return self.send(400, {"detail": "expected limit=10"})
        text = q.get("search", "")

        if url.path == "/api/v1/media/game":
            if text == "BOOM":
                return self.send(500, {"detail": "Server error."})
            if text == "HUGE":
                big = entry("7", 0)
                big["item"]["synopsis"] = "x" * 80000
                return self.send(200, page([big]))
            if text == "CUT":
                self.send_response(200)
                self.send_header("Content-Length", "5000")
                self.end_headers()
                self.wfile.write(json.dumps(page([entry("7", 0)])).encode()[:200])
                self.close_connection = True
                return
            if text == "Ratchet & Clank: Rift Apart™":
                text = "Ratchet & Clank: Rift Apart"
            found = [e for e in state["entries"]
                     if text.lower() in e["item"]["title"].lower()]
            return self.send(200, page(found), chunked=text == "CHUNKED")

        if url.path == "/api/v1/search/game":
            if q.get("source") != "igdb":
                return self.send(400, {"detail": "expected source=igdb"})
            return self.send(200, page(IGDB.get(text, [])))

        return self.send(404, {"detail": "Not found."})

    def do_POST(self):
        url = urlsplit(self.path)
        if url.path == "/mock/reset":
            reset()
            return self.send(200, {})
        if url.path == "/mock/mode":
            state["mode"] = self.body()["mode"]
            return self.send(200, {})
        if url.path == "/mock/set":  # an outside change, like a Steam import
            body = self.body()
            for e in state["entries"]:
                if e["item"]["media_id"] == body["media_id"]:
                    e["progress"] = body["progress"]
            return self.send(200, {})
        if url.path == "/mock/dump":
            return self.send(200, state["entries"])
        if not self.authorised():
            return

        if url.path == "/api/v1/media/game":
            body = self.body()
            if state["mode"] == "refuse":
                state["mode"] = "normal"
                return self.send(400, {"detail": "Invalid media data."})
            media_id = str(body.get("media_id"))
            if body.get("source") != "igdb" or media_id not in TITLES:
                return self.send(400, {"detail": "Invalid media data."})
            try:
                minutes = duration_minutes(body.get("progress", 0))
            except ValueError:
                return self.send(400, {"detail": "Invalid time format"})
            # Like Floppy: no check for an existing entry.
            new = entry(media_id, minutes, body.get("status", "Planning"))
            state["entries"].append(new)
            return self.misbehave(201, new)

        return self.send(404, {"detail": "Not found."})

    def do_PATCH(self):
        url = urlsplit(self.path)
        if not self.authorised():
            return
        m = re.fullmatch(r"/api/v1/media/game/igdb/(\d+)", url.path)
        if not m:
            return self.send(404, {"detail": "Not found."})
        mine = [e for e in state["entries"]
                if e["item"]["media_id"] == m.group(1)]
        if not mine:
            return self.send(404, {"detail": "Media not found or not tracked."})
        if state["mode"] == "refuse":
            state["mode"] = "normal"
            return self.send(400, {"detail": "Failed to update media."})
        progress = self.body().get("progress")
        if not isinstance(progress, int) or progress < 0:
            return self.send(400, {"detail": "Failed to update media."})
        mine[0]["progress"] = progress
        detail = {"id": 1, "media_id": m.group(1), "source": "igdb",
                  "title": TITLES[m.group(1)], "max_progress": 1,
                  "consumptions_number": len(mine),
                  "consumptions": [{"consumption_id": i,
                                    "progress": e["progress"],
                                    "status": 1}
                                   for i, e in enumerate(mine)]}
        return self.misbehave(200, detail)


if __name__ == "__main__":
    reset()
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), Handler).serve_forever()
