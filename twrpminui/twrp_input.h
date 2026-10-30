#pragma once

// TWRP hooks: descriptor lifetime remains owned by minui events.cpp.
void twrp_input_device_added(int fd);
void twrp_input_device_removed(int fd);
