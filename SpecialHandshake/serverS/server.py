from hm10_esp32 import HM10ESP32Bridge
from Mapping import get_path_and_commands, get_explore_map_commands, get_next_explore_commands
import time
import sys
import threading
import re
from linktoserver import ScoreboardServer

PORT = '/dev/tty.usbserial-110'
EXPECTED_NAME = 'AlexCarCar'
MAP_CSV_PATH = "../map/big_maze_114.csv" 
PATH = None 

# Scoreboard configuration
TEAM_NAME = "guaiguai"
SERVER_URL = "http://140.112.175.18"
scoreboard = None

# Global states
remaining_time = 0
input_lock = threading.Lock()

# Use an event to signal the main thread when "NEXT" is received
next_event = threading.Event()

def background_listener(bridge):
    global remaining_time
    while True:
        msg = bridge.listen()
        if msg:
            # 1. Check for NEXT command to unblock the main thread
            if "NEXT" in msg:
                next_event.set()

            # 2. Process RFID data
            m = re.search(r"RFID:([0-9A-Fa-f:]+)", msg)
            if m:
                clean_rfid = m.group(1).replace(":", "").upper()
                if scoreboard:
                    try:
                        score, time_left = scoreboard.add_UID(clean_rfid)
                        remaining_time = time_left
                        current_total = scoreboard.get_current_score()
                        print(f"\n[Scoreboard] UID {clean_rfid} -> Score: {score} | Total: {current_total} | Time Left: {time_left}s")
                    except Exception as e:
                        print(f"\n[Scoreboard] Error sending UID {clean_rfid}: {e}")
                else:
                    print(f"\n[Scoreboard] Detected UID {clean_rfid} but no scoreboard connected.")

            # Print general ESP32 messages. We don't use input_lock here 
            # to prevent deadlocking the background thread while the car is moving.
            print(f"\n[ESP32]: {msg.strip()}")
            
        time.sleep(0.05)

def main():
    global PATH
    global remaining_time 
    global scoreboard
    
    bridge = HM10ESP32Bridge(port=PORT)
    
    current_name = bridge.get_hm10_name()
    if current_name != EXPECTED_NAME:
        print(f"Target mismatch. Current: {current_name}, Expected: {EXPECTED_NAME}")
        if bridge.set_hm10_name(EXPECTED_NAME):
            bridge.reset()
            bridge = HM10ESP32Bridge(port=PORT)
        else:
            sys.exit(1)

    if bridge.get_status() != "CONNECTED":
        sys.exit(0)

    print(f"✨ Ready! Connected to {EXPECTED_NAME}")
    
    # Start the background listener FIRST so it doesn't miss early messages
    threading.Thread(target=background_listener, args=(bridge,), daemon=True).start()

    print("✅ Handshake complete! Ready to calibrate IR sensors.")
    print("--- Instructions ---")
    print(" 'w' : Start WHITE calibration")
    print(" 'b' : Start BLACK calibration")
    print(" 'c' : Enter NEW nodes and send path")
    print(" 's' : Explore full map from a node")
    print(" 'exit' : Quit")
    print("--------------------")

    try:
        while True:
            with input_lock:
                user_msg = input("\n> ").strip().lower()
            
            if user_msg in ['exit', 'quit']: 
                break
            
            elif user_msg == 'w':
                print("📤 Starting WHITE calibration...")
                bridge.send("CALIB_WHITE\n")
                
            elif user_msg == 'b':
                print("📤 Starting BLACK calibration...")
                bridge.send("CALIB_BLACK\n")
                
            elif user_msg == 'c':
                with input_lock:
                    print("\n--- Node Selection ---")
                    s_input = input("Enter start node: ").strip()
                    e_input = input("Enter end node: ").strip()
                
                # Out of the lock so background thread can process "NEXT" messages
                try:
                    start_node = int(s_input)
                    end_node = int(e_input)
                    path_nodes, PATH = get_path_and_commands(MAP_CSV_PATH, start_node, end_node)
                    
                    if PATH:
                        print(f"📤 Sending path: {PATH}")
                        bridge.send("SEND_PATH\n")
                        time.sleep(1.0)
                        
                        for i in range(0, len(PATH), 5):
                            chunk = PATH[i:i+5]
                            next_event.clear() # Reset the event before sending
                            bridge.send(chunk + "\n")
                            
                            # Wait for background thread to receive "NEXT"
                            if not next_event.wait(timeout=10):
                                print("⚠️ Timeout waiting for NEXT")
                                break
                                
                        bridge.send("END\n")
                        print("✅ Path sent! Listening for data...")
                    else:
                        print("❌ Error: Pathfinding returned empty.")
                except ValueError:
                    print("❌ Error: Please enter valid integer numbers.")

            elif user_msg == 's':
                with input_lock:
                    print("\n--- Full Map Exploration ---")
                    s_input = input("Enter start node (blank for default 1): ").strip()
                
                # Out of the lock so background thread can function
                try:
                    explore_start = 1 if s_input == "" else int(s_input)
                    path_nodes, PATH = get_explore_map_commands(MAP_CSV_PATH, explore_start)

                    if PATH:
                        if scoreboard is None:
                            attempts = 5
                            for attempt in range(1, attempts + 1):
                                try:
                                    print(f"🌐 Connecting to scoreboard... ({attempt}/{attempts})")
                                    scoreboard = ScoreboardServer(teamname=TEAM_NAME, host=SERVER_URL, debug=False)
                                    print(f"✅ Connected!")
                                    break
                                except Exception as e:
                                    time.sleep(1)

                        current_node = explore_start
                        remaining_time = 65
                        current_heading = None
                        visited_nodes = set([current_node])
                        
                        print(f"📤 Starting dynamic exploration from node {current_node}")
                        bridge.send("SEND_PATH\n")
                        time.sleep(1.0)
                        
                        while True:
                            path_segment, chunk, ending_heading = get_next_explore_commands(
                                csv_path=MAP_CSV_PATH, 
                                current_node=current_node, 
                                remaining_time=remaining_time, 
                                visited_nodes=visited_nodes, 
                                max_steps=3, 
                                current_heading=current_heading,
                                score_reference_node=explore_start
                            )
                            
                            if not chunk:
                                bridge.send("END\n")
                                print("✅ No more commands to send.")
                                break
                            
                            print(f"📤 Sending chunk: {chunk} (Node {current_node})")
                            
                            next_event.clear() # Reset event flag before sending
                            bridge.send(chunk + "\n")
                            
                            # Pause execution here until background thread hears "NEXT"
                            if not next_event.wait(timeout=10):
                                print("⚠️ Timeout waiting for NEXT")
                                break
                            
                            current_node = path_segment[-1]
                            current_heading = ending_heading
                            visited_nodes.update(path_segment)
                            
                        print("✅ Dynamic exploration completed!")
                    else:
                        print("❌ Error: Exploration start failed.")
                except ValueError:
                    print("❌ Error: Please enter a valid integer.")

            elif user_msg: 
                bridge.send(user_msg + "\n")

    except KeyboardInterrupt:
        pass
    
    print("\nChat closed.")

if __name__ == "__main__":
    main()