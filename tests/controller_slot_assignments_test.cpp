#include "ship/controller/physicaldevice/ControllerSlotAssignments.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {
using Assignments = Ship::ControllerSlotAssignments;

struct InputState {
    bool button = false;
    int axis = 0;
};

[[noreturn]] void Fail(const std::string& message) {
    std::cerr << "controller slot regression failed: " << message << '\n';
    std::exit(1);
}

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        Fail(message);
    }
}

InputState ReadSlot(const Assignments& assignments, uint8_t slot,
                    const std::unordered_map<int32_t, InputState>& input) {
    auto instanceId = assignments.GetInstanceId(slot);
    if (!instanceId.has_value()) {
        return {};
    }
    auto state = input.find(*instanceId);
    return state == input.end() ? InputState{} : state->second;
}
} // namespace

int main() {
    Assignments assignments;

    // A missed removal while input is held must release player 1 and make the
    // next gameplay read neutral even if stale fake state still exists.
    assignments.Reconcile({ { 11, "pad-a" } });
    std::unordered_map<int32_t, InputState> input = { { 11, { true, 32767 } } };
    Expect(ReadSlot(assignments, 0, input).button, "initial held button was not visible");
    assignments.Reconcile({});
    auto neutral = ReadSlot(assignments, 0, input);
    Expect(!neutral.button && neutral.axis == 0, "stale held button or axis survived removal");

    // A sole returning controller with a new SDL instance ID reclaims player 1.
    assignments.Reconcile({ { 12, "pad-a" } });
    Expect(assignments.GetSlot(12) == 0, "sole returning controller did not reclaim player 1");

    // A genuinely additional controller takes player 2 without moving player 1.
    assignments.Reconcile({ { 12, "pad-a" }, { 21, "pad-b" } });
    Expect(assignments.GetSlot(12) == 0, "additional controller displaced player 1");
    Expect(assignments.GetSlot(21) == 1, "additional controller did not take player 2");

    // When one of two controllers reconnects, the still-connected controller
    // and the returning controller both retain their ownership.
    assignments.Reconcile({ { 12, "pad-a" }, { 22, "pad-b" } });
    Expect(assignments.GetSlot(12) == 0, "unchanged player 1 ownership was lost");
    Expect(assignments.GetSlot(22) == 1, "returning player 2 ownership was lost");

    // A valid sole remaining controller keeps player 2 rather than being moved.
    assignments.Reconcile({ { 22, "pad-b" } });
    Expect(assignments.GetSlot(22) == 1, "valid connected controller changed player slot");

    // Foreground reconciliation follows the same current-enumeration path: a
    // stale player 1 is released and its returning identity gets player 1.
    assignments.Reconcile({});
    assignments.Reconcile({ { 13, "pad-a" } });
    Expect(assignments.GetSlot(13) == 0, "foreground return did not reconcile player 1");

    // A deliberate per-port disable remains disabled across active checks;
    // reconnect logic must not erase the user's controller selection.
    Expect(assignments.Unassign(13, 0), "settings could not disable player 1 controller");
    assignments.Reconcile({ { 13, "pad-a" } });
    Expect(!assignments.GetSlot(13).has_value(), "active check erased disabled controller preference");
    Expect(assignments.Assign(13, 0), "settings could not restore player 1 controller");

    std::cout << "controller slot regression passed\n";
    return 0;
}
