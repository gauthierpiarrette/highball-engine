/* Writable executable image section for imagequery's late-load checks. */
extern unsigned char image_query_code[];
__declspec(dllexport) void *GetCode(void) { return image_query_code; }
