#ifndef KEYBOARD_LISTENER_H
#define KEYBOARD_LISTENER_H

#include <string>
#include <vector>

void keyboardListener();
bool isAnyKeyPressed(const std::vector<std::string>& keys);
bool isAnyKeyPressedWin32Only(const std::vector<std::string>& keys);

#endif // KEYBOARD_LISTENER_H
