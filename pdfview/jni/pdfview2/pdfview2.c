#include <string.h>
#include <wctype.h>
#include <jni.h>

#include "pdfview2.h"
#include "android/log.h"

#define PDFVIEW_LOG_TAG "cx.hell.android.pdfview"
#define PDFVIEW_MAX_PAGES_LOADED 16

#define BITMAP_STORE_MAX_AGE  1
#define FIND_STORE_MAX_AGE    4
#define TEXT_STORE_MAX_AGE    4

static jintArray get_page_image_bitmap(JNIEnv *env,
      pdf_t *pdf, int pageno, int zoom_pmil, int left, int top, int rotation,
      int gray, int skipImages,
      int *width, int *height);
static void copy_alpha(unsigned char* out, unsigned char *in, unsigned int w, unsigned int h);


extern char fz_errorbuf[150*20]; /* defined in fitz/apv_base_error.c */

#define NUM_BOXES 5

const char boxes[NUM_BOXES][MAX_BOX_NAME+1] = {
    "ArtBox",
    "BleedBox",
    "CropBox",
    "MediaBox",
    "TrimBox"
};

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM *jvm, void *reserved) {
    __android_log_print(ANDROID_LOG_INFO, PDFVIEW_LOG_TAG, "JNI_OnLoad");
    return JNI_VERSION_1_2;
}


/**
 * Implementation of native method PDF.parseFile.
 * Opens file and parses at least some bytes - so it could take a while.
 * @param file_name file name to parse.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_lib_pdf_PDF_parseFile(
        JNIEnv *env,
        jobject jthis,
        jstring file_name,
        jint box_type,
        jstring password
        ) {
    const char *c_file_name = NULL;
    const char *c_password = NULL;
    jboolean iscopy;
    jclass this_class;
    jfieldID pdf_field_id;
    jfieldID invalid_password_field_id;
    pdf_t *pdf = NULL;

    c_file_name = (*env)->GetStringUTFChars(env, file_name, &iscopy);
    c_password = (*env)->GetStringUTFChars(env, password, &iscopy);
    this_class = (*env)->GetObjectClass(env, jthis);
    pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
    invalid_password_field_id = (*env)->GetFieldID(env, this_class, "invalid_password", "I");
    __android_log_print(ANDROID_LOG_INFO, PDFVIEW_LOG_TAG, "Parsing");
    pdf = parse_pdf_file(c_file_name, 0, c_password);

    if (pdf != NULL && pdf->invalid_password) {
       (*env)->SetIntField(env, jthis, invalid_password_field_id, 1);
       free (pdf);
       pdf = NULL;
    }
    else {
       (*env)->SetIntField(env, jthis, invalid_password_field_id, 0);
    }

    if (pdf != NULL) {
        if (NUM_BOXES <= box_type)
            strcpy(pdf->box, "CropBox");
        else
            strcpy(pdf->box, boxes[box_type]);
    }

    (*env)->ReleaseStringUTFChars(env, file_name, c_file_name);
    (*env)->ReleaseStringUTFChars(env, password, c_password);

    (*env)->SetIntField(env, jthis, pdf_field_id, (int)pdf);

    if (pdf != NULL)
       __android_log_print(ANDROID_LOG_INFO, PDFVIEW_LOG_TAG, "Loading %s in page mode %s.", c_file_name, pdf->box);
}


/**
 * Create pdf_t struct from opened file descriptor.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_lib_pdf_PDF_parseFileDescriptor(
        JNIEnv *env,
        jobject jthis,
        jobject fileDescriptor,
        jint box_type,
        jstring password
        ) {
    int fileno;
    jclass this_class;
    jfieldID pdf_field_id;
    pdf_t *pdf = NULL;
    jfieldID invalid_password_field_id;
    jboolean iscopy;
    const char* c_password;

    c_password = (*env)->GetStringUTFChars(env, password, &iscopy);
    this_class = (*env)->GetObjectClass(env, jthis);
    pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
    invalid_password_field_id = (*env)->GetFieldID(env, this_class, "invalid_password", "I");

    fileno = get_descriptor_from_file_descriptor(env, fileDescriptor);
    pdf = parse_pdf_file(NULL, fileno, c_password);

    if (pdf != NULL && pdf->invalid_password) {
       (*env)->SetIntField(env, jthis, invalid_password_field_id, 1);
       free (pdf);
       pdf = NULL;
    }
    else {
       (*env)->SetIntField(env, jthis, invalid_password_field_id, 0);
    }

    if (pdf != NULL) {
        if (NUM_BOXES <= box_type)
            strcpy(pdf->box, "CropBox");
        else
            strcpy(pdf->box, boxes[box_type]);
    }
    (*env)->ReleaseStringUTFChars(env, password, c_password);
    (*env)->SetIntField(env, jthis, pdf_field_id, (int)pdf);
}


/**
 * Implementation of native method PDF.getPageCount - return page count of this PDF file.
 * Returns -1 on error, eg if pdf_ptr is NULL.
 * @param env JNI Environment
 * @param this PDF object
 * @return page count or -1 on error
 */
JNIEXPORT jint JNICALL
Java_cx_hell_android_lib_pdf_PDF_getPageCount(
        JNIEnv *env,
        jobject this) {
    pdf_t *pdf = NULL;
    pdf = get_pdf_from_this(env, this);
    if (pdf == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "pdf is null");
        return -1;
    }
    return pdf_count_pages(pdf->doc);
}


