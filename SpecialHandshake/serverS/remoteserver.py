# Simple test client for the scoreboard server
# Usage: python remoteserver.py [server_url] [team_name]

import sys
import time

from linktoserver import ScoreboardServer

DEFAULT_SERVER = "http://140.112.175.18"
DEFAULT_TEAM = "TestTeam"


def main():
    server_url = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SERVER
    team_name = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_TEAM

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


if __name__ == '__main__':
    main()
