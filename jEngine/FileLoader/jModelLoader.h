#pragma once

#include <exception>

class jMeshObject;

#define MINIMUM_WORKER_THREAD_COUNT 10

struct jTask
{
    using HashFunc = std::function<size_t()>;
    using Func = std::function<void()>;

    void Do()
    {
        pFunc();
    }

    HashFunc pHashFunc = nullptr;
    Func pFunc = nullptr;
    size_t GetHash() const { return pHashFunc ? pHashFunc() : 0; }
    bool IsValid() const { return !!pFunc; }
};

class jThreadPool 
{
public:
    jThreadPool(size_t num_threads);
    ~jThreadPool();
    
    void Enqueue(const jTask& file_path);
    
    // Wait for the batch to finish, then propagate its first worker failure.
    void WaitAll();
    
private:
    std::vector<std::thread> WorkerThreads;
    std::condition_variable ConditionVariable;
    std::condition_variable CompletionConditionVariable;
    std::atomic<bool> IsStop;

    std::queue<jTask> TaskQueue;
    std::mutex QueueLock;
    std::atomic<int32> RemainTask{0};
    std::unordered_set<size_t> ProcessingTaskHashes;
    std::exception_ptr TaskException;
};


class jModelLoader
{
public:
	~jModelLoader();

	static jModelLoader& GetInstance()
	{
		if (!_instance)
			_instance = new jModelLoader();
		return *_instance;
	}

	//bool ConvertToFBX(const char* destFilename, const char* filename) const;

	jMeshObject* LoadFromFile(const char* filename);
	jMeshObject* LoadFromFile(const char* filename, const char* materialRootDir);

private:
    jThreadPool ThreadPool;

private:
	jModelLoader();

	static jModelLoader* _instance;
};
