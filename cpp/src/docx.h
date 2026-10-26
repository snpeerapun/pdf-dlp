#pragma once
// PDF -> structured document (paragraphs/runs/images) for the Rust DOCX writer.
#include <QtCore>
#include "doc.h"
class EditSession;
namespace DocxExport { QJsonObject extract(const PdfDoc& doc, EditSession* session); }
