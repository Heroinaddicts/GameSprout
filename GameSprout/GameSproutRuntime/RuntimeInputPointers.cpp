#include "GameSproutRuntime.h"
#include <algorithm>

namespace GameSproutRuntime {
void Runtime::PublishInputPointer(RuntimePointerId id, InputPointerPhase phase, Vector2 position,std::uint64_t time) {
    const auto current=std::find_if(_LiveInputPointers.begin(),_LiveInputPointers.end(),[&](const auto& entry) {
        return entry.Id==id;
    });
    if (phase==InputPointerPhase::Down) {
        if (current!=_LiveInputPointers.end()) return;
        _LiveInputPointers.push_back({id,position});
        _Input.PointerChanges.push_back({id,phase,position,{},time});
        return;
    }
    if (current==_LiveInputPointers.end()) return;
    const auto delta=phase==InputPointerPhase::Cancel ? Vector2{} : position-current->Position;
    _Input.PointerChanges.push_back({id,phase,position,delta,time});
    if (phase==InputPointerPhase::Move) current->Position=position;
    else _LiveInputPointers.erase(current);
}

void Runtime::CancelInputPointers() {
    for (const auto& pointer:_LiveInputPointers)
        _Input.PointerChanges.push_back({pointer.Id,InputPointerPhase::Cancel,pointer.Position,{}});
    _LiveInputPointers.clear();
    _InputPointerCaptures.clear();
}

void Runtime::CompleteInputPointers() {
    _Input.PointerChanges.clear();
    std::erase_if(_InputPointerCaptures,[&](const auto& capture) {
        return !IsInputUIOwner(capture.Owner) || std::none_of(_LiveInputPointers.begin(),_LiveInputPointers.end(),
            [&](const auto& pointer) { return pointer.Id==capture.Id; });
    });
}

bool Runtime::CaptureUIPointer(RuntimePointerId id,std::uint64_t owner) {
    if (!_InputEnabled || !_Input.Focused || !IsInputUIOwner(owner)) return false;
    auto existing=std::find_if(_InputPointerCaptures.begin(),_InputPointerCaptures.end(),[&](const auto& capture) {
        return capture.Id==id;
    });
    if (existing!=_InputPointerCaptures.end()) {
        if (IsInputUIOwner(existing->Owner)) return existing->Owner==owner;
        _InputPointerCaptures.erase(existing);
    }
    if (std::none_of(_Input.PointerChanges.begin(),_Input.PointerChanges.end(),[&](const auto& change) {
        return change.Pointer==id && change.Phase==InputPointerPhase::Down;
    })) return false;
    _InputPointerCaptures.push_back({id,owner});
    return true;
}

bool Runtime::ReleaseUIPointer(RuntimePointerId id,std::uint64_t owner) {
    return std::erase_if(_InputPointerCaptures,[&](const auto& capture) { return capture.Id==id && capture.Owner==owner; })!=0;
}

std::uint64_t Runtime::GetUIPointerCapture(RuntimePointerId id) const {
    const auto existing=std::find_if(_InputPointerCaptures.begin(),_InputPointerCaptures.end(),[&](const auto& capture) {
        return capture.Id==id;
    });
    return existing!=_InputPointerCaptures.end() && IsInputUIOwner(existing->Owner) ? existing->Owner : 0;
}
}
