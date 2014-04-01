// autobuild.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

typedef std::basic_string<TCHAR> tstring;

#ifdef _UNICODE
#define tcout std::wcout
#else
#define tcout std::cout
#endif

//tstring getFileExt(const tstring& path) {
//	tstring::size_type last_dot = path.find_first_of(_T('.'));
//	if (last_dot != tstring::npos) {
//		return path.substr(last_dot);
//	} else {
//		return _T("");
//	}
//}

tstring getFileDir(const tstring& path) {
	tstring::size_type last_slash = path.find_last_of(_T('\\'));
	if (last_slash != tstring::npos) {
		return path.substr(0, last_slash);
	} else {
		return _T("");
	}
}

struct ChangeItem {
	tstring relative_path;
	unsigned action_type;

	ChangeItem(const tstring& path, unsigned action) : relative_path(path), action_type(action) {
	}
};

struct ChangeItemPtrLess {
	bool operator () (const ChangeItem* left, const ChangeItem* right) const {
		return left->relative_path < right->relative_path;
	}
};

class MonitorThread {
public:
	MonitorThread() : _thread(nullptr), _version(0) {
	}

	void start(const tstring& dir, const tstring& filter);

	void join() {
		if (_thread) _thread->join();
	}

	typedef set<ChangeItem*, ChangeItemPtrLess> ChangeItemSet;

	// copy and clear, or swap
	void takeChangeSet(ChangeItemSet& changeList);

	unsigned getVersion() const {
		return _version;
	}

private:
	boost::thread* _thread;
	boost::mutex _dataMutex;
	ChangeItemSet _changeList;
	unsigned _version;
};

void MonitorThread::start(const tstring& dir, const tstring& filter) {
	_thread = new boost::thread([dir, filter, this] () {
		basic_regex<TCHAR> filter_regex(filter);
		HANDLE directory = ::CreateFile(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);	
		const DWORD bufferLen = 102400;
		char notifyBuffer[bufferLen] = { 0 };
		DWORD bytesWritten = 0;
		while (::ReadDirectoryChangesW(directory, notifyBuffer, bufferLen, TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE,
			&bytesWritten, NULL, NULL)) {
			FILE_NOTIFY_INFORMATION* notifyInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(notifyBuffer);
			for(;;) {
				if (notifyInfo->Action == FILE_ACTION_MODIFIED) {
					unsigned pathLen = notifyInfo->FileNameLength / sizeof(TCHAR);
					tstring path(pathLen, '\0');
					memcpy(const_cast<TCHAR*>(path.data()), notifyInfo->FileName, notifyInfo->FileNameLength);
					if (regex_match(path, filter_regex)) {
						boost::lock_guard<boost::mutex> _(_dataMutex);
						ChangeItem* changeItem = new ChangeItem(path, FILE_ACTION_MODIFIED);
						if (!_changeList.insert(changeItem).second) {
							delete changeItem;
						}
						++_version;
					}
				}
				if (notifyInfo->NextEntryOffset > 0) {
					notifyInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
						reinterpret_cast<unsigned>(notifyInfo) + notifyInfo->NextEntryOffset);
				} else {
					break;
				}
			}
		}
		::CloseHandle(directory);
	});
}

void MonitorThread::takeChangeSet(MonitorThread::ChangeItemSet& changeList) {
	boost::lock_guard<boost::mutex> _(_dataMutex);
	_changeList.swap(changeList);
}

void printUsage() {
	tcout << "usage: autobuild {Solution File Path} [{msbuild arguments}...]" << endl;
}

void callMsbuild(const tstring& slnFilePath, const vector<tstring>& args) {
	basic_ostringstream<TCHAR> cmd;
	cmd << _T("msbuild /m ") << slnFilePath;
	for (auto i = args.begin(); i != args.end(); ++i) {
		cmd << _T(" ") << *i;
	}
	int ret = 0;
	do {
#ifdef _UNICODE
		ret = _wsystem(cmd.str().c_str());
#else
		ret = system(cmd.str().c_str());
#endif
	} while (ret != 0);
}

void callMsbuild(const tstring& slnFilePath) {
	vector<tstring> emptyArgs;
	callMsbuild(slnFilePath, emptyArgs);
}


int _tmain(int argc, _TCHAR* argv[]) {
	if (argc < 2) {
		printUsage();
		return 0;
	}

	tstring slnFile = argv[1];

	vector<tstring> msbuildArgs;
	for (int i = 2; i < argc; ++i) {
		msbuildArgs.push_back(argv[i]);
	}

	tstring dir = getFileDir(slnFile);
	if (dir.empty()) {
		dir = _T(".");
	}
	MonitorThread mt;
	mt.start(dir, _T(".*\\.cpp|.*\\.h|.*\\.vcxproj|.*\\.sln"));

	callMsbuild(slnFile, msbuildArgs);

	unsigned lastVersion = 0;
	MonitorThread::ChangeItemSet changeList;
	for (;;) {
		unsigned curVersion = mt.getVersion();
		if (curVersion > lastVersion) {
			MonitorThread::ChangeItemSet lastChangeList;
			mt.takeChangeSet(lastChangeList);
			changeList.insert(lastChangeList.begin(), lastChangeList.end());
			lastVersion = curVersion;
			
		} else {
			if (!changeList.empty()) {
				callMsbuild(slnFile, msbuildArgs);
				tcout << "------------------ change list ------------------" << endl;
				for (auto i = changeList.begin(); i != changeList.end(); ++i) {
					ChangeItem* changeItem = *i;
					tcout << changeItem->relative_path << endl;
					delete changeItem;
				}
				changeList.clear();
			}

			::Sleep(500);
		}
	}

	mt.join();
	return 0;
}
