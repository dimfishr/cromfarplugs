#include "StdAfx.h"
#include "fardroid.h"
#include <vector>
#include <ctime>
#include "framebuffer.h"

DWORD WINAPI ProcessThreadProc(LPVOID lpParam)
{
  auto android = static_cast<fardroid *>(lpParam);
  if (!android)
    return 0;

  while (true)
  {
    if (CheckForKey(VK_ESCAPE) && android->BreakProcessDialog()) 
      android->m_bForceBreak = true;

    if (android->m_bForceBreak)
      break;

    android->ShowProgressMessage();
    Sleep(100);
  }

  return 0;
}


fardroid::fardroid(void)
{
  m_currentPath = _T("");
  m_currentDevice = _T("");
  InfoPanelLineArray = nullptr;
  lastError = S_OK;
  m_bForceBreak = false;
}

fardroid::~fardroid(void)
{
  if (InfoPanelLineArray)
    delete [] InfoPanelLineArray;

  DeleteRecords(records);
}

bool fardroid::CopyFilesDialog(CString& dest, const wchar_t* title)
{
  const auto width = 55;
  struct InitDialogItem InitItems[] = {
    /*00*/FDI_DOUBLEBOX(width - 4, 6, (farStr *)title),
    /*01*/FDI_LABEL(5, 2, (farStr *)MCopyDest),
    /*02*/FDI_EDIT(5, 3, width - 6, _F("fardroidDestinationDir")),
    /*03*/FDI_DEFCBUTTON(5,(farStr *)MOk),
    /*04*/FDI_CBUTTON(5,(farStr *)MCancel),
    /*--*/FDI_SEPARATOR(4,_F("")),
  };
  const int size = sizeof InitItems / sizeof InitItems[0];

  FarDialogItem DialogItemsC[size];
  InitDialogItems(InitItems, DialogItemsC, size);

  wchar_t* editbuf = static_cast<wchar_t *>(my_malloc(1024));
  lstrcpyW(editbuf, _C(dest));
  DialogItemsC[2].Data = editbuf;
  HANDLE hdlg;

  bool res = ShowDialog(width, 8, _F("CopyDialog"), DialogItemsC, size, hdlg) == 3;
  if (res)
    dest = GetItemData(hdlg, 2);

  fInfo.DialogFree(hdlg);
  my_free(editbuf);

  return res;
}

bool fardroid::CreateDirDialog(CString& dest)
{
  const auto width = 55;
  struct InitDialogItem InitItems[] = {
    /*00*/FDI_DOUBLEBOX(width - 4, 6,(farStr *)MCreateDir),
    /*01*/FDI_LABEL(5, 2, (farStr *)MDirName),
    /*02*/FDI_EDIT(5, 3,width - 6, _F("fardroidDirName")),
    /*03*/FDI_DEFCBUTTON(5,(farStr *)MOk),
    /*04*/FDI_CBUTTON(5,(farStr *)MCancel),
    /*--*/FDI_SEPARATOR(4,_F("")),
  };
  const int size = sizeof InitItems / sizeof InitItems[0];

  FarDialogItem DialogItems[size];
  InitDialogItems(InitItems, DialogItems, size);

  wchar_t* editbuf = static_cast<wchar_t *>(my_malloc(1024));
  lstrcpyW(editbuf, _C(dest));
  DialogItems[2].Data = editbuf;
  HANDLE hdlg;

  bool res = ShowDialog(width, 8, _F("CreateDirDialog"), DialogItems, size, hdlg) == 3;
  if (res)
    dest = GetItemData(hdlg, 2);

  fInfo.DialogFree(hdlg);
  my_free(editbuf);

  return res;
}

HANDLE fardroid::OpenFromMainMenu()
{
  fileUnderCursor = ExtractName(GetCurrentFileName());

  if (DeviceTest())
  {
    if (conf.RemountSystem)
    {
      CString sRes;
      ADB_mount(_T("/system"), TRUE, sRes, false);
    }

    CString lastPath;
    conf.GetSub(0, _T("devices"), m_currentDevice, lastPath, _T("/"));
    if (ChangeDir(lastPath))
      return static_cast<HANDLE>(this);

    return INVALID_HANDLE_VALUE;
  }
  return INVALID_HANDLE_VALUE;
}

bool fardroid::DeleteFilesDialog()
{
  CString msg;
  msg.Format(L"%s\n%s\n%s\n%s", LOC(MDeleteTitle), LOC(MDeleteWarn), LOC(MYes), LOC(MNo));

  return ShowMessage(msg, 2, nullptr, true) == 0;
}

bool fardroid::BreakProcessDialog()
{
  if (m_procStruct.Hide())
  {
    CString msg;
    msg.Format(L"%s\n%s\n%s\n%s", m_procStruct.title, LOC(MBreakWarn), LOC(MYes), LOC(MNo));

    bool bOk = ShowMessage(msg, 2, nullptr, true) == 0;
    m_procStruct.Restore();

    return bOk;
  }
  return false;
}

int fardroid::FileExistsDialog(LPCTSTR sName)
{
  if (m_procStruct.Hide())
  {
    CString msg;
    CString msgexists;
    msg.Format(L"%s\n%s\n%s\n%s\n%s\n%s\n%s", LOC(MGetFile), LOC(MCopyWarnIfExists), LOC(MYes), LOC(MNo), LOC(MAlwaysYes), LOC(MAlwaysNo), LOC(MCancel));
    msgexists.Format(msg, sName);

    int res = ShowMessage(msgexists, 5, _F("warnifexists"), true);

    m_procStruct.Restore();
    return res;
  }
  return 4;
}

int fardroid::CopyErrorDialog(LPCTSTR sTitle, LPCTSTR sErr)
{
  if (m_procStruct.Hide())
  {
    CString errmsg;
    errmsg.Format(_T("%s\n%s\n\n%s\n\n%s\n%s\n%s"), sTitle, LOC(MCopyError), sErr, LOC(MYes), LOC(MNo), LOC(MCancel));
    int ret = ShowMessage(errmsg, 3, _F("copyerror"), true);

    m_procStruct.Restore();
    return ret;
  }
  return 2;
}

void fardroid::ShowError(CString& error)
{
  CString msg;
  error.TrimRight();
  msg.Format(L"%s\n%s\n%s", LOC(MError), error, LOC(MOk));
  ShowMessage(msg, 1, nullptr, true);
}

HANDLE fardroid::OpenFromCommandLine(const CString& cmd)
{
  if (!DeviceTest())
    return INVALID_HANDLE_VALUE;

  CString sRes;
  if (conf.RemountSystem)
    ADB_mount(_T("/system"), TRUE, sRes, false);

  strvec tokens;
  Tokenize(cmd, tokens, _T(" -"));

  if (tokens.GetSize() == 0)
    return OpenFromMainMenu();

  TokensToParams(tokens, params);

  CString dir = GetParam(params, _T("filename"));
  bool havefile = !dir.IsEmpty();

  if (ExistsParam(params, _T("remount")))
  {
    CString fs = _T("/system");
    if (havefile) fs = dir;
    CString par = GetParam(params, _T("remount"));
    if (par.IsEmpty()) par = _T("rw");
    par.MakeLower();

    ADB_mount(fs, par == _T("rw"), sRes, false);
    return INVALID_HANDLE_VALUE;
  }

  if (havefile)
  {
    fileUnderCursor = ExtractName(GetCurrentFileName());

    DelEndSlash(dir, true);
    if (ChangeDir(dir))
      return static_cast<HANDLE>(this);
    return OpenFromMainMenu();
  }
  return INVALID_HANDLE_VALUE;
}

int fardroid::GetItems(PluginPanelItem* PanelItem, int ItemsNumber, const CString& srcdir, const CString& dstdir, bool noPromt, bool ansYes, bool bSilent)
{
  CString sdir = srcdir;
  CString ddir = dstdir;
  AddEndSlash(sdir, true);
  AddEndSlash(ddir);

  CString sname;
  CString dname;
  CCopyRecords files;
  for (auto i = 0; i < ItemsNumber; i++)
  {
    if (m_bForceBreak)
      return ABORT;

    sname.Format(_T("%s%s"), sdir, PanelItem[i].FileName);
    dname.Format(_T("%s%s"), ddir, PanelItem[i].FileName);

    if (IsDirectory(PanelItem[i].FileAttributes))
    {
      ADBPullDirGetFiles(sname, dname, files);
    }
    else
    {
      auto rec = new CCopyRecord;
      rec->src = sname;
      rec->dst = dname;
      rec->size = PanelItem[i].FileSize;
      files.Add(rec);
    }
  }

  UINT64 totalSize = 0;
  int filesSize = files.GetSize();
  for (auto i = 0; i < files.GetSize(); i++)
    totalSize += files[i]->size;

  if (m_procStruct.Lock())
  {
    m_procStruct.nTotalStartTime = GetTickCount();
    m_procStruct.nTotalFileSize = totalSize;
    m_procStruct.nTotalFiles = filesSize;
    m_procStruct.Unlock();
  }

  int result = TRUE;
  for (auto i = 0; i < filesSize; i++)
  {
    if (m_bForceBreak)
      break;

    if (m_procStruct.Lock())
    {
      m_procStruct.nStartTime = GetTickCount();
      m_procStruct.nPosition = i;
      m_procStruct.from = files[i]->src;
      m_procStruct.to = files[i]->dst;
      m_procStruct.nTransmitted = 0;
      m_procStruct.nFileSize = files[i]->size;
      m_procStruct.Unlock();
    }

    if (FileExists(files[i]->dst))
    {
      if (!noPromt)
      {
        auto exResult = FileExistsDialog(files[i]->dst);
        if (exResult < 0 || exResult > 3)
        {
          m_bForceBreak = true;
          break;
        }

        ansYes = exResult == 0 || exResult == 2;
        noPromt = exResult == 2 || exResult == 3;
      }

      if (!ansYes)
      {
        if (m_procStruct.Lock())
        {
          m_procStruct.nTotalFileSize -= m_procStruct.nFileSize;
          m_procStruct.Unlock();
        }
        continue;
      }

      if (!DeleteFile(files[i]->dst, false))
      {
        result = FALSE;
        break;
      }
    }

    if (!CopyFileFrom(files[i]->src, files[i]->dst, bSilent))
    {
      result = FALSE;
      break;
    }
  }

  DeleteRecords(files);

  if (m_bForceBreak)
    return ABORT;

  return result;
}

