from hm10_esp32 import HM10ESP32Bridge
from Mapping import get_path_and_commands, get_explore_map_commands
import time
import sys
import threading
import os

PORT = '/dev/tty.usbserial-110'
EXPECTED_NAME = 'AlexCarCar'
MAP_CSV_PATH = "../map/medium_maze.csv" 
PATH = None 

# Lock to prevent background prints from interrupting manual input
input_lock = threading.Lock()

def background_listener(bridge):
    while True:
        msg = bridge.listen()
        if msg:
            # Only print if the main thread isn't busy asking for input
            with input_lock:
                print(f"\n[ESP32]: {msg}")
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
                            bridge.send(PATH + "\n")
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
                            # PATH is the LRFU command string from Mapping
                            print(f"📤 Sending exploration path: {PATH}")
                            bridge.send("SEND_PATH\n")
                            time.sleep(1.0)
                            bridge.send(PATH + "\n")
                            print("✅ Exploration path sent! Listening for data...")
                        else:
                            print("❌ Error: Exploration pathfinding returned empty.")
                    except ValueError:
                        print("❌ Error: Please enter a valid integer for start node.")

            elif user_msg: 
                bridge.send(user_msg + "\n")

    except KeyboardInterrupt:
        pass
    
    print("\nChat closed.")

if __name__ == "__main__":
    main()