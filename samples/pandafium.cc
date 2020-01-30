// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "public/fpdf_dataavail.h"
#include "public/fpdf_edit.h"
#include "public/fpdf_ext.h"
#include "public/fpdf_formfill.h"
#include "public/fpdf_text.h"
#include "public/fpdfview.h"

#include "testing/test_support.h"
#include "jpeglib.h"

#define START_PAGE_NUMBER 1

// Hold a map of the currently loaded pages in order to avoid them
// to get loaded twice.
std::map<int, FPDF_PAGE> g_loadedPages;

// Hold a global pointer of FPDF_FORMHANDLE so that PDFium
// app hooks can made use of it.
FPDF_FORMHANDLE g_formHandle;

struct Options {
  Options() : output_pattern("%s.%d"), first_page(-1), last_page(-1) {}
  std::string scale_factor_as_string;
  std::string output_pattern;
  int first_page;
  int last_page;
};

static bool CheckDimensions(int stride, int width, int height) {
  if (stride < 0 || width < 0 || height < 0)
    return false;
  if (height > 0 && width > INT_MAX / height)
    return false;
  return true;
}

static std::string outputFileName(std::string pattern, const char* pdf_name, int pagenumber, const char* ext){
  char buffer[256];
  size_t spos = pattern.find("%s");
  size_t dpos = pattern.find("%d");
  std::string with_ext = pattern + ".%s";
  size_t formatted_chars;
  if (spos == std::string::npos){ // Didn't use input filename in format string
    formatted_chars = snprintf(buffer, sizeof(buffer), with_ext.c_str(),
                               pagenumber + START_PAGE_NUMBER, ext);
  }else{
    if (spos < dpos){
      formatted_chars = snprintf(buffer, sizeof(buffer), with_ext.c_str(), pdf_name,
                                 pagenumber + START_PAGE_NUMBER, ext);
    }else{
      formatted_chars = snprintf(buffer, sizeof(buffer), with_ext.c_str(),
                                 pagenumber + START_PAGE_NUMBER, pdf_name, ext);
    }
  }
  if(formatted_chars > sizeof(buffer)){
    fprintf(stderr, "Filname %s is too long\n", buffer);
  }
  return std::string(buffer);
}

void ExampleUnsupportedHandler(UNSUPPORT_INFO*, int type) {
  std::string feature = "Unknown";
  switch (type) {
    case FPDF_UNSP_DOC_XFAFORM:
      feature = "XFA";
      break;
    case FPDF_UNSP_DOC_PORTABLECOLLECTION:
      feature = "Portfolios_Packages";
      break;
    case FPDF_UNSP_DOC_ATTACHMENT:
    case FPDF_UNSP_ANNOT_ATTACHMENT:
      feature = "Attachment";
      break;
    case FPDF_UNSP_DOC_SECURITY:
      feature = "Rights_Management";
      break;
    case FPDF_UNSP_DOC_SHAREDREVIEW:
      feature = "Shared_Review";
      break;
    case FPDF_UNSP_DOC_SHAREDFORM_ACROBAT:
    case FPDF_UNSP_DOC_SHAREDFORM_FILESYSTEM:
    case FPDF_UNSP_DOC_SHAREDFORM_EMAIL:
      feature = "Shared_Form";
      break;
    case FPDF_UNSP_ANNOT_3DANNOT:
      feature = "3D";
      break;
    case FPDF_UNSP_ANNOT_MOVIE:
      feature = "Movie";
      break;
    case FPDF_UNSP_ANNOT_SOUND:
      feature = "Sound";
      break;
    case FPDF_UNSP_ANNOT_SCREEN_MEDIA:
    case FPDF_UNSP_ANNOT_SCREEN_RICHMEDIA:
      feature = "Screen";
      break;
    case FPDF_UNSP_ANNOT_SIG:
      feature = "Digital_Signature";
      break;
  }
  printf("Unsupported feature: %s.\n", feature.c_str());
}

