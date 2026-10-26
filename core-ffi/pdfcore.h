/* PDF DLP core — C ABI. All functions return a malloc'd UTF-8 JSON string ({"ok":true,...} | {"ok":false,"error":...});
 * free it with pdfcore_free(). Inputs are UTF-8; NULL allowed for out_path / previous_json. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void  pdfcore_free(char* p);
char* pdfcore_info(const char* path, const char* password);
char* pdfcore_apply(const char* path, const char* password, const char* ops_json, const char* out_path);
char* pdfcore_merge(const char* inputs_json, const char* out_path);
char* pdfcore_split(const char* path, const char* password, const char* mode, const char* spec, uint32_t every, const char* out_dir);
char* pdfcore_images_to_pdf(const char* paths_json, int32_t fit_a4, const char* out_path);
char* pdfcore_parse_ranges(const char* spec, uint32_t total);
char* pdfcore_discover_labels(void);
char* pdfcore_labels_from_file(const char* path, const char* password);
char* pdfcore_msip_metadata(const char* label_json, const char* site_id, uint32_t content_bits, const char* owner, const char* previous_json);
char* pdfcore_new_guid(void);
char* pdfcore_shell(const char* action, const char* items_json);
#ifdef __cplusplus
}
#endif
