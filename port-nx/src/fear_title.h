// F.E.A.R. 360 - which game this program is. port-nx builds F.E.A.R. (fear.exe); port-files
// builds F.E.A.R. Files (fearfiles.exe) from the same host code and defines FEAR_TITLE_FILES
// (port-files/CMakeLists.txt). The id is the title's in the collection file (fear_collection.h).

#pragma once

namespace fear::title {

#if defined(FEAR_TITLE_FILES)
inline constexpr bool kFiles = true;
inline constexpr const char* kId = "files";
#else
inline constexpr bool kFiles = false;
inline constexpr const char* kId = "fear";
#endif

}  // namespace fear::title
