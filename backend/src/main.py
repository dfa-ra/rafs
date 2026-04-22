


def main():
    import uvicorn
    uvicorn.run("app.app:app", port=8080, reload=True)


if __name__ == "__main__":
    main()
