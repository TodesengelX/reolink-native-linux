#pragma once

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcCore)
Q_DECLARE_LOGGING_CATEGORY(lcProto)
Q_DECLARE_LOGGING_CATEGORY(lcMedia)
Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace rl {
void installLogging();
}
