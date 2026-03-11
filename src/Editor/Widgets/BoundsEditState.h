/**
 * @file src/Editor/Widgets/BoundsEditState.h
 * @brief Declarations for BoundsEditState.
 */

#ifndef WIDGETS_BOUNDS_EDIT_STATE_H
#define WIDGETS_BOUNDS_EDIT_STATE_H

#include <string>

namespace BoundsEditState {
    /**
     * @brief Activates editing state for an entity.
     * @param entityId Identifier or index value.
     */
    void ActivateForEntity(const std::string& entityId);
    /**
     * @brief Deactivates the current editing state.
     */
    void Deactivate();
    /**
     * @brief Checks whether active for entity.
     * @param entityId Identifier or index value.
     * @return True when the condition is satisfied; otherwise false.
     */
    bool IsActiveForEntity(const std::string& entityId);
    /**
     * @brief Returns the active entity id.
     * @return Resulting string value.
     */
    const std::string& GetActiveEntityId();
}

#endif // WIDGETS_BOUNDS_EDIT_STATE_H
