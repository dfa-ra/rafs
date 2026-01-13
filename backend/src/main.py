#!/usr/bin/env python3
"""
Script to run the RAFS backend server
"""
import uvicorn


if __name__ == "__main__":
    uvicorn.run(
        "app.app:app",
        host="0.0.0.0",
        port=8080,
        reload=True,
        log_level="info"
    )
