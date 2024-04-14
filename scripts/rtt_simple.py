import pylink # pip install pylink-square

def open_rtt_channels():
    jlink = pylink.JLink()
    jlink.open()
    jlink.connect(chip_name='ATSAMD51P20A')
    jlink.rtt_start()

    while True:
        buf1 = jlink.rtt_read(1, 1024)

        if buf1:
            text1 = bytes(buf1).decode('ascii', errors='replace')
            print(text1)

if __name__ == "__main__":
    open_rtt_channels()
