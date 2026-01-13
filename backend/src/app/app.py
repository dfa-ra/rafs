from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from .models.database import create_tables
from .controllers import file_controller, session_controller

create_tables()

app = FastAPI(
    title="RAFS Backend API",
    description="""
    Файловая система RAFS.
    """,
    version="1.0.0",
    docs_url="/docs",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(
    file_controller.router,
    prefix="/api/file",
    tags=["File Operations"]
)

app.include_router(
    session_controller.router,
    prefix="/api/session",
    tags=["Session Management"]
)

@app.get("/")
async def root():
    return {
        "message": "RAFS Backend API",
        "version": "1.0.0",
        "docs": "/docs",
        "redoc": "/redoc",
    }

@app.get("/health")
async def health_check():
    return {"status": "healthy"}