int fardroid::PutItems(PluginPanelItem* PanelItem, int ItemsNumber, const CString& srcdir, const CString& dstdir, bool noPromt, bool ansYes, bool bSilent)
{
  CString sdir = srcdir;
  CString ddir = dstdir;
  AddEndSlash(sdir);
  AddEndSlash(ddir, true);

  CString sname;
  CString dname;
  CCopyRecords files;
  for (auto i = 0; i < ItemsNumber; i++)
  {
    if (m_bForceBreak)
      return ABORT;

    sname.Format(_T("%s%s"), sdir, PanelItem[i].FileName);
    dname.Format(_T("%s%s"), ddir, PanelItem[i].FileName);

    if (IsDirectory(PanelItem[i].FileAttributes))
    {
      ADBPushDirGetFiles(sname, dname, files);
    }
    else
    {
      auto rec = new CCopyRecord;
      rec->src = sname;
      rec->dst = dname;
      rec->size = PanelItem[i].FileSize;
      files.Add(rec);
    }
  }

  UINT64 totalSize = 0;
  int filesSize = files.GetSize();
  for (auto i = 0; i < files.GetSize(); i++)
    totalSize += files[i]->size;

  if (m_procStruct.Lock())
  {
    m_procStruct.nTotalStartTime = GetTickCount();
    m_procStruct.nTotalFileSize = totalSize;
    m_procStruct.nTotalFiles = filesSize;
    m_procStruct.Unlock();
  }

  auto result = TRUE;
  for (auto i = 0; i < filesSize; i++)
  {
    if (m_bForceBreak)
      break;

    if (m_procStruct.Lock())
    {
      m_procStruct.nStartTime = GetTickCount();
      m_procStruct.nPosition = i;
      m_procStruct.from = files[i]->src;
      m_procStruct.to = files[i]->dst;
      m_procStruct.nTransmitted = 0;
      m_procStruct.nFileSize = files[i]->size;
      m_procStruct.Unlock();
    }

    CString permissions = GetPermissionsFile(files[i]->dst);
    if (!permissions.IsEmpty())
    {
      if (!noPromt)
      {
        auto exResult = FileExistsDialog(files[i]->dst);
        if (exResult < 0 || exResult > 3)
        {
          m_bForceBreak = true;
          break;
        }

        ansYes = exResult == 0 || exResult == 2;
        noPromt = exResult == 2 || exResult == 3;
      }

      if (!ansYes)
      {
        if (m_procStruct.Lock())
        {
          m_procStruct.nTotalFileSize -= m_procStruct.nFileSize;
          m_procStruct.Unlock();
        }
        continue;
      }
    }

    if (!CopyFileTo(files[i]->src, files[i]->dst, permissions, bSilent))
    {
      result = FALSE;
      break;
    }
  }

  DeleteRecords(files);
  Reread();

  if (m_bForceBreak)
    return ABORT;

  return result;
}

bool fardroid::DeleteFile(const CString& name, bool bSilent)
{
  if (name.IsEmpty())
    return false;

  CString msg;
  CString msgfail;
  msg.Format(L"%s\n%s", LOC(MDeleteTitle), LOC(MCopyDeleteError));
  msgfail.Format(msg, name, LOC(MRetry), LOC(MCancel));
deltry:
  if (!::DeleteFile(name))
  {
    if (bSilent)
      return false;

    if (ShowMessage(msgfail, 2, L"delerror", true) == 0)
      goto deltry;

    return false;
  }

  return true;
}

bool fardroid::CopyFileFrom(const CString& src, const CString& dst, bool bSilent)
{
repeatcopy:
  // "adb.exe pull" в принципе не может читать файл с устройства с правами Superuser.
  // Если у файла не установлены права доступа на чтения для простых пользователей,
  // то файл не будет прочитан. 

  // Чтобы такие файлы все же прочитать, добавим к правам доступа файла, право на чтение
  // для простых пользователей. А затем вернем оригинальные права доступа к файлу.
  // Этот работает только в Native Mode с включенным Superuser, при включенном ExtendedAccess

  CString old_permissions;
  bool UseChmod = (conf.WorkMode == WORKMODE_NATIVE && conf.UseSU && conf.UseExtendedAccess);
  if (UseChmod)
  {
    UseChmod = false;
    old_permissions = GetPermissionsFile(src);
    // Проверяем, имеет ли уже файл разрешение r для группы Others
    if (!old_permissions.IsEmpty() && old_permissions.GetAt(old_permissions.GetLength() - 3) != _T('r'))
    {
      CString new_permissions = old_permissions;
      // Добавляем к правам доступа файла право на чтение для всех пользователей.
      new_permissions.SetAt(new_permissions.GetLength() - 3, _T('r'));
      SetPermissionsFile(src, new_permissions);
      UseChmod = true;
    }
  }

  // Читаем файл
  CString sRes;
  BOOL res = ADB_pull(src, dst, sRes, bSilent);

  // Восстановим оригинальные права на файл
  if (UseChmod)
    SetPermissionsFile(src, old_permissions);

  if (!res)
  {
    // Silent предполагает не задавать пользователю лишних вопросов. 
    //        Но сообщения об ошибках нужно все же выводить.
    //   if (bSilent)//если отключен вывод на экран, то просто возвращаем что все ОК
    //	    return true;

    if (conf.WorkMode == WORKMODE_NATIVE)
    {
      if (!sRes.IsEmpty()) sRes += _T("\n");
      sRes += (conf.UseSU) ? LOC(MNeedFolderExePerm) : LOC(MNeedSuperuserPerm);
    }
    else
    {
      if (!sRes.IsEmpty()) sRes += _T("\n");
      sRes += LOC(MNeedNativeSuperuserPerm);
    }

    int ret = CopyErrorDialog(LOC(MGetFile), sRes);
    switch (ret)
    {
    case 0:
      sRes.Empty();
      goto repeatcopy;
    case 1:
      return true;
    case 2:
      return false;
    }
  }

  return true;
}

bool fardroid::CopyFileTo(const CString& src, const CString& dst, const CString& old_permissions, bool bSilent)
{
repeatcopy:
  // "adb.exe push" в принципе не может перезаписывать файл в устройстве с правами Superuser.
  // Если у файла не установлены прав доступа на запись для простых пользователей,
  // то файл не будет перезаписан. (т.е. например? после редактирования файл нельзя будет сохранить)

  // Чтобы такие файлы все же перезаписать, добавим к правам доступа файла, право на запись
  // для простых пользователей. А затем вернем оригинальные права доступа к файлу.
  // Этот работает только в Native Mode с включенным Superuser, при включенном ExtendedAccess

  bool UseChmod = (conf.WorkMode == WORKMODE_NATIVE && conf.UseSU && conf.UseExtendedAccess);
  if (UseChmod)
  {
    UseChmod = false;
    // Проверяем, имеет ли уже файл разрешение w для группы Others
    if (!old_permissions.IsEmpty() && old_permissions.GetAt(old_permissions.GetLength() - 2) != _T('w'))
    {
      CString new_permissions = old_permissions;
      // Добавляем к правам доступа файла право на запись для всех пользователей.	
      new_permissions.SetAt(new_permissions.GetLength() - 2, _T('w'));
      SetPermissionsFile(dst, new_permissions);
      UseChmod = true;
    }
  }

  // Запись файла
  CString sRes;
  BOOL res = ADB_push(src, dst, sRes, bSilent);

  // Восстановим оригинальные права на файл
  if (UseChmod)
    SetPermissionsFile(dst, old_permissions);

  if (!res)
  {
    // Silent предполагает не задавать пользователю лишних вопросов. 
    //        Но сообщения об ошибках нужно все же выводить.
    //  if (bSilent) //если отключен вывод на экран, то просто возвращаем что все ОК
    //	   return true;

    if (conf.WorkMode == WORKMODE_NATIVE)
    {
      if (!sRes.IsEmpty()) sRes += _T("\n");
      sRes += (conf.UseSU) ? LOC(MNeedFolderExePerm) : LOC(MNeedSuperuserPerm);
    }
    else
    {
      if (!sRes.IsEmpty()) sRes += _T("\n");
      sRes += LOC(MNeedNativeSuperuserPerm);
    }

    int ret = CopyErrorDialog(LOC(MPutFile), sRes);
    switch (ret)
    {
    case 0:
      sRes = _T("");
      goto repeatcopy;
    case 1:
      return true;
    case 2:
      return false;
    }
  }

  return true;
}

bool fardroid::DeleteFileFrom(const CString& src, bool bSilent)
{
  CString sRes;
  BOOL res = ADB_rm(src, sRes, bSilent);
  if (!res)
  {
    if (bSilent)//если отключен вывод на экран, то просто возвращаем что все ОК
      return true;

    return true;
  }

  return true;
}

void fardroid::DeleteRecords(CFileRecords& recs)
{
  for (auto i = 0; i < recs.GetSize(); i++)
  {
    delete recs[i];
  }
  recs.RemoveAll();
}

void fardroid::DeleteRecords(CCopyRecords& recs)
{
  for (auto i = 0; i < recs.GetSize(); i++)
  {
    delete recs[i];
  }
  recs.RemoveAll();
}

int fardroid::GetFindData(struct PluginPanelItem** pPanelItem, size_t* pItemsNumber, OPERATION_MODES OpMode)
{
  *pPanelItem = nullptr;
  *pItemsNumber = 0;

  int items = records.GetSize();

  PluginPanelItem* NewPanelItem = static_cast<PluginPanelItem *>(my_malloc(sizeof(PluginPanelItem) * items));
  *pPanelItem = NewPanelItem;

  if (NewPanelItem == nullptr)
    return FALSE;

  CFileRecord* item;

  for (int Z = 0; Z < items; Z++)
  {
    my_memset(&NewPanelItem[Z], 0, sizeof(PluginPanelItem));

    item = records[Z];
    NewPanelItem[Z].FileAttributes = item->attr;
    NewPanelItem[Z].UserData.Data = reinterpret_cast<void *>(Z);
    NewPanelItem[Z].FileSize = item->size;
    NewPanelItem[Z].LastWriteTime = UnixTimeToFileTime(item->time);

    NewPanelItem[Z].FileName = my_strdupW(item->filename);
    NewPanelItem[Z].Owner = my_strdupW(item->owner);
    NewPanelItem[Z].Description = my_strdupW(item->desc);

    //3 custom columns
    //NewPanelItem[Z].CustomColumnData=(farStr**)my_malloc(sizeof(farStr*)*2);
    NewPanelItem[Z].CustomColumnNumber = 0;
  }
  *pItemsNumber = items;

  return TRUE;
}

void fardroid::FreeFindData(struct PluginPanelItem* PanelItem, int ItemsNumber)
{
  for (int I = 0; I < ItemsNumber; I++)
  {
    if (PanelItem[I].FileName)
      my_free((void*)PanelItem[I].FileName);
    if (PanelItem[I].Owner)
      my_free((void*)PanelItem[I].Owner);
    if (PanelItem[I].Description)
      my_free((void*)PanelItem[I].Description);
  }
  if (PanelItem)
  {
    my_free(PanelItem);
    PanelItem = nullptr;
  }
}