JNIEXPORT jintArray JNICALL
Java_cx_hell_android_lib_pdf_PDF_renderPage(
        JNIEnv *env,
        jobject this,
        jint pageno,
        jint zoom,
        jint left,
        jint top,
        jint rotation,
        jboolean gray,
        jboolean skipImages,
        jobject size) {

    jint *buf; /* rendered page, freed before return, as bitmap */
    jintArray jints; /* return value */
    pdf_t *pdf; /* parsed pdf data, extracted from java's "this" object */
    int width, height;

    get_size(env, size, &width, &height);

    __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "jni renderPage(pageno: %d, zoom: %d, left: %d, top: %d, width: %d, height: %d) start",
            (int)pageno, (int)zoom,
            (int)left, (int)top,
            (int)width, (int)height);

    pdf = get_pdf_from_this(env, this);

    jints = get_page_image_bitmap(env, pdf, pageno, zoom, left, top, rotation, gray,
          skipImages, &width, &height);

    if (jints != NULL)
        save_size(env, size, width, height);

    return jints;
}


JNIEXPORT jint JNICALL
Java_cx_hell_android_lib_pdf_PDF_getPageSize(
        JNIEnv *env,
        jobject this,
        jint pageno,
        jobject size) {
    int width, height, error;
    pdf_t *pdf = NULL;

    pdf = get_pdf_from_this(env, this);
    if (pdf == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "this.pdf is null");
        return 1;
    }

    error = get_page_size(pdf, pageno, &width, &height);
    if (error != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "get_page_size error: %d", (int)error);
        return 2;
    }

    save_size(env, size, width, height);
    return 0;
}


// #ifdef pro
// /**
//  * Get document outline.
//  */
// JNIEXPORT jobject JNICALL
// Java_cx_hell_android_lib_pdf_PDF_getOutlineNative(
//         JNIEnv *env,
//         jobject this) {
//     int error;
//     pdf_t *pdf = NULL;
//     jobject joutline = NULL;
//     fz_outline *outline = NULL; /* outline root */
//     fz_outline *curr_outline = NULL; /* for walking over fz_outline tree */
// 
//     pdf = get_pdf_from_this(env, this);
//     if (pdf == NULL) {
//         __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "this.pdf is null");
//         return NULL;
//     }
// 
//     outline = pdf_load_outline(pdf->doc);
//     if (outline == NULL) return NULL;
// 
//     /* recursively copy fz_outline to PDF.Outline */
//     /* TODO: rewrite pdf_load_outline to create Java's PDF.Outline objects directly */
//     joutline = create_outline_recursive(env, NULL, outline);
//     __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "joutline converted");
//     return joutline;
// }
// #endif


/**
 * Free resources allocated in native code.
 */
JNIEXPORT void JNICALL
Java_cx_hell_android_lib_pdf_PDF_freeMemory(
        JNIEnv *env,
        jobject this) {
    pdf_t *pdf = NULL;
    jclass this_class = (*env)->GetObjectClass(env, this);
    jfieldID pdf_field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");

    __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "jni freeMemory()");
    pdf = (pdf_t*) (*env)->GetIntField(env, this, pdf_field_id);
    (*env)->SetIntField(env, this, pdf_field_id, 0);

    if (pdf->pages) {
        int i;
        int pagecount;
        pagecount = pdf_count_pages(pdf->doc);

        for(i = 0; i < pagecount; ++i) {
            if (pdf->pages[i]) {
                pdf_free_page(pdf->doc, pdf->pages[i]);
                pdf->pages[i] = NULL;
            }
        }

        free(pdf->pages);
    }

    /* pdf->fileno is dup()-ed in parse_pdf_fileno */
    if (pdf->fileno >= 0) close(pdf->fileno);
    if (pdf->glyph_cache)
        fz_drop_glyph_cache_context(pdf->ctx);
    if (pdf->doc)
        pdf_close_document(pdf->doc);

    free(pdf);
}

/* wcsstr() seems broken--it matches too much */
wchar_t* widestrstr(wchar_t* haystack, int haystack_length, wchar_t* needle, int needle_length) {
    char* found;
    int byte_haystack_length;
    int byte_needle_length;

    if (needle_length == 0)
         return haystack;
         
    byte_haystack_length = haystack_length * sizeof(wchar_t);
    byte_needle_length = needle_length * sizeof(wchar_t);

    while(haystack_length >= needle_length &&
        NULL != (found = memmem(haystack, byte_haystack_length, needle, byte_needle_length))) {
          int delta = found - (char*)haystack;
          int new_offset;

          /* Check if the find is wchar_t-aligned */
          if (delta % sizeof(wchar_t) == 0)
              return (wchar_t*)found;

          new_offset = (delta + sizeof(wchar_t) - 1) / sizeof(wchar_t);

          haystack += new_offset;
          haystack_length -= new_offset;
          byte_haystack_length = haystack_length * sizeof(wchar_t);
    }

    return NULL;
}

