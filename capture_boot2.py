import serial
import time
import sys

def capture():
    try:
        ser = serial.Serial('COM8', 115200, timeout=1)
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.dtr = False
        ser.rts = False
        
        start_time = time.time()
        while time.time() - start_time < 20:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)
        ser.close()
    except Exception as e:
        print(f"Error: {e}")

if __name__ == '__main__':
    capture()