int fardroid::GetFiles(PluginPanelItem* PanelItem, int ItemsNumber, CString& DestPath, BOOL Move, OPERATION_MODES OpMode)
{
  CString srcdir = m_currentPath;
  AddBeginSlash(srcdir);
  AddEndSlash(srcdir, true);

  CString path = DestPath;

  bool bSilent = IS_FLAG(OpMode, OPM_SILENT) || IS_FLAG(OpMode, OPM_FIND);
  bool noPromt = bSilent;

  if (IS_FLAG(OpMode, OPM_VIEW) || IS_FLAG(OpMode, OPM_QUICKVIEW) || IS_FLAG(OpMode, OPM_EDIT))
    bSilent = false;

  if (!bSilent && !IS_FLAG(OpMode, OPM_QUICKVIEW) && !IS_FLAG(OpMode, OPM_VIEW) && !IS_FLAG(OpMode, OPM_EDIT))
  {
    if (!CopyFilesDialog(DestPath, Move ? LOC(MMoveFile) : LOC(MGetFile)))
      return ABORT;
  }

  m_bForceBreak = false;
  if (m_procStruct.Lock())
  {
    m_procStruct.title = Move ? LOC(MMoveFile) : LOC(MGetFile);
    m_procStruct.pType = Move ? PS_MOVE : PS_COPY;
    m_procStruct.bSilent = false;
    m_procStruct.nTransmitted = 0;
    m_procStruct.nTotalTransmitted = 0;
    m_procStruct.nFileSize = 0;
    m_procStruct.nTotalFileSize = 0;
    m_procStruct.Unlock();
  }

  DWORD threadID = 0;
  HANDLE hThread = CreateThread(nullptr, 0, ProcessThreadProc, this, 0, &threadID);

  auto result = GetItems(PanelItem, ItemsNumber, srcdir, DestPath, noPromt, noPromt, bSilent);

  m_bForceBreak = true;
  CloseHandle(hThread);
  taskbarIcon.SetState(taskbarIcon.S_NO_PROGRESS);

  return result;
}

int fardroid::UpdateInfoLines()
{
  //version first
  lines.RemoveAll();
  CPanelLine pl;
  pl.text = GetVersionString();
  pl.separator = TRUE;

  lines.Add(pl);

#ifdef USELOGGING
	CPerfCounter counter;
	counter.Start(_T("Get Memory Info"));
#endif

  GetMemoryInfo();

#ifdef USELOGGING
	counter.Stop(NULL);
	counter.Start(_T("Get Partitions Info"));
#endif

  GetPartitionsInfo();

  return lines.GetSize();
}

void fardroid::PreparePanel(OpenPanelInfo* Info)
{
  panelTitle.Format(_T("%s/%s"), m_currentDevice, m_currentPath);

  Info->HostFile = _C(fileUnderCursor);
  Info->PanelTitle = _C(panelTitle);
  Info->CurDir = _C(panelTitle);

  if (InfoPanelLineArray)
  {
    delete InfoPanelLineArray;
    InfoPanelLineArray = nullptr;
  }

  auto len = lines.GetSize();
  if (len > 0)
  {
    InfoPanelLineArray = new InfoPanelLine[len];
    for (auto i = 0; i < len; i++)
    {
      InfoPanelLineArray[i].Text = lines[i].text;
      InfoPanelLineArray[i].Data = lines[i].data;
      InfoPanelLineArray[i].Flags = lines[i].separator ? IPLFLAGS_SEPARATOR : 0;
    }
  }

  Info->InfoLines = InfoPanelLineArray;
  Info->InfoLinesNumber = len;
}

void fardroid::ChangePermissionsDialog()
{
  if (conf.WorkMode != WORKMODE_NATIVE)
  {
    CString msg;
    msg.Format(L"%s\n%s\n%s", LOC(MWarningTitle), LOC(MOnlyNative), LOC(MOk));
    ShowMessage(msg, 1, nullptr, true);
    return;
  }

  CString CurrentDir = GetPanelPath(false);
  CString CurrentFileName = GetCurrentFileName(false);
  CString CurrentFullFileName = CString(_T("/")) + CurrentDir + _T("/") + CurrentFileName;

  CString permissions = GetPermissionsFile(CurrentFullFileName);
  if (permissions.IsEmpty())
    return;

  const auto width = 45;
  struct InitDialogItem InitItems[] = {
    /*00*/FDI_DOUBLEBOX (width - 4, 11, (farStr *)MPermTitle),
    /*01*/FDI_LABEL ( 5, 2, (farStr *)MPermFileName),
    /*02*/FDI_LABEL ( 5, 3, (farStr *)MPermFileAttr),
    /*03*/FDI_LABEL ( 14, 5, _T("R   W   X")),
    /*04*/FDI_LABEL ( 5, 6, _T("Owner")),
    /*05*/FDI_LABEL ( 5, 7, _T("Group")),
    /*06*/FDI_LABEL ( 5, 8, _T("Others")),
    /*07*/FDI_CHECK ( 13, 6, _T("") ),
    /*08*/FDI_CHECK ( 13, 7, _T("") ),
    /*09*/FDI_CHECK ( 13, 8, _T("") ),
    /*10*/FDI_CHECK ( 17, 6, _T("") ),
    /*11*/FDI_CHECK ( 17, 7, _T("") ),
    /*12*/FDI_CHECK ( 17, 8, _T("") ),
    /*13*/FDI_CHECK ( 21, 6, _T("") ),
    /*14*/FDI_CHECK ( 21, 7, _T("") ),
    /*15*/FDI_CHECK ( 21, 8, _T("") ),
    /*16*/FDI_DEFCBUTTON (10, (farStr *)MOk),
    /*17*/FDI_CBUTTON (10,(farStr *)MCancel),
    /*--*/FDI_SEPARATOR(4,_F("")),
    /*--*/FDI_SEPARATOR(9,_F("")),
  };
  const int size = sizeof InitItems / sizeof InitItems[0];

  FarDialogItem DialogItems[size];
  InitDialogItems(InitItems, DialogItems, size);

  CString LabelTxt1 = LOC(MPermFileName) + CurrentFileName;
  DialogItems[1].Data = LabelTxt1;
  CString LabelTxt2 = LOC(MPermFileAttr) + permissions;
  DialogItems[2].Data = LabelTxt2;

  DialogItems[7].Selected = (permissions[1] != _T('-'));
  DialogItems[10].Selected = (permissions[2] != _T('-'));
  DialogItems[13].Selected = (permissions[3] != _T('-'));

  DialogItems[8].Selected = (permissions[4] != _T('-'));
  DialogItems[11].Selected = (permissions[5] != _T('-'));
  DialogItems[14].Selected = (permissions[6] != _T('-'));

  DialogItems[9].Selected = (permissions[7] != _T('-'));
  DialogItems[12].Selected = (permissions[8] != _T('-'));
  DialogItems[15].Selected = (permissions[9] != _T('-'));

  HANDLE hdlg;

  int res = ShowDialog(width, 13, nullptr, DialogItems, size, hdlg);
  if (res == 16)
  {
    permissions.SetAt(1, GetItemSelected(hdlg, 7) ? _T('r') : _T('-'));
    permissions.SetAt(2, GetItemSelected(hdlg, 10) ? _T('w') : _T('-'));
    permissions.SetAt(3, GetItemSelected(hdlg, 13) ? _T('x') : _T('-'));

    permissions.SetAt(4, GetItemSelected(hdlg, 8) ? _T('r') : _T('-'));
    permissions.SetAt(5, GetItemSelected(hdlg, 11) ? _T('w') : _T('-'));
    permissions.SetAt(6, GetItemSelected(hdlg, 14) ? _T('x') : _T('-'));

    permissions.SetAt(7, GetItemSelected(hdlg, 9) ? _T('r') : _T('-'));
    permissions.SetAt(8, GetItemSelected(hdlg, 12) ? _T('w') : _T('-'));
    permissions.SetAt(9, GetItemSelected(hdlg, 15) ? _T('x') : _T('-'));

    SetPermissionsFile(CurrentFullFileName, permissions);

    CString permissions_chk = GetPermissionsFile(CurrentFullFileName);

    if (permissions_chk != permissions)
    {
      CString msg;
      msg.Format(L"%s\n%s\n%s", LOC(MWarningTitle), LOC(MSetPermFail), LOC(MOk));
      ShowMessage(msg, 1, nullptr, true);
    }
  }
  fInfo.DialogFree(hdlg);
}

CString fardroid::GetDeviceName(CString& device)
{
  strvec name;
  Tokenize(device, name, _T("\t"));
  return name[0];
}

bool fardroid::DeviceMenu(CString& text)
{
  strvec devices;
  std::vector<FarMenuItem> items;

  Tokenize(text, devices, _T("\n"));

  auto size = devices.GetSize();

  if (size == 0)
  {
    return false;
  }
  if (size == 1)
  {
    m_currentDevice = GetDeviceName(devices[0]);
    return true;
  }

  for (auto i = 0; i < devices.GetSize(); i++)
  {
    FarMenuItem item;
    ::ZeroMemory(&item, sizeof(item));
    SetItemText(&item, GetDeviceName(devices[i]));
    items.push_back(item);
  }

  int res = ShowMenu(LOC(MSelectDevice), _F(""), _F(""), items.data(), items.size());
  if (res >= 0)
  {
    m_currentDevice = GetDeviceName(devices[res]);
    return true;
  }

  return false;
}

void fardroid::SetItemText(FarMenuItem* item, const CString& text)
{
  size_t len = text.GetLength() + 1;
  wchar_t* buf = new wchar_t[len];
  wcscpy(buf, text);
  delete[] item->Text;
  item->Text = buf;
}

void fardroid::Reread()
{
  CString p = m_currentPath;
  AddBeginSlash(p);
  ChangeDir(p);
}

