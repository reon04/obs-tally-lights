# OBS Tally Lights

This project adds tally light functionality to OBS Studio. The active Program and, optionally, Preview scenes are monitored and used to control up to four wireless tally lights via the CC1101 radio module.

## Installation

### OBS

This project uses OBS Studio's Python scripting functionality. Installation instructions can be found in the [OBS Scripting Guide](https://obsproject.com/kb/scripting-guide).

The Python environment used by OBS must have the _pyserial_ package installed:

```
pip install pyserial
```

After setting up Python scripting, download the [Python script](obs/obs-tally-lights.py) and load it in OBS via _Tools > Scripts_. Once loaded, the script can be configured from the same menu.

## License
This project is licensed under the [MIT License](LICENSE).