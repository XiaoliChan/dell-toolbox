#pragma once

#include <QStringList>

namespace dtb::win {

// Dell's own software runs resident loops that write the same AWCC WMI
// interface we do; two controllers fight and writes get overwritten.
// Detect the usual suspects so the UI can warn.
QStringList runningAwccProcs();

} // namespace dtb::win