int fardroid::ChangeDir(LPCTSTR sDir, OPERATION_MODES OpMode)
{
  CString s = sDir;
  CFileRecord* item = GetFileRecord(sDir);
  if (s != _T("..") && item && IsLink(item->attr) && (!conf.LinksAsDir() && OpMode == 0))
    s = item->linkto;

  CString tempPath;
  int i = s.Find(_T("/"));
  int j = s.Find(_T("\\"));
  if (i != -1 || j != -1)//перемещение с помощью команды cd
  {
    if (s[0] == _T('/') || s[0] == _T('\\')) //абсолютный путь
      tempPath = s;
    else //относительный путь в глубь иерархии (пока отбрасываем всякие ..\path. TODO!!!)
      tempPath.Format(_T("/%s%s%s"), m_currentPath, m_currentPath.IsEmpty() ? _T("") : _T("/"), s);
  }
  if (i == -1 && j == -1)//простое относительное перемещение
  {
    if (s == _T(".."))
      tempPath = ExtractPath(m_currentPath);
    else
      tempPath.Format(_T("/%s%s%s"), m_currentPath, m_currentPath.IsEmpty() ? _T("") : _T("/"), s);
  }

  AddBeginSlash(tempPath);

  if (OpenPanel(tempPath))
  {
    m_currentPath = tempPath;
    conf.SetSub(0, _T("devices"), m_currentDevice, m_currentPath);
    if (!m_currentPath.IsEmpty())
      m_currentPath.Delete(0);
    return TRUE;
  }

  if (OpMode != 0 || lastError != S_OK)
    return FALSE;

  tempPath = m_currentPath;
  AddBeginSlash(tempPath);

  if (OpenPanel(tempPath))
  {
    m_currentPath = tempPath;
    conf.SetSub(0, _T("devices"), m_currentDevice, m_currentPath);
    if (!m_currentPath.IsEmpty())
      m_currentPath.Delete(0);
    return TRUE;
  }

  return FALSE;
}

void fardroid::ADBSyncQuit(SOCKET sockADB)
{
  syncmsg msg;

  msg.req.id = ID_QUIT;
  msg.req.namelen = 0;

  SendADBPacket(sockADB, &msg.req, sizeof(msg.req));
}

bool fardroid::ADBReadMode(SOCKET sockADB, LPCTSTR path, int& mode)
{
  syncmsg msg;
  CString file = WtoUTF8(path);
  int len = lstrlen(file);

  msg.req.id = ID_STAT;
  msg.req.namelen = len;

  bool bOk = false;
  if (SendADBPacket(sockADB, &msg.req, sizeof(msg.req)))
  {
    char* buf = getAnsiString(file);
    bOk = SendADBPacket(sockADB, buf, len);
    my_free(buf);
  }

  if ((ReadADBPacket(sockADB, &msg.stat, sizeof(msg.stat)) <= 0) ||
    (msg.stat.id != ID_STAT))
    bOk = false;

  mode = msg.stat.mode;
  return bOk;
}