// NOTE use fz_print_text_page + fmemopen
void print_text_page(fz_context *ctx, fz_text_page *page, char** buffer)
{
    fz_text_block *block;
    fz_text_line *line;
    fz_text_span *span;
    fz_text_char *ch;
    int i, n;
    int len;
    char* buffer_pos;

    len = 0;
    for (block = page->blocks; block < page->blocks + page->len; block++)
    {
        for (line = block->lines; line < block->lines + block->len; line++)
        {
            for (span = line->spans; span < line->spans + line->len; span++)
            {
                for (ch = span->text; ch < span->text + span->len; ch++)
                {
                    len += fz_runelen(ch->c);
                }
            }

            len += 1; // new line
        }

        len += 1; // new line
    }

    if (!len)
    {
        return;
    }

    *buffer = fz_malloc(ctx, len);
    buffer_pos = *buffer;
    for (block = page->blocks; block < page->blocks + page->len; block++)
    {
        for (line = block->lines; line < block->lines + block->len; line++)
        {
            for (span = line->spans; span < line->spans + line->len; span++)
            {
                for (ch = span->text; ch < span->text + span->len; ch++)
                {
                    n = fz_runetochar(buffer_pos, ch->c);
                    buffer_pos += n;
                }
            }

            *buffer_pos++ = '\n';
        }

        *buffer_pos++ = '\n';
    }
}

fz_text_char textcharat(fz_text_page *page, int idx)
{
    static fz_text_char emptychar = { {0,0,0,0}, ' ' };
    fz_text_block *block;
    fz_text_line *line;
    fz_text_span *span;
    int ofs = 0;
    for (block = page->blocks; block < page->blocks + page->len; block++)
    {
        for (line = block->lines; line < block->lines + block->len; line++)
        {
            for (span = line->spans; span < line->spans + line->len; span++)
            {
                if (idx < ofs + span->len)
                    return span->text[idx - ofs];
                /* pseudo-newline */
                if (span + 1 == line->spans + line->len)
                {
                    if (idx == ofs + span->len)
                        return emptychar;
                    ofs++;
                }
                ofs += span->len;
            }
        }
    }
    return emptychar;
}

int charat(fz_text_page *page, int idx)
{
    return textcharat(page, idx).c;
}

fz_bbox bboxcharat(fz_text_page *page, int idx)
{
    return fz_round_rect(textcharat(page, idx).bbox);
}

int textlen(fz_text_page *page)
{
    fz_text_block *block;
    fz_text_line *line;
    fz_text_span *span;
    int len = 0;
    for (block = page->blocks; block < page->blocks + page->len; block++)
    {
        for (line = block->lines; line < block->lines + block->len; line++)
        {
            for (span = line->spans; span < line->spans + line->len; span++)
                len += span->len;
            len++; /* pseudo-newline */
        }
    }
    return len;
}

int match(fz_text_page *page, const char *s, int n)
{
    int orig = n;
    int c;
    while (*s) {
        s += fz_chartorune(&c, (char *)s);
        if (c == ' ' && charat(page, n) == ' ') {
            while (charat(page, n) == ' ')
                n++;
        } else {
            if (tolower(c) != tolower(charat(page, n)))
                return 0;
            n++;
        }
    }
    return n - orig;
}

/* TODO: Specialcase searches for 7-bit text to make them faster */
JNIEXPORT jobject JNICALL
Java_cx_hell_android_lib_pdf_PDF_find(
        JNIEnv *env,
        jobject this,
        jstring text,
        jint pageno) {
    pdf_t *pdf = NULL;
    const jchar *jtext = NULL;
    const char *ctext = NULL;
    jboolean is_copy;
    jobject results = NULL;
    pdf_page *page = NULL;
    fz_text_span *text_span = NULL, *ln = NULL;
    fz_device *dev = NULL;
    wchar_t *textlinechars;
    wchar_t *found = NULL;
    jobject find_result = NULL;
    int length;
    int i;
    fz_text_sheet *text_sheet = NULL;
    fz_text_page *text_page = NULL;
    fz_rect page_rect;
    int pos, len, n;
    fz_matrix ctm;

    ctext = (*env)->GetStringUTFChars(env, text, NULL);
    if (ctext == NULL) return NULL;

    pdf = get_pdf_from_this(env, this);
    page = get_page(pdf, pageno);

    fz_try(pdf->ctx)
    {
        ctm = fz_identity;
        page_rect = fz_bound_page(pdf->doc, page);
        page_rect = fz_transform_rect(ctm, page_rect);

        text_sheet = fz_new_text_sheet(pdf->ctx);
        text_page = fz_new_text_page(pdf->ctx, page_rect);
        dev = fz_new_text_device(pdf->ctx, text_sheet, text_page);
        fz_run_page(pdf->doc, page, dev, ctm, NULL);
        fz_free_device(dev); dev = 0;

        len = textlen(text_page);
        __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page (%d,%d,%d,%d), text length = %d", (int)page_rect.x0, (int)page_rect.y0, (int)page_rect.x1, (int)page_rect.y1, (int)len);
        for (pos = 0; pos < len; pos++)
        {
            fz_bbox rr = fz_empty_bbox;
            n = match(text_page, ctext, pos);
            for (i = 0; i < n; i++)
                rr = fz_union_bbox(rr, bboxcharat(text_page, pos + i));

            if (!fz_is_empty_bbox(rr))
            {
                __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "match at %d", (int)pos);
                find_result = create_find_result(env);
                if (!find_result)
                {
                    fz_throw(pdf->ctx, "tried to create empty find result, but got NULL instead");
                }

                set_find_result_page(env, find_result, pageno);
                add_find_result_marker(env, find_result, rr.x0, rr.y0, rr.x1, rr.y1);
                add_find_result_to_list(env, &results, find_result);
            }
        }
    }
    fz_always(pdf->ctx)
    {
        fz_free_device(dev);
        fz_free_text_page(pdf->ctx, text_page);
        fz_free_text_sheet(pdf->ctx, text_sheet);
        (*env)->ReleaseStringUTFChars(env, text, ctext);
    }
    fz_catch(pdf->ctx)
    {
        return NULL;
    }

    return results;
}


