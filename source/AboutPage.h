#ifndef ABOUTPAGE_H
#define ABOUTPAGE_H

#include <windows.h>

// ID cho mục menu About
#define ID_MENU_ABOUT       2001

// ID cho Dialog
#define IDD_GUIDE_DIALOG    3001

// ID cho các khung hiển thị ảnh (Static Controls)
#define IDC_STATIC_IMG_1    3002
#define IDC_STATIC_IMG_2    3003

// ID cho các tệp ảnh Bitmap (.bmp)
#define IDB_GUIDE_1         4001
#define IDB_GUIDE_2         4002

// Hàm hiển thị trang About
void ShowAboutPage(HWND hWndParent, HINSTANCE hInstance);

#endif 
// Đảm bảo có ít nhất một dòng trống bên dưới dòng này


