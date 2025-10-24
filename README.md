You can use avrdude to set the lock bits that protect flash from reprogramming.
For example:
avrdude -c usbasp -p m328p -U lock:w:0x0F:m

Locks both the application and bootloader sections from being written.
Prevents sketch upload through serial bootloader.
The only way to re-flash afterward is by using a high-voltage programmer or chip erase via ISP.
To reverse, you’d need to erase the chip:

avrdude -c usbasp -p m328p -e

TODO: add second passcode @ address 0

TODO: implement uBitcoin for wallet generation

