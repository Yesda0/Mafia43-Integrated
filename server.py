import asyncio, json, uuid
from collections import defaultdict, Counter
import random

MAX_ROOM_CAP = 8
# rid -> {title,max,state,players:set(uid),host,roles,alive:set(uid),night:dict,day_votes:dict,player_numbers:dict,next_player_num:int,night_submitted:set}
ROOMS = {}
# uid -> {name,writer,room}
CLIENTS = {}

def js(obj):
    return (json.dumps(obj, ensure_ascii=False) + "\n").encode()

async def send(uid, obj):
    c = CLIENTS.get(uid)
    if not c:
        return
    w = c["writer"]
    try:
        w.write(js(obj))
        await w.drain()
    except Exception:
        pass

async def bcast_room(rid, obj, only_alive=False):
    r = ROOMS.get(rid)
    if not r:
        return
    for uid in list(r["players"]):
        if only_alive and uid not in r["alive"]:
            continue
        await send(uid, obj)

def mk_room(title, maxp, host_uid):
    rid = "R" + uuid.uuid4().hex[:6].upper()
    cap = min(int(maxp or MAX_ROOM_CAP), MAX_ROOM_CAP)
    ROOMS[rid] = {
        "title": title[:30],
        "max": cap,
        "state": "LOBBY",
        "players": set([host_uid]),
        "host": host_uid,
        "roles": {},
        "alive": set([host_uid]),
        "night": {"mafia": None, "doctor": None, "cop": None},
        "day_votes": {},
        "player_numbers": {host_uid: 1},
        "next_player_num": 2,
        "night_submitted": set(),  # 밤 액션을 제출한 플레이어들
        "day_submitted": set(),    # 낮 투표를 제출한 플레이어들
    }
    CLIENTS[host_uid]["room"] = rid
    return rid

def room_list_payload():
    return {
        "op": "ROOM_LIST",
        "rooms": [
            {
                "id": rid,
                "title": r["title"],
                "cur": len(r["players"]),
                "max": r["max"],
                "state": r["state"],
            }
            for rid, r in ROOMS.items()
        ],
    }

async def push_room_state(rid):
    r = ROOMS.get(rid)
    if not r:
        return
    players = []
    for uid in r["players"]:
        c = CLIENTS.get(uid, {})
        players.append(
            {
                "uid": uid,
                "number": r["player_numbers"].get(uid, 0),
                "name": c.get("name", f"Guest_{uid}"),
                "alive": uid in r["alive"],
                "is_host": uid == r["host"],
            }
        )
    await bcast_room(
        rid,
        {
            "op": "ROOM_STATE",
            "room_id": rid,
            "state": r["state"],
            "players": players,
            "host": r["host"],
        },
    )

def assign_roles(r):
    players = list(r["players"])
    n = len(players)
    if n < 2:
        return False

    roles = {p: "CITIZEN" for p in players}

    mafia_count = 2 if n >= 7 else 1
    mafia_uids = set(random.sample(players, mafia_count))
    for u in mafia_uids:
        roles[u] = "MAFIA"

    remaining = [u for u in players if u not in mafia_uids]
    if n >= 5:
        cop_uid = random.choice(remaining)
        roles[cop_uid] = "COP"
        remaining.remove(cop_uid)
    if n >= 6 and remaining:
        doc_uid = random.choice(remaining)
        roles[doc_uid] = "DOCTOR"

    r["roles"] = roles
    r["alive"] = set(players)
    r["night"] = {"mafia": None, "doctor": None, "cop": None}
    r["day_votes"] = {}
    r["night_submitted"] = set()  # 초기화
    r["day_submitted"] = set()    # 초기화
    return True

async def push_roles_dm(rid):
    r = ROOMS[rid]
    for uid, role in r["roles"].items():
        player_num = r["player_numbers"].get(uid, 0)
        await send(uid, {
            "op": "ROLE",
            "role": role,
            "player_number": player_num
        })

