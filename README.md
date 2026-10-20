# revelations-server

A replacement for Capcom's service for **Resident Evil Revelations (PS4)**,for playing online with shadPS4 and shadNet. It brings back the game's Raid Mode online features:
rewards, events and stat uploads. On top of them it adds leaderboards and tools for running the server.

## Features

- **Rewards.** Give players supplies, custom parts or weapons. Offer them to everyone or to chosen players, always, between two dates, or as a new gift every week.
- **Events.** Bonus EXP for killing chosen enemies, bonus EXP on a difficulty, Raid shop sales and named Wanted enemies. Run one now, or schedule them weekly or for set dates.
- **Leaderboards.** Built from the stats the game uploads after each Raid stage: fastest clears per stage, and players by level. Optionally on a public web page that also shows the running and upcoming events.
- **Player stats.** Every upload is kept and decoded: level, EXP, stage records, runs per character and Wanted enemies met.
- **shadNet sign-in.** Every player is a shadNet account.
- **Admin app.** `revelations-admin` manages a running server: players, rewards, events with ready-made presets, the schedule and the leaderboards.
- **Live config.** Edits to `config.json` apply without a restart, and a broken edit is rejected with a clear message.

## Requirements

- Qt 6.2 or later (Core, Network, Sql with SQLite,Widgets for the admin app)
- CMake 3.19 or later
- A running shadNet server

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Windows, add `-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64` to the first command.

## Run

```sh
./build/revelations-server
```

The server reads `config.json` from the folder it is started in. The first run creates one with default values and a new admin key. Set your shadNet address in it:

```json
"shadnet": { "url": "http://127.0.0.1:31315" }
```

The game connects to the server's address on port 8080 (`http://<server>:8080`).

## Admin app

```sh
./build/revelations-admin
```

Enter the admin address (`http://127.0.0.1:8081` by default) and the `admin.key` from`config.json`, then press **Connect**. The admin port only listens on the server's own computer.

## Leaderboard page

Add this to `config.json` to share the leaderboards and events with players:

```json
"leaderboards": { "public": true, "size": 10 }
```

Players then open `http://<server>:8080/leaderboards` in a browser.
