import re 

text = open("firmware/include/cu_logo.h").read()
# read all hex values in c data struct
data = [int(h,16) for h in re.findall(r'0x[0-9A-Fa-f]+', text)]

WIDTH, HEIGHT = 64,64
BYTES_PER_ROW = WIDTH // 8

for row in range(HEIGHT):
    line = ""
    for x in range(WIDTH):
        byte = data[row * BYTES_PER_ROW + x // 8]
        bit = (byte >> (x%8)) & 1
        line += "  " if bit else "██"
    print(line)