// #ifdef pro
// /**
//  * Return text of given page.
//  */
// JNIEXPORT jobject JNICALL
// Java_cx_hell_android_lib_pdf_PDF_getText(
//         JNIEnv *env,
//         jobject this,
//         jint pageno) {
//     char *text = NULL;
//     pdf_t *pdf = NULL;
//     pdf = get_pdf_from_this(env, this);
//     jstring jtext = NULL;
// 
//     if (pdf == NULL) {
//         __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "getText: pdf is NULL");
//         return NULL;
//     }
//     text = extract_text(pdf, pageno);
//     jtext = (*env)->NewStringUTF(env, text);
//     fz_free(pdf->ctx, text);
//     return jtext;
// }
// #endif


/**
 * Create empty FindResult object.
 * @param env JNI Environment
 * @return newly created, empty FindResult object
 */
jobject create_find_result(JNIEnv *env) {
    static jmethodID constructorID;
    jclass findResultClass = NULL;
    static int jni_ids_cached = 0;
    jobject findResultObject = NULL;

    findResultClass = (*env)->FindClass(env, "cx/hell/android/lib/pagesview/FindResult");

    if (findResultClass == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_find_result: FindClass returned NULL");
        return NULL;
    }

    if (jni_ids_cached == 0) {
        constructorID = (*env)->GetMethodID(env, findResultClass, "<init>", "()V");
        if (constructorID == NULL) {
            (*env)->DeleteLocalRef(env, findResultClass);
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_find_result: couldn't get method id for FindResult constructor");
            return NULL;
        }
        jni_ids_cached = 1;
    }

    findResultObject = (*env)->NewObject(env, findResultClass, constructorID);
    (*env)->DeleteLocalRef(env, findResultClass);
    return findResultObject;
}


void add_find_result_to_list(JNIEnv *env, jobject *list, jobject find_result) {
    static int jni_ids_cached = 0;
    static jmethodID list_add_method_id = NULL;
    jclass list_class = NULL;
    if (list == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "list cannot be null - it must be a pointer jobject variable");
        return;
    }
    if (find_result == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "find_result cannot be null");
        return;
    }
    if (*list == NULL) {
        jmethodID list_constructor_id;
        __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "creating ArrayList");
        list_class = (*env)->FindClass(env, "java/util/ArrayList");
        if (list_class == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't find class java/util/ArrayList");
            return;
        }
        list_constructor_id = (*env)->GetMethodID(env, list_class, "<init>", "()V");
        if (!list_constructor_id) {
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "couldn't find ArrayList constructor");
            return;
        }
        *list = (*env)->NewObject(env, list_class, list_constructor_id);
        if (*list == NULL) {
            __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "failed to create ArrayList: NewObject returned NULL");
            return;
        }
    }

    if (!jni_ids_cached) {
        if (list_class == NULL) {
            list_class = (*env)->FindClass(env, "java/util/ArrayList");
            if (list_class == NULL) {
                __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't find class java/util/ArrayList");
                return;
            }
        }
        list_add_method_id = (*env)->GetMethodID(env, list_class, "add", "(Ljava/lang/Object;)Z");
        if (list_add_method_id == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "couldn't get ArrayList.add method id");
            return;
        }
        jni_ids_cached = 1;
    } 

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "calling ArrayList.add");
    (*env)->CallBooleanMethod(env, *list, list_add_method_id, find_result);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "add_find_result_to_list done");
}


/**
 * Set find results page member.
 * @param JNI environment
 * @param findResult find result object that should be modified
 * @param page new value for page field
 */
void set_find_result_page(JNIEnv *env, jobject findResult, int page) {
    static char jni_ids_cached = 0;
    static jfieldID page_field_id = 0;
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "trying to set find results page number");
    if (jni_ids_cached == 0) {
        jclass findResultClass = (*env)->GetObjectClass(env, findResult);
        page_field_id = (*env)->GetFieldID(env, findResultClass, "page", "I");
        jni_ids_cached = 1;
    }
    (*env)->SetIntField(env, findResult, page_field_id, page);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "find result page number set");
}


/**
 * Add marker to find result.
 */
void add_find_result_marker(JNIEnv *env, jobject findResult, int x0, int y0, int x1, int y1) {
    static jmethodID addMarker_methodID = 0;
    static unsigned char jni_ids_cached = 0;
    if (!jni_ids_cached) {
        jclass findResultClass = NULL;
        findResultClass = (*env)->FindClass(env, "cx/hell/android/lib/pagesview/FindResult");
        if (findResultClass == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "add_find_result_marker: FindClass returned NULL");
            return;
        }
        addMarker_methodID = (*env)->GetMethodID(env, findResultClass, "addMarker", "(IIII)V");
        if (addMarker_methodID == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "add_find_result_marker: couldn't find FindResult.addMarker method ID");
            return;
        }
        jni_ids_cached = 1;
    }
    (*env)->CallVoidMethod(env, findResult, addMarker_methodID, x0, y0, x1, y1); /* TODO: is always really int jint? */
}


