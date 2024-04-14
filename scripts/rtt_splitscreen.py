import pylink
import curses
import re

def setup_color_pairs():
    curses.start_color()
    curses.use_default_colors()
    # Explicitly initializing color content might help
    curses.init_color(curses.COLOR_RED, 1000, 0, 0)
    curses.init_color(curses.COLOR_GREEN, 0, 1000, 0)
    curses.init_color(curses.COLOR_BLUE, 0, 0, 1000)
    color_map = {
        30: curses.COLOR_BLACK,   # Black
        31: curses.COLOR_RED,     # Red
        32: curses.COLOR_GREEN,   # Green
        33: curses.COLOR_YELLOW,  # Yellow
        34: curses.COLOR_BLUE,    # Blue
        35: curses.COLOR_MAGENTA, # Magenta
        36: curses.COLOR_CYAN,    # Cyan
        37: curses.COLOR_WHITE,   # White
        40: curses.COLOR_BLACK,   # Background Black
        41: curses.COLOR_RED,     # Background Red
        42: curses.COLOR_GREEN,   # Background Green
        43: curses.COLOR_YELLOW,  # Background Yellow
        44: curses.COLOR_BLUE,    # Background Blue
        45: curses.COLOR_MAGENTA, # Background Magenta
        46: curses.COLOR_CYAN,    # Background Cyan
        47: curses.COLOR_WHITE    # Background White
    }
    i = 1
    for fg in range(30, 38):
        for bg in range(40, 48):
            fg_curses = color_map[fg]
            bg_curses = color_map[bg]
            curses.init_pair(i, fg_curses, -1)
            i += 1
    curses.init_pair(65, -1, -1)  # Default colors

def parse_and_print(window, text):
    # Replace or remove embedded null characters
    text = text.replace('\0', '')
    color_pattern = re.compile(r"(\x1B\[\d*;?\d*m)")
    parts = color_pattern.split(text)
    default_pair = curses.color_pair(65)
    current_pair = default_pair
    for part in parts:
        if color_pattern.match(part):
            if part == "\x1B[0m":  # Reset code
                current_pair = default_pair
            else:
                codes = part[2:-1].split(';')
                if len(codes) == 2:
                    fg_index = -1
                    bg_index = -1
                    identifier, colorcode = map(int, codes)
                    if identifier == 1 or identifier == 2:
                        # Text modifier (foreground)
                        if 30 <= colorcode <= 37:
                            fg_index = colorcode - 30
                    if identifier == 4 or identifier == 24:
                        # Background modifier
                        if 40 <= colorcode <= 47:
                            bg_index = colorcode - 40
                    current_pair = curses.color_pair(fg_index * 8 + bg_index + 1)
        else:
            # Make sure to handle the case where part might still contain null characters
            part = part.replace('\0', '')
            window.addstr(part, current_pair)
    window.refresh()

def open_rtt_channels(stdscr):
    setup_color_pairs()
    stdscr.clear()
    win0 = curses.newwin(curses.LINES // 2, curses.COLS, 0, 0)
    win0.scrollok(True)
    y = curses.LINES // 2  # Draw horizontal line to separate windows
    for x in range(curses.COLS):  # Draw across the entire width of the screen
        stdscr.addch(y, x, curses.ACS_HLINE)  # Using horizontal line character
    win1 = curses.newwin(curses.LINES // 2 - 1, curses.COLS, curses.LINES // 2 + 1, 0)
    win1.scrollok(True)
    jlink = pylink.JLink()
    jlink.open()
    jlink.connect(chip_name='ATSAMD51P20A')
    jlink.rtt_start()

    win0.nodelay(True)

    stdscr.refresh()

    user_input = ""

    while True:
        # Check for user input
        ch = win0.getch()
        if 0 < ch <= 127:
            if ch == 8: # If the user presses backspace, move the cursor back one space
                if len(user_input) == 0:
                    continue
                y, x = win0.getyx()
                if x > 0:  # Only move back if not at the start of the line
                    win0.move(y, x - 1)
                    win0.delch()  # Remove the character at the cursor
                    user_input = user_input[:-1]
            # Otherwise, print the character to the screen
            else:
                win0.addch(ch)
                user_input += chr(ch)
                stdscr.refresh()
                if ch == 10:  # ASCII value for newline
                    #Send to jlink!
                    to_send = user_input.encode('ascii')
                    jlink.rtt_write(0, to_send)
                    user_input = ""  # Reset command string for new input

        # Read from RTT channels
        buf0 = jlink.rtt_read(0, 1024)
        buf1 = jlink.rtt_read(1, 1024)

        if buf0:
            text0 = bytes(buf0).decode('ascii', errors='replace')
            parse_and_print(win0, text0)
        if buf1:
            text1 = bytes(buf1).decode('ascii', errors='replace')
            parse_and_print(win1, text1)

if __name__ == "__main__":
    curses.wrapper(open_rtt_channels)
