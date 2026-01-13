from sqlalchemy import create_engine, Column, Integer, String, LargeBinary, BigInteger, ForeignKey
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, relationship

SQLALCHEMY_DATABASE_URL = "sqlite:///./rafs.db"

engine = create_engine(
    SQLALCHEMY_DATABASE_URL, connect_args={"check_same_thread": False}
)
SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

Base = declarative_base()


class Session(Base):
    __tablename__ = "sessions"

    id = Column(Integer, primary_key=True, index=True)
    token = Column(String, unique=True, index=True, nullable=False)
    name = Column(String, nullable=False)

    files = relationship("File", back_populates="session")


class File(Base):
    __tablename__ = "files"

    id = Column(Integer, primary_key=True, index=True)
    session_id = Column(Integer, ForeignKey("sessions.id"), nullable=False)

    name = Column(String(256), nullable=False)
    ino = Column(BigInteger, nullable=False, index=True)
    parent_ino = Column(BigInteger, nullable=False, index=True)
    mode = Column(Integer, nullable=False)
    data = Column(LargeBinary, nullable=True)
    size = Column(BigInteger, default=0)
    capacity = Column(BigInteger, default=0)
    link_count = Column(Integer, default=1)

    session = relationship("Session", back_populates="files")

    __table_args__ = (
        {"sqlite_autoincrement": True},
    )


def get_db():
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def create_tables():
    Base.metadata.create_all(bind=engine)
