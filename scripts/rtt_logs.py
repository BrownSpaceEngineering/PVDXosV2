import platform
import pylink # pip install pylink-square

def open_rtt_channels():
    jlink = pylink.JLink()
    jlink.open()
    jlink.connect(chip_name='ATSAMD51P20A')
    jlink.rtt_start()

    while True:
        buf1 = jlink.rtt_read(1, 2048)

        if buf1:
            text1 = bytes(buf1).decode('ascii', errors='replace')
            print(text1)

def main():
    # Sanity check to make sure this is not in WSL
    if "microsoft" in platform.uname().release:
        print("This code cannot be executed in a WSL environment. Run natively on Windows instead!")
        exit(1)
    open_rtt_channels()

if __name__ == "__main__":
    main()
