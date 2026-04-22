from __future__ import annotations

from sqlalchemy import Integer, Text, LargeBinary
from sqlalchemy.orm import Mapped, mapped_column
from sqlalchemy.schema import PrimaryKeyConstraint

from app.db import Base


class Inode(Base):
    __tablename__ = "inodes"

    token: Mapped[str] = mapped_column(Text, nullable=False)
    id: Mapped[int] = mapped_column(Integer, nullable=False)
    mode: Mapped[int] = mapped_column(Integer, nullable=False)
    nlink: Mapped[int] = mapped_column(Integer, nullable=False, default=1, server_default="1")
    size: Mapped[int] = mapped_column(Integer, nullable=False, default=0, server_default="0")
    content: Mapped[bytes | None] = mapped_column(LargeBinary, nullable=True)

    __table_args__ = (
        PrimaryKeyConstraint("token", "id", name="pk_inodes_token_id"),
    )


class Dirent(Base):
    __tablename__ = "dirents"

    token: Mapped[str] = mapped_column(Text, nullable=False)
    parent_id: Mapped[int] = mapped_column(Integer, nullable=False)
    name: Mapped[str] = mapped_column(Text, nullable=False)
    inode_id: Mapped[int] = mapped_column(Integer, nullable=False)

    __table_args__ = (
        PrimaryKeyConstraint("token", "parent_id", "name", name="pk_dirents_token_parent_name"),
    )
