import secrets

from fastapi import APIRouter, Depends, Query
from sqlalchemy.orm import Session

from app.models.database import get_db, Session as DBSession
from app.services.file_service import FileService

router = APIRouter()


@router.post("/create")
async def create_session(
    name: str = Query(..., description="Session name"),
    db: Session = Depends(get_db)
):
    """Создать новую сессию"""
    # Генерировать уникальный токен
    token = secrets.token_hex(16)

    # Проверить уникальность токена
    while db.query(DBSession).filter(DBSession.token == token).first():
        token = secrets.token_hex(16)

    session = DBSession(name=name, token=token)
    db.add(session)
    db.commit()
    db.refresh(session)

    # Создать корневую директорию
    FileService.ensure_root_exists(db, session)

    return {
        "success": True,
        "token": token,
        "session_id": session.id,
        "root_ino": FileService.ROOT_INO
    }


@router.get("/list")
async def list_sessions(db: Session = Depends(get_db)):
    """Получить список всех сессий"""
    sessions = db.query(DBSession).all()

    result = []
    for session in sessions:
        result.append({
            "id": session.id,
            "name": session.name,
            "token": session.token
        })

    return {"sessions": result}


@router.delete("/delete")
async def delete_session(
    token: str = Query(..., description="Session token"),
    db: Session = Depends(get_db)
):
    """Удалить сессию и все ее файлы"""
    session = FileService.get_session_by_token(db, token)

    # Удалить все файлы сессии
    db.query(DBSession).filter(DBSession.id == session.id).delete()

    db.commit()

    return {"success": True}


@router.get("/info")
async def get_session_info(
    token: str = Query(..., description="Session token"),
    db: Session = Depends(get_db)
):
    """Получить информацию о сессии"""
    session = FileService.get_session_by_token(db, token)

    file_count = db.query(DBSession).filter(DBSession.id == session.id).count()

    return {
        "id": session.id,
        "name": session.name,
        "token": session.token,
        "file_count": file_count,
        "root_ino": FileService.ROOT_INO
    }