bool ParseCommandLine(const std::vector<std::string>& args,
                      Options* options,
                      std::vector<std::string>* files) {
  if (args.empty()) {
    return false;
  }

  size_t cur_idx = 1;
  for (; cur_idx < args.size(); ++cur_idx) {
    const std::string& cur_arg = args[cur_idx];
    if (cur_arg.size() > 8 && cur_arg.compare(0, 8, "--scale=") == 0) {
      if (!options->scale_factor_as_string.empty()) {
        fprintf(stderr, "Duplicate --scale argument\n");
        return false;
      }
      options->scale_factor_as_string = cur_arg.substr(8);
    } else if (cur_arg.size() > 9 && cur_arg.compare(0, 9, "--output=") == 0) {
      options->output_pattern = cur_arg.substr(9);
    } else if (cur_arg.size() > 6 && cur_arg.compare(0, 6, "--res=") == 0) {
      if (!options->scale_factor_as_string.empty()) {
        fprintf(stderr, "Duplicate --res or --scale argument\n");
        return false;
      }
      double res = std::stod(cur_arg.substr(6));
      double scale = res/72.0;
      options->scale_factor_as_string = std::to_string(scale);
    } else if (cur_arg.size() > 8 && cur_arg.compare(0, 8, "--first=") == 0) {

      int first = std::stoi(cur_arg.substr(8));
      options->first_page = first;
    } else if (cur_arg.size() > 7 && cur_arg.compare(0, 7, "--last=") == 0) {

      int last = std::stoi(cur_arg.substr(7));
      options->last_page = last;
    } else if (cur_arg.size() >= 2 && cur_arg[0] == '-' && cur_arg[1] == '-') {
      fprintf(stderr, "Unrecognized argument %s\n", cur_arg.c_str());
      return false;
    } else {
      break;
    }
  }

  for (size_t i = cur_idx; i < args.size(); i++) {
    files->push_back(args[i]);
  }

  return true;
}

FPDF_BOOL Is_Data_Avail(FX_FILEAVAIL* pThis, size_t offset, size_t size) {
  return true;
}

void Add_Segment(FX_DOWNLOADHINTS* pThis, size_t offset, size_t size) {
}

FPDF_PAGE GetPageForIndex(FPDF_FORMFILLINFO* param,
                          FPDF_DOCUMENT doc,
                          int index) {
  auto iter = g_loadedPages.find(index);
  if (iter != g_loadedPages.end())
    return iter->second;

  FPDF_PAGE page = FPDF_LoadPage(doc, index);
  if (!page)
    return nullptr;

  FORM_OnAfterLoadPage(page, g_formHandle);
  FORM_DoPageAAction(page, g_formHandle, FPDFPAGE_AACTION_OPEN);

  g_loadedPages[index] = page;
  return page;
}

static void WriteJpg(const char* pdf_name, int num, const void* buffer_void,
                     int stride, int width, int height, std::string output_pattern) {
  const char* buffer = reinterpret_cast<const char*>(buffer_void);

  if (!CheckDimensions(stride, width, height))
    return;

  int out_len = width * height;
  if (out_len > INT_MAX / 3)
    return;
  out_len *= 3;

  std::string filename = outputFileName(output_pattern, pdf_name, num, "jpg");
  FILE* fp = fopen(filename.c_str(), "wb");
  if (!fp)
    return;

  JSAMPLE * image_buffer = (JSAMPLE *)buffer;  /* Points to large array of R,G,B-order data */
  int image_height =  height;        /* Number of rows in image */
  int image_width = width;         /* Number of columns in image */

  struct jpeg_compress_struct cinfo;

  struct jpeg_error_mgr jerr;
  /* More stuff */
  JSAMPROW row_pointer[1];      /* pointer to JSAMPLE row[s] */
  int row_stride;               /* physical row width in image buffer */

  cinfo.err = jpeg_std_error(&jerr);
  /* Now we can initialize the JPEG compression object. */
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, fp);
  // Source data is B, G, R, unused.
  // Dest data is R, G, B.
  cinfo.image_width = image_width;      /* image width and height, in pixels */
  cinfo.image_height = image_height;
  cinfo.input_components = 4;           /* # of color components per pixel */
  cinfo.in_color_space = JCS_EXT_BGRX;       /* colorspace of input image */
  /* Now use the library's routine to set default compression parameters.
   * (You must set at least cinfo.in_color_space before calling this,
   * since the defaults depend on the source color space.)
   */
  jpeg_set_defaults(&cinfo);

  jpeg_start_compress(&cinfo, TRUE);

  row_stride = image_width * 4; /* JSAMPLEs per row in image_buffer */

  while (cinfo.next_scanline < cinfo.image_height) {
      /* jpeg_write_scanlines expects an array of pointers to scanlines.
       * Here the array is only one element long, but you could pass
       * more than one scanline at a time if that's more convenient.
       */
      row_pointer[0] = & image_buffer[cinfo.next_scanline * row_stride];
      (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  /* Step 6: Finish compression */

  jpeg_finish_compress(&cinfo);

  fclose(fp);
  jpeg_destroy_compress(&cinfo);
}

bool RenderPage(const std::string& name,
                FPDF_DOCUMENT doc,
                FPDF_FORMHANDLE& form,
                const int page_index,
                const Options& options) {
  FPDF_PAGE page = GetPageForIndex(nullptr, doc, page_index);
  if (!page)
    return false;

  FPDF_TEXTPAGE text_page = FPDFText_LoadPage(page);

  double scale = 1.0;
  if (!options.scale_factor_as_string.empty())
    std::stringstream(options.scale_factor_as_string) >> scale;

  int width = static_cast<int>(FPDF_GetPageWidth(page) * scale);
  int height = static_cast<int>(FPDF_GetPageHeight(page) * scale);
  int alpha = FPDFPage_HasTransparency(page) ? 1 : 0;
  FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, alpha);
  if (bitmap) {
    FPDF_DWORD fill_color = alpha ? 0x00000000 : 0xFFFFFFFF;
    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, fill_color);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, 0, 0);

    FPDF_FFLDraw(form, bitmap, page, 0, 0, width, height, 0, 0);
    int stride = FPDFBitmap_GetStride(bitmap);
    const char* buffer = reinterpret_cast<const char*>(FPDFBitmap_GetBuffer(bitmap));

    WriteJpg(name.c_str(), page_index, buffer, stride, width, height, options.output_pattern);

    FPDFBitmap_Destroy(bitmap);
  } else {
    fprintf(stderr, "Page was too large to be rendered.\n");
  }

  g_loadedPages.erase(page_index);
  FORM_DoPageAAction(page, form, FPDFPAGE_AACTION_CLOSE);
  FORM_OnBeforeClosePage(page, form);
  FPDFText_ClosePage(text_page);
  FPDF_ClosePage(page);
  return !!bitmap;
}