bool fardroid::ADBTransmitFile(SOCKET sockADB, LPCTSTR sFileName, time_t& mtime)
{
  HANDLE hFile = CreateFile(sFileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (hFile == INVALID_HANDLE_VALUE)
    return false;

  FILETIME ft;
  SYSTEMTIME st;
  GetFileTime(hFile, nullptr, nullptr, &ft);
  FileTimeToSystemTime(&ft, &st);
  SystemTimeToUnixTime(&st, &mtime);

  syncsendbuf* sbuf = new syncsendbuf;
  sbuf->id = ID_DATA;

  auto result = true;
  while (true)
  {
    DWORD readed = 0;
    if (!ReadFile(hFile, sbuf->data, SYNC_DATA_MAX, &readed, nullptr))
    {
      result = false;
      break;
    }

    if (readed == 0)
      break;

    sbuf->size = readed;
    if (!SendADBPacket(sockADB, sbuf, sizeof(unsigned) * 2 + readed))
    {
      result = false;
      break;
    }

    if (m_procStruct.Lock())
    {
      m_procStruct.nTransmitted += readed;
      m_procStruct.nTotalTransmitted += readed;
      m_procStruct.Unlock();
    }

    if (m_bForceBreak)
      break;
  }

  delete sbuf;
  CloseHandle(hFile);

  return result;
}

bool fardroid::ADBSendFile(SOCKET sockADB, LPCTSTR sSrc, LPCTSTR sDst, CString& sRes, int mode)
{
  syncmsg msg;
  int len;

  CString sData;
  sData.Format(_T("%s,%d"), sDst, mode);

  len = lstrlen(sDst);
  if (len > 1024) return false;

  msg.req.id = ID_SEND;
  msg.req.namelen = sData.GetLength();

  if (SendADBPacket(sockADB, &msg.req, sizeof(msg.req)))
  {
    char* buf = getAnsiString(sData);
    bool bOK = SendADBPacket(sockADB, buf, sData.GetLength());
    my_free(buf);
    if (!bOK) return false;
  }

  time_t mtime;
  if (!ADBTransmitFile(sockADB, sSrc, mtime))
    return false;

  msg.data.id = ID_DONE;
  msg.data.size = static_cast<unsigned>(mtime);
  if (!SendADBPacket(sockADB, &msg.data, sizeof(msg.data)))
    return false;

  if (ReadADBPacket(sockADB, &msg.status, sizeof(msg.status)) <= 0)
    return false;

  if (msg.status.id != ID_OKAY)
  {
    if (msg.status.id == ID_FAIL)
    {
      len = msg.status.msglen;
      if (len > 256) len = 256;
      char* buf = new char[257];
      if (ReadADBPacket(sockADB, buf, len))
      {
        delete[] buf;
        return false;
      }
      buf[len] = 0;
      sRes = buf;
    }
    else
      sRes = _T("unknown reason");

    return false;
  }

  return true;
}

bool fardroid::ADBPushDir(SOCKET sockADB, LPCTSTR sSrc, LPCTSTR sDst, CString& sRes)
{
  if (!ADB_mkdir(sDst, sRes, true))
    return false;

  CString ddir = sDst;
  CString sdir = sSrc;
  AddEndSlash(sdir);
  AddEndSlash(ddir, true);
  CString ssaved = sdir;
  CString dsaved = ddir;

  sdir += _T("*.*");

  WIN32_FIND_DATA fd;
  HANDLE h = FindFirstFile(sdir, &fd);
  if (h == INVALID_HANDLE_VALUE) 
    return false;

  CString sname, dname;
  do {
    if (m_bForceBreak)
      break;

    if (lstrcmp(fd.cFileName, _T(".")) == 0 || lstrcmp(fd.cFileName, _T("..")) == 0)
      continue;

    sname.Format(_T("%s%s"), ssaved, fd.cFileName);
    dname.Format(_T("%s%s"), dsaved, fd.cFileName);
    if (IsDirectory(fd.dwFileAttributes))
      ADBPushDir(sockADB, sname, dname, sRes);
    else
      ADBPushFile(sockADB, sname, dname, sRes);
  } while (FindNextFile(h, &fd) != 0);

  FindClose(h);
  return true;
}

void fardroid::ADBPushDirGetFiles(LPCTSTR sSrc, LPCTSTR sDst, CCopyRecords& files)
{
  if (m_procStruct.Lock())
  {
    m_procStruct.from = sSrc;
    m_procStruct.Unlock();
  }

  CString sdir = sSrc;
  CString ddir = sDst;
  AddEndSlash(sdir);
  AddEndSlash(ddir, true);

  WIN32_FIND_DATA fd;
  HANDLE h = FindFirstFile(sdir + _T("*.*"), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;

  CString sname;
  CString dname;
  do {
    if (m_bForceBreak)
      break;

    if (lstrcmp(fd.cFileName, _T(".")) == 0 || lstrcmp(fd.cFileName, _T("..")) == 0)
      continue;

    sname.Format(_T("%s%s"), sdir, fd.cFileName);
    dname.Format(_T("%s%s"), ddir, fd.cFileName);

    if (IsDirectory(fd.dwFileAttributes))
    {
      ADBPushDirGetFiles(sname, dname, files);
    } 
    else
    {
      auto rec = new CCopyRecord;
      rec->src = sname;
      rec->dst = dname;
      rec->size = fd.nFileSizeHigh * (MAXDWORD + UINT64(1)) + fd.nFileSizeLow;
      files.Add(rec);
    }

  } while (FindNextFile(h, &fd) != 0);

  FindClose(h);
}

BOOL fardroid::ADBPushFile(SOCKET sockADB, LPCTSTR sSrc, LPCTSTR sDst, CString& sRes)
{
  CString dest = WtoUTF8(sDst);
  int mode = 0;
  if (!ADBReadMode(sockADB, dest, mode))
    return FALSE;

  if ((mode != 0) && IS_FLAG(mode, S_IFDIR))
  {
    CString name = ExtractName(sSrc, false);
    AddEndSlash(dest, true);
    dest += name;
  }

  if (!ADBSendFile(sockADB, sSrc, dest, sRes, mode))
    return FALSE;
  if (conf.WorkMode == WORKMODE_NATIVE && conf.UseSU && conf.UseExtendedAccess)
    SetPermissionsFile(sDst, "-rw-rw-rw-");
  return TRUE;
}

BOOL fardroid::ADB_push(LPCTSTR sSrc, LPCTSTR sDst, CString& sRes, bool bSilent)
{
  SOCKET sock = PrepareADBSocket();

  if (!SendADBCommand(sock, _T("sync:")))
  {
    CloseADBSocket(sock);
    return FALSE;
  }

  if (IsDirectory(sSrc))
  {
    if (!ADBPushDir(sock, sSrc, sDst, sRes))
    {
      CloseADBSocket(sock);
      return FALSE;
    }
    ADBSyncQuit(sock);
  }
  else
  {
    if (!ADBPushFile(sock, sSrc, sDst, sRes))
    {
      CloseADBSocket(sock);
      return FALSE;
    }
    ADBSyncQuit(sock);
  }

  CloseADBSocket(sock);
  return TRUE;
}

bool fardroid::ADBPullDir(SOCKET sockADB, LPCTSTR sSrc, LPCTSTR sDst, CString& sRes)
{
  MakeDirs(sDst);

  CString ddir = sDst;
  CString sdir = sSrc;
  sRes.Empty();
  AddEndSlash(sdir, true);
  AddEndSlash(ddir);
  CString ssaved = sdir;
  CString dsaved = ddir;

  CFileRecords recs;
  if (ADB_ls(WtoUTF8(sSrc), recs, sRes, true))
  {
    CString sname, dname;
    for (int i = 0; i < recs.GetSize(); i++)
    {
      sname.Format(_T("%s%s"), ssaved, recs[i]->filename);
      dname.Format(_T("%s%s"), dsaved, recs[i]->filename);

      if (!IsDirectory(recs[i]->attr))
      {
        if (m_procStruct.Lock())
        {
          m_procStruct.nStartTime = GetTickCount();
          m_procStruct.from = sname;
          m_procStruct.to = dname;
          m_procStruct.nTransmitted = 0;
          m_procStruct.nFileSize = recs[i]->size;
          m_procStruct.Unlock();
        }
        ADBPullFile(sockADB, sname, dname, sRes);
      }
      else
        ADBPullDir(sockADB, sname, dname, sRes);
    }
  }

  DeleteRecords(recs);
  return true;
}

void fardroid::ADBPullDirGetFiles(LPCTSTR sSrc, LPCTSTR sDst, CCopyRecords& files)
{
  if (m_procStruct.Lock())
  {
    m_procStruct.from = sSrc;
    m_procStruct.Unlock();
  }

  CString sdir = sSrc;
  CString ddir = sDst;
  AddEndSlash(sdir, true);
  AddEndSlash(ddir);

  CString sRes;
  CFileRecords recs;
  if (ADB_ls(WtoUTF8(sSrc), recs, sRes, true))
  {
    CString sname;
    CString dname;
    for (auto i = 0; i < recs.GetSize(); i++)
    {
      if (m_bForceBreak)
        break;

      sname.Format(_T("%s%s"), sdir, recs[i]->filename);
      dname.Format(_T("%s%s"), ddir, recs[i]->filename);

      if (IsDirectory(recs[i]->attr))
      {
        sname.Format(_T("%s%s"), sdir, recs[i]->filename);
        ADBPullDirGetFiles(sname, dname, files);
      }
      else
      {
        auto rec = new CCopyRecord;
        rec->src = sname;
        rec->dst = dname;
        rec->size = recs[i]->size;
        files.Add(rec);
      }
    }
  }
  DeleteRecords(recs);
}

BOOL fardroid::ADBPullFile(SOCKET sockADB, LPCTSTR sSrc, LPCTSTR sDst, CString& sRes)
{
  syncmsg msg;
  HANDLE hFile = nullptr;
  int len;
  unsigned id;
  CString file = WtoUTF8(sSrc);

  len = lstrlen(file);
  if (len > 1024) return FALSE;

  msg.req.id = ID_RECV;
  msg.req.namelen = len;
  if (SendADBPacket(sockADB, &msg.req, sizeof(msg.req)))
  {
    bool bOK = false;
    char* buf = getAnsiString(file);
    bOK = SendADBPacket(sockADB, buf, len);
    my_free(buf);
    if (!bOK) return FALSE;
  }

  if (ReadADBPacket(sockADB, &msg.data, sizeof(msg.data)) <= 0)
    return FALSE;

  id = msg.data.id;
  if ((id != ID_DATA) && (id != ID_DONE))
    goto remoteerror;

  DeleteFile(sDst, true);
  MakeDirs(ExtractPath(sDst, false));
  hFile = CreateFile(sDst, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
    return FALSE;

  char* buffer = new char[SYNC_DATA_MAX];
  DWORD written = 0;
  bool bFirst = true;
  while (true)
  {
    if (!bFirst)
    {
      if (ReadADBPacket(sockADB, &msg.data, sizeof(msg.data)) <= 0)
      {
        delete [] buffer;
        CloseHandle(hFile);
        return FALSE;
      }
      id = msg.data.id;
    }


    len = msg.data.size;
    if (id == ID_DONE) break;
    if (id != ID_DATA)
    {
      delete [] buffer;
      CloseHandle(hFile);
      goto remoteerror;
    }

    if (len > SYNC_DATA_MAX)
    {
      delete [] buffer;
      CloseHandle(hFile);
      return FALSE;
    }

    if (ReadADBPacket(sockADB, buffer, len) <= 0)
    {
      delete [] buffer;
      CloseHandle(hFile);
      return FALSE;
    }

    if (!WriteFile(hFile, buffer, len, &written, nullptr))
    {
      delete [] buffer;
      CloseHandle(hFile);
      return FALSE;
    }

    if (m_procStruct.Lock())
    {
      m_procStruct.nTransmitted += written;
      m_procStruct.nTotalTransmitted += written;
      m_procStruct.Unlock();
    }

    if (m_bForceBreak)
    {
      delete [] buffer;
      CloseHandle(hFile);
      DeleteFile(sDst, true);
      return TRUE;
    }
    bFirst = false;
  }

  delete [] buffer;
  CloseHandle(hFile);
  return TRUE;

remoteerror:
  if (id == ID_FAIL)
  {
    char* errbuffer = new char[SYNC_DATA_MAX];
    len = msg.data.size;
    if (len > 256) len = 256;
    if (ReadADBPacket(sockADB, errbuffer, len) <= 0) {
      delete[] errbuffer;
      return FALSE;
    }

    errbuffer[len] = 0;
    sRes = errbuffer;
  }
  return FALSE;
}

BOOL fardroid::ADB_pull(LPCTSTR sSrc, LPCTSTR sDst, CString& sRes, bool bSilent)
{
  int mode;
  CString dest = sDst;

  SOCKET sock = PrepareADBSocket();

  if (!SendADBCommand(sock, _T("sync:")))
  {
    CloseADBSocket(sock);
    return FALSE;
  }

  if (!ADBReadMode(sock, sSrc, mode))
  {
    CloseADBSocket(sock);
    return FALSE;
  }
  if (IS_FLAG(mode, S_IFREG) || IS_FLAG(mode, S_IFCHR) || IS_FLAG(mode, S_IFBLK))
  {
    if (IsDirectory(dest))
    {
      AddEndSlash(dest);
      dest += ExtractName(sSrc);
    }
    if (!ADBPullFile(sock, sSrc, dest, sRes))
    {
      CloseADBSocket(sock);
      return FALSE;
    }
    ADBSyncQuit(sock);
  }
  else if (IS_FLAG(mode, S_IFDIR))
  {
    if (!ADBPullDir(sock, sSrc, dest, sRes))
    {
      CloseADBSocket(sock);
      return FALSE;
    }
    ADBSyncQuit(sock);
  }
  else
  {
    CloseADBSocket(sock);
    return FALSE;
  }

  CloseADBSocket(sock);
  return TRUE;
}

BOOL fardroid::ADB_ls(LPCTSTR sDir, CFileRecords& files, CString& sRes, bool bSilent)
{
  DeleteRecords(files);

  if (conf.WorkMode != WORKMODE_SAFE)
  {
    CString s;
    switch (conf.WorkMode)
    {
    case WORKMODE_NATIVE:
      s.Format(_T("ls -l -a \"%s\""), sDir);
      break;
    case WORKMODE_BUSYBOX:
      s.Format(_T("busybox ls -lAe%s --color=never \"%s\""), conf.LinksAsDir() ? _T("") : _T("L"), sDir);
      break;
    }
    if (ADBShellExecute(s, sRes, bSilent))
      return ReadFileList(sRes, files);
  }
  else
  {
    SOCKET sockADB = PrepareADBSocket();

    if (SendADBCommand(sockADB, _T("sync:")))
    {
      syncmsg msg;
      char buf[257];
      int len;

      len = lstrlen(sDir);
      if (len > 1024) return FALSE;

      msg.req.id = ID_LIST;
      msg.req.namelen = len;

      if (SendADBPacket(sockADB, &msg.req, sizeof(msg.req)))
      {
        char* dir = getAnsiString(sDir);
        if (SendADBPacket(sockADB, dir, len))
        {
          for (;;)
          {
            if (ReadADBPacket(sockADB, &msg.dent, sizeof(msg.dent)) <= 0) break;
            if (msg.dent.id == ID_DONE)
            {
              my_free(dir);
              return TRUE;
            }
            if (msg.dent.id != ID_DENT) break;
            len = msg.dent.namelen;
            if (len > 256) break;

            if (ReadADBPacket(sockADB, buf, len) <= 0)
            {
              delete[] buf;
              break;
            }

            buf[len] = 0;

            if (lstrcmpA(buf, ".") != 0 && lstrcmpA(buf, "..") != 0)
            {
              CFileRecord* rec = new CFileRecord;
              rec->attr = ModeToAttr(msg.dent.mode);
              rec->size = msg.dent.size;
              rec->time = msg.dent.time;

              CString s = buf;
              rec->filename = UTF8toW(s);

              files.Add(rec);
            }
          }
        }
        my_free(dir);
      }
    }
  }

  return FALSE;
}

BOOL fardroid::ADB_rm(LPCTSTR sDir, CString& sRes, bool bSilent)
{
  CString s;
  s.Format(_T("rm -r \"%s\""), WtoUTF8(sDir));
  return ADBShellExecute(s, sRes, bSilent);
}

BOOL fardroid::ADB_mkdir(LPCTSTR sDir, CString& sRes, bool bSilent)
{
  CString s;
  s.Format(_T("mkdir \"%s\""), WtoUTF8(sDir));
  ADBShellExecute(s, sRes, bSilent);
  return sRes.GetLength() == 0;
}

BOOL fardroid::ADB_rename(LPCTSTR sSource, LPCTSTR sDest, CString& sRes)
{
  CString s;
  s.Format(_T("mv \"%s\" \"%s\""), WtoUTF8(sSource), WtoUTF8(sDest));
  ADBShellExecute(s, sRes, false);
  return sRes.GetLength() == 0;
}

BOOL fardroid::ReadFileList(CString& sFileList, CFileRecords& files) const
{
  DeleteRecords(files);
  strvec lines;
#ifdef USELOGGING
	CPerfCounter counter;
	counter.Start(_T("Prepare listing"));
#endif

  Tokenize(sFileList, lines, _T("\r\n"));

#ifdef USELOGGING
	counter.Stop(NULL);
	counter.Start(_T("Parse listing"));
#endif

  for (int i = 0; i < lines.GetSize(); i++)
  {
    switch (conf.WorkMode)
    {
    case WORKMODE_BUSYBOX:
      if (!ParseFileLineBB(lines[i], files))
        return FALSE;
      break;
    case WORKMODE_NATIVE:
      if (!ParseFileLine(lines[i], files))
        return FALSE;
      break;
    }
  }
  return TRUE;
}

bool fardroid::ParseFileLine(CString& sLine, CFileRecords& files) const
{
  strvec tokens;
  CString regex;
  CFileRecord* rec = nullptr;
  if (sLine.IsEmpty())
    return true;

  switch (sLine[0])
  {
  case 'd': //directory
    regex = _T("/([\\w-]+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w-]+(?=\\s))\\s+([\\w:]+(?=\\s))\\s(.+)$/");
    RegExTokenize(sLine, regex, tokens);
    if (tokens.GetSize() == 6)
    {
      rec = new CFileRecord;
      rec->attr = StringToAttr(tokens[0]);
      rec->owner = tokens[1];
      rec->grp = tokens[2];
      rec->time = StringTimeToUnixTime(tokens[3], tokens[4]);
      rec->size = 0;
      rec->filename = UTF8toW(tokens[5]);
    }
    break;
  case 'l': //symlink
    regex = _T("/([\\w-]+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w-]+(?=\\s))\\s+([\\w:]+(?=\\s))\\s(.+(?=\\s->))\\s->\\s(.+)$/");
    RegExTokenize(sLine, regex, tokens);
    if (tokens.GetSize() == 7)
    {
      rec = new CFileRecord;
      rec->attr = StringToAttr(tokens[0]);
      rec->owner = tokens[1];
      rec->grp = tokens[2];
      rec->time = StringTimeToUnixTime(tokens[3], tokens[4]);
      rec->size = 0;
      rec->filename = UTF8toW(tokens[5]);
      rec->linkto = UTF8toW(tokens[6]);
      rec->desc.Format(_T("-> %s"), UTF8toW(tokens[6]));
    }
    break;
  case 'c': //device
  case 'b':
  case 's': //socket
    regex = _T("/([\\w-]+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w,]+\\s+\\w+(?=\\s))\\s+([\\w-]+(?=\\s))\\s+([\\w:]+(?=\\s))\\s(.+)$/");
    RegExTokenize(sLine, regex, tokens);
    if (tokens.GetSize() == 7)
    {
      rec = new CFileRecord;
      rec->attr = StringToAttr(tokens[0]);
      rec->owner = tokens[1];
      rec->grp = tokens[2];
      rec->size = 0;
      rec->desc = tokens[3];
      rec->time = StringTimeToUnixTime(tokens[4], tokens[5]);
      rec->filename = UTF8toW(tokens[6]);
    }
    else
    {
    case '-': //file
    case 'p': //FIFO	
      regex = _T("/([\\w-]+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w-]+(?=\\s))\\s+([\\w:]+(?=\\s))\\s(.+)$/");
      RegExTokenize(sLine, regex, tokens);
      if (tokens.GetSize() == 7)
      {
        rec = new CFileRecord;
        rec->attr = StringToAttr(tokens[0]);
        rec->owner = tokens[1];
        rec->grp = tokens[2];
        rec->size = _ttoi(tokens[3]);
        rec->time = StringTimeToUnixTime(tokens[4], tokens[5]);
        rec->filename = UTF8toW(tokens[6]);
      }
      else
      {
        regex = _T("/([\\w-]+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w-]+(?=\\s))\\s+([\\w:]+(?=\\s))\\s(.+)$/");
        RegExTokenize(sLine, regex, tokens);
        if (tokens.GetSize() == 6)
        {
          rec = new CFileRecord;
          rec->attr = StringToAttr(tokens[0]);
          rec->owner = tokens[1];
          rec->grp = tokens[2];
          rec->size = 0;
          rec->time = StringTimeToUnixTime(tokens[3], tokens[4]);
          rec->filename = UTF8toW(tokens[5]);
        }
      }
    }
    break;
  }

  if (rec)
  {
    files.Add(rec);
    return true;
  }

  return false;
}

bool fardroid::ParseFileLineBB(CString& sLine, CFileRecords& files) const
{
  strvec tokens;
  CString regex;
  CFileRecord* rec = nullptr;
  if (sLine.IsEmpty())
    return true;
  if (sLine.Left(5) == _T("total") || sLine.Left(3) == _T("ls:"))
    return true;
  switch (sLine[0])
  {
  case 'l': //symlink
    regex = _T("/([\\w-]+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w:]+(?=\\s))\\s+(\\w+(?=\\s))\\s(.+(?=\\s->))\\s->\\s(.+)$/");
    RegExTokenize(sLine, regex, tokens);
    if (tokens.GetSize() == 12)
    {
      rec = new CFileRecord;
      rec->attr = StringToAttr(tokens[0]);
      rec->owner = tokens[2];
      rec->grp = tokens[3];
      rec->size = _ttoi(tokens[4]);
      rec->time = StringTimeToUnixTime(tokens[7], tokens[6], tokens[9], tokens[8]);
      rec->filename = UTF8toW(tokens[10]);
      rec->linkto = UTF8toW(tokens[11]);
      rec->desc.Format(_T("-> %s"), UTF8toW(tokens[11]));
    }
    break;
  case 'd': //directory
  case '-': //file
  case 'p': //FIFO
    regex = _T("/([\\w-]+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w:]+(?=\\s))\\s+(\\w+(?=\\s))\\s(.+)$/");
    RegExTokenize(sLine, regex, tokens);
    if (tokens.GetSize() == 11)
    {
      rec = new CFileRecord;
      rec->attr = StringToAttr(tokens[0]);
      //tokens[1] - links count
      rec->owner = tokens[2];
      rec->grp = tokens[3];
      rec->size = _ttoi(tokens[4]);
      //tokens[5] - day of week
      rec->time = StringTimeToUnixTime(tokens[7], tokens[6], tokens[9], tokens[8]);
      rec->filename = UTF8toW(tokens[10]);
    }
    break;
  case 'c': //device
  case 'b':
  case 's':
    regex = _T("/([\\w-]+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w,]+\\s+\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+([\\w:]+(?=\\s))\\s+(\\w+(?=\\s))\\s(.+)$/");
    RegExTokenize(sLine, regex, tokens);
    if (tokens.GetSize() == 11)
    {
      rec = new CFileRecord;
      rec->attr = StringToAttr(tokens[0]);
      rec->owner = tokens[2];
      rec->grp = tokens[3];
      rec->size = 0;
      rec->desc = tokens[4];
      rec->time = StringTimeToUnixTime(tokens[7], tokens[6], tokens[9], tokens[8]);
      rec->filename = UTF8toW(tokens[10]);
    }
    break;
  }

  if (rec)
  {
    files.Add(rec);
    return true;
  }

  return false;
}

BOOL fardroid::OpenPanel(LPCTSTR sPath)
{
  BOOL bOK = FALSE;

  SOCKET sockADB = PrepareADBSocket();
  if (sockADB)
  {
    CloseADBSocket(sockADB);

    UpdateInfoLines();

    CString sRes;
    return ADB_ls(WtoUTF8(sPath), records, sRes, false);
  }

  return bOK;
}

void fardroid::ShowADBExecError(CString err, bool bSilent)
{
  if (bSilent)
    return;

  if (m_procStruct.Hide())
  {
    CString msg;
    if (err.IsEmpty())
      err = LOC(MADBExecError);

    err.TrimLeft();
    err.TrimRight();
    msg.Format(_T("%s\n%s\n%s"), LOC(MTitle), err, LOC(MOk));
    ShowMessage(msg, 1, nullptr, true);

    m_procStruct.Restore();
  }
}

void fardroid::DrawProgress(CString& sProgress, int size, double pc)
{
  int fn = static_cast<int>(pc * size);
  int en = size - fn;
  wchar_t buf[512];
  wchar_t *bp = buf;
  for (auto i = 0; i < fn; i++)
    *bp++ = 0x2588; //'█'
  for (auto i = 0; i < en; i++)
    *bp++ = 0x2591; //'░'
  // ReSharper disable once CppAssignedValueIsNeverUsed
  *bp++ = 0x0;
  sProgress.Format(_T("%s %3d%%"), buf, static_cast<int>(pc * 100));
}

void fardroid::DrawProgress(CString& sProgress, int size, LPCTSTR current, LPCTSTR total)
{
  CString left = sProgress;
  CString right, center;
  right.Format(_T("%s / %s"), current, total);
  for (auto i = 0; i < size - (left.GetLength() + right.GetLength()); i++)
  {
    center += " ";
  }
  sProgress.Format(_T("%s%s%s"), left, center, right);
}

void fardroid::ShowProgressMessage()
{
  if (m_procStruct.Lock())
  {
    static DWORD time = 0;

    if (m_procStruct.bSilent || GetTickCount() - time < 500)
    {
      m_procStruct.Unlock();
      return;
    }

    time = GetTickCount();
    if (m_procStruct.pType == PS_COPY || m_procStruct.pType == PS_MOVE)
    {
      CString mFrom = m_procStruct.nTotalFileSize == 0 ? LOC(MScanDirectory) : LOC(MFrom);
      CString mTo = m_procStruct.nTotalFileSize == 0 ? _T("") : LOC(MTo);
      CString mTotal;
      mTotal.Format(_T("\x1%s"), LOC(MTotal));

      static CString sInfo;
      int elapsed = m_procStruct.nTotalFileSize == 0 ? 0 : (time - m_procStruct.nTotalStartTime) / 1000;
      auto speed = 0;
      auto remain = 0;
      if (elapsed > 0)
        speed = m_procStruct.nTotalTransmitted / elapsed;
      if (speed > 0)
        remain = (m_procStruct.nTotalFileSize - m_procStruct.nTotalTransmitted) / speed;
      sInfo.Format(LOC(MProgress), FormatTime(elapsed), FormatTime(remain), FormatSpeed(speed));

      int size = sInfo.GetLength() - 5;
      CString sFrom = m_procStruct.from;
      if (m_procStruct.from.GetLength() >= size)
      {
        sFrom.Delete(0, m_procStruct.from.GetLength() - size - 2);
        sFrom = "..." + sFrom;
      }
      CString sTo = m_procStruct.to;
      if (m_procStruct.to.GetLength() >= size)
      {
        sTo.Delete(0, m_procStruct.to.GetLength() - size - 2);
        sTo = "..." + sTo;
      }

      CString sFiles = LOC(MFiles);
      CString sBytes = LOC(MBytes);
      DrawProgress(sFiles, size, FormatNumber(m_procStruct.nPosition), FormatNumber(m_procStruct.nTotalFiles));
      DrawProgress(sBytes, size, FormatNumber(m_procStruct.nTotalTransmitted), FormatNumber(m_procStruct.nTotalFileSize));

      double pc = m_procStruct.nFileSize > 0 ? static_cast<double>(m_procStruct.nTransmitted) / static_cast<double>(m_procStruct.nFileSize) : 0;
      double tpc = m_procStruct.nTotalFileSize > 0 ? static_cast<double>(m_procStruct.nTotalTransmitted) / static_cast<double>(m_procStruct.nTotalFileSize) : 0;

      static CString sProgress;
      DrawProgress(sProgress, size, pc);

      if (m_procStruct.nTotalFiles > 1)
      {
        static CString sTotalProgress;
        DrawProgress(sTotalProgress, size, tpc);
        const farStr* MsgItems[] = { m_procStruct.title, mFrom, sFrom, mTo, sTo, sProgress, mTotal, sFiles, sBytes, sTotalProgress, _T("\x1"), sInfo };
        ShowMessageWait(MsgItems, sizeof(MsgItems) / sizeof(MsgItems[0]));
      }
      else
      {
        const farStr* MsgItems[] = { m_procStruct.title, mFrom, sFrom, mTo, sTo, sProgress, mTotal, sFiles, sBytes, _T("\x1"), sInfo };
        ShowMessageWait(MsgItems, sizeof(MsgItems) / sizeof(MsgItems[0]));
      }
      taskbarIcon.SetState(taskbarIcon.S_PROGRESS, tpc);
    }
    else if (m_procStruct.pType == PS_DELETE)
    {
      int size = 50;

      double pc = static_cast<double>(m_procStruct.nPosition) / static_cast<double>(m_procStruct.nTotalFiles);
      static CString sProgress;
      DrawProgress(sProgress, size, pc);

      const farStr* MsgItems[] = { m_procStruct.title, m_procStruct.from, sProgress };
      ShowMessageWait(MsgItems, sizeof(MsgItems) / sizeof(MsgItems[0]));
      taskbarIcon.SetState(taskbarIcon.S_PROGRESS, pc);
    }
    else if (m_procStruct.pType == PS_FB)
    {
      CString mFrom = LOC(MScreenshotComplete);
      int size =  mFrom.GetLength() - 5;

      double pc = m_procStruct.nFileSize > 0 ? static_cast<double>(m_procStruct.nTransmitted) / static_cast<double>(m_procStruct.nFileSize) : 0;
      static CString sProgress;
      DrawProgress(sProgress, size, pc);

      const farStr* MsgItems[] = { m_procStruct.title, sProgress };
      ShowMessageWait(MsgItems, sizeof(MsgItems) / sizeof(MsgItems[0]));
      taskbarIcon.SetState(taskbarIcon.S_PROGRESS, pc);
    }

    m_procStruct.Unlock();
  }
}

CString fardroid::FormatSpeed(int cb)
{
  int n = cb;
  int pw = 0;
  int div = 1;
  while (n >= 1000)
  {
    div *= 1024;
    n /= 1024;
    pw++;
  }
  CString un;
  switch (pw)
  {
  case 0:
    un = "Bt/s";
    break;
  case 1:
    un = "KB/s";
    break;
  case 2:
    un = "MB/s";
    break;
  case 3:
    un = "GB/s";
    break;
  case 4:
    un = "TB/s";
    break;
  case 5:
    un = "PB/s";
    break;
  }

  CString res;
  res.Format(_T("%10.2f%s"), static_cast<float>(cb)/static_cast<float>(div), un);
  return res;
}

CString fardroid::FormatTime(int time)
{
  CString res;
  res.Format(_T("%2.2d:%2.2d:%2.2d"), time / 3600, (time % 3600) / 60, time % 3600 % 60);
  return res;
}

int fardroid::DelItems(PluginPanelItem* PanelItem, int ItemsNumber, bool noPromt, bool ansYes, bool bSilent)
{
  CString sdir = m_currentPath;
  AddBeginSlash(sdir);
  AddEndSlash(sdir, true);

  CString sname;
  auto result = TRUE;
  for (auto i = 0; i < ItemsNumber; i++)
  {
    if (m_bForceBreak)
      break;

    sname.Format(_T("%s%s"), sdir, PanelItem[i].FileName);
    if (m_procStruct.Lock())
    {
      m_procStruct.from = sname;
      m_procStruct.nPosition = i;
      m_procStruct.Unlock();
    }

    if (!DeleteFileFrom(sname, bSilent))
    {
      result = FALSE;
      break;
    }
  }

  Reread();

  if (m_bForceBreak)
    return ABORT;

  return result;
}

int fardroid::DeleteFiles(PluginPanelItem* PanelItem, int ItemsNumber, OPERATION_MODES OpMode)
{
  bool bSilent = IS_FLAG(OpMode, OPM_SILENT) || IS_FLAG(OpMode, OPM_FIND);
  bool noPromt = bSilent;

  if (!bSilent && !IS_FLAG(OpMode, OPM_QUICKVIEW) && !IS_FLAG(OpMode, OPM_VIEW) && !IS_FLAG(OpMode, OPM_EDIT))
  {
    if (!DeleteFilesDialog())
      return ABORT;
  }

  m_bForceBreak = false;
  if (m_procStruct.Lock())
  {
    m_procStruct.title = LOC(MDelFile);
    m_procStruct.pType = PS_DELETE;
    m_procStruct.bSilent = false;
    m_procStruct.nTotalFiles = ItemsNumber;
    m_procStruct.Unlock();
  }

  DWORD threadID = 0;
  HANDLE hThread = CreateThread(nullptr, 0, ProcessThreadProc, this, 0, &threadID);

  auto result = DelItems(PanelItem, ItemsNumber, noPromt, noPromt, bSilent);

  m_bForceBreak = true;
  CloseHandle(hThread);
  taskbarIcon.SetState(taskbarIcon.S_NO_PROGRESS);

  return result;
}

int fardroid::PutFiles(PluginPanelItem* PanelItem, int ItemsNumber, CString SrcPath, BOOL Move, OPERATION_MODES OpMode)
{
  CString srcdir = SrcPath;
  CString path = m_currentPath;
  AddBeginSlash(path);

  bool bSilent = IS_FLAG(OpMode, OPM_SILENT) || IS_FLAG(OpMode, OPM_FIND);
  bool noPromt = bSilent;

  if (IS_FLAG(OpMode, OPM_VIEW) || IS_FLAG(OpMode, OPM_QUICKVIEW) || IS_FLAG(OpMode, OPM_EDIT))
    bSilent = false;

  if (IS_FLAG(OpMode, OPM_EDIT))
    noPromt = true;

  m_bForceBreak = false;
  if (m_procStruct.Lock())
  {
    m_procStruct.title = Move ? LOC(MMoveFile) : LOC(MGetFile);
    m_procStruct.pType = Move ? PS_MOVE : PS_COPY;
    m_procStruct.bSilent = false;
    m_procStruct.nTransmitted = 0;
    m_procStruct.nTotalTransmitted = 0;
    m_procStruct.nFileSize = 0;
    m_procStruct.nTotalFileSize = 0;
    m_procStruct.Unlock();
  }

  DWORD threadID = 0;
  HANDLE hThread = CreateThread(nullptr, 0, ProcessThreadProc, this, 0, &threadID);

  auto result = PutItems(PanelItem, ItemsNumber, srcdir, path, noPromt, noPromt, bSilent);

  m_bForceBreak = true;
  CloseHandle(hThread);
  taskbarIcon.SetState(taskbarIcon.S_NO_PROGRESS);

  return result;
}

int fardroid::CreateDir(CString& DestPath, OPERATION_MODES OpMode)
{
  CString path;
  CString srcdir = m_currentPath;
  AddBeginSlash(srcdir);
  AddEndSlash(srcdir, true);

  bool bSilent = IS_FLAG(OpMode, OPM_SILENT) || IS_FLAG(OpMode, OPM_FIND);

  if (IS_FLAG(OpMode, OPM_VIEW) || IS_FLAG(OpMode, OPM_QUICKVIEW) || IS_FLAG(OpMode, OPM_EDIT))
    bSilent = false;

  if (!bSilent && !IS_FLAG(OpMode, OPM_QUICKVIEW) && !IS_FLAG(OpMode, OPM_VIEW) && !IS_FLAG(OpMode, OPM_EDIT))
  {
    if (!CreateDirDialog(path))
      return ABORT;

    DestPath = path;
  }

  srcdir += path;

  CString sRes;
  if (ADB_mkdir(srcdir, sRes, bSilent))
  {
    Reread();
  }
  else
  {
    ShowError(sRes);
  }
  return TRUE;
}

int fardroid::Rename(CString& DestPath)
{
  CString srcdir = m_currentPath;
  AddBeginSlash(srcdir);
  AddEndSlash(srcdir, true);

  CString src = srcdir + DestPath;
  if (!CopyFilesDialog(DestPath, LOC(MRenameFile)))
    return ABORT;
  CString dst = srcdir + DestPath;

  CString sRes;
  if (ADB_rename(src, dst, sRes))
  {
    Reread();
  }
  else
  {
    ShowError(sRes);
  }
  return TRUE;
}

int fardroid::GetFramebuffer()
{
  m_bForceBreak = false;
  if (m_procStruct.Lock())
  {
    m_procStruct.title = LOC(MScreenshot);
    m_procStruct.pType = PS_FB;
    m_procStruct.bSilent = false;
    m_procStruct.nTransmitted = 0;
    m_procStruct.nTotalFiles = 1;
    m_procStruct.Unlock();
  }

  DWORD threadID = 0;
  HANDLE hThread = CreateThread(nullptr, 0, ProcessThreadProc, this, 0, &threadID);

  fb fb;
  auto result = ADBReadFramebuffer(&fb);
  if (result == TRUE)
  {
    result = SaveToClipboard(&fb);
  }

  m_bForceBreak = true;
  CloseHandle(hThread);
  taskbarIcon.SetState(taskbarIcon.S_NO_PROGRESS);

  if (result == TRUE)
  {
    CString msg;
    msg.Format(L"%s\n%s\n%s", LOC(MScreenshot), LOC(MScreenshotComplete), LOC(MOk));
    ShowMessage(msg, 1, nullptr);
  }

  if (fb.data) my_free(fb.data);
  return result;
}



CFileRecord* fardroid::GetFileRecord(LPCTSTR sFileName)
{
  for (int i = 0; i < records.GetSize(); i++)
  {
    if (records[i]->filename.Compare(sFileName) == 0)
      return records[i];
  }
  return nullptr;
}

void fardroid::ParseMemoryInfo(CString s)
{
  strvec tokens;

  CPanelLine pl;
  pl.separator = FALSE;

  CString regex = _T("/([\\w]+(?=:)):\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s))\\s+(\\w+(?=\\s|$))/");
  RegExTokenize(s, regex, tokens);
  if (tokens.GetSize() == 4)
  {
    pl.text = tokens[0];
    pl.data.Format(_T("%s/%s"), tokens[1], tokens[3]);
    lines.Add(pl);

    /*pl.text.Format(_T("%s %s:"), tokens[0], LOC(MMemUsed));
    pl.data = tokens[2];
    lines.Add(pl);

    pl.text.Format(_T("%s %s:"), tokens[0], LOC(MMemFree));
    pl.data = tokens[3];
    lines.Add(pl);*/
  }
}

void fardroid::GetMemoryInfo()
{
  CString sRes;
  ADBShellExecute(_T("free"), sRes, false);

  strvec str;
  Tokenize(sRes, str, _T("\n"));

  CPanelLine pl;
  pl.separator = TRUE;
  pl.text = LOC(MMemoryInfo);
  lines.Add(pl);

  if (str.GetSize() == 4)
  {
    for (int i = 1; i < str.GetSize(); i++)
      ParseMemoryInfo(str[i]);
  }
}

void fardroid::ParsePartitionInfo(CString s)
{
  strvec tokens;

  CPanelLine pl;
  pl.separator = FALSE;

  CString regex = _T("/(.*(?=:)):\\W+(\\w+(?=\\stotal)).+,\\s(\\w+(?=\\savailable))/");
  RegExTokenize(s, regex, tokens);
  if (tokens.GetSize() != 3)
  {
    regex = _T("/^(\\S+)\\s+(\\d\\S*)\\s+\\S+\\s+(\\d\\S*)/");
    RegExTokenize(s, regex, tokens);
  }
  if (tokens.GetSize() == 3)
  {
    pl.text = tokens[0];
    pl.data.Format(_T("%s/%s"), tokens[1], tokens[2]);
    lines.Add(pl);
  }
}

void fardroid::GetPartitionsInfo()
{
  CString sRes;

  if (conf.ShowAllPartitions)
    ADBShellExecute(_T("df"), sRes, false);
  else
  {
    ADBShellExecute(_T("df /data"), sRes, false);
    sRes += _T("\n");
    ADBShellExecute(_T("df /sdcard"), sRes, false);
  }

  strvec str;
  Tokenize(sRes, str, _T("\n"));

  CPanelLine pl;
  pl.separator = TRUE;
  pl.text = LOC(MPartitionsInfo);
  lines.Add(pl);

  for (int i = 0; i < str.GetSize(); i++)
    ParsePartitionInfo(str[i]);
}

CString fardroid::GetPermissionsFile(const CString& FullFileName)
{
  CString permissions;
  CString s;
  CString sRes;
  s.Format(_T("ls -l -a -d \"%s\""), WtoUTF8(FullFileName));
  if (ADBShellExecute(s, sRes, false))
  {
    strvec lines;
    Tokenize(sRes, lines, _T(" "));

    if (!sRes.IsEmpty() &&
      sRes.Find(_T("No such file or directory")) == -1 &&
      lines[0].GetLength() == 10)
    {
      permissions = lines[0];
    }
  }
  return permissions;
}

CString fardroid::PermissionsFileToMask(CString Permission)
{
  CString permission_mask, tmp_str;

  Permission.Replace(_T('-'),_T('0'));
  Permission.Replace(_T('r'),_T('1'));
  Permission.Replace(_T('w'),_T('1'));
  Permission.Replace(_T('x'),_T('1'));

  permission_mask.Format(_T("%d"),_tcstoul(Permission.Mid(1, 3).GetBuffer(11), nullptr, 2));
  tmp_str.Format(_T("%d"),_tcstoul(Permission.Mid(4, 3).GetBuffer(11), nullptr, 2));
  permission_mask += tmp_str;
  tmp_str.Format(_T("%d"),_tcstoul(Permission.Mid(7, 3).GetBuffer(11), nullptr, 2));
  permission_mask += tmp_str;

  return permission_mask;
}

bool fardroid::SetPermissionsFile(const CString& FullFileName, const CString& PermissionsFile)
{
  CString s;
  CString sRes;
  s.Format(_T("chmod %s \"%s\""), PermissionsFileToMask(PermissionsFile), WtoUTF8(FullFileName));
  return ADBShellExecute(s, sRes, false) != FALSE;
}

bool fardroid::DeviceTest()
{
  SOCKET sock = PrepareADBSocket();
  if (sock)
  {
    CloseADBSocket(sock);
    return true;
  }

  return false;
}

SOCKET fardroid::CreateADBSocket()
{
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2,2), &wsaData);

  SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock)
  {
    sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(5037);
    if (inet_addr("127.0.0.1") != INADDR_NONE)
    {
      dest_addr.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");
      if (connect(sock, reinterpret_cast<sockaddr *>(&dest_addr), sizeof(dest_addr)) == 0)
        return sock;
    }
    closesocket(sock);
  }
  WSACleanup();

  return 0;
}

