#!/usr/bin/env python3
"""index.html 의 `js/*.js?v=` 를 지금 시각으로 바꾼다.

붙이지 않으면 브라우저가 예전 js 를 계속 쓴다. 로컬 서버로 고쳐 가며 시험할 때
"분명 고쳤는데 그대로다" 가 나오는 원인이라, 새로고침에 기대지 않는다.

    python3 web/bump_version.py
"""

import pathlib
import re
import time

HTML = pathlib.Path(__file__).with_name("index.html")


def main():
    ver = time.strftime("%Y%m%d%H%M%S")   # 같은 분에 두 번 고쳐도 바뀌게 초까지
    text = HTML.read_text(encoding="utf-8")

    new, n = re.subn(r'(<script src="js/[a-z0-9_]+\.js)(\?v=\d+)?(">)',
                     rf"\1?v={ver}\3", text)
    if n == 0:
        raise SystemExit("index.html 에서 js 태그를 못 찾았다")

    if new == text:
        print(f"이미 {ver} 이다")
        return

    HTML.write_text(new, encoding="utf-8")
    print(f"{n} 개 갱신 : ?v={ver}")


if __name__ == "__main__":
    main()
