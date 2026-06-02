//	VirtualDub / Altirra — SDL3 implementation of IVDFileAsync
//	Uses synchronous buffered I/O via VDFile.  All Mode variants behave
//	identically (synchronous) since we don't need the overlapped-I/O
//	optimization path that the Windows version provides.

#include <vd2/system/fileasync.h>
#include <vd2/system/file.h>
#include <vd2/system/error.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <string.h>

class VDFileAsyncSDL3 final : public IVDFileAsync {
public:
	VDFileAsyncSDL3();
	~VDFileAsyncSDL3();

	void SetPreemptiveExtend(bool b) override;
	bool IsPreemptiveExtendActive() override;
	bool IsOpen() override;
	void Open(const wchar_t *pszFilename, uint32 count, uint32 bufferSize) override;
	void Open(VDFileHandle h, uint32 count, uint32 bufferSize) override;
	void Close() override;
	void FastWrite(const void *pData, uint32 bytes) override;
	void FastWriteEnd() override;
	void Write(sint64 pos, const void *pData, uint32 bytes) override;
	bool Extend(sint64 pos) override;
	void Truncate(sint64 pos) override;
	void SafeTruncateAndClose(sint64 pos) override;
	sint64 GetFastWritePos() override;
	sint64 GetSize() override;

private:
	VDFile mFile;
	sint64 mFastWritePos = 0;
	sint64 mFileSize = 0;
	bool mbOpen = false;
	bool mbPreemptiveExtend = false;

	// Write buffer for FastWrite path
	enum { kBufferSize = 65536 };
	uint8 mBuffer[kBufferSize];
	uint32 mBufferLevel = 0;

	void FlushBuffer();
};

VDFileAsyncSDL3::VDFileAsyncSDL3() {
}

VDFileAsyncSDL3::~VDFileAsyncSDL3() {
	if (mbOpen) {
		try { Close(); } catch(...) {}
	}
}

void VDFileAsyncSDL3::SetPreemptiveExtend(bool b) {
	mbPreemptiveExtend = b;
}

bool VDFileAsyncSDL3::IsPreemptiveExtendActive() {
	return mbPreemptiveExtend;
}

bool VDFileAsyncSDL3::IsOpen() {
	return mbOpen;
}

void VDFileAsyncSDL3::Open(const wchar_t *pszFilename, uint32 count, uint32 bufferSize) {
	mFile.open(pszFilename, nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
	mbOpen = true;
	mFastWritePos = 0;
	mFileSize = 0;
	mBufferLevel = 0;
}

void VDFileAsyncSDL3::Open(VDFileHandle h, uint32 count, uint32 bufferSize) {
	// Not needed for AVI writer — the wchar_t path version is used.
	throw MyError("VDFileAsyncSDL3::Open(handle) is not implemented.");
}

void VDFileAsyncSDL3::Close() {
	if (!mbOpen)
		return;

	FlushBuffer();
	mFile.close();
	mbOpen = false;
}

void VDFileAsyncSDL3::FastWrite(const void *pData, uint32 bytes) {
	if (!bytes)
		return;

	// If pData is null, write zeros (used for alignment padding)
	if (!pData) {
		while (bytes > 0) {
			uint32 space = kBufferSize - mBufferLevel;
			uint32 tc = bytes < space ? bytes : space;
			memset(mBuffer + mBufferLevel, 0, tc);
			mBufferLevel += tc;
			bytes -= tc;

			if (mBufferLevel >= kBufferSize)
				FlushBuffer();
		}
		return;
	}

	const uint8 *src = (const uint8 *)pData;
	while (bytes > 0) {
		uint32 space = kBufferSize - mBufferLevel;
		uint32 tc = bytes < space ? bytes : space;
		memcpy(mBuffer + mBufferLevel, src, tc);
		mBufferLevel += tc;
		src += tc;
		bytes -= tc;

		if (mBufferLevel >= kBufferSize)
			FlushBuffer();
	}
}

void VDFileAsyncSDL3::FastWriteEnd() {
	FlushBuffer();
}

void VDFileAsyncSDL3::Write(sint64 pos, const void *pData, uint32 bytes) {
	FlushBuffer();
	mFile.seek(pos);
	mFile.write(pData, bytes);

	sint64 endPos = pos + bytes;
	if (endPos > mFileSize)
		mFileSize = endPos;
}

bool VDFileAsyncSDL3::Extend(sint64 pos) {
	// On POSIX/SDL3, we don't pre-extend files.
	return true;
}

void VDFileAsyncSDL3::Truncate(sint64 pos) {
	FlushBuffer();
	mFile.seek(pos);
	mFile.truncate();
	mFileSize = pos;
}

void VDFileAsyncSDL3::SafeTruncateAndClose(sint64 pos) {
	if (!mbOpen)
		return;

	try {
		FlushBuffer();
		mFile.seek(pos);
		mFile.truncate();
	} catch (...) {
	}

	try {
		mFile.close();
	} catch (...) {
	}

	mbOpen = false;
}

sint64 VDFileAsyncSDL3::GetFastWritePos() {
	return mFastWritePos;
}

sint64 VDFileAsyncSDL3::GetSize() {
	return mFileSize;
}

void VDFileAsyncSDL3::FlushBuffer() {
	if (mBufferLevel > 0) {
		mFile.seek(mFastWritePos);
		mFile.write(mBuffer, mBufferLevel);
		mFastWritePos += mBufferLevel;

		if (mFastWritePos > mFileSize)
			mFileSize = mFastWritePos;

		mBufferLevel = 0;
	}
}

IVDFileAsync *VDCreateFileAsync(IVDFileAsync::Mode) {
	return new VDFileAsyncSDL3;
}
