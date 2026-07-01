# OBS Tally Lights

This project adds tally light functionality to OBS Studio. The active Program and, optionally, Preview scenes are monitored and used to control up to four wireless tally lights via the CC1101 radio module.

## Setup instructions

### OBS

This project uses OBS Studio's Python scripting functionality. Installation instructions can be found in the [OBS Scripting Guide](https://obsproject.com/kb/scripting-guide).

The Python environment used by OBS must have the _pyserial_ package installed:

```
pip install pyserial
```

After setting up Python scripting, download the [Python script](obs/otl.py) and load it in OBS via _Tools > Scripts_. Once loaded, the script can be configured from the same menu.

### Arduino

The Arduino Nanos used in this project are programmed using the Arduino IDE. Besides the default Arduino library that comes with the Arduino IDE, additional libraries are needed, which can be installed using the library manager. These libraries include:
- Adafruit NeoPixel by Adafruit
- Queue by SMFSW
- CC1101 by Mateusz Furga

After installing the libraries, upload the [OTL sender sketch](arduino/otl-sender/otl-sender.ino) to the Arduino of the sender module. After that, upload the [OTL receiver sketch](arduino/otl-receiver/otl-receiver.ino) to each Arduino for the receiver modules (change the id in line 9 accordingly).

## License
This project is licensed under the [MIT License](LICENSE).