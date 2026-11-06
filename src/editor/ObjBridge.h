// The editor's objects across the snapcore C ABI (SnapcoreObj in rust/snapcore/include/snapcore.h), both ways
#pragma once
#include "Document.h"
#include "SnapcoreBridge.h"
#include <memory>

namespace snap::ed {

// a DrawObj as snapcore takes it: pointers into the object (and into the children's own SnapcoreObj), valid while the
// DrawObj and this live
struct ObjC {
    SnapcoreObj c{};
    std::vector<std::unique_ptr<ObjC>> kids;
    std::vector<SnapcoreObj> kidsC;
    explicit ObjC(const DrawObj& o);
};

// several objects as an array of SnapcoreObj
struct ObjsC {
    std::vector<std::unique_ptr<ObjC>> keep;
    std::vector<SnapcoreObj> c;
    void Add(const DrawObj& o) {
        keep.push_back(std::make_unique<ObjC>(o));
        c.push_back(keep.back()->c);
    }
};

DrawObj ObjFromC(const SnapcoreObj& c);         // a copy of an object snapcore made

} // namespace snap::ed