void RenderPdf(const std::string& name,
               const char* pBuf,
               size_t len,
               const Options& options) {
  IPDF_JSPLATFORM platform_callbacks;
  memset(&platform_callbacks, '\0', sizeof(platform_callbacks));
  platform_callbacks.version = 3;

  FPDF_FORMFILLINFO form_callbacks;
  memset(&form_callbacks, '\0', sizeof(form_callbacks));
  form_callbacks.version = 1;
  form_callbacks.FFI_GetPage = GetPageForIndex;
  form_callbacks.m_pJsPlatform = &platform_callbacks;

  TestLoader loader(pBuf, len);
  FPDF_FILEACCESS file_access;
  memset(&file_access, '\0', sizeof(file_access));
  file_access.m_FileLen = static_cast<unsigned long>(len);
  file_access.m_GetBlock = TestLoader::GetBlock;
  file_access.m_Param = &loader;

  FX_FILEAVAIL file_avail;
  memset(&file_avail, '\0', sizeof(file_avail));
  file_avail.version = 1;
  file_avail.IsDataAvail = Is_Data_Avail;

  FX_DOWNLOADHINTS hints;
  memset(&hints, '\0', sizeof(hints));
  hints.version = 1;
  hints.AddSegment = Add_Segment;

  FPDF_DOCUMENT doc;
  int nRet = PDF_DATA_NOTAVAIL;
  bool bIsLinearized = false;
  FPDF_AVAIL pdf_avail = FPDFAvail_Create(&file_avail, &file_access);

  if (FPDFAvail_IsLinearized(pdf_avail) == PDF_LINEARIZED) {
    doc = FPDFAvail_GetDocument(pdf_avail, nullptr);
    if (doc) {
      while (nRet == PDF_DATA_NOTAVAIL)
        nRet = FPDFAvail_IsDocAvail(pdf_avail, &hints);

      if (nRet == PDF_DATA_ERROR) {
        fprintf(stderr, "Unknown error in checking if doc was available.\n");
        return;
      }
      nRet = FPDFAvail_IsFormAvail(pdf_avail, &hints);
      if (nRet == PDF_FORM_ERROR || nRet == PDF_FORM_NOTAVAIL) {
        fprintf(stderr,
                "Error %d was returned in checking if form was available.\n",
                nRet);
        return;
      }
      bIsLinearized = true;
    }
  } else {
    doc = FPDF_LoadCustomDocument(&file_access, nullptr);
  }

  if (!doc) {
    unsigned long err = FPDF_GetLastError();
    fprintf(stderr, "Load pdf docs unsuccessful: ");
    switch (err) {
      case FPDF_ERR_SUCCESS:
        fprintf(stderr, "Success");
        break;
      case FPDF_ERR_UNKNOWN:
        fprintf(stderr, "Unknown error");
        break;
      case FPDF_ERR_FILE:
        fprintf(stderr, "File not found or could not be opened");
        break;
      case FPDF_ERR_FORMAT:
        fprintf(stderr, "File not in PDF format or corrupted");
        break;
      case FPDF_ERR_PASSWORD:
        fprintf(stderr, "Password required or incorrect password");
        break;
      case FPDF_ERR_SECURITY:
        fprintf(stderr, "Unsupported security scheme");
        break;
      case FPDF_ERR_PAGE:
        fprintf(stderr, "Page not found or content error");
        break;
      default:
        fprintf(stderr, "Unknown error %ld", err);
    }
    fprintf(stderr, ".\n");

    FPDFAvail_Destroy(pdf_avail);
    return;
  }

  (void)FPDF_GetDocPermissions(doc);

  FPDF_FORMHANDLE form = g_formHandle =
      FPDFDOC_InitFormFillEnvironment(doc, &form_callbacks);
  FPDF_SetFormFieldHighlightColor(form, 0, 0xFFE4DD);
  FPDF_SetFormFieldHighlightAlpha(form, 100);

  FORM_DoDocumentJSAction(form);
  FORM_DoDocumentOpenAction(form);

  int page_count = FPDF_GetPageCount(doc);
  int rendered_pages = 0;
  int bad_pages = 0;
  int first_page = 1;
  int last_page = page_count;
  if (options.first_page > 0 and options.first_page <= page_count){
    first_page = options.first_page;
  }
  if (options.last_page > 0 and options.last_page <= page_count) {
    last_page = options.last_page;
  }
  for (int i = first_page - 1; i < last_page; ++i) {
    if (bIsLinearized) {
      nRet = PDF_DATA_NOTAVAIL;
      while (nRet == PDF_DATA_NOTAVAIL)
        nRet = FPDFAvail_IsPageAvail(pdf_avail, i, &hints);

      if (nRet == PDF_DATA_ERROR) {
        fprintf(stderr, "Unknown error in checking if page %d is available.\n",
                i);
        return;
      }
    }
    if (RenderPage(name, doc, form, i, options))
      ++rendered_pages;
    else
      ++bad_pages;
  }

  FORM_DoDocumentAAction(form, FPDFDOC_AACTION_WC);

  FPDFDOC_ExitFormFillEnvironment(form);
  FPDF_CloseDocument(doc);

  FPDFAvail_Destroy(pdf_avail);

  fprintf(stderr, "Rendered %d pages.\n", rendered_pages);
  if (bad_pages)
    fprintf(stderr, "Skipped %d bad pages.\n", bad_pages);
}

