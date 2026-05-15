Import("env")
import os
import shutil
import re
import subprocess

def get_firmware_version():
    try:
        with open("include/config/version.h", "r") as f:
            content = f.read()
            match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
            if match:
                return match.group(1)
    except Exception as e:
        print(f"Could not read version.h: {e}")
    return "unknown"

def auto_sign_and_rename(source, target, env):
    print("=" * 60)
    print("Running Custom Post-Build Action: Sign and Rename")
    
    firmware_path = str(target[0])
    version = get_firmware_version()
    
    # We want the final file to be named flashCharge-vX.X.X.bin
    bin_dir = "bin"
    if not os.path.exists(bin_dir):
        os.makedirs(bin_dir)
    final_name = os.path.join(bin_dir, f"flashCharge-v{version}.bin")
    
    # Run the signing script
    key_path = "ota_private_key.pem"
    
    if not os.path.exists(key_path):
        print(f"⚠️ WARNING: {key_path} not found. Cannot sign firmware automatically.")
        print("Copying unsigned firmware instead...")
        shutil.copy(firmware_path, final_name)
    else:
        print(f"Signing firmware with {key_path}...")
        cmd = [
            "python",
            "scripts/sign_firmware.py",
            firmware_path,
            key_path,
            final_name
        ]
        
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            print(f"✅ Success! Signed firmware created: {final_name}")
        else:
            print("❌ Failed to sign firmware:")
            print(result.stderr)
            
    print("=" * 60)

# Register the post-action hook
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", auto_sign_and_rename)