/**
 * Get pdf_ptr field value, cache field address as a static field.
 * @param env Java JNI Environment
 * @param this object to get "pdf_ptr" field from
 * @return pdf_ptr field value
 */
pdf_t* get_pdf_from_this(JNIEnv *env, jobject this) {
    static jfieldID field_id = 0;
    static unsigned char field_is_cached = 0;
    pdf_t *pdf = NULL;
    if (! field_is_cached) {
        jclass this_class = (*env)->GetObjectClass(env, this);
        field_id = (*env)->GetFieldID(env, this_class, "pdf_ptr", "I");
        field_is_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached pdf_ptr field id %d", (int)field_id);
    }
    pdf = (pdf_t*) (*env)->GetIntField(env, this, field_id);
    return pdf;
}


/**
 * Get descriptor field value from FileDescriptor class, cache field offset.
 * This is undocumented private field.
 * @param env JNI Environment
 * @param this FileDescriptor object
 * @return file descriptor field value
 */
int get_descriptor_from_file_descriptor(JNIEnv *env, jobject this) {
    static jfieldID field_id = 0;
    static unsigned char is_cached = 0;
    if (!is_cached) {
        jclass this_class = (*env)->GetObjectClass(env, this);
        field_id = (*env)->GetFieldID(env, this_class, "descriptor", "I");
        is_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached descriptor field id %d", (int)field_id);
    }
    __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "will get descriptor field...");
    return (*env)->GetIntField(env, this, field_id);
}


void get_size(JNIEnv *env, jobject size, int *width, int *height) {
    static jfieldID width_field_id = 0;
    static jfieldID height_field_id = 0;
    static unsigned char fields_are_cached = 0;
    if (! fields_are_cached) {
        jclass size_class = (*env)->GetObjectClass(env, size);
        width_field_id = (*env)->GetFieldID(env, size_class, "width", "I");
        height_field_id = (*env)->GetFieldID(env, size_class, "height", "I");
        fields_are_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached Size fields");
    }
    *width = (*env)->GetIntField(env, size, width_field_id);
    *height = (*env)->GetIntField(env, size, height_field_id);
}


/**
 * Store width and height values into PDF.Size object, cache field ids in static members.
 * @param env JNI Environment
 * @param width width to store
 * @param height height field value to be stored
 * @param size target PDF.Size object
 */
void save_size(JNIEnv *env, jobject size, int width, int height) {
    static jfieldID width_field_id = 0;
    static jfieldID height_field_id = 0;
    static unsigned char fields_are_cached = 0;
    if (! fields_are_cached) {
        jclass size_class = (*env)->GetObjectClass(env, size);
        width_field_id = (*env)->GetFieldID(env, size_class, "width", "I");
        height_field_id = (*env)->GetFieldID(env, size_class, "height", "I");
        fields_are_cached = 1;
        __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview", "cached Size fields");
    }
    (*env)->SetIntField(env, size, width_field_id, width);
    (*env)->SetIntField(env, size, height_field_id, height);
}


/**
 * pdf_t "constructor": create empty pdf_t with default values.
 * @return newly allocated pdf_t struct with fields set to default values
 */
pdf_t* create_pdf_t() {
    pdf_t *pdf = NULL;
    pdf = (pdf_t*)malloc(sizeof(pdf_t));
    pdf->doc = NULL;
    pdf->outline = NULL;
    pdf->fileno = -1;
    pdf->pages = NULL;
    pdf->glyph_cache = NULL;
    
    return pdf;
}

/**
 * Parse file into PDF struct.
 * Use filename if it's not null, otherwise use fileno.
 */
pdf_t* parse_pdf_file(const char *filename, int fileno, const char* password) {
    pdf_t *pdf;
    int fd;
    fz_stream *file;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "parse_pdf_file(%s, %d)", filename, fileno);

    pdf = create_pdf_t();
    if (filename) {
        fd = open(filename, O_BINARY | O_RDONLY, 0666);
        if (fd < 0) {
            free(pdf);
            return NULL;
        }
    } else {
        pdf->fileno = dup(fileno);
        fd = pdf->fileno;
    }

    pdf->ctx = fz_new_context(NULL, NULL, 128 << 20);
    fz_try(pdf->ctx)
    {
      file = fz_open_fd(pdf->ctx, fd);
      pdf->doc = pdf_open_document_with_stream(file);
    }
    fz_catch(pdf->ctx)
    {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "got NULL from pdf_openxref");
            pdf_close_document(pdf->doc);
            fz_free_context(pdf->ctx);
        free(pdf);
        return NULL;
    }

    pdf->invalid_password = 0;
    if (pdf_needs_password(pdf->doc)) {
        int authenticated = 0;
        authenticated = pdf_authenticate_password(pdf->doc, (char*)password);
        if (!authenticated) {
            /* TODO: ask for password */
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "failed to authenticate");
            pdf->invalid_password = 1;
            return pdf;
        }
    }

    pdf->outline = pdf_load_outline(pdf->doc);

    {
        int c = 0;
        c = pdf_count_pages(pdf->doc);
        __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page count: %d", c);
    }
    
    pdf->last_pageno = -1;

    return pdf;
}


