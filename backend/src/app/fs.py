from __future__ import annotations

import struct
import urllib.parse
from sqlalchemy import select, func, delete, update
from sqlalchemy.orm import Session

from app.models import Inode, Dirent

ROOT_INO = 1000
S_IFDIR = 0o040000


def pack_stat(inode_id: int, mode: int, size: int) -> bytes:
    return struct.pack("<IIQ", inode_id, mode, size)


def ensure_root_exists(db: Session, token: str) -> None:
    exists = db.execute(
        select(Inode.id).where(Inode.token == token, Inode.id == ROOT_INO)
    ).first()
    if exists:
        return

    root = Inode(
        token=token,
        id=ROOT_INO,
        mode=S_IFDIR | 0o777,
        nlink=1,
        size=0,
        content=None,
    )
    db.add(root)


def handle_lookup(db: Session, token: str, parent_id: int, name: str) -> tuple[int, bytes]:
    stmt = (
        select(Inode.id, Inode.mode, Inode.size)
        .select_from(Dirent)
        .join(Inode, (Dirent.inode_id == Inode.id) & (Dirent.token == Inode.token))
        .where(Dirent.token == token, Dirent.parent_id == parent_id, Dirent.name == name)
    )
    row = db.execute(stmt).first()
    if row:
        inode_id, mode, size = row
        return 0, pack_stat(inode_id, mode, size)
    return -1, b""


def handle_create(db: Session, token: str, parent_id: int, name: str, mode: int) -> tuple[int, bytes]:
    lookup_result, _ = handle_lookup(db, token, parent_id, name)
    print(f"CREATE: lookup_result={lookup_result} for {parent_id}/{name}")
    if lookup_result == 0:
        print(f"CREATE: file {parent_id}/{name} exists, returning -1")
        return -1, b""


    max_id = db.execute(
        select(func.max(Inode.id)).where(Inode.token == token)
    ).scalar_one()

    new_id = (max_id if max_id is not None else ROOT_INO) + 1

    nlink = 2 if mode & S_IFDIR else 1

    try:
        inode = Inode(token=token, id=new_id, mode=mode, nlink=nlink, size=0, content=None)
        db.add(inode)
        db.add(Dirent(token=token, parent_id=parent_id, name=name, inode_id=new_id))

        if mode & S_IFDIR:
            db.execute(
                update(Inode)
                .where(Inode.token == token, Inode.id == parent_id)
                .values(nlink=Inode.nlink + 1)
            )

        print(f"CREATE: successfully created {parent_id}/{name} with inode {new_id}")
        return 0, pack_stat(new_id, mode, 0)
    except Exception as e:
        print(f"CREATE: error creating {parent_id}/{name}: {e}")
        raise


def handle_link(db: Session, token: str, parent_id: int, name: str, target_id: int) -> tuple[int, bytes]:
    target_mode = db.execute(
        select(Inode.mode).where(Inode.token == token, Inode.id == target_id)
    ).scalar_one_or_none()

    if target_mode is None:
        return -1, b""

    if target_mode & S_IFDIR:
        return -1, b""

    db.add(Dirent(token=token, parent_id=parent_id, name=name, inode_id=target_id))
    db.execute(
        update(Inode)
        .where(Inode.token == token, Inode.id == target_id)
        .values(nlink=Inode.nlink + 1)
    )
    return 0, b""


def handle_unlink(db: Session, token: str, parent_id: int, name: str) -> tuple[int, bytes]:
    inode_id = db.execute(
        select(Dirent.inode_id).where(
            Dirent.token == token, Dirent.parent_id == parent_id, Dirent.name == name
        )
    ).scalar_one_or_none()

    print(f"UNLINK: inode_id={inode_id} for {parent_id}/{name}")
    if inode_id is None:
        print(f"UNLINK: file {parent_id}/{name} not found")
        return -1, b""

    db.execute(
        delete(Dirent).where(
            Dirent.token == token, Dirent.parent_id == parent_id, Dirent.name == name
        )
    )

    result = db.execute(
        update(Inode)
        .where(Inode.token == token, Inode.id == inode_id)
        .values(nlink=Inode.nlink - 1)
        .returning(Inode.nlink)
    )
    new_nlink = result.scalar_one()

    if new_nlink == 0:
        print(f"UNLINK: deleting inode {inode_id}")
        db.execute(
            delete(Inode).where(Inode.token == token, Inode.id == inode_id)
        )
    else:
        print(f"UNLINK: keeping inode {inode_id}, nlink={new_nlink}")

    print(f"UNLINK: completed for {parent_id}/{name}")
    return 0, b""


