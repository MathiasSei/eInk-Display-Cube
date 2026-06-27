import os
from os.path import exists
from typing import Any

# Declare SCons globals for IDE/linter static analysis
# At runtime, PlatformIO/SCons injects Import and env.
if "Import" not in globals():
    def Import(*args, **kwargs):
        pass
    env: Any = None

Import("env")

# Default values
ssid = os.environ.get("WIFI_SSID", "")
password = os.environ.get("WIFI_PASS", "")
aws_key = os.environ.get("AWS_API_KEY", "")
fw_version = os.environ.get("FIRMWARE_VERSION", "v0.0.0-local")

# Try reading from secret_flags.ini if it exists
if exists("secret_flags.ini"):
    try:
        import configparser
        config = configparser.ConfigParser()
        config.read("secret_flags.ini")
        if "secrets" in config:
            ssid = config["secrets"].get("local_ssid", ssid)
            password = config["secrets"].get("local_pass", password)
            aws_key = config["secrets"].get("local_aws", aws_key)
            print("[INFO] Secrets loaded from secret_flags.ini successfully.")
    except Exception as e:
        print(f"[WARNING] Error reading secret_flags.ini: {e}")
else:
    print("[INFO] secret_flags.ini not found, using environment variables.")

# Strip quotes if they were added in the ini file or env
ssid = ssid.strip('"')
password = password.strip('"')
aws_key = aws_key.strip('"')
fw_version = fw_version.strip('"')

# Inject build flags
env.Append(CPPDEFINES=[
    ("WIFI_SSID", env.StringifyMacro(ssid)),
    ("WIFI_PASS", env.StringifyMacro(password)),
    ("AWS_API_KEY", env.StringifyMacro(aws_key)),
    ("FIRMWARE_VERSION", env.StringifyMacro(fw_version)),
    ("ARDUINO_USB_MODE", "1"),
    ("ARDUINO_USB_CDC_ON_BOOT", "1")
])