/**
 * Lazy get-or-load page.
 * Only PDFVIEW_MAX_PAGES_LOADED pages can be loaded at the time.
 * @param pdf pdf struct
 * @param pageno 0-based page number
 * @return pdf_page
 */
pdf_page* get_page(pdf_t *pdf, int pageno) {
    int loaded_pages = 0;
    int pagecount;

    pagecount = pdf_count_pages(pdf->doc);

    if (!pdf->pages) {
        int i;
        pdf->pages = (pdf_page**)malloc(pagecount * sizeof(pdf_page*));
        for(i = 0; i < pagecount; ++i) pdf->pages[i] = NULL;
    }

    if (!pdf->pages[pageno])
    {
        pdf_page *page = NULL;
        int loaded_pages = 0;
        int i = 0;

        for(i = 0; i < pagecount; ++i)
        {
            if (pdf->pages[i]) loaded_pages++;
        }

        fz_try(pdf->ctx)
        {
          page = pdf_load_page(pdf->doc, pageno);
        }
        fz_catch(pdf->ctx)
        {
            __android_log_print(ANDROID_LOG_ERROR, "cx.hell.android.pdfview", "pdf_loadpage -> %d", (int)pdf->ctx->error->top);
            return NULL;
        }

        pdf->pages[pageno] = page;
    }

    return pdf->pages[pageno];
}


/**
 * Get part of page as bitmap.
 * Parameters left, top, width and height are interpreted after scaling, so if we have 100x200 page scaled by 25% and
 * request 0x0 x 25x50 tile, we should get 25x50 bitmap of whole page content.
 * pageno is 0-based.
 */
static jintArray get_page_image_bitmap(JNIEnv *env,
      pdf_t *pdf, int pageno, int zoom_pmil, int left, int top, int rotation,
      int gray, int skipImages,
      int *width, int *height) {
    unsigned char *bytes = NULL;
    fz_matrix ctm;
    double zoom;
    fz_rect bbox;
    pdf_page *page = NULL;
    fz_pixmap *image = NULL;
    static int runs = 0;
    fz_device *dev = NULL;
    int num_pixels;
    jintArray jints; /* return value */
    int *jbuf; /* pointer to internal jint */
    pdf_obj *pageobj;
    pdf_obj *trimobj;
    fz_rect trimbox;

    zoom = (double)zoom_pmil / 1000.0;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "get_page_image_bitmap(pageno: %d) start", (int)pageno);

    if (!pdf->glyph_cache) {
        fz_new_glyph_cache_context(pdf->ctx);
        pdf->glyph_cache = fz_keep_glyph_cache(pdf->ctx);
        if (!pdf->glyph_cache) {
            __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "failed to create glyphcache");
            return NULL;
        }
    }

    page = get_page(pdf, pageno);
    if (!page) return NULL; /* TODO: handle/propagate errors */

    ctm = fz_identity;
    pageobj = pdf->doc->page_objs[pageno];
    trimobj = pdf_dict_gets(pageobj, pdf->box);
    if (trimobj != NULL)
        trimbox = pdf_to_rect(pdf->ctx, trimobj);
    else
        trimbox = page->mediabox;

    ctm = fz_concat(ctm, fz_translate(-trimbox.x0, -trimbox.y1));
    ctm = fz_concat(ctm, fz_scale(zoom, zoom));
    rotation = page->rotate + rotation * -90;
    if (rotation != 0) ctm = fz_concat(ctm, fz_rotate(rotation));
    bbox = fz_transform_rect(ctm, trimbox);

    /* not bbox holds page after transform, but we only need tile at (left,right) from top-left corner */
    bbox.x0 = bbox.x0 + left;
    bbox.y0 = bbox.y0 + top;
    bbox.x1 = bbox.x0 + *width;
    bbox.y1 = bbox.y0 + *height;

    image = fz_new_pixmap(pdf->ctx, gray ? fz_device_gray : fz_device_bgr, *width, *height);
    image->x = bbox.x0;
    image->y = bbox.y0;
    fz_clear_pixmap_with_value(pdf->ctx, image, gray ? 0 : 0xff);
    memset(image->samples, gray ? 0 : 0xff, image->h * image->w * image->n);
    dev = fz_new_draw_device(pdf->ctx, image);

    if (skipImages)
        dev->hints |= FZ_IGNORE_IMAGE;

    fz_try(pdf->ctx)
    {
      pdf_run_page(pdf->doc, page, dev, ctm, 0);
    }
    fz_catch(pdf->ctx)
    {
        /* TODO: cleanup */
        fz_throw(pdf->ctx, "rendering failed");
        return NULL;
    }

    fz_free_device(dev);

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got image %d x %d (x%d), asked for %d x %d",
            (int)(image->w), (int)(image->h), (int)image->n,
            *width, *height);

    /* TODO: learn jni and avoid copying bytes ;) */
    num_pixels = image->w * image->h;

    jints = (*env)->NewIntArray(env, num_pixels);
    jbuf = (*env)->GetIntArrayElements(env, jints, NULL);
    if (gray) {
        copy_alpha((unsigned char*)jbuf, image->samples, image->w, image->h);
    }
    else {
        assert(image->n == 4);
        memcpy(jbuf, image->samples, num_pixels * image->n);
    }
    (*env)->ReleaseIntArrayElements(env, jints, jbuf, 0);

    *width = image->w;
    *height = image->h;
    fz_drop_pixmap(pdf->ctx, image);

    runs += 1;
    return jints;
}