def handle_rmdir(db: Session, token: str, parent_id: int, name: str) -> tuple[int, bytes]:
    inode_id = db.execute(
        select(Dirent.inode_id).where(
            Dirent.token == token, Dirent.parent_id == parent_id, Dirent.name == name
        )
    ).scalar_one_or_none()

    if inode_id is None:
        return -1, b""

    db.execute(
        delete(Dirent).where(
            Dirent.token == token, Dirent.parent_id == parent_id, Dirent.name == name
        )
    )

    db.execute(
        update(Inode)
        .where(Inode.token == token, Inode.id == parent_id)
        .values(nlink=Inode.nlink - 1)
    )

    result = db.execute(
        update(Inode)
        .where(Inode.token == token, Inode.id == inode_id)
        .values(nlink=Inode.nlink - 1)
        .returning(Inode.nlink)
    )
    new_nlink = result.scalar_one()

    if new_nlink == 0:
        db.execute(
            delete(Inode).where(Inode.token == token, Inode.id == inode_id)
        )

    return 0, b""


def handle_getattr(db: Session, token: str, inode_id: int) -> tuple[int, bytes]:
    row = db.execute(
        select(Inode.id, Inode.mode, Inode.size).where(Inode.token == token, Inode.id == inode_id)
    ).first()

    if row:
        i_id, mode, size = row
        return 0, pack_stat(i_id, mode, size)

    return -1, b""


def handle_read(db: Session, token: str, inode_id: int, offset: int, size: int) -> tuple[int, bytes]:
    content = db.execute(
        select(Inode.content).where(Inode.token == token, Inode.id == inode_id)
    ).scalar_one_or_none()

    data = content if content else b""
    if offset >= len(data):
        return 0, b""

    chunk = data[offset : offset + size]
    return len(chunk), chunk


def handle_write(db: Session, token: str, inode_id: int, offset: int, buf_param: str) -> tuple[int, bytes]:
    buf = urllib.parse.unquote_to_bytes(buf_param)

    content = db.execute(
        select(Inode.content).where(Inode.token == token, Inode.id == inode_id)
    ).scalar_one_or_none()

    data = bytearray(content) if content else bytearray()

    new_end = offset + len(buf)
    if new_end > len(data):
        data.extend(b"\0" * (new_end - len(data)))
    data[offset : offset + len(buf)] = buf

    db.execute(
        update(Inode)
        .where(Inode.token == token, Inode.id == inode_id)
        .values(content=bytes(data), size=len(data))
    )
    return len(buf), b""


def handle_iterate(db: Session, token: str, inode_id: int, offset: int) -> tuple[int, bytes]:
    entries: list[tuple[int, str, int]] = []
    entries.append((inode_id, ".", S_IFDIR | 0o777))
    entries.append((inode_id, "..", S_IFDIR | 0o777))

    rows = db.execute(
        select(Dirent.name, Dirent.inode_id, Inode.mode)
        .select_from(Dirent)
        .join(Inode, (Dirent.inode_id == Inode.id) & (Dirent.token == Inode.token))
        .where(Dirent.token == token, Dirent.parent_id == inode_id)
    ).all()

    for name, child_id, mode in rows:
        entries.append((child_id, name, mode))

    if offset >= len(entries):
        return -1, b""
    print(entries)
    child_id, name, mode = entries[offset]
    print(child_id, name, mode)
    name_bytes = name.encode("utf-8")
    packed = struct.pack("<I256sI", child_id, name_bytes, mode)
    return 0, packed


def handle_is_empty_dir(db: Session, token: str, inode_id: int) -> tuple[int, bytes]:
    count = db.execute(
        select(func.count())
        .select_from(Dirent)
        .where(Dirent.token == token, Dirent.parent_id == inode_id)
    ).scalar()

    return 0 if count == 0 else 1, b""
