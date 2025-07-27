Because CSI does not have the function of automatic camera model identification, it is necessary to add the setting model and picture size to its own board, such as:

CONFIG_CAMERA_OV5647=y
CONFIG_CAMERA_OV5647_MIPI_RAW8_800x640_50FPS=y

Then modify the config.h file according to the set size, and refer to the official C-bound MicroPython document when compiling it into firmware.
