from hm10_esp32 import HM10ESP32Bridge
from Mapping import get_path_and_commands, get_explore_map_commands, get_next_explore_commands
import time
import sys
import threading
import os
import re
from linktoserver import ScoreboardServer

PORT = '/dev/tty.usbserial-110'
EXPECTED_NAME = 'AlexCarCar'
MAP_CSV_PATH = "../map/medium_maze.csv" 
PATH = None 

# Scoreboard configuration (optional)
TEAM_NAME = "TheBestTeam"
SERVER_URL = "http://140.112.175.18"
# Global scoreboard instance (may be None if connection fails)
scoreboard = None

# Global remaining time
remaining_time = 0

# Lock to prevent background prints from interrupting manual input
input_lock = threading.Lock()

def background_listener(bridge):
    global remaining_time
    while True:
        msg = bridge.listen()
        if msg:
            # Only print if the main thread isn't busy asking for input
            with input_lock:
                print(f"\n[ESP32]: {msg}")
                # If message contains an RFID payload, extract and send to scoreboard
                # Example expected format in msg: "RFID:AA:BB:CC:DD"
                m = re.search(r"RFID:([0-9A-Fa-f:]+)", msg)
                if m:
                    raw_rfid = m.group(1)
                    clean_rfid = raw_rfid.replace(":", "").upper()
                    if scoreboard:
                        try:
                            score, time_left = scoreboard.add_UID(clean_rfid)
                            remaining_time = time_left
                            current_total = scoreboard.get_current_score()
                            print(f"[Scoreboard] Sent UID {clean_rfid} -> awarded: {score}, total: {current_total}, time left: {time_left}")
                        except Exception as e:
                            print(f"[Scoreboard] Error sending UID {clean_rfid}: {e}")
                    else:
                        print(f"[Scoreboard] Detected UID {clean_rfid} but no scoreboard connected.")
                print("> ", end="", flush=True)
        time.sleep(0.1)

