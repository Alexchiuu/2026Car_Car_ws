import abc
import csv
import logging
import re
import time
import threading
import sys
from typing import Optional, Tuple, cast

import requests
import socketio
from hm10_esp32 import HM10ESP32Bridge
from Mapping import get_path_and_commands, get_explore_map_commands

# --- Scoreboard Classes (Paste your provided Scoreboard code here) ---
log = logging.getLogger("scoreboard")

class Scoreboard(abc.ABC):
    @abc.abstractmethod
    def add_UID(self, UID_str: str) -> Tuple[int, float]: pass
    @abc.abstractmethod
    def get_current_score(self) -> Optional[int]: pass

class ScoreboardServer(Scoreboard):
    def __init__(self, teamname: str, host="http://140.112.175.18", debug=False):
        self.teamname = teamname
        self.ip = host
        self.socket = socketio.Client(logger=debug, engineio_logger=debug)
        self.socket.register_namespace(TeamNamespace("/team"))
        self.socket.connect(self.ip, socketio_path="scoreboard.io")
        self.sid = self.socket.get_sid(namespace="/team")
        self._start_game(self.teamname)

    def _start_game(self, teamname: str):
        payload = {"teamname": teamname}
        self.socket.call("start_game", payload, namespace="/team")

    def add_UID(self, UID_str: str) -> Tuple[int, float]:
        # Standardize format (remove colons if present, ensure uppercase)
        clean_uid = UID_str.replace(":", "").upper()
        res = self.socket.call("add_UID", clean_uid, namespace="/team")
        if not res: return 0, 0
        res = cast(dict, res)
        return res.get("score", 0), res.get("time_remaining", 0)

    def get_current_score(self) -> Optional[int]:
        try:
            res = requests.get(self.ip + "/current_score", params={"sid": self.sid})
            return res.json()["current_score"]
        except: return None

class TeamNamespace(socketio.ClientNamespace):
    def on_connect(self): pass
    def on_UID_added(self, message): log.info(f"Server Message: {message}")
    def on_disconnect(self): pass

# --- Main Logic ---

PORT = '/dev/tty.usbserial-110'
EXPECTED_NAME = 'AlexCarCar'
MAP_CSV_PATH = "../map/maze (3).csv"
TEAM_NAME = "TeamName2"
SERVER_URL = "http://140.112.175.18"
DEFAULT_REMOTE = "http://140.112.175.18"
DEFAULT_LOCAL = "http://localhost:5000"

# Global scoreboard instance
scoreboard = None

def background_listener(bridge):
    global scoreboard
    while True:
        msg = bridge.listen()
        if msg:
            print(f"ESP32: {msg}")
            
            # Detect RFID pattern (e.g., RFID:F5:C8:23:73)
            # Regex looks for "RFID:" followed by hex characters and colons
            match = re.search(r"RFID:([0-9A-Fa-f:]+)", msg)
            if match and scoreboard:
                raw_rfid = match.group(1)
                # Clean the RFID (Remove colons to match the 8-char hex requirement)
                clean_rfid = raw_rfid.replace(":", "")
                
                print(f"🎯 RFID Detected: {clean_rfid}. Sending to server...")
                try:
                    score, time_left = scoreboard.add_UID(clean_rfid)
                    current_total = scoreboard.get_current_score()
                    print(f"✅ Success! Points earned: {score} | Total Score: {current_total}")
                except Exception as e:
                    print(f"❌ Scoreboard Error: {e}")

        time.sleep(0.1)

def main():
    global PATH, scoreboard
    logging.basicConfig(level=logging.INFO)
    
    # 1. Initialize Scoreboard
    print("🌐 Connecting to Scoreboard Server...")
    try:
        scoreboard = ScoreboardServer(TEAM_NAME, SERVER_URL)
        print(f"✅ Connected to scoreboard as {TEAM_NAME}")
    except Exception as e:
        print(f"❌ Could not connect to scoreboard: {e}")
        sys.exit(1)

    # 2. Initialize HM10 Bridge
    bridge = HM10ESP32Bridge(port=PORT)
    
    # ... (Configuration Check Logic remains the same as your original) ...
    current_name = bridge.get_hm10_name()
    if current_name != EXPECTED_NAME:
        bridge.set_hm10_name(EXPECTED_NAME)
        bridge.reset()
        bridge = HM10ESP32Bridge(port=PORT)

    # 3. Connection Check
    status = bridge.get_status()
    if status != "CONNECTED":
        print(f"⚠️ ESP32 is {status}. Exiting.")
        sys.exit(0)

    # 4. Fetch path
    path_nodes, PATH = get_path_and_commands(MAP_CSV_PATH, 1, 20)
    
    # Start the listener thread
    threading.Thread(target=background_listener, args=(bridge,), daemon=True).start()

    print("✨ System Ready. Enter 'c' to send path, or 'w'/'b' for calibration.")

    try:
        while True:
            user_msg = input()
            if user_msg.lower() in ['exit', 'quit']: 
                break
            elif user_msg.lower() == 'w':
                bridge.send("CALIB_WHITE\n")
            elif user_msg.lower() == 'b':
                bridge.send("CALIB_BLACK\n")
            elif user_msg.lower() == 'c':
                print(f"📤 Sending path: {PATH}")
                bridge.send("SEND_PATH\n")
                time.sleep(1.0)
                bridge.send(PATH + "\n")
            elif user_msg: 
                bridge.send(user_msg + "\n")
    except KeyboardInterrupt:
        pass

def client_test(server_url: str = None, team_name: str = None):
    """Simple test client (merged from remoteserver.py).
    Connects to a remote scoreboard server and sends a test UID.
    """
    server_url = server_url or DEFAULT_REMOTE
    team_name = team_name or "TestTeam"

    print(f"Connecting to scoreboard server at {server_url} as '{team_name}'...")
    try:
        sb = ScoreboardServer(teamname=team_name, host=server_url, debug=False)
        print("Connected via Socket.IO. SID:", sb.sid)
    except Exception as e:
        print("Failed to connect to scoreboard server:", e)
        return

    # Small pause to ensure server-side session established
    time.sleep(0.5)

    # Test add_UID
    test_uid = "DEADBEEF"
    print(f"Sending test UID: {test_uid}")
    try:
        score, time_left = sb.add_UID(test_uid)
        print(f"Server returned -> score: {score}, time_remaining: {time_left}")
    except Exception as e:
        print("add_UID failed:", e)

    # Test REST current score endpoint (may return None if unavailable)
    cur = sb.get_current_score()
    print("Current score (via REST):", cur)

    print("Test completed. Disconnecting...")
    try:
        sb.socket.disconnect()
    except Exception:
        pass


if __name__ == "__main__":
    # Usage:
    #  python linktoserver.py server [local_scoreboard_url]
    #  python linktoserver.py client [remote_scoreboard_url] [team_name]
    # Default: server mode with local scoreboard at http://localhost:5000
    mode = sys.argv[1] if len(sys.argv) > 1 else "server"

    if mode == "client":
        server_url = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_REMOTE
        team_name = sys.argv[3] if len(sys.argv) > 3 else "TestTeam"
        client_test(server_url, team_name)
    else:
        # server mode (local). Allow overriding the scoreboard URL
        if len(sys.argv) > 1 and sys.argv[1] != "server":
            # if first arg is a URL directly
            SERVER_URL = sys.argv[1]
        else:
            SERVER_URL = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_LOCAL
        print(f"Running in local server mode. Using scoreboard at {SERVER_URL}")
        main()