def night_resolve(rid):
    r = ROOMS[rid]
    maf_t = r["night"].get("mafia")
    doc_t = r["night"].get("doctor")
    cop_t = r["night"].get("cop")
    saved = False
    victim = None
    cop_info = None

    if maf_t and maf_t in r["alive"]:
        victim = maf_t
        if doc_t == victim:
            saved = True
        else:
            r["alive"].discard(victim)

    if cop_t and cop_t in r["roles"]:
        cop_info = {
            "target": cop_t,
            "target_number": r["player_numbers"].get(cop_t, 0),
            "is_mafia": (r["roles"][cop_t] == "MAFIA"),
        }

    r["night"] = {"mafia": None, "doctor": None, "cop": None}
    r["night_submitted"] = set()  # 리셋
    return victim, saved, cop_info

def check_win(rid):
    r = ROOMS[rid]
    maf = sum(1 for u in r["alive"] if r["roles"].get(u) == "MAFIA")
    cit = sum(1 for u in r["alive"] if r["roles"].get(u) != "MAFIA")
    if maf == 0:
        return "CITIZEN"
    if maf >= cit:
        return "MAFIA"
    return None

async def try_resolve_night(rid):
    """모든 살아있는 플레이어가 밤 액션을 보냈으면 자동으로 낮으로 전환"""
    r = ROOMS.get(rid)
    if not r or r["state"] != "NIGHT":
        return False

    # 모든 살아있는 플레이어가 액션을 보냈는지 확인
    if r["night_submitted"] >= r["alive"]:
        print(f"[Room {rid}] All players submitted night actions, resolving...")

        victim, saved, cop_info = night_resolve(rid)

        # 경찰에 조사 DM
        for p, role in r["roles"].items():
            if role == "COP":
                if cop_info:
                    await send(p, {"op": "COP_RESULT", **cop_info})

        # 밤 결과 방송
        victim_num = r["player_numbers"].get(victim, 0) if victim else None
        await bcast_room(
            rid,
            {
                "op": "NIGHT_RESULT",
                "victim": victim,
                "victim_number": victim_num,
                "saved": saved,
            },
        )

        winner = check_win(rid)
        if winner:
            r["state"] = "END"
            await bcast_room(rid, {"op": "GAME_END", "winners": winner})
        else:
            r["state"] = "DAY"
            r["day_votes"] = {}
            r["day_submitted"] = set()
            await bcast_room(rid, {"op": "PHASE", "phase": "DAY"})

        await push_room_state(rid)
        return True

    return False

async def try_resolve_day(rid):
    """모든 살아있는 플레이어가 낮 투표를 보냈으면 자동으로 밤으로 전환"""
    r = ROOMS.get(rid)
    if not r or r["state"] != "DAY":
        return False

    # 모든 살아있는 플레이어가 투표했는지 확인
    if r["day_submitted"] >= r["alive"]:
        print(f"[Room {rid}] All players submitted day votes, resolving...")

        day_votes = r.get("day_votes", {})
        victim = None

        if day_votes:
            cnt = Counter(day_votes.values())
            most_common = cnt.most_common()
            if most_common:
                top_target, top_count = most_common[0]
                ties = [t for t, c in cnt.items() if c == top_count]
                if len(ties) == 1 and top_target in r["alive"]:
                    victim = top_target
                    r["alive"].discard(victim)

        victim_num = r["player_numbers"].get(victim, 0) if victim else None
        await bcast_room(
            rid,
            {
                "op": "DAY_RESULT",
                "victim": victim,
                "victim_number": victim_num,
            },
        )

        winner = check_win(rid)
        if winner:
            r["state"] = "END"
            await bcast_room(rid, {"op": "GAME_END", "winners": winner})
        else:
            r["state"] = "NIGHT"
            r["night"] = {"mafia": None, "doctor": None, "cop": None}
            r["day_votes"] = {}
            r["night_submitted"] = set()
            await bcast_room(rid, {"op": "PHASE", "phase": "NIGHT"})

        await push_room_state(rid)
        return True

    return False

