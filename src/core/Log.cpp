#include "Log.h"

Q_LOGGING_CATEGORY(lcCore, "rl.core")
Q_LOGGING_CATEGORY(lcProto, "rl.proto")
Q_LOGGING_CATEGORY(lcMedia, "rl.media")
Q_LOGGING_CATEGORY(lcUi, "rl.ui")

namespace rl {

void installLogging()
{
    qSetMessagePattern(
        QStringLiteral("%{time hh:mm:ss.zzz} %{category} "
                       "%{if-debug}D%{endif}%{if-info}I%{endif}%{if-warning}W%{endif}"
                       "%{if-critical}C%{endif}%{if-fatal}F%{endif} %{message}"));
}

} // namespace rl
