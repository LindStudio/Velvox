#pragma once

#include "AssistantTypes.h"

class VelvoxAudioProcessor;

namespace velvox::assistant
{
    // Copies everything the planner may use from the running plugin: current
    // parameter values, VOCAL CHECK measurements (only when there was enough
    // voice), the live correlation and the beat sidechain status.
    // Message thread only; the result is a plain value that can go anywhere.
    AssistantContext buildContext (VelvoxAudioProcessor& processor);
}
