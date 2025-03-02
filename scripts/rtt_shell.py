import platform
import pylink # pip install pylink-square
import os
import datetime

def open_rtt_channels(logfile = None):
    jlink = pylink.JLink()
    jlink.open()
    jlink.connect(chip_name='ATSAMD51P20A')
    jlink.rtt_start()

    while True:
        buf1 = jlink.rtt_read(0, 2048)

        if buf1:
            text1 = bytes(buf1).decode('ascii', errors='replace')
            print(text1)
            # Write to a log file if specified
            if logfile is not None:
                logfile.write(text1)

            if "PVDXos Shell> $" in text1:
                jlink.rtt_write(0, (input("> ") + "\n").encode('ascii'))

def main():
    # Sanity check to make sure this is not in WSL
    if "microsoft" in platform.uname().release:
        print("This code cannot be executed in a WSL environment. Run natively on Windows instead!")
        exit(1)

    #Check if the ./logs directory exists
    current_script_dir = os.path.dirname(__file__)
    logs_path = os.path.join(current_script_dir, '..', 'logs')
    logs_path = os.path.normpath(logs_path)

    #If the logs directory does not exist, create it
    if not os.path.exists(logs_path):
        os.makedirs(logs_path)

    # Create a log file in the logs directory with the current date and time
    current_time = datetime.datetime.now()
    log_filename = current_time.strftime("%Y-%m-%d_%H-%M-%S.log")
    log_file_path = os.path.join(logs_path, log_filename)
    
    with open (log_file_path, "w") as file:
        open_rtt_channels(file)

if __name__ == "__main__":
    main()
