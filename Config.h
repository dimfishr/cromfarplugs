#pragma once

enum{
	WORKMODE_SAFE,
	WORKMODE_NATIVE,
	WORKMODE_BUSYBOX,
};

class CConfig
{
public:
	//common
	HANDLE hHandle;
	//plugin
	CString Prefix;
	CString ADBPath;
	//interface
	int			PanelMode;
	int			SortMode;
	int			SortOrder;
	int			WorkMode;
	BOOL		AddToDiskMenu;
	BOOL		ShowLinksAsDir;
	BOOL		KillServer;
  BOOL    SU;
  BOOL    SU0;
  BOOL		UseSU;
	BOOL		CopySD;
	BOOL		RemountSystem;

	CConfig(void);
	bool InitHandle();
	void FreeHandle();
	void Set(size_t Root, const wchar_t* Name, CString & Value) const;
	void Set(size_t Root, const wchar_t* Name, int Value) const;
	void Get(size_t Root, const wchar_t* Name, int & Value, int Default) const;
	void Get(size_t Root, const wchar_t* Name, CString & Value, const wchar_t * Default) const;
  int CreateSubKey(size_t Root, const wchar_t* Name) const;
  int OpenSubKey(size_t Root, const wchar_t* Name) const;
  bool DeleteSubKey(size_t Root) const;
  void GetSub(size_t Root, const wchar_t* Sub, const wchar_t* Name, CString& Value, const wchar_t* Default);
  void SetSub(size_t Root, const wchar_t* Sub, const wchar_t* Name, CString& Value);
  bool Load();
	void Save();
  void SavePanel();
  BOOL LinksAsDir() const;
};
