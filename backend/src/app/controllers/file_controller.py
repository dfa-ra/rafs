from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy.orm import Session

from app.models.database import get_db
from app.services.file_service import FileService

router = APIRouter()


@router.get("/lookup")
async def lookup(
    token: str = Query(..., description="Session token"),
    parent_ino: int = Query(..., description="Parent directory inode"),
    name: str = Query(..., description="File name"),
    db: Session = Depends(get_db)
):
    """Найти файл по имени в директории"""
    try:
        session = FileService.get_session_by_token(db, token)
        file_obj = FileService.find_file(db, session, parent_ino, name)

        if not file_obj:
            return {"found": False, "ino": 0, "mode": 0}

        return {
            "found": True,
            "ino": file_obj.ino,
            "mode": file_obj.mode,
            "size": file_obj.size
        }
    except HTTPException:
        return {"found": False, "ino": 0, "mode": 0}


@router.post("/create")
async def create_file(
    token: str = Query(..., description="Session token"),
    parent_ino: int = Query(..., description="Parent directory inode"),
    name: str = Query(..., description="File name"),
    mode: int = Query(..., description="File mode"),
    db: Session = Depends(get_db)
):
    """Создать файл"""
    session = FileService.get_session_by_token(db, token)
    file_obj = FileService.create_file(db, session, parent_ino, name, mode)

    return {
        "success": True,
        "ino": file_obj.ino,
        "mode": file_obj.mode
    }


@router.post("/mkdir")
async def create_directory(
    token: str = Query(..., description="Session token"),
    parent_ino: int = Query(..., description="Parent directory inode"),
    name: str = Query(..., description="Directory name"),
    mode: int = Query(..., description="Directory mode"),
    db: Session = Depends(get_db)
):
    """Создать директорию"""
    session = FileService.get_session_by_token(db, token)
    dir_obj = FileService.create_directory(db, session, parent_ino, name, mode)

    return {
        "success": True,
        "ino": dir_obj.ino,
        "mode": dir_obj.mode
    }


@router.delete("/unlink")
async def unlink_file(
    token: str = Query(..., description="Session token"),
    parent_ino: int = Query(..., description="Parent directory inode"),
    name: str = Query(..., description="File name"),
    db: Session = Depends(get_db)
):
    """Удалить файл"""
    session = FileService.get_session_by_token(db, token)
    FileService.delete_file(db, session, parent_ino, name)

    return {"success": True}


@router.delete("/rmdir")
async def remove_directory(
    token: str = Query(..., description="Session token"),
    parent_ino: int = Query(..., description="Parent directory inode"),
    name: str = Query(..., description="Directory name"),
    db: Session = Depends(get_db)
):
    """Удалить директорию"""
    session = FileService.get_session_by_token(db, token)
    FileService.delete_file(db, session, parent_ino, name)

    return {"success": True}


@router.post("/link")
async def create_link(
    token: str = Query(..., description="Session token"),
    target_ino: int = Query(..., description="Target file inode"),
    parent_ino: int = Query(..., description="Parent directory inode"),
    name: str = Query(..., description="Link name"),
    db: Session = Depends(get_db)
):
    """Создать жесткую ссылку"""
    session = FileService.get_session_by_token(db, token)
    link_obj = FileService.create_hard_link(db, session, target_ino, parent_ino, name)

    return {
        "success": True,
        "ino": link_obj.ino,
        "mode": link_obj.mode
    }


@router.get("/read")
async def read_file(
    token: str = Query(..., description="Session token"),
    ino: int = Query(..., description="File inode"),
    offset: int = Query(0, description="Read offset"),
    length: int = Query(..., description="Read length"),
    db: Session = Depends(get_db)
):
    """Прочитать данные из файла"""
    session = FileService.get_session_by_token(db, token)
    data = FileService.read_file(db, session, ino, offset, length)

    return {
        "success": True,
        "data": data.hex() if data else "",
        "length": len(data)
    }


@router.post("/write")
async def write_file(
    token: str = Query(..., description="Session token"),
    ino: int = Query(..., description="File inode"),
    offset: int = Query(0, description="Write offset"),
    data: str = Query(..., description="Data to write (hex string)"),
    db: Session = Depends(get_db)
):
    """Записать данные в файл"""
    session = FileService.get_session_by_token(db, token)

    try:
        binary_data = bytes.fromhex(data)
    except ValueError:
        raise HTTPException(status_code=400, detail="Invalid hex data")

    written = FileService.write_file(db, session, ino, offset, binary_data)

    return {
        "success": True,
        "written": written
    }


@router.get("/iterate")
async def iterate_directory(
    token: str = Query(..., description="Session token"),
    dir_ino: int = Query(..., description="Directory inode"),
    db: Session = Depends(get_db)
):
    """Получить список файлов в директории"""
    session = FileService.get_session_by_token(db, token)
    files = FileService.list_directory(db, session, dir_ino)

    result = []
    for file_obj in files:
        result.append({
            "name": file_obj.name,
            "ino": file_obj.ino,
            "mode": file_obj.mode,
            "size": file_obj.size
        })

    return {
        "success": True,
        "files": result
    }


@router.get("/stat")
async def get_file_stat(
    token: str = Query(..., description="Session token"),
    ino: int = Query(..., description="File inode"),
    db: Session = Depends(get_db)
):
    """Получить информацию о файле"""
    session = FileService.get_session_by_token(db, token)
    file_obj = FileService.get_file_info(db, session, ino)

    return {
        "success": True,
        "name": file_obj.name,
        "ino": file_obj.ino,
        "parent_ino": file_obj.parent_ino,
        "mode": file_obj.mode,
        "size": file_obj.size,
        "link_count": file_obj.link_count
    }


@router.get("/tree")
async def get_filesystem_tree(
    token: str = Query(..., description="Session token"),
    db: Session = Depends(get_db)
):
    """Получить всю структуру файловой системы в виде дерева"""
    session = FileService.get_session_by_token(db, token)
    tree = FileService.get_filesystem_tree(db, session)

    return tree
