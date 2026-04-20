from hm10_esp32 import HM10ESP32Bridge
from Mapping import get_path_and_commands, get_explore_map_commands
import time
import sys
import threading
import os

PORT = '/dev/tty.usbserial-110'
EXPECTED_NAME = 'AlexCarCar'
MAP_CSV_PATH = "../map/maze (3).csv"  # Path to the CSV file for pathfinding
PATH = None  # Will be fetched from Mapping.py

def background_listener(bridge):
    while True:
        msg = bridge.listen()
        if msg:
            print(f"{msg}")
            print("", end="", flush=True)
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
            # Re-init after reset
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
    
    # 3. Fetch path from Mapping.py
    print("📍 Fetching path from map...")
    start_node = 1  # Change these to your desired start/end nodes
    end_node = 20   # Change these to your desired start/end nodes
    
    path_nodes, PATH = get_path_and_commands(MAP_CSV_PATH, start_node, end_node)
    if PATH:
        print(f"✅ Path fetched successfully: {PATH}")
    else:
        print("⚠️ Could not fetch path. Using default.")
        PATH = "FUF"  # Fallback
    
    # Handshake: Wait for client ready signal
    print("⏳ Waiting for client to be ready...")
    time.sleep(0.5)
    
    print("✅ Handshake complete! Ready to calibrate IR sensors.")
    print("� IR Calibration Instructions:")
    print("   - Press 'w' to start WHITE calibration")
    print("   - Press 'b' to start BLACK calibration")
    print("   - Press 'c' to skip calibration and send path")
    print("   - Press 'exit' or 'quit' to quit")
    
    threading.Thread(target=background_listener, args=(bridge,), daemon=True).start()

    try:
        while True:
            user_msg = input()
            if user_msg.lower() in ['exit', 'quit']: 
                break
            elif user_msg.lower() == 'w':
                print("📤 Starting WHITE calibration...")
                bridge.send("CALIB_WHITE\n")
                time.sleep(0.5)
            elif user_msg.lower() == 'b':
                print("📤 Starting BLACK calibration...")
                bridge.send("CALIB_BLACK\n")
                time.sleep(0.5)
            elif user_msg.lower() == 'c':
                print(f"📤 Sending path: {PATH}")
                time.sleep(0.5)
                bridge.send("SEND_PATH\n")
                time.sleep(3.0)
                bridge.send(PATH + "\n")
                time.sleep(0.5)
                print("✅ Path sent! Car is executing. Listening for RFID and sensor data...")
                # Don't break - continue listening for messages
            elif user_msg: 
                bridge.send(user_msg + "\n")
    except KeyboardInterrupt:
        pass
    print("\nChat closed.")

if __name__ == "__main__":
    main()