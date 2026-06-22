import subprocess
import sys
import re
import os

# PlatformIO Python and esptool paths (specific to Windows)
USER_HOME = os.path.expanduser("~")
PYTHON_EXE = os.path.join(USER_HOME, ".platformio", "penv", "Scripts", "python.exe")
ESPTOOL_PY = os.path.join(USER_HOME, ".platformio", "packages", "tool-esptoolpy", "esptool.py")

def get_charger_id(port="COM8"):
    print(f"Reading MAC address from {port}...")
    
    # Run the esptool read_mac command
    cmd = [PYTHON_EXE, ESPTOOL_PY, "--port", port, "read_mac"]
    
    try:
        # Run command and capture output
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        output = result.stdout
        
        # Look for the line containing "MAC: "
        for line in output.split('\n'):
            if line.startswith("MAC:"):
                # Extract just the mac address string (e.g., "e0:8c:fe:33:40:2c")
                raw_mac = line.replace("MAC:", "").strip()
                
                # Remove colons and convert to uppercase
                charger_id = raw_mac.replace(":", "").upper()
                
                print("\n" + "="*40)
                print(f"Raw MAC Address : {raw_mac}")
                print(f"Final Charger ID: {charger_id}")
                print("="*40 + "\n")
                
                return charger_id
                
        print("Error: Could not find MAC address in esptool output.")
        
    except subprocess.CalledProcessError as e:
        print(f"Error running esptool: {e}")
        print("Please check if the device is connected to the correct COM port.")

if __name__ == "__main__":
    # You can pass the COM port as an argument (e.g., python get_charger_id.py COM8)
    port = sys.argv[1] if len(sys.argv) > 1 else "COM8"
    get_charger_id(port)
