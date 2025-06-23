import os  # Import os for directory handling
import http.server  # Import http.server for handling HTTP requests
from http.server import HTTPServer, SimpleHTTPRequestHandler ## Import os for directory handling
from zeroconf import Zeroconf, ServiceInfo # Import Zeroconf for mDNS service discovery
import socket # Import socket for network operations
import sys # Import sys for command line arguments
import shutil
import socketserver # Import socketserver for creating a threaded HTTP server
#-------------------------------------------------------
# (0) Clear Terminal screen
#-------------------------------------------------------
def clear_screen():
    # For Windows
    if os.name == 'nt':
        os.system('cls')
    # For macOS and Linux
    else:
        os.system('clear')
# Do it at the beginning
clear_screen()
#-------------------------------------------------------
# (1) Get the project name from current folder
#-------------------------------------------------------
# .bin to be copied has this project name
project_name = os.path.basename(os.getcwd())
print()
print("Project name:", project_name)
print("-------------")
#-------------------------------------------------------
# (2) Prepare the needed OTA.bin file from latest build
#-------------------------------------------------------
DIRECTORY = "ota_update"                               # Directory to place the file for OTA-update 
# Remove existing OTA.bin if it exists
dst = DIRECTORY + '/OTA.bin'                           # Target file
if os.path.exists(dst):                                # Remove OTA.bin if it exists
    print("* Removing the existing OTA.bin")
    os.remove(dst)    
src = os.path.join('build/', project_name + '.bin')    # Build Path to Source-file
if not os.path.exists(src):                           # Check if the source file exists
    print()
    print(f"* ERROR: Source file '{src}' does not exist. Please build the firmware first.")
    print()
    sys.exit(1)                                        # Exit if the source file is not found
# Copy the latest build .bin file to OTA.bin 
shutil.copy2(src, dst)        # copy2 preserves metadata; use copy() for a simple cop
print("* Copied last build firmware (.bin) to OTA.bin")
orgDIRECTORY = os.getcwd() # Save the original directory 
os.chdir(DIRECTORY) # Change to the directory where OTA.bin is located for SERVING
#-------------------------------------------------------
# (3) Get infos needed for mDNS service
#-------------------------------------------------------
def ota_sanitize_hostname(hostname: str) -> str:
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.:/") # Allowed characters in the hostname (url)
    return ''.join(c if c in allowed else '-' for c in hostname)
mDNS_HOSTNAME = "".join([ota_sanitize_hostname(project_name)[:31], ".local."])  # Build the mDNS hostname from the project name with limit of 31 characters from project name
mDNS_SERVICE_NAME = "firmware._http._tcp.local."    # Name of the service for mDNS
mDNS_PORT = 8070                                    # Port (for OTA) on which the HTTP server will listen
ip_address = socket.gethostbyname(socket.gethostname()) # Get the current IP address of the host 
ip_bytes = socket.inet_aton(ip_address)                 # Convert the IP address to bytes for mDNS
#-------------------------------------------------------
# (4) onfig mDNS service announcement
#-------------------------------------------------------
# Function ServiceInfo() from zeroconf-lib defines Service Info for mDNS (Multicast DNS) 
info = ServiceInfo( 
    type_="_http._tcp.local.", # Type of service, here HTTP over TCP
    name=mDNS_SERVICE_NAME,         # Name of the service, here firmware update
    addresses=[ip_bytes],      # List of IP addresses for the service
    port=mDNS_PORT,                 # Port number on which the service is available
    server=mDNS_HOSTNAME,           # mDNS_HOSTNAME of the service, here firmware.local.
)
#-------------------------------------------------------
# (5) Start Zeroconf (mDNS) service = announce
#-------------------------------------------------------
zeroconf = Zeroconf()
print(f"* [mDNS] Registering service as http://{mDNS_HOSTNAME}:{mDNS_PORT}")
print(f"*            refers to Host-IP        {'.'.join(map(str, ip_bytes))}")
print()
zeroconf.register_service(info)
#------------------------------------------------------------------
# (6) Define OneShotHandler for HTTP server used with (7) 
#------------------------------------------------------------------
class OneShotHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        super().do_GET()
        print("* Shutting down after one request.")
        self.server.shutdown()  # This will cause serve_forever() to return
#------------------------------------------------------------------
# (7) LAUNCH a HTTP server in a thread for One-Shot-OTA
#------------------------------------------------------------------
# Custom server class to enable SO_REUSEADDR
class ReusableTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True  # Enable port reuse
# Use the custom server class
with ReusableTCPServer(("", mDNS_PORT), OneShotHandler) as httpd:
    print(f"* SERVING 'OTA.bin' @port {mDNS_PORT} in Subfolder: '{DIRECTORY}'")
    httpd.serve_forever()  # This will return after shutdown is called
#-----------------------------------------------------
# After the HTTP server has served one request, it will shut down
#-----------------------------------------------------
print()
# delete file OTA.bin after serving it
os.chdir(orgDIRECTORY) # Change to the directory where OTA.bin is located for SERVING
if os.path.exists(dst):  # Check if OTA.bin exists
    print("* Removing OTA.bin after served successfully.")
    os.remove(dst)  # Remove the OTA.bin file
# Ensure the server socket is closed
httpd.server_close()
print("* HTTP server socket closed.")
# Unregister the mDNS service
zeroconf.unregister_service(info)
print("* [mDNS] Service unregistered.")
# Close the Zeroconf instance
zeroconf.close()
print("* [mDNS] Zeroconf instance closed.")   
print()
print("YOU CAN CLOSE THE WINDOWS (in ESP-IDF VSCode-Extention it will be reused)")
sys.exit(0)

#-----------------------------------------------------
# (6) Get directory from command line argument, default to current directory
#-----------------------------------------------------
#directory = "ota_update"  # Default directory for OTA files
#directory = sys.argv[1] if len(sys.argv) > 1 else '.'
#os.chdir(directory)  # Change to the specified directory