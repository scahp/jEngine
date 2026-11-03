#include <pch.h>
#include "jFile.h"

uint64 jFile::GetFileTimeStamp(const char* filename)
{
	if (!filename || !*filename)
		return 0;

    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(filename, ec);
    return ec ? 0 : static_cast<uint64>(stamp.time_since_epoch().count());
}

void jFile::SearchFilesRecursive(std::vector<std::string>& OutFiles, const std::string& InTargetDirectory, const std::vector<std::string>& extensions)
{
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(InTargetDirectory, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code entryError;
        if (!it->is_regular_file(entryError)) continue;
        const auto ext = it->path().extension().string();
        if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
            OutFiles.push_back(it->path().string());
    }
}

std::string jFile::ExtractFileName(const std::string& path)
{
    // Find path splitter(Windows, POSIX system)
    size_t lastSlashPos = path.find_last_of("/\\");

    if (lastSlashPos != std::string::npos)
        return path.substr(lastSlashPos + 1);	// Return the string after path splitter
    
	return path;
}

jFile::~jFile()
{
	CloseFile();
}

bool jFile::OpenFile(const char* szFileName, FileType::Enum fileType /*= FileType::BINARY*/, ReadWriteType::Enum readWriteType /*= ReadWriteType::READ*/)
{
	CloseFile();
	m_buffer.clear();
	if (!szFileName || !*szFileName)
		return false;

	std::string option;
	switch (readWriteType)
	{
	case ReadWriteType::READ:
		option += "r";
		break;
	case ReadWriteType::WRITE:
		option += "w";
		break;
	case ReadWriteType::APPEND:
		option += "a";
		break;
	case ReadWriteType::READ_UPDATE:
		option += "r+";
		break;
	case ReadWriteType::WRITE_UPDATE:
		option += "w+";
		break;
	case ReadWriteType::APPEND_UPDATE:
		option += "a+";
		break;
	default:
		return false;
	}

	switch(fileType)
	{
	case FileType::BINARY:
		option += "b";
		break;
	case FileType::TEXT:
		#ifdef _WIN32
		option += "t";
		#endif
		break;
	default:
		return false;
	}

	fopen_s(&m_fp, szFileName, option.c_str());

	return (nullptr != m_fp);
}

size_t jFile::ReadFileToBuffer(bool appendToEndofBuffer/* = true*/, unsigned int index/* = 0*/, unsigned int count/* = 0*/)
{
	if (!m_fp)
		return 0;

	// Size and seek errors must not become huge unsigned allocations. Use the CRT's
	// 64-bit file positions on Windows, where long/ftell are limited to 2 GiB.
	clearerr(m_fp);
#ifdef _WIN32
	if (_fseeki64(m_fp, 0, SEEK_END) != 0)
		return 0;
	const auto fileSize = _ftelli64(m_fp);
	if (fileSize < 0 || _fseeki64(m_fp, index, SEEK_SET) != 0)
		return 0;
#else
	if (fseeko(m_fp, 0, SEEK_END) != 0)
		return 0;
	const auto fileSize = ftello(m_fp);
	if (fileSize < 0 || fseeko(m_fp, index, SEEK_SET) != 0)
		return 0;
#endif
	const uint64 remaining = uint64(index) < uint64(fileSize) ? uint64(fileSize) - index : 0;
	const uint64 requested = count ? std::min(uint64(count), remaining) : remaining;
	const size_t writeStartIndex = appendToEndofBuffer && !m_buffer.empty() ? m_buffer.size() - 1 : 0;
	if (requested > m_buffer.max_size() - writeStartIndex - 1)
		return 0;
	m_buffer.resize(writeStartIndex + size_t(requested) + 1);
	const size_t readSize = fread(m_buffer.data() + writeStartIndex, sizeof(ELEMENT_TYPE), size_t(requested), m_fp);
	m_buffer.resize(writeStartIndex + readSize + 1);
	m_buffer[writeStartIndex + readSize] = 0;
	return readSize;
}

void jFile::CloseFile()
{
	if (m_fp)
	{
		fclose(m_fp);
		m_fp = NULL;
	}
}

const char* jFile::GetBuffer(unsigned int index /*= 0*/, unsigned int count /*= 0*/) const
{
	if (m_buffer.empty())
	{
		return nullptr;
	}

	const size_t payloadSize = m_buffer.size() - 1;
	if (index > payloadSize || count > payloadSize - index)
		return nullptr;
	return m_buffer.data() + index;
}

bool jFile::GetBuffer(FILE_BUFFER& outBuffer, const char* startToken, const char* endToken)
{
	if (m_buffer.empty() || !startToken || !endToken)
	{
		return false;
	}

	char* startPos = strstr(&m_buffer[0], startToken);

	if (startPos)
	{
		size_t startTokenLength = strlen(startToken);
		startPos += startTokenLength;

		char* endPos = strstr(startPos, endToken);
		size_t count = 0;
		if (endPos)
		{
			count = endPos - startPos;
		}
		else
		{
			count = &m_buffer[m_buffer.size() - 1] - startPos;
		}

		if (0 < count)
		{
			outBuffer.resize(count + 1);
			memcpy(&outBuffer[0], startPos, count * sizeof(ELEMENT_TYPE));
			outBuffer[count] = 0;

			return true;
		}
	}

	return false;
}