void copy_alpha(unsigned char* out, unsigned char *in, unsigned int w, unsigned int h) {
        unsigned int count = w*h;
        while(count--) {
            out+= 3;
            *out++ = 255-((255-in[0]) * in[1])/255;
            in += 2;
        }
}


/**
 * Get page size in APV's convention.
 * @param page 0-based page number
 * @param pdf pdf struct
 * @param width target for width value
 * @param height target for height value
 * @return error code - 0 means ok
 */
int get_page_size(pdf_t *pdf, int pageno, int *width, int *height) {
    pdf_obj *pageobj = NULL;
    pdf_obj *sizeobj = NULL;
    fz_rect bbox;
    pdf_obj *rotateobj = NULL;
    int rotate = 0;

    pageobj = pdf->doc->page_objs[pageno];
    sizeobj = pdf_dict_gets(pageobj, pdf->box);
    if (sizeobj == NULL)
         sizeobj = pdf_dict_gets(pageobj, "MediaBox");
    rotateobj = pdf_dict_gets(pageobj, "Rotate");
    if (pdf_is_int(rotateobj)) {
        rotate = pdf_to_int(rotateobj);
    } else {
        rotate = 0;
    }
    bbox = pdf_to_rect(pdf->ctx, sizeobj);
    if (rotate != 0 && (rotate % 180) == 90) {
        *width = bbox.y1 - bbox.y0;
        *height = bbox.x1 - bbox.x0;
    } else {
        *width = bbox.x1 - bbox.x0;
        *height = bbox.y1 - bbox.y0;
    }
    return 0;
}

/**
 * Convert coordinates from pdf to APV.
 * Result is stored in location pointed to by bbox param.
 * This function has to get page TrimBox relative to which bbox is located.
 * This function should not allocate any memory.
 * @return error code, 0 means ok
 */
int convert_box_pdf_to_apv(pdf_t *pdf, int page, fz_bbox *bbox) {
    pdf_obj *pageobj = NULL;
    pdf_obj *rotateobj = NULL;
    pdf_obj *sizeobj = NULL;
    fz_rect page_bbox;
    fz_rect param_bbox;
    int rotate = 0;
    float height = 0;
    float width = 0;

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "convert_box_pdf_to_apv(page: %d, bbox: %d %d %d %d)", page, bbox->x0, bbox->y0, bbox->x1, bbox->y1);

    /* copying field by field because param_bbox is fz_rect (floats) and *bbox is fz_bbox (ints) */
    param_bbox.x0 = bbox->x0;
    param_bbox.y0 = bbox->y0;
    param_bbox.x1 = bbox->x1;
    param_bbox.y1 = bbox->y1;

    pageobj = pdf->doc->page_objs[page];
    if (!pageobj) return -1;
    sizeobj = pdf_dict_gets(pageobj, pdf->box);
    if (sizeobj == NULL)
         sizeobj = pdf_dict_gets(pageobj, "MediaBox");
    if (!sizeobj) return -1;
    page_bbox = pdf_to_rect(pdf->ctx, sizeobj);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page bbox is %.1f, %.1f, %.1f, %.1f", page_bbox.x0, page_bbox.y0, page_bbox.x1, page_bbox.y1);
    rotateobj = pdf_dict_gets(pageobj, "Rotate");
    if (pdf_is_int(rotateobj)) {
        rotate = pdf_to_int(rotateobj);
    } else {
        rotate = 0;
    }
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "rotate is %d", (int)rotate);

    if (rotate != 0) {
        fz_matrix m;
        m = fz_rotate(-rotate);
        param_bbox = fz_transform_rect(m, param_bbox);
        page_bbox = fz_transform_rect(m, page_bbox);
    }

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate page bbox is: %.1f, %.1f, %.1f, %.1f", page_bbox.x0, page_bbox.y0, page_bbox.x1, page_bbox.y1);
    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "after rotate param bbox is: %.1f, %.1f, %.1f, %.1f", param_bbox.x0, param_bbox.y0, param_bbox.x1, param_bbox.y1);

    /* set result: param bounding box relative to left-top corner of page bounding box */
    width = fz_abs(page_bbox.x0 - page_bbox.x1);
    height = fz_abs(page_bbox.y0 - page_bbox.y1);

    bbox->x0 = (fz_min(param_bbox.x0, param_bbox.x1) - fz_min(page_bbox.x0, page_bbox.x1));
    bbox->y1 = height - (fz_min(param_bbox.y0, param_bbox.y1) - fz_min(page_bbox.y0, page_bbox.y1));
    bbox->x1 = (fz_max(param_bbox.x0, param_bbox.x1) - fz_min(page_bbox.x0, page_bbox.x1));
    bbox->y0 = height - (fz_max(param_bbox.y0, param_bbox.y1) - fz_min(page_bbox.y0, page_bbox.y1));

    __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "result after transformations: %d, %d, %d, %d", bbox->x0, bbox->y0, bbox->x1, bbox->y1);

    return 0;
}


void pdf_android_loghandler(const char *m) {
    __android_log_print(ANDROID_LOG_DEBUG, "cx.hell.android.pdfview.mupdf", m);
}

