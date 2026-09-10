// bindings/anytermqt_global.h
//
// The single header Shiboken parses. Include the specific Qt headers the
// bound API mentions, not the module umbrellas: <QtCore/QtCore> pulls in
// qplugin.h, which Clang cannot parse under MSVC because offsetof expands to
// a reinterpret_cast in a constexpr context.

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtWidgets/QAbstractScrollArea>

#include "qtpyte/palette.h"
#include "qtpyte/ptysession.h"
#include "qtpyte/terminalwidget.h"