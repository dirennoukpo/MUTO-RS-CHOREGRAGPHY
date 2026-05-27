python3 -c "
import serial, time
s = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
time.sleep(0.3)
# Reset déviation servo ID X à 0 : addr=0x28, data=[ID, 0x00, 0x00]
# Remplace X par l'ID du servo corrompu (1-18)
for servo_id in range(1, 19):
    # deviation = 0 → data2=0x00, data3=0x00
    # len=0x0B, CHK=255-(0x0B+0x01+0x28+servo_id+0x00+0x00)%256
    chk = (255 - (0x0B + 0x01 + 0x28 + servo_id + 0x00 + 0x00) % 256) & 0xFF
    frame = bytes([0x55, 0x00, 0x0B, 0x01, 0x28, servo_id, 0x00, 0x00, chk, 0x00, 0xAA])
    s.write(frame)
    time.sleep(0.1)
    print(f'Reset servo {servo_id}: {frame.hex(\" \").upper()}')
s.close()
print('Done.')
"
