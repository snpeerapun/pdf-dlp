#pragma once
#include <QtCore>
// Single-language UI (th or en), switchable at runtime. TXT("key") returns the string for the current language.
namespace I18n {
QString lang();
void setLang(const QString& l);
QString t(const char* key);
}
#define TXT(k) I18n::t(k)
