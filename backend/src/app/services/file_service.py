import os
from typing import List, Optional
from sqlalchemy.orm import Session
from app.models.database import File, Session as DBSession
from fastapi import HTTPException


class FileService:
    ROOT_INO = 1000

    @staticmethod
    def get_session_by_token(db: Session, token: str) -> DBSession:
        """Получить сессию по токену"""
        session = db.query(DBSession).filter(DBSession.token == token).first()
        if not session:
            raise HTTPException(status_code=404, detail="Session not found")
        return session

    @staticmethod
    def create_root_directory(db: Session, session: DBSession):
        """Создать корневую директорию для сессии"""
        root_dir = File(
            session_id=session.id,
            name="",
            ino=FileService.ROOT_INO,
            parent_ino=0,
            mode=0o755 | 0o40000,
            data=None,
            size=0,
            capacity=0,
            link_count=2
        )
        db.add(root_dir)
        db.commit()
        return root_dir

    @staticmethod
    def ensure_root_exists(db: Session, session: DBSession) -> File:
        """Убедиться, что корневая директория существует"""
        root = db.query(File).filter(
            File.session_id == session.id,
            File.ino == FileService.ROOT_INO
        ).first()

        if not root:
            root = FileService.create_root_directory(db, session)

        return root

    @staticmethod
    def get_next_ino(db: Session, session: DBSession) -> int:
        """Получить следующий свободный inode номер"""
        max_ino = db.query(File).filter(File.session_id == session.id).order_by(File.ino.desc()).first()
        return (max_ino.ino + 1) if max_ino else FileService.ROOT_INO + 1

    @staticmethod
    def find_file(db: Session, session: DBSession, parent_ino: int, name: str) -> Optional[File]:
        """Найти файл по имени в директории"""
        return db.query(File).filter(
            File.session_id == session.id,
            File.parent_ino == parent_ino,
            File.name == name
        ).first()

    @staticmethod
    def create_file(db: Session, session: DBSession, parent_ino: int, name: str, mode: int) -> File:
        """Создать новый файл"""

        existing = FileService.find_file(db, session, parent_ino, name)
        if existing:
            raise HTTPException(status_code=409, detail="File already exists")

        parent = db.query(File).filter(
            File.session_id == session.id,
            File.ino == parent_ino
        ).first()
        if not parent:
            raise HTTPException(status_code=404, detail="Parent directory not found")

        if not (parent.mode & 0o40000):  # S_IFDIR
            raise HTTPException(status_code=400, detail="Parent is not a directory")

        ino = FileService.get_next_ino(db, session)

        file_obj = File(
            session_id=session.id,
            name=name,
            ino=ino,
            parent_ino=parent_ino,
            mode=mode,
            data=b"" if mode & 0o100000 else None,  # S_IFREG - обычный файл
            size=0,
            capacity=0,
            link_count=1
        )

        db.add(file_obj)
        db.commit()
        db.refresh(file_obj)
        return file_obj

    @staticmethod
    def create_directory(db: Session, session: DBSession, parent_ino: int, name: str, mode: int) -> File:
        """Создать директорию"""
        mode = mode | 0o40000
        return FileService.create_file(db, session, parent_ino, name, mode)

    @staticmethod
    def read_file(db: Session, session: DBSession, ino: int, offset: int, length: int) -> bytes:
        """Прочитать данные из файла"""
        file_obj = db.query(File).filter(
            File.session_id == session.id,
            File.ino == ino
        ).first()

        if not file_obj:
            raise HTTPException(status_code=404, detail="File not found")

        if not (file_obj.mode & 0o100000):  # S_IFREG
            raise HTTPException(status_code=400, detail="Not a regular file")

        if not file_obj.data:
            return b""

        data = file_obj.data[offset:offset + length] if offset < len(file_obj.data) else b""
        return data

    @staticmethod
    def write_file(db: Session, session: DBSession, ino: int, offset: int, data: bytes) -> int:
        """Записать данные в файл"""
        file_obj = db.query(File).filter(
            File.session_id == session.id,
            File.ino == ino
        ).first()

        if not file_obj:
            raise HTTPException(status_code=404, detail="File not found")

        if not (file_obj.mode & 0o100000):  # S_IFREG
            raise HTTPException(status_code=400, detail="Not a regular file")

        if file_obj.data is None:
            file_obj.data = b""

        required_size = offset + len(data)
        if required_size > len(file_obj.data):

            new_capacity = max(required_size, len(file_obj.data) * 2 or 4096)
            file_obj.data = file_obj.data.ljust(new_capacity, b'\x00')

        file_obj.data = file_obj.data[:offset] + data + file_obj.data[offset + len(data):]
        file_obj.size = max(file_obj.size, required_size)
        file_obj.capacity = len(file_obj.data)

        db.commit()
        return len(data)

    @staticmethod
    def delete_file(db: Session, session: DBSession, parent_ino: int, name: str):
        """Удалить файл"""
        file_obj = FileService.find_file(db, session, parent_ino, name)
        if not file_obj:
            raise HTTPException(status_code=404, detail="File not found")

        if file_obj.mode & 0o40000:  # S_IFDIR
            if not FileService.is_directory_empty(db, session, file_obj.ino):
                raise HTTPException(status_code=400, detail="Directory not empty")

        file_obj.link_count -= 1
        if file_obj.link_count <= 0:
            db.delete(file_obj)

        db.commit()

    @staticmethod
    def is_directory_empty(db: Session, session: DBSession, dir_ino: int) -> bool:
        """Проверить, пуста ли директория"""
        children = db.query(File).filter(
            File.session_id == session.id,
            File.parent_ino == dir_ino
        ).count()
        return children == 0

    @staticmethod
    def list_directory(db: Session, session: DBSession, dir_ino: int) -> List[File]:
        """Получить список файлов в директории"""
        return db.query(File).filter(
            File.session_id == session.id,
            File.parent_ino == dir_ino
        ).all()

    @staticmethod
    def create_hard_link(db: Session, session: DBSession, target_ino: int, parent_ino: int, name: str) -> File:
        """Создать жесткую ссылку"""
        target = db.query(File).filter(
            File.session_id == session.id,
            File.ino == target_ino
        ).first()

        if not target:
            raise HTTPException(status_code=404, detail="Target file not found")

        existing = FileService.find_file(db, session, parent_ino, name)
        if existing:
            raise HTTPException(status_code=409, detail="Link name already exists")

        link = File(
            session_id=session.id,
            name=name,
            ino=target.ino,
            parent_ino=parent_ino,
            mode=target.mode,
            data=target.data,
            size=target.size,
            capacity=target.capacity,
            link_count=target.link_count + 1
        )

        target.link_count += 1

        db.add(link)
        db.commit()
        db.refresh(link)
        return link

    @staticmethod
    def get_file_info(db: Session, session: DBSession, ino: int) -> File:
        """Получить информацию о файле"""
        file_obj = db.query(File).filter(
            File.session_id == session.id,
            File.ino == ino
        ).first()

        if not file_obj:
            raise HTTPException(status_code=404, detail="File not found")

        return file_obj

    @staticmethod
    def get_filesystem_tree(db: Session, session: DBSession) -> dict:
        """Получить всю структуру файловой системы в виде дерева"""
        root = FileService.ensure_root_exists(db, session)

        def build_tree(dir_ino: int) -> dict:
            """Рекурсивно построить дерево для директории"""
            children = db.query(File).filter(
                File.session_id == session.id,
                File.parent_ino == dir_ino
            ).order_by(File.name).all()

            current_dir = db.query(File).filter(
                File.session_id == session.id,
                File.ino == dir_ino
            ).first()

            if not current_dir:
                return None

            is_directory = current_dir.mode & 0o40000  # S_IFDIR

            node = {
                "name": current_dir.name if current_dir.name else "/",
                "ino": current_dir.ino,
                "mode": current_dir.mode,
                "size": current_dir.size,
                "link_count": current_dir.link_count,
                "type": "directory" if is_directory else "file"
            }

            if is_directory:
                node["children"] = []
                for child in children:
                    child_tree = build_tree(child.ino)
                    if child_tree:
                        node["children"].append(child_tree)
            else:
                node["has_data"] = current_dir.data is not None

            return node

        tree = build_tree(root.ino)
        return {"filesystem": tree}