// #ifdef pro
// jobject create_outline_recursive(JNIEnv *env, jclass outline_class, const fz_outline *outline) {
//     static int jni_ids_cached = 0;
//     static jmethodID constructor_id = NULL;
//     static jfieldID title_field_id = NULL;
//     static jfieldID page_field_id = NULL;
//     static jfieldID next_field_id = NULL;
//     static jfieldID down_field_id = NULL;
//     int outline_class_found = 0;
//     jobject joutline = NULL;
//     jstring jtitle = NULL;
// 
//     if (outline == NULL) return NULL;
// 
//     if (outline_class == NULL) {
//         outline_class = (*env)->FindClass(env, "cx/hell/android/lib/pdf/PDF$Outline");
//         if (outline_class == NULL) {
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "can't find outline class");
//             return NULL;
//         }
//         outline_class_found = 1;
//     }
// 
//     if (!jni_ids_cached) {
//         constructor_id = (*env)->GetMethodID(env, outline_class, "<init>", "()V");
//         if (constructor_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get method id for Outline constructor");
//             return NULL;
//         }
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got constructor id");
//         title_field_id = (*env)->GetFieldID(env, outline_class, "title", "Ljava/lang/String;");
//         if (title_field_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get field id for Outline.title");
//             return NULL;
//         }
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got title field id");
//         page_field_id = (*env)->GetFieldID(env, outline_class, "page", "I");
//         if (page_field_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get field id for Outline.page");
//             return NULL;
//         }
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got page field id");
//         next_field_id = (*env)->GetFieldID(env, outline_class, "next", "Lcx/hell/android/lib/pdf/PDF$Outline;");
//         if (next_field_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get field id for Outline.next");
//             return NULL;
//         }
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "got down field id");
//         down_field_id = (*env)->GetFieldID(env, outline_class, "down", "Lcx/hell/android/lib/pdf/PDF$Outline;");
//         if (down_field_id == NULL) {
//             (*env)->DeleteLocalRef(env, outline_class);
//             __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "create_outline_recursive: couldn't get field id for Outline.down");
//             return NULL;
//         }
// 
//         jni_ids_cached = 1;
//     }
// 
//     joutline = (*env)->NewObject(env, outline_class, constructor_id);
//     if (joutline == NULL) {
//         (*env)->DeleteLocalRef(env, outline_class);
//         __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "failed to create joutline");
//         return NULL;
//     }
//     // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "joutline created");
//     if (outline->title) {
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "title to set: %s", outline->title);
//         jtitle = (*env)->NewStringUTF(env, outline->title);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "jtitle created");
//         (*env)->SetObjectField(env, joutline, title_field_id, jtitle);
//         (*env)->DeleteLocalRef(env, jtitle);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "title set");
//     } else {
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "title is null, won't create not set");
//     }
// 
//     if (outline->dest.kind == FZ_LINK_GOTO)
//     {
//         (*env)->SetIntField(env, joutline, page_field_id, outline->dest.ld.gotor.page);
//     }
// 
//     // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "page set");
//     if (outline->next) {
//         jobject next_outline = NULL;
//         next_outline = create_outline_recursive(env, outline_class, outline->next);
//         (*env)->SetObjectField(env, joutline, next_field_id, next_outline);
//         (*env)->DeleteLocalRef(env, next_outline);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "next set");
//     }
//     if (outline->down) {
//         jobject down_outline = NULL;
//         down_outline = create_outline_recursive(env, outline_class, outline->down);
//         (*env)->SetObjectField(env, joutline, down_field_id, down_outline);
//         (*env)->DeleteLocalRef(env, down_outline);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "down set");
//     }
// 
//     if (outline_class_found) {
//         (*env)->DeleteLocalRef(env, outline_class);
//         // __android_log_print(ANDROID_LOG_DEBUG, PDFVIEW_LOG_TAG, "local ref deleted");
//     }
// 
//     return joutline;
// }
// #endif

/**
 * Extract text from given pdf page.
 */
char* extract_text(pdf_t *pdf, int pageno) {

    fz_device *dev = NULL;
    fz_text_sheet *text_sheet = NULL;
    fz_text_page *text_page = NULL;
    fz_text_span *text_span = NULL, *ln = NULL;

    pdf_page *page = NULL;
    fz_rect page_rect;
    fz_matrix ctm;

    int text_len = 0;
    char *text = NULL; /* utf-8 text */
    int i = 0;

    if (pdf == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, PDFVIEW_LOG_TAG, "extract_text: pdf is NULL");
        return NULL;
    }

    page = get_page(pdf, pageno);

    ctm = fz_identity;
    page_rect = fz_bound_page(pdf->doc, page);
    page_rect = fz_transform_rect(ctm, page_rect);

    fz_try(pdf->ctx)
    {
        text_sheet = fz_new_text_sheet(pdf->ctx);
        text_page = fz_new_text_page(pdf->ctx, page_rect);
        dev = fz_new_text_device(pdf->ctx, text_sheet, text_page);
        fz_run_page(pdf->doc, page, dev, ctm, NULL);
        fz_free_device(dev); dev = 0;
        print_text_page(pdf->ctx, text_page, &text);
    }
    fz_always(pdf->ctx)
    {
        fz_free_device(dev);
        fz_free_text_page(pdf->ctx, text_page);
        fz_free_text_sheet(pdf->ctx, text_sheet);
    }
    fz_catch(pdf->ctx)
    {
        return NULL;
    }

    return text;
}
