#!/usr/bin/env python3
"""scanner CLI를 감싸는 로컬 테스트용 웹 서버.

외부 패키지 없이 파이썬 표준 라이브러리만 사용합니다.

    python3 frontend/web/server.py [포트]

첫 실행 시 scanner 바이너리가 없으면 자동으로 빌드를 시도합니다.
"""

import json
import mimetypes
import subprocess
import sys
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[2]
WEB_DIR = Path(__file__).resolve().parent
SCANNER = ROOT / "scanner"
UPLOAD_DIR = ROOT / "uploads"
OUTPUT_DIR = ROOT / "outputs"

MODES = {"color", "gray", "bw"}
EXTENSION_BY_TYPE = {
    "image/jpeg": ".jpg",
    "image/png": ".png",
    "image/webp": ".webp",
    "image/bmp": ".bmp",
    "image/tiff": ".tif",
}
MAX_UPLOAD_BYTES = 40 * 1024 * 1024

BUILD_COMMAND = (
    "clang++ -std=c++17 -O2 "
    "backend/src/main.cpp backend/src/scanner_config.cpp "
    "backend/src/document_scanner.cpp -Ibackend/include "
    "$(pkg-config --cflags --libs opencv4) -o scanner"
)


def ensure_scanner_binary() -> bool:
    if SCANNER.exists():
        return True
    print("scanner 바이너리가 없어 빌드를 시도합니다...")
    result = subprocess.run(
        BUILD_COMMAND, shell=True, cwd=ROOT, capture_output=True, text=True
    )
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        return False
    print("빌드 완료.")
    return True


class ScannerRequestHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # 기본 로그 간결화
        print("[web]", fmt % args)

    # --- 응답 헬퍼 ---

    def _send_json(self, payload: dict, status: int = 200) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path, status: int = 200) -> None:
        content_type = (
            mimetypes.guess_type(str(path))[0] or "application/octet-stream"
        )
        data = path.read_bytes()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _resolve_static(self, url_path: str) -> Path | None:
        """/outputs/, /uploads/ 하위 파일만 허용 (경로 탈출 방지)."""
        for prefix, directory in (("/outputs/", OUTPUT_DIR), ("/uploads/", UPLOAD_DIR)):
            if url_path.startswith(prefix):
                candidate = (directory / url_path[len(prefix):]).resolve()
                if candidate.is_file() and directory.resolve() in candidate.parents:
                    return candidate
        return None

    # --- 라우팅 ---

    def do_GET(self) -> None:
        url = urlparse(self.path)
        if url.path in ("/", "/index.html"):
            self._send_file(WEB_DIR / "index.html")
            return

        static_file = self._resolve_static(url.path)
        if static_file is not None:
            self._send_file(static_file)
            return

        self._send_json({"success": False, "message": "Not found"}, 404)

    def do_POST(self) -> None:
        url = urlparse(self.path)
        if url.path != "/api/scan":
            self._send_json({"success": False, "message": "Not found"}, 404)
            return

        mode = parse_qs(url.query).get("mode", ["color"])[0]
        if mode not in MODES:
            self._send_json(
                {"success": False, "message": f"지원하지 않는 모드: {mode}"}, 400
            )
            return

        content_length = int(self.headers.get("Content-Length", 0))
        if content_length <= 0:
            self._send_json({"success": False, "message": "빈 요청입니다."}, 400)
            return
        if content_length > MAX_UPLOAD_BYTES:
            self._send_json(
                {"success": False, "message": "파일이 너무 큽니다 (최대 40MB)."}, 413
            )
            return

        content_type = self.headers.get("Content-Type", "").split(";")[0].strip()
        extension = EXTENSION_BY_TYPE.get(content_type)
        if extension is None:
            self._send_json(
                {"success": False, "message": f"지원하지 않는 형식: {content_type}"},
                415,
            )
            return

        UPLOAD_DIR.mkdir(exist_ok=True)
        stem = uuid.uuid4().hex[:12]
        upload_path = UPLOAD_DIR / f"{stem}{extension}"
        upload_path.write_bytes(self.rfile.read(content_length))

        result = subprocess.run(
            [str(SCANNER), str(upload_path.relative_to(ROOT)), f"--mode={mode}"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=120,
        )

        try:
            payload = json.loads(result.stdout.strip().splitlines()[-1])
        except (json.JSONDecodeError, IndexError):
            self._send_json(
                {
                    "success": False,
                    "message": "scanner 출력 해석 실패",
                    "stderr": result.stderr[-2000:],
                },
                500,
            )
            return

        payload["mode"] = mode
        payload["input_url"] = f"/uploads/{upload_path.name}"
        if payload.get("success"):
            payload["output_url"] = f"/outputs/{stem}{extension}"
            payload["marked_url"] = f"/outputs/{stem}_marked{extension}"
        self._send_json(payload)


def main() -> None:
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    if not ensure_scanner_binary():
        print("빌드 실패 — README의 Build 섹션을 확인하세요.", file=sys.stderr)
        sys.exit(1)

    server = ThreadingHTTPServer(("127.0.0.1", port), ScannerRequestHandler)
    print(f"http://127.0.0.1:{port} 에서 실행 중 (Ctrl+C로 종료)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
