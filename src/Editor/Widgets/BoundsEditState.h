#ifndef WIDGETS_BOUNDS_EDIT_STATE_H
#define WIDGETS_BOUNDS_EDIT_STATE_H

#include <string>

namespace BoundsEditState {
    void ActivateForEntity(const std::string& entityId);
    void Deactivate();
    bool IsActiveForEntity(const std::string& entityId);
    const std::string& GetActiveEntityId();
}

#endif // WIDGETS_BOUNDS_EDIT_STATE_H