async def handle(reader, writer):
    uid = uuid.uuid4().hex[:6].upper()
    CLIENTS[uid] = {"name": f"Guest_{uid}", "writer": writer, "room": None}
    await send(uid, {"op": "HELLO", "ver": "1.0", "uid": uid})
    await send(uid, room_list_payload())

    try:
        while not reader.at_eof():
            raw = await reader.readline()
            if not raw:
                break
            try:
                msg = json.loads(raw.decode().strip() or "{}")
                print(f"[{uid}] Parsed: {msg}")
            except Exception as e:
                print(f"[{uid}] JSON Error: {e}")
                await send(uid, {"op": "ERROR", "code": "BAD_JSON"})
                continue

            op = msg.get("op", "")
            print(f"[{uid}] Operation: '{op}'")

            me = CLIENTS.get(uid)
            if not me:
                break

            if op == "LOGIN":
                name = msg.get("name", "").strip()
                if name:
                    me["name"] = name[:20]
                await send(uid, {"op": "ACK", "of": "LOGIN"})

            elif op == "LIST_ROOMS":
                await send(uid, room_list_payload())

            elif op == "CREATE_ROOM":
                if me["room"]:
                    await send(uid, {"op": "ERROR", "code": "ALREADY_IN_ROOM"})
                    continue
                rid = mk_room(
                    msg.get("title", "Room"),
                    msg.get("max", MAX_ROOM_CAP),
                    uid,
                )
                await send(uid, {"op": "PLAYER_NUMBER", "number": 1})
                await send(uid, room_list_payload())
                await push_room_state(rid)

            elif op == "JOIN_ROOM":
                rid = msg.get("room_id")
                r = ROOMS.get(rid)
                if not r:
                    await send(uid, {"op": "ERROR", "code": "NO_SUCH_ROOM"})
                    continue
                if me["room"]:
                    await send(uid, {"op": "ERROR", "code": "ALREADY_IN_ROOM"})
                    continue
                if r["state"] != "LOBBY":
                    await send(uid, {"op": "ERROR", "code": "NOT_LOBBY"})
                    continue
                if len(r["players"]) >= r["max"]:
                    await send(uid, {"op": "ERROR", "code": "ROOM_FULL"})
                    continue

                r["players"].add(uid)
                r["alive"].add(uid)

                player_num = r["next_player_num"]
                r["player_numbers"][uid] = player_num
                r["next_player_num"] += 1

                me["room"] = rid

                await send(uid, {"op": "PLAYER_NUMBER", "number": player_num})
                await send(uid, room_list_payload())
                await push_room_state(rid)

            elif op == "LEAVE_ROOM":
                rid = me["room"]
                if rid and rid in ROOMS:
                    r = ROOMS[rid]
                    r["players"].discard(uid)
                    r["alive"].discard(uid)
                    r.get("day_votes", {}).pop(uid, None)
                    r.get("night_submitted", set()).discard(uid)
                    r.get("day_submitted", set()).discard(uid)
                    r["player_numbers"].pop(uid, None)

                    me["room"] = None
                    if uid == r["host"]:
                        if r["players"]:
                            r["host"] = next(iter(r["players"]))
                            await push_room_state(rid)
                        else:
                            del ROOMS[rid]
                    else:
                        await push_room_state(rid)
                await send(uid, room_list_payload())

            elif op == "START":
                print(f"[{uid}] -> START")
                rid = me["room"]
                if not rid:
                    await send(uid, {"op": "ERROR", "code": "NOT_IN_ROOM"})
                    continue
                r = ROOMS[rid]

                if uid != r["host"]:
                    await send(uid, {"op": "ERROR", "code": "NOT_HOST"})
                    continue
                if r["state"] != "LOBBY":
                    await send(uid, {"op": "ERROR", "code": "NOT_LOBBY_START"})
                    continue

                if not assign_roles(r):
                    await send(uid, {"op": "ERROR", "code": "NEED_4P"})
                    continue

                await push_roles_dm(rid)
                r["state"] = "NIGHT"
                r["night_submitted"] = set()  # 밤 시작 시 초기화
                await bcast_room(rid, {"op": "PHASE", "phase": "NIGHT"})
                await push_room_state(rid)

            elif op == "NEXT_PHASE":
                # 방장의 수동 전환 (백업용으로 유지)
                rid = me["room"]
                if not rid:
                    continue
                r = ROOMS[rid]
                if uid != r["host"]:
                    await send(uid, {"op": "ERROR", "code": "NOT_HOST"})
                    continue

                if r["state"] == "NIGHT":
                    victim, saved, cop_info = night_resolve(rid)

                    for p, role in r["roles"].items():
                        if role == "COP":
                            if cop_info:
                                await send(p, {"op": "COP_RESULT", **cop_info})

                    victim_num = r["player_numbers"].get(victim, 0) if victim else None
                    await bcast_room(
                        rid,
                        {
                            "op": "NIGHT_RESULT",
                            "victim": victim,
                            "victim_number": victim_num,
                            "saved": saved,
                        },
                    )

                    winner = check_win(rid)
                    if winner:
                        r["state"] = "END"
                        await bcast_room(rid, {"op": "GAME_END", "winners": winner})
                    else:
                        r["state"] = "DAY"
                        r["day_votes"] = {}
                        r["day_submitted"] = set()
                        await bcast_room(rid, {"op": "PHASE", "phase": "DAY"})
                    await push_room_state(rid)

                elif r["state"] == "DAY":
                    day_votes = r.get("day_votes", {})
                    victim = None

                    if day_votes:
                        cnt = Counter(day_votes.values())
                        most_common = cnt.most_common()
                        if most_common:
                            top_target, top_count = most_common[0]
                            ties = [t for t, c in cnt.items() if c == top_count]
                            if len(ties) == 1 and top_target in r["alive"]:
                                victim = top_target
                                r["alive"].discard(victim)

                    victim_num = r["player_numbers"].get(victim, 0) if victim else None
                    await bcast_room(
                        rid,
                        {
                            "op": "DAY_RESULT",
                            "victim": victim,
                            "victim_number": victim_num,
                        },
                    )

                    winner = check_win(rid)
                    if winner:
                        r["state"] = "END"
                        await bcast_room(rid, {"op": "GAME_END", "winners": winner})
                    else:
                        r["state"] = "NIGHT"
                        r["night"] = {"mafia": None, "doctor": None, "cop": None}
                        r["day_votes"] = {}
                        r["night_submitted"] = set()
                        await bcast_room(rid, {"op": "PHASE", "phase": "NIGHT"})
                    await push_room_state(rid)

            elif op == "DAY_CHAT":
                rid = me["room"]
                if not rid:
                    continue
                r = ROOMS[rid]
                if r["state"] != "DAY":
                    await send(uid, {"op": "ERROR", "code": "NOT_DAY"})
                    continue
                if uid not in r["alive"]:
                    await send(uid, {"op": "ERROR", "code": "DEAD"})
                    continue

                player_num = r["player_numbers"].get(uid, 0)
                await bcast_room(
                    rid,
                    {
                        "op": "CHAT",
                        "from": me["name"],
                        "from_number": player_num,
                        "text": msg.get("text", ""),
                    },
                    only_alive=True,
                )

            elif op == "NIGHT_CHAT":
                rid = me["room"]
                if not rid:
                    continue
                r = ROOMS[rid]

                if r["state"] != "NIGHT":
                    await send(uid, {"op": "ERROR", "code": "NOT_NIGHT"})
                    continue
                if uid not in r["alive"]:
                    await send(uid, {"op": "ERROR", "code": "DEAD"})
                    continue

                role = r["roles"].get(uid)
                if role != "MAFIA":
                    await send(uid, {"op": "ERROR", "code": "NOT_MAFIA"})
                    continue

                player_num = r["player_numbers"].get(uid, 0)
                chat_msg = {
                    "op": "MAFIA_CHAT",
                    "from": me["name"],
                    "from_number": player_num,
                    "text": msg.get("text", ""),
                }

                for target_uid in r["alive"]:
                    target_role = r["roles"].get(target_uid)
                    if target_role == "MAFIA":
                        await send(target_uid, chat_msg)

            elif op == "DAY_VOTE":
                rid = me["room"]
                if not rid:
                    continue
                r = ROOMS[rid]

                if r["state"] != "DAY":
                    await send(uid, {"op": "ERROR", "code": "NOT_DAY"})
                    continue
                if uid not in r["alive"]:
                    await send(uid, {"op": "ERROR", "code": "DEAD"})
                    continue

                target = msg.get("target")

                # SKIP 투표 처리 (기권)
                if target == "SKIP" or target == "" or target is None:
                    r.setdefault("day_submitted", set())
                    r["day_submitted"].add(uid)  # 기권도 제출로 기록
                    await send(uid, {
                        "op": "ACK",
                        "of": "DAY_VOTE",
                        "target": "SKIP",
                        "target_number": None
                    })
                    await try_resolve_day(rid)
                    continue

                if target not in r["players"]:
                    await send(uid, {"op": "ERROR", "code": "BAD_TARGET"})
                    continue

                r.setdefault("day_votes", {})
                r["day_votes"][uid] = target
                r.setdefault("day_submitted", set())
                r["day_submitted"].add(uid)  # 투표 제출 기록

                target_num = r["player_numbers"].get(target, 0)
                await send(uid, {
                    "op": "ACK",
                    "of": "DAY_VOTE",
                    "target": target,
                    "target_number": target_num
                })

                # 모든 플레이어가 투표했으면 자동 전환
                await try_resolve_day(rid)

            elif op == "NIGHT_ACTION":
                rid = me["room"]
                if not rid:
                    continue
                r = ROOMS[rid]
                if r["state"] != "NIGHT":
                    await send(uid, {"op": "ERROR", "code": "NOT_NIGHT"})
                    continue
                if uid not in r["alive"]:
                    await send(uid, {"op": "ERROR", "code": "DEAD"})
                    continue

                role = r["roles"].get(uid)
                target = msg.get("target")

                # 타겟이 "NONE"이거나 빈 문자열이면 None으로 처리
                if target == "NONE" or target == "":
                    target = None

                if role == "MAFIA":
                    r["night"]["mafia"] = target
                elif role == "DOCTOR":
                    r["night"]["doctor"] = target
                elif role == "COP":
                    r["night"]["cop"] = target
                # 시민도 NONE 액션을 보낼 수 있음 (아무것도 안 함)

                # 액션 제출 기록
                r.setdefault("night_submitted", set())
                r["night_submitted"].add(uid)

                target_num = r["player_numbers"].get(target, 0) if target else None
                await send(uid, {
                    "op": "ACK",
                    "of": "NIGHT_ACTION",
                    "target_number": target_num
                })

                # 모든 플레이어가 액션을 보냈으면 자동 전환
                await try_resolve_night(rid)

            elif op == "PING":
                await send(uid, {"op": "PONG"})

            else:
                await send(
                    uid,
                    {
                        "op": "ERROR",
                        "code": "UNKNOWN_OP",
                        "msg": f"Unknown operation: {op}",
                    },
                )

    except Exception as e:
        print(f"[{uid}] Exception in handle: {e}")
    finally:
        rid = CLIENTS.get(uid, {}).get("room")
        CLIENTS.pop(uid, None)
        if rid and rid in ROOMS:
            r = ROOMS[rid]
            r["players"].discard(uid)
            r["alive"].discard(uid)
            r.get("day_votes", {}).pop(uid, None)
            r.get("night_submitted", set()).discard(uid)
            r.get("day_submitted", set()).discard(uid)
            r.get("player_numbers", {}).pop(uid, None)
            if uid == r["host"]:
                if r["players"]:
                    r["host"] = next(iter(r["players"]))
                    asyncio.create_task(push_room_state(rid))
                else:
                    del ROOMS[rid]
            else:
                asyncio.create_task(push_room_state(rid))
        try:
            writer.close()
            await writer.wait_closed()
        except Exception:
            pass

async def main():
    srv = await asyncio.start_server(handle, "0.0.0.0", 5566)
    print("server on :5566")
    async with srv:
        await srv.serve_forever()

if __name__ == "__main__":
    asyncio.run(main())