bool fardroid::SendADBCommand(SOCKET sockADB, LPCTSTR sCMD)
{
  if (sockADB == 0)
    return false;

  CString sCommand;
  sCommand.Format(_T("%04X%s"), lstrlen(sCMD), sCMD);
  char* buf = getAnsiString(sCommand);

  SendADBPacket(sockADB, buf, sCommand.GetLength());
  my_free(buf);

  return CheckADBResponse(sockADB);
}

void fardroid::CloseADBSocket(SOCKET sockADB)
{
  if (sockADB)
    closesocket(sockADB);
  WSACleanup();
}

bool fardroid::CheckADBResponse(SOCKET sockADB)
{
  long msg;
  ReadADBPacket(sockADB, &msg, sizeof(msg));
  return msg == ID_OKAY;
}

bool fardroid::ReadADBSocket(SOCKET sockADB, char* buf, int bufSize)
{
  int nsize;
  int received = 0;
  while (received < bufSize)
  {
    nsize = recv(sockADB, buf, bufSize, 0);
    if (nsize == SOCKET_ERROR)//ошибко
      return false;

    received += nsize;

    if (nsize > 0)
      buf[nsize] = 0;

    if (nsize == 0 || nsize == WSAECONNRESET)
      break;
  }

  return true;
}

SOCKET fardroid::PrepareADBSocket()
{
  lastError = S_OK;
  int tryCnt = 0;
tryagain:
  SOCKET sock = CreateADBSocket();
  if (sock)
  {
    if (m_currentDevice.IsEmpty())
    {
      if (SendADBCommand(sock, _T("host:devices")))
      {
        CString devices = "";
        auto buf = new char[4097];
        ReadADBPacket(sock, buf, 4);
        while (true)
        {
          auto len = ReadADBPacket(sock, buf, 4096);
          if (len <= 0)
            break;

          buf[len] = 0;
          devices += buf;
        }

        CloseADBSocket(sock);
        sock = 0;

        if (DeviceMenu(devices))
          goto tryagain;

        lastError = ERROR_DEV_NOT_EXIST;
      }
      else
      {
        CloseADBSocket(sock);
        sock = 0;
        lastError = ERROR_DEV_NOT_EXIST;
      }
    }
    else
    {
      if (!SendADBCommand(sock, _T("host:transport:") + m_currentDevice))
      {
        CloseADBSocket(sock);
        sock = 0;
        lastError = ERROR_DEV_NOT_EXIST;
      }
    }
  }
  else
  {
    if (tryCnt == 0)
    {
      tryCnt++;
      CString adb = conf.ADBPath;
      if (adb.GetLength() > 0)
        AddEndSlash(adb);
      adb += _T("adb.exe");
      HINSTANCE hInstance = ShellExecute(nullptr, nullptr, adb, _T("start-server"), nullptr, SW_HIDE);
      if (hInstance > reinterpret_cast<HINSTANCE>(32))
        goto tryagain;
    }
    else
    {
      lastError = ERROR_DEV_NOT_EXIST;
    }
  }

  if (lastError == ERROR_DEV_NOT_EXIST)
    ShowADBExecError(LOC(MDeviceNotFound), false);

  return sock;
}

