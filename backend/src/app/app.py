from __future__ import annotations

import struct
from typing import Optional

from fastapi import FastAPI, Query
from fastapi.responses import Response

from app.db import engine, get_session
from app.models import Base
from . import fs

SERVER_PORT = 8080

app = FastAPI(title="YUFS Backend (FastAPI + SQLAlchemy + SQLite)")


@app.on_event("startup")
def on_startup() -> None:
    Base.metadata.create_all(bind=engine)


def pack_response(ret_val: int, body: bytes) -> Response:
    payload = struct.pack("<q", ret_val) + body
    return Response(content=payload, media_type="application/octet-stream")


@app.get("/api/{cmd}")
def api_dispatch(
        cmd: str,
        token: str = Query(default="default"),
        parent_id: Optional[int] = Query(default=None),
        name: Optional[str] = Query(default=None),
        mode: Optional[int] = Query(default=None),
        target_id: Optional[int] = Query(default=None),
        id: Optional[int] = Query(default=None),
        offset: Optional[int] = Query(default=None),
        size: Optional[int] = Query(default=None),
        buf: Optional[str] = Query(default=None),
):
    try:
        with get_session() as db:
            fs.ensure_root_exists(db, token)

            if cmd == "lookup":
                if parent_id is None or name is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_lookup(db, token, parent_id, name)

            elif cmd == "create":
                if parent_id is None or name is None or mode is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_create(db, token, parent_id, name, int(mode))

            elif cmd == "link":
                if parent_id is None or name is None or target_id is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_link(db, token, parent_id, name, int(target_id))

            elif cmd == "unlink":
                if parent_id is None or name is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_unlink(db, token, parent_id, name)

            elif cmd == "rmdir":
                if parent_id is None or name is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_rmdir(db, token, parent_id, name)

            elif cmd == "getattr":
                if id is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_getattr(db, token, int(id))

            elif cmd == "read":
                if id is None or offset is None or size is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_read(db, token, int(id), int(offset), int(size))

            elif cmd == "write":
                if id is None or offset is None or buf is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_write(db, token, int(id), int(offset), buf)

            elif cmd == "iterate":
                if id is None or offset is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_iterate(db, token, int(id), int(offset))

            elif cmd == "is_empty_dir":
                if id is None:
                    return pack_response(-1, b"")
                ret, body = fs.handle_is_empty_dir(db, token, int(id))

            elif cmd == "get_num_dir":
                if id is None:
                    return pack_response(-1, b"")
                ret, body = fs.get_num_dir(db, token, int(id))

            else:
                return pack_response(-1, b"")

            return pack_response(ret, body)

    except Exception:
        return pack_response(-1, b"")


@app.get("/health")
def is_health():
    return "OK i am health"
