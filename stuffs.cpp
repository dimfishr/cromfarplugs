#include "StdAfx.h"
#include "stuffs.h"

void MakeDirs(CString path)
{
  LPTSTR DestPath = path.GetBuffer(0);

  for (LPTSTR ChPtr = DestPath; *ChPtr != 0; ChPtr++)
  {
    if (*ChPtr == _T('\\'))
    {
      *ChPtr = 0;
      CreateDirectory(DestPath, nullptr);
      *ChPtr = _T('\\');
    }
  }
  CreateDirectory(DestPath, nullptr);
  path.ReleaseBuffer();
}

bool FileExists(const CString& path)
{
  return (GetFileAttributes(path) != 0xFFFFFFFF) ? true : false;
}

DWORD GetFileSizeS(const CString& path)
{
  HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE)
  {
    DWORD ret = GetFileSize(h, nullptr);
    CloseHandle(h);
    return ret;
  }
  return 0;
}

CString GetVersionString()
{
  CString ver;
  ver.Format(_T("FARDroid %u.%u.%u"), MAJORVERSION, MINORVERSION, BUILDNUMBER);
  return ver;
}

bool WriteLine(HANDLE stream, const CString& line, int CodePage)
{
#ifdef _UNICODE
  if (stream != INVALID_HANDLE_VALUE)
  {
    DWORD wr = 0;
    switch (CodePage)
    {
    case CodePage_Unicode:
      return (WriteFile(stream, static_cast<LPCTSTR>(line), line.GetAllocLength(), &wr, nullptr) && wr == line.GetAllocLength());
    case CodePage_OEM:
      {
        char* oem = getOemString(line);
        int len = lstrlenA(oem);
        HRESULT res = WriteFile(stream, oem, len, &wr, nullptr);
        my_free(oem);
        return res && wr == len;
      }
    case CodePage_ANSI:
      {
        char* ansi = getAnsiString(line);
        int len = lstrlenA(ansi);
        HRESULT res = WriteFile(stream, ansi, len, &wr, nullptr);
        my_free(ansi);
        return res && wr == len;
      }
    }
  }
#else
	#error TODO!!!
#endif
  return false;
}

bool CheckForKey(const int key)
{
  bool ExitCode = false;
  while (1)
  {
    INPUT_RECORD rec;
    HANDLE hConInp = GetStdHandle(STD_INPUT_HANDLE);
    DWORD ReadCount;
    PeekConsoleInput(hConInp, &rec, 1, &ReadCount);
    if (ReadCount == 0)
      break;
    ReadConsoleInput(hConInp, &rec, 1, &ReadCount);
    if (rec.EventType == KEY_EVENT)
      if (rec.Event.KeyEvent.wVirtualKeyCode == key &&
        rec.Event.KeyEvent.bKeyDown)
        ExitCode = true;
  }
  return ExitCode;
}

int rnd(int maxRnd)
{
  //сделаем более случайный выбор
  int rnds[10];
  for (int i = 0; i < 10; i++)
    rnds[i] = rand() % maxRnd;

  return (rnds[rand() % 10]);
}

bool CheckAndConvertParh(bool another, CString& res, bool selected, int i)
{
  res = GetPanelPath(another);
  AddEndSlash(res);
  res += GetFileName(another, selected, i);

  return true;
}

bool IsDirectory(DWORD attr)
{
  return (attr & FILE_ATTRIBUTE_DIRECTORY) ? true : false;
}

bool IsDirectory(uintptr_t attr)
{
  return (attr & FILE_ATTRIBUTE_DIRECTORY) ? true : false;
}

bool IsLink(DWORD attr)
{
  return (attr & FILE_ATTRIBUTE_REPARSE_POINT) ? true : false;
}

bool IsDevice(DWORD attr)
{
  return (attr & FILE_ATTRIBUTE_DEVICE) ? true : false;
}

bool IsDirectory(bool another, bool selected, int i)
{
  DWORD attr = GetFileAttributes(another, selected, i);

  return (attr & FILE_ATTRIBUTE_DIRECTORY) ? true : false;
}

