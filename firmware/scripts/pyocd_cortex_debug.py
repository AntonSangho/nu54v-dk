# pyOCD 사용자 스크립트 (cortex-debug 연동용)
#
# cortex-debug 1.12.1 은 pyOCD 서버가 떴는지를 출력 문구로만 판단하는데
# (dist/debugadapter.js: initMatch() → /GDB server started (at|on) port/)
# pyOCD 0.36 부터 문구가 "GDB server listening on port N (core N)" 으로 바뀌어
# 영영 매칭되지 않고 "Failed to launch PyOCD GDB Server: Timeout." 이 난다.
#
# 서버는 정상 동작하므로 로그 문구만 예전 형식으로 되돌려 준다.
# launch.json 의 serverArgs 에 --script 로 넘긴다.
import logging

_OLD = "GDB server listening on port"
_NEW = "GDB server started on port"


class _CortexDebugCompat(logging.Filter):
    def filter(self, record):
        msg = record.msg
        if isinstance(msg, str) and msg.startswith(_OLD):
            record.msg = _NEW + msg[len(_OLD):]
        return True


_filter = _CortexDebugCompat()
for _handler in logging.getLogger().handlers:
    _handler.addFilter(_filter)
