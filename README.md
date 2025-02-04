# websocket-in-cpp-with-electron-computer-app-client

I told take care of it so I did that instead of waiting a person to do.

This project has a authentication SQLite table which is that:

``` c++
const char* sql =
    "CREATE TABLE IF NOT EXISTS users ("
    "username TEXT PRIMARY KEY,"
    "password TEXT NOT NULL,"
    "token TEXT,"
    "token_expiry DATETIME);"
    "CREATE INDEX IF NOT EXISTS idx_token ON users(token);"
    "CREATE INDEX IF NOT EXISTS idx_username ON users(username);";
```

This structure is designed specifically for user authentication and is not intended for logging purposes.

## Client-Side Implementation

The client-side is kept simple and serves the basic purpose of establishing a reliable WebSocket connection and handling user authentication.

![front page look](./pictures/Saygılar_orkun_bey.jpg)

maybe I could add rooms on the code, but I guess it was enough to do for now.