bool IsDirectory(LPCTSTR sPath)
{
  DWORD attr = GetFileAttributes(sPath);

  return ((attr != -1) && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? true : false;
}

void PrepareInfoLine(const wchar_t* str, void* ansi, CString& line, CString format)
{
  line.Format(format, str, ansi);
}

void PrepareInfoLineDate(const wchar_t* str, time_t* time, CString& line, bool b64)
{
  CString date;
  if (b64)
    date = _wctime(time);
  else
    date = _wctime32(reinterpret_cast<__time32_t*>(time));

  line.Format(_T("%s%s"), str, (date && *date) ? date : _T("\n"));
}

time_t StringTimeToUnixTime(CString sData, CString sTime)
{
  SYSTEMTIME time = {0};
  strvec a;
  Tokenize(sData, a, _T("-"), false);
  if (a.GetSize() == 3)
  {
    time.wYear = _ttoi(a[0]);
    time.wMonth = _ttoi(a[1]);
    time.wDay = _ttoi(a[2]);
  }
  a.RemoveAll();
  Tokenize(sTime, a, _T(":"), false);
  if (a.GetSize() == 2)
  {
    time.wHour = _ttoi(a[0]);
    time.wMinute = _ttoi(a[1]);
  }

  time_t t = 0;
  SystemTimeToUnixTime(&time, &t);
  return t;
}

time_t StringTimeToUnixTime(CString sDay, CString sMonth, CString sYear, CString sTime)
{
  SYSTEMTIME time = {0};
  time.wYear = _ttoi(sYear);
  time.wDay = _ttoi(sDay);

  if (sMonth.CompareNoCase(_T("Jan")) == 0)
    time.wMonth = 1;
  else if (sMonth.CompareNoCase(_T("Feb")) == 0)
    time.wMonth = 2;
  else if (sMonth.CompareNoCase(_T("Mar")) == 0)
    time.wMonth = 3;
  else if (sMonth.CompareNoCase(_T("Apr")) == 0)
    time.wMonth = 4;
  else if (sMonth.CompareNoCase(_T("May")) == 0)
    time.wMonth = 5;
  else if (sMonth.CompareNoCase(_T("Jun")) == 0)
    time.wMonth = 6;
  else if (sMonth.CompareNoCase(_T("Jul")) == 0)
    time.wMonth = 7;
  else if (sMonth.CompareNoCase(_T("Aug")) == 0)
    time.wMonth = 8;
  else if (sMonth.CompareNoCase(_T("Sep")) == 0)
    time.wMonth = 9;
  else if (sMonth.CompareNoCase(_T("Oct")) == 0)
    time.wMonth = 10;
  else if (sMonth.CompareNoCase(_T("Nov")) == 0)
    time.wMonth = 11;
  else if (sMonth.CompareNoCase(_T("Dec")) == 0)
    time.wMonth = 12;

  strvec a;
  Tokenize(sTime, a, _T(":"), false);
  if (a.GetSize() == 3)
  {
    time.wHour = _ttoi(a[0]);
    time.wMinute = _ttoi(a[1]);
    time.wSecond = _ttoi(a[2]);
  }

  time_t t = 0;
  SystemTimeToUnixTime(&time, &t);
  return t;
}

time_t* SystemTimeToUnixTime(LPSYSTEMTIME pst, time_t* pt)
{
  FILETIME ft;
  SystemTimeToFileTime(pst, &ft);
  FileTimeToUnixTime(&ft, pt);
  return pt;
}

CString SystemTimeToString(LPSYSTEMTIME pst)
{
  CString str;
  str.Format(_T("%0d.%0d.%04d %0d:%0d:%0d"), pst->wDay, pst->wMonth, pst->wYear, pst->wHour, pst->wMinute, pst->wSecond);
  return str;
}

FILETIME UnixTimeToFileTime(time_t time)
{
  FILETIME ft = {0};
  UINT64 i64 = time;
  i64 *= 10000000;
  i64 += 116444736000000000;
  ft.dwLowDateTime = static_cast<DWORD>(i64);
  ft.dwHighDateTime = static_cast<DWORD>(i64 >> 32);
  return ft;
}

void FileTimeToUnixTime(LPFILETIME pft, time_t* pt)
{
  LONGLONG ll; // 64 bit value 
  ll = (static_cast<LONGLONG>(pft->dwHighDateTime) << 32) + pft->dwLowDateTime;
  *pt = static_cast<time_t>((ll - 116444736000000000ui64) / 10000000ui64);
}

DWORD StringToAttr(CString sAttr)
{
  if (sAttr.GetLength() != 10)
    return FILE_ATTRIBUTE_OFFLINE;
  return
    ((sAttr[0] == _T('d')) ? FILE_ATTRIBUTE_DIRECTORY : 0) | //directory flag
    ((sAttr[0] == _T('l')) ? FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT : 0) | //link
    ((sAttr[0] == _T('-')) ? FILE_ATTRIBUTE_ARCHIVE : 0) | //file
    ((sAttr[0] == _T('c')) ? FILE_ATTRIBUTE_DEVICE : 0) | //symbol device
    ((sAttr[0] == _T('b')) ? FILE_ATTRIBUTE_DEVICE : 0) | //block device
    ((sAttr[0] == _T('p')) ? FILE_ATTRIBUTE_READONLY : 0) | //FIFO device
    ((sAttr[0] == _T('s')) ? FILE_ATTRIBUTE_DEVICE : 0) | //socket device
    ((sAttr[5] != _T('w')) ? FILE_ATTRIBUTE_READONLY : 0);//writable flag
}

DWORD ModeToAttr(int mode)
{
  return
    (IS_FLAG(mode, S_IFDIR) ? FILE_ATTRIBUTE_DIRECTORY : 0) | //directory flag
    (IS_FLAG(mode, S_IFLNK) ? FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT : 0) | //link
    (IS_FLAG(mode, S_IFREG) ? FILE_ATTRIBUTE_ARCHIVE : 0) | //file
    (IS_FLAG(mode, S_IFCHR) ? FILE_ATTRIBUTE_DEVICE : 0) | //symbol device
    (IS_FLAG(mode, S_IFBLK) ? FILE_ATTRIBUTE_DEVICE : 0) | //block device
    (IS_FLAG(mode, S_IFIFO) ? FILE_ATTRIBUTE_READONLY : 0) | //FIFO device
    (IS_FLAG(mode, S_IFSOCK) ? FILE_ATTRIBUTE_DEVICE : 0) | //socket device
    (!IS_FLAG(mode, S_IWRITE) ? FILE_ATTRIBUTE_READONLY : 0);//writable flag
}

#ifdef USELOGGING
void CPerfCounter::Log( LPCTSTR lpMsg )
{
	CString s;
	SYSTEMTIME st;
	GetSystemTime(&st);
	s.Format(_T("%s: %s"), SystemTimeToString(&st), (lpMsg == NULL)?lastStr:lpMsg);
	OutputDebugString(lpMsg);

	HANDLE hFile; 
	CString path;
	GetTempPath(1024, path.GetBuffer(1024));
	path.ReleaseBuffer();
	AddEndSlash(path);
	path += _T("fardroid.log");
	hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != INVALID_HANDLE_VALUE) 
	{ 
		SetFilePointer(hFile, 0, NULL, FILE_END);
		WriteLine(hFile, s, CodePage_ANSI);
		CloseHandle(hFile);
	}
}
#endif

