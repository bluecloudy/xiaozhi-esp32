#ifndef MIMI_CONTROLLER_H
#define MIMI_CONTROLLER_H

#include <string>

#include "../../../config.h"

class MimiController;

void InitializeMimiController(const HardwareConfig& hw_config);
MimiController* GetMimiController();

bool MimiControllerExecuteAction(MimiController* controller,
                                 const std::string& action,
                                 int steps,
                                 int speed,
                                 int direction,
                                 int amount,
                                 int arm_swing,
                                 std::string* error);
void MimiControllerStopActions(MimiController* controller, bool home = true);
const char* MimiControllerGetActionStatus(MimiController* controller);
bool MimiControllerHasHands(MimiController* controller);

#endif // MIMI_CONTROLLER_H
