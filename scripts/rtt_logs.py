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
        buf1 = jlink.rtt_read(1, 2048)

        if buf1:
            text1 = bytes(buf1).decode('ascii', errors='replace')
            print(text1)
            # Write to a log file if specified
            if logfile is not None:
                logfile.write(text1)


def open_rtt_channels_arducam_debug(logfile = None):
    """Same as open_rtt_channels but reads image bytes from RTT channel 2"""
    jlink = pylink.JLink()
    jlink.open()
    jlink.connect(chip_name='ATSAMD51P20A')
    jlink.rtt_start()

    receiving = False
    expected_len = 0
    received = 0
    img_file = None

    while True:
        # Read logs (channel 1)
        buf1 = jlink.rtt_read(1, 4096)
        if buf1:
            text1 = bytes(buf1).decode('ascii', errors='replace')
            print(text1, end="")
            if logfile is not None:
                logfile.write(text1)

            # Detect image start / end markers in logs
            m = re.search(r"IMG_BEGIN len=(\d+)", text1)
            if m and not receiving:
                expected_len = int(m.group(1))
                received = 0
                # Make a file name based on time
                current_time = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                logs_dir = os.path.join(os.path.dirname(__file__), '..', 'logs')
                logs_dir = os.path.normpath(logs_dir)
                os.makedirs(logs_dir, exist_ok=True)
                img_path = os.path.join(logs_dir, f"{current_time}_capture.jpg")
                img_file = open(img_path, "wb")
                receiving = True
                print(f"[RTT] Starting image capture to {img_path} ({expected_len} bytes)")

            if "IMG_END" in text1 and receiving and img_file is not None:
                # We also stop by size guard below; this is just informational.
                print(f"[RTT] IMG_END seen in logs.")

        # If we’re receiving, drain channel 2 (binary)
        if receiving and img_file is not None:
            buf2 = jlink.rtt_read(2, 8192)
            if buf2:
                img_file.write(bytes(buf2))
                received += len(buf2)
                if received >= expected_len:
                    img_file.flush()
                    img_file.close()
                    print(f"[RTT] Image saved ({received} bytes).")
                    receiving = False
                    img_file = None


def main():
    # Sanity check to make sure this is not in WSL
    # if "microsoft" in platform.uname().release:
    #     print("This code cannot be executed in a WSL environment. Run natively on Windows instead!")
    #     exit(1)

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
        # open_rtt_channels(file)
        open_rtt_channels_arducam_debug(file)


if __name__ == "__main__":
    main()