def main():
    global PATH
    
    bridge = HM10ESP32Bridge(port=PORT)
    
    # 1. Configuration Check
    current_name = bridge.get_hm10_name()
    if current_name != EXPECTED_NAME:
        print(f"Target mismatch. Current: {current_name}, Expected: {EXPECTED_NAME}")
        print(f"Updating target name to {EXPECTED_NAME}...")
        
        if bridge.set_hm10_name(EXPECTED_NAME):
            print("✅ Name updated successfully. Resetting ESP32...")
            bridge.reset()
            bridge = HM10ESP32Bridge(port=PORT)
        else:
            print("❌ Failed to set name. Exiting.")
            sys.exit(1)

    # 2. Connection Check
    status = bridge.get_status()
    if status != "CONNECTED":
        print(f"⚠️ ESP32 is {status}. Please ensure HM-10 is advertising. Exiting.")
        sys.exit(0)

    print(f"✨ Ready! Connected to {EXPECTED_NAME}")
    
    # 3. Initial Path Setup
    print("📍 Fetching initial path from map...")
    start_node = 1  
    end_node = 12   
    
    path_nodes, PATH = get_path_and_commands(MAP_CSV_PATH, start_node, end_node)
    if PATH:
        print(f"✅ Path fetched successfully: {PATH}")
    else:
        print("⚠️ Could not fetch path. Using default.")
        PATH = "FUF"
    
    print("⏳ Waiting for client to be ready...")
    time.sleep(0.5)
    
    print("✅ Handshake complete! Ready to calibrate IR sensors.")
    print("--- Instructions ---")
    print(" 'w' : Start WHITE calibration")
    print(" 'b' : Start BLACK calibration")
    print(" 'c' : Enter NEW nodes and send path")
    print(" 's' : Explore full map from a node")
    print(" 'exit' : Quit")
    print("--------------------")
    
    # Start the listener thread
    threading.Thread(target=background_listener, args=(bridge,), daemon=True).start()

    try:
        while True:
            user_msg = input("> ").strip().lower()
            
            if user_msg in ['exit', 'quit']: 
                break
            
            elif user_msg == 'w':
                print("📤 Starting WHITE calibration...")
                bridge.send("CALIB_WHITE\n")
                
            elif user_msg == 'b':
                print("📤 Starting BLACK calibration...")
                bridge.send("CALIB_BLACK\n")
                
            elif user_msg == 'c':
                      
                # Use the lock so the background thread doesn't print while we type
                with input_lock:
                    try:
                        print("\n--- Node Selection ---")
                        s_input = input("Enter start node: ").strip()
                        e_input = input("Enter end node: ").strip()
                        
                        start_node = int(s_input)
                        end_node = int(e_input)
                        
                        path_nodes, PATH = get_path_and_commands(MAP_CSV_PATH, start_node, end_node)
                        
                        if PATH:
                            
                            print(f"📤 Sending path: {PATH}")
                            bridge.send("SEND_PATH\n")
                            time.sleep(1.0) # Give ESP32 time to prep
                            for i in range(0, len(PATH), 5):
                                chunk = PATH[i:i+5]
                                bridge.send(chunk + "\n")
                                # Wait for NEXT
                                next_received = False
                                timeout = time.time() + 10  # 10 second timeout
                                while not next_received and time.time() < timeout:
                                    msg = bridge.listen()
                                    if msg and "NEXT" in msg:
                                        next_received = True
                                    time.sleep(0.1)
                                if not next_received:
                                    print("Timeout waiting for NEXT")
                                    break
                            # Send END
                            bridge.send("END\n")
                            print("✅ Path sent! Listening for data...")
                        else:
                            print("❌ Error: Pathfinding returned empty.")
                            
                    except ValueError:
                        print("❌ Error: Please enter valid integer numbers for nodes.")

            elif user_msg == 's':
                # Full map exploration: ask for start node then compute exploration route
                with input_lock:
                    try:
                        print("\n--- Full Map Exploration ---")
                        s_input = input("Enter start node (blank for default 1): ").strip()
                        if s_input == "":
                            explore_start = 1
                        else:
                            explore_start = int(s_input)

                        path_nodes, PATH = get_explore_map_commands(MAP_CSV_PATH, explore_start)

                        if PATH:
                            global scoreboard
    
                            # Try to connect to scoreboard server (optional) with a few retries
                            attempts = 3
                            for attempt in range(1, attempts + 1):
                                try:
                                    print(f"🌐 Connecting to scoreboard server... (attempt {attempt}/{attempts})")
                                    scoreboard = ScoreboardServer(teamname=TEAM_NAME, host=SERVER_URL, debug=False)
                                    print(f"✅ Connected to scoreboard as {TEAM_NAME}")
                                    break
                                except Exception as e:
                                    print(f"⚠️ Scoreboard connect attempt {attempt} failed: {e}")
                                    time.sleep(1)
                            else:
                                print("⚠️ Could not connect to scoreboard after retries. Continuing without scoreboard.")
                    
                            # Dynamic exploration
                            current_node = explore_start
                            remaining_time = 70  # initial estimate
                            current_heading = None  # initial, assume correct
                            print(f"📤 Starting dynamic exploration from node {current_node} with {remaining_time}s")
                            bridge.send("SEND_PATH\n")
                            time.sleep(1.0)
                            
                            while True:
                                path_segment, chunk, ending_heading = get_next_explore_commands(MAP_CSV_PATH, current_node, remaining_time, 5, current_heading)
                                if not chunk:
                                    bridge.send("END\n")
                                    print("No more commands to send")
                                    break
                                
                                print(f"📤 Sending chunk: {chunk} from node {current_node}, heading {current_heading}")
                                bridge.send(chunk + "\n")
                                
                                # Wait for NEXT
                                next_received = False
                                timeout = time.time() + 10
                                while not next_received and time.time() < timeout:
                                    msg = bridge.listen()
                                    if msg and "NEXT" in msg:
                                        next_received = True
                                    time.sleep(0.1)
                                if not next_received:
                                    print("Timeout waiting for NEXT")
                                    break
                                
                                # Update current_node and current_heading
                                current_node = path_segment[-1]
                                current_heading = ending_heading
                                # Update remaining_time from global
                                remaining_time = remaining_time
                                
                                # Check if time is too low
                                if remaining_time < 10:
                                    print(f"Time running out ({remaining_time}s), stopping exploration")
                                    bridge.send("END\n")
                                    break
                            
                            print("✅ Dynamic exploration completed!")
                        else:
                            print("❌ Error: Exploration start failed.")
                    except ValueError:
                        print("❌ Error: Please enter a valid integer for start node.")

            elif user_msg: 
                bridge.send(user_msg + "\n")

    except KeyboardInterrupt:
        pass
    
    print("\nChat closed.")

if __name__ == "__main__":
    main()