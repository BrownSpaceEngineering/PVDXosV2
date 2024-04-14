import pylink
import curses
import re
import platform
import os
import datetime

"""
ABOUT THIS FILE:
This is supposed to use the curses module to create a split screen terminal interface with both the PVDX Shell and the log output in the same screen
It's a very fragile script, has only been tested on windows, and much of it is AI-generated
Therefore, if anything needs to be changed it's probably best to start from scratch
"""

def setup_color_pairs():
    curses.start_color()
    curses.use_default_colors()
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
                    current_pair = curses.color_pair(1 + (fg_index * 8 + bg_index + 1))
        else:
            # Make sure to handle the case where part might still contain null characters
            part = part.replace('\0', '')
            window.addstr(part, current_pair)
    window.refresh()

def open_rtt_channels(stdscr, logfile = None):
    setup_color_pairs()

    jlink = pylink.JLink()
    jlink.open()
    jlink.connect(chip_name='ATSAMD51P20A')
    jlink.rtt_start()

    def redraw_windows():
        stdscr.clear()  # Clear the main screen (stdscr)
        rows, cols = stdscr.getmaxyx()  # Get the new dimensions of the screen

        # Reinitialize windows with new dimensions
        win0 = curses.newwin(rows // 2, cols, 0, 0)
        win1 = curses.newwin(rows // 2 - 1, cols, rows // 2 + 1, 0)
        win0.scrollok(True)
        win1.scrollok(True)
        win0.nodelay(True)

        # Redraw the horizontal separator line
        y = rows // 2
        for x in range(cols):
            stdscr.addch(y, x, curses.ACS_HLINE)

        stdscr.refresh()
        return win0, win1

    win0, win1 = redraw_windows()

    user_input = ""

    while True:
        # Check for user input
        ch = win0.getch()
        if ch == curses.KEY_RESIZE:
            #Not sure if this ever works...
            win0, win1 = redraw_windows()
            continue
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
            # Write to a log file if specified
            if logfile is not None:
                logfile.write(text1)

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

    def curses_opener(stdscr):
        with open (log_file_path, "w") as file:
            try:
                open_rtt_channels(stdscr, log_file_path)
            except Exception as e:
                #Clean up the whole curses thing
                print(f"An error occurred: {e}")
                curses.endwin()
    
    curses.wrapper(curses_opener)

if __name__ == "__main__":
    main()