bool fardroid::SendADBPacket(SOCKET sockADB, void* packet, int size)
{
  char* p = static_cast<char*>(packet);
  int r;

  while (size > 0)
  {
    r = send(sockADB, p, size, 0);

    if (r > 0)
    {
      size -= r;
      p += r;
    }
    else if (r < 0) return false;
    else if (r == 0) return true;
  }
  return true;
}

int fardroid::ReadADBPacket(SOCKET sockADB, void* packet, int size)
{
  char* p = static_cast<char*>(packet);
  int r;
  int received = 0;

  while (size > 0)
  {
    r = recv(sockADB, p, size, 0);
    if (r > 0)
    {
      received += r;
      size -= r;
      p += r;
    }
    else if (r == 0) break;
    else return r;
  }

  return received;
}

BOOL fardroid::ADBShellExecute(LPCTSTR sCMD, CString& sRes, bool bSilent)
{
  SOCKET sockADB = PrepareADBSocket();

  BOOL bOK = FALSE;
  CString cmd;
  if (conf.UseSU)
    cmd.Format(_T("shell:su -c \'%s\'"), sCMD);
  else
    cmd.Format(_T("shell:%s"), sCMD);
  if (SendADBCommand(sockADB, cmd))
  {
    char* buf = new char[4097];
    while (true)
    {
      int len = ReadADBPacket(sockADB, buf, 4096);
      if (len <= 0)
        break;

      buf[len] = 0;
      sRes += buf;
    }
    delete[] buf;
    bOK = TRUE;
  }

  CloseADBSocket(sockADB);
  return bOK;
}