static const char kUsageString[] =
    "Usage: pandafium [OPTION] [FILE]...\n"
    "  --output=<path>   - output file pattern (no extension)\n"
    "                      use %s for filename or %d for page number\n"
    "  --scale=<number>  - scale output size by number (e.g. 0.5)\n"
    "  --res=<number>    - output dpi (scale=res/72.0)\n"
    "  --first=<number>  - first page to convert (1-based)\n"
    "  --last=<number>   - last page to convert\n"
    "";

int main(int argc, const char* argv[]) {
  std::vector<std::string> args(argv, argv + argc);
  Options options;
  std::vector<std::string> files;
  if (!ParseCommandLine(args, &options, &files)) {
    fprintf(stderr, "%s", kUsageString);
    return 1;
  }

  if (files.empty()) {
    fprintf(stderr, "No input files.\n");
    return 1;
  }

  FPDF_LIBRARY_CONFIG config;
  config.version = 2;
  config.m_pUserFontPaths = nullptr;
  config.m_pIsolate = nullptr;
  config.m_v8EmbedderSlot = 0;

  FPDF_InitLibraryWithConfig(&config);

  UNSUPPORT_INFO unsuppored_info;
  memset(&unsuppored_info, '\0', sizeof(unsuppored_info));
  unsuppored_info.version = 1;
  unsuppored_info.FSDK_UnSupport_Handler = ExampleUnsupportedHandler;

  FSDK_SetUnSpObjProcessHandler(&unsuppored_info);

  for (const std::string& filename : files) {
    size_t file_length = 0;
    std::unique_ptr<char, pdfium::FreeDeleter> file_contents =
        GetFileContents(filename.c_str(), &file_length);
    if (!file_contents)
      continue;
    fprintf(stderr, "Rendering PDF file %s.\n", filename.c_str());
    RenderPdf(filename, file_contents.get(), file_length, options);
  }

  FPDF_DestroyLibrary();

  return 0;
}
