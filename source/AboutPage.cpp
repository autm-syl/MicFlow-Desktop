#include "AboutPage.h"

//void ShowAboutPage(HWND hWndParent) {
//    const wchar_t* title = L"Về MicFlow";
//    const wchar_t* content =
//        L"MicFlow - Phiên bản 1.0.2\n"
//        L"Phát triển bởi: [Tên của bạn]\n\n"
//        L"--- TÍNH NĂNG ---\n"
//        L"\u2022 Truyền âm thanh chất lượng cao từ Phone sang PC.\n"
//        L"\u2022 Hỗ trợ VB-Cable làm đầu vào hệ thống.\n"
//        L"\u2022 Tự động ẩn khay hệ thống (System Tray).\n\n"
//        L"--- CÁCH SỬ DỤNG ---\n"
//        L"1. Đảm bảo điện thoại và PC cùng mạng Wi-Fi.\n"
//        L"2. Nhấn 'Start' trên PC trước khi truyền từ Phone.\n"
//        L"3. Chọn 'CABLE Input' trong phần cài đặt Micro của Discord/Zoom.";
//
//    MessageBoxW(hWndParent, content, title, MB_OK | MB_ICONINFORMATION);
//}

INT_PTR CALLBACK AboutDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static HBITMAP hBmp1 = NULL;
    static HBITMAP hBmp2 = NULL;

    switch (message) {
    case WM_INITDIALOG: {
        HINSTANCE hInst = (HINSTANCE)lParam;

        // Nạp 2 ảnh từ Resource
        hBmp1 = (HBITMAP)LoadImage(hInst, MAKEINTRESOURCE(IDB_GUIDE_1), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
        hBmp2 = (HBITMAP)LoadImage(hInst, MAKEINTRESOURCE(IDB_GUIDE_2), IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);

        // Hiển thị lên 2 khung ảnh tương ứng
        SendDlgItemMessage(hDlg, IDC_STATIC_IMG_1, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp1);
        SendDlgItemMessage(hDlg, IDC_STATIC_IMG_2, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp2);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    case WM_DESTROY:
        if (hBmp1) DeleteObject(hBmp1);
        if (hBmp2) DeleteObject(hBmp2);
        break;
    }
    return (INT_PTR)FALSE;
}

void ShowAboutPage(HWND hWndParent, HINSTANCE hInstance) {
    DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_GUIDE_DIALOG), hWndParent, AboutDialogProc, (LPARAM)hInstance);
}