int fardroid::ADBReadFramebuffer(struct fb* fb)
{
  auto result = TRUE;

  SOCKET sockADB = PrepareADBSocket();
  if (SendADBCommand(sockADB, _T("framebuffer:")))
  {
    auto buffer = new char[sizeof(struct fbinfo)];
    if (ReadADBPacket(sockADB, buffer, sizeof(struct fbinfo)) <= 0)
    {
      CloseADBSocket(sockADB);
      return false;
    }

    const struct fbinfo* fbinfo = reinterpret_cast<struct fbinfo*>(buffer);
    memcpy(fb, &fbinfo->bpp, sizeof(struct fbinfo) - 4);

    if (m_procStruct.Lock())
    {
      m_procStruct.nFileSize = fb->size;
      m_procStruct.Unlock();
    }

    fb->data = my_malloc(fb->size);
    int size = fb->size;
    auto position = static_cast<char*>(fb->data);
    int readed;
    while (size > 0)
    {
      readed = ReadADBPacket(sockADB, position, min(size, SYNC_DATA_MAX));
      if (readed == 0) break;

      position += readed;
      size -= readed;

      if (m_procStruct.Lock())
      {
        m_procStruct.nTransmitted += readed;
        m_procStruct.Unlock();
      }

      if (m_bForceBreak)
      {
        result = ABORT;
        break;
      }
    }
  }
  else
  {
    result = FALSE;
  }

  CloseADBSocket(sockADB);
  return result;
}

BOOL fardroid::ADB_findmount(LPCTSTR sFS, strvec& fs_params, CString& sRes, bool bSilent)
{
  sRes.Empty();
  if (ADBShellExecute(_T("mount"), sRes, bSilent))
  {
    strvec tokens;
    Tokenize(sRes, tokens, _T("\n"));
    for (int i = 0; i < tokens.GetSize(); i++)
    {
      fs_params.RemoveAll();
      Tokenize(tokens[i], fs_params, _T(" "));
      if (fs_params.GetSize() == 6 && fs_params[1] == sFS)
        return TRUE;
    }
  }
  return FALSE;
}

BOOL fardroid::ADB_mount(LPCTSTR sFS, BOOL bAsRW, CString& sRes, bool bSilent)
{
  strvec fs_params;
  if (ADB_findmount(sFS, fs_params, sRes, bSilent))
  {
    CString cmd;
    cmd.Format(_T("mount -o remount,%s -t %s %s %s"), bAsRW ? _T("rw") : _T("ro"), fs_params[2], fs_params[0], sFS);
    sRes.Empty();
    return ADBShellExecute(cmd, sRes, bSilent);
  }
  return FALSE;
}