BOOL DeleteDir(LPCTSTR sSrc)
{
  CString sDir = sSrc;
  AddEndSlash(sDir);

  WIN32_FIND_DATA fd;
  HANDLE h = FindFirstFile(sDir + _T("*.*"), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return FALSE;

  auto result = TRUE;
  CString sname;
  do
  {
    if (lstrcmp(fd.cFileName, _T(".")) == 0 || lstrcmp(fd.cFileName, _T("..")) == 0)
      continue;

    sname.Format(_T("%s%s"), sDir, fd.cFileName);
    if (IsDirectory(fd.dwFileAttributes))
    {
      if (!DeleteDir(sname))
      {
        result = FALSE;
        break;
      }
    }
    else
    {
      if (!DeleteFile(sname))
      {
        result = FALSE;
        break;
      }
    }
  } while (FindNextFile(h, &fd) != 0);

  FindClose(h);

  if (result)
    result = RemoveDirectory(sSrc);
  return result;
}

BOOL DeletePanelItems(CString& sPath, struct PluginPanelItem* PanelItem, int ItemsNumber)
{
  BOOL result = TRUE;
  AddEndSlash(sPath);
  CString sName;

  for (auto i = 0; i < ItemsNumber; i++)
  {
    sName.Format(_T("%s%s"), sPath, PanelItem[i].FileName);
    if (IsDirectory(PanelItem[i].FileAttributes))
    {
      if (!DeleteDir(sName))
      {
        result = FALSE;
        break;
      }
    }
    else
    {
      if (!DeleteFile(sName))
      {
        result = FALSE;
        break;
      }
    }
  }

  return result;
}