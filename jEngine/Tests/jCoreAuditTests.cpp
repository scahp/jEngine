#include "pch.h"
#include "Core/TResourcePool.h"
#include "Core/jMemStackAllocator.h"
#include "Core/jName.h"
#include "Math/Quaternion.h"

namespace
{
    struct CoreAuditLock
    {
        void LockRead() { ++ReadCount; }
        void UnlockRead() { }
        void LockWrite() { ++WriteCount; }
        void UnlockWrite() { }
        int ReadCount = 0;
        int WriteCount = 0;
    };

    struct CoreAuditInitializer
    {
        size_t Key;
        size_t GetHash() const { return Key; }
    };

    struct CoreAuditResource
    {
        explicit CoreAuditResource(const CoreAuditInitializer& InInitializer) : Key(InInitializer.Key) { }
        void Initialize() { Initialized = true; }
        size_t Key;
        bool Initialized = false;
    };
}

int RunCoreAuditTests()
{
    int Failures = 0;
    const auto Expect = [&](bool Condition, const char* Name)
    {
        std::cout << (Condition ? "PASS " : "FAIL ") << Name << std::endl;
        Failures += !Condition;
    };

    const auto RotationRoundtrip = [](const Matrix& Rotation)
    {
        const Quaternion Converted = Quaternion::CreateRotation(Rotation);
        const Vector Probe(1.0f, -2.0f, 3.0f);
        return IsNearlyEqual(Converted.GetLengthSq(), 1.0f)
            && Converted.GetMatrix() == Rotation
            && IsNearlyEqual(Converted.TransformDirection(Probe), Rotation.TransformDirection(Probe));
    };
    const Matrix QuaternionIdentity = Quaternion(0, 0, 0, 1).GetMatrix();
    Expect(QuaternionIdentity == Matrix(IdentityType) && IsNearlyEqual(QuaternionIdentity.GetDeterminant(), 1.0f),
        "quaternion identity produces invertible homogeneous matrix");
    const Matrix Composed = Matrix::MakeTranslate(10, 20, 30) * QuaternionIdentity;
    Expect(IsNearlyEqual(Composed.TransformPoint(Vector(1, 2, 3)), Vector(11, 22, 33)),
        "quaternion matrix preserves translation during composition");
    Expect(RotationRoundtrip(Matrix(IdentityType)), "quaternion identity matrix roundtrip");
    Expect(RotationRoundtrip(Matrix::MakeRotateX(PI)), "quaternion 180-degree X matrix roundtrip");
    Expect(RotationRoundtrip(Matrix::MakeRotateY(PI)), "quaternion 180-degree Y matrix roundtrip");
    Expect(RotationRoundtrip(Matrix::MakeRotateZ(PI)), "quaternion 180-degree Z matrix roundtrip");
    // Exact half-turn matrices around normalized (1,1,0), (1,0,1) and (0,1,1).
    Expect(RotationRoundtrip(Matrix(0,1,0,0, 1,0,0,0, 0,0,-1,0, 0,0,0,1)),
        "quaternion tied XY half-turn matrix roundtrip");
    Expect(RotationRoundtrip(Matrix(0,0,1,0, 0,-1,0,0, 1,0,0,0, 0,0,0,1)),
        "quaternion tied XZ half-turn matrix roundtrip");
    Expect(RotationRoundtrip(Matrix(-1,0,0,0, 0,0,1,0, 0,1,0,0, 0,0,0,1)),
        "quaternion tied YZ half-turn matrix roundtrip");
    const float Third = 1.0f / 3.0f;
    Expect(RotationRoundtrip(Matrix(-Third,2*Third,2*Third,0, 2*Third,-Third,2*Third,0,
        2*Third,2*Third,-Third,0, 0,0,0,1)), "quaternion tied XYZ half-turn matrix roundtrip");
    Expect(RotationRoundtrip(Matrix::MakeRotateX(DegreeToRadian(179.99f))),
        "quaternion near-180-degree matrix roundtrip");
    Expect(RotationRoundtrip(Matrix::MakeRotateZ(DegreeToRadian(90.0f))),
        "quaternion positive-trace matrix roundtrip");

#ifndef _WIN32
    struct { char Text[8]; char Guard[16]; } ScanBuffer;
    std::memset(ScanBuffer.Text, 'T', sizeof(ScanBuffer.Text));
    std::memset(ScanBuffer.Guard, 'G', sizeof(ScanBuffer.Guard));
    const int Oversized = sscanf_s("abcdefghijklmn", "%s", ScanBuffer.Text, unsigned(sizeof(ScanBuffer.Text)));
    Expect(Oversized == 0
        && std::all_of(ScanBuffer.Text, ScanBuffer.Text + sizeof(ScanBuffer.Text), [](char Value) { return Value == 'T'; })
        && std::all_of(ScanBuffer.Guard, ScanBuffer.Guard + sizeof(ScanBuffer.Guard), [](char Value) { return Value == 'G'; }),
        "secure scan rejects oversized token without writing buffer or guard");
    char Word[16]{};
    int Number = -1;
    float Value = -1;
    Expect(sscanf_s("tag 7 1.5", "%s %d %f", Word, unsigned(sizeof(Word)), &Number, &Value) == 3
        && std::strcmp(Word, "tag") == 0 && Number == 7 && IsNearlyEqual(Value, 1.5f),
        "secure scan consumes string capacity before numeric arguments");
    Expect(sscanf_s("9 tag 2.5", "%d %s %f", &Number, Word, unsigned(sizeof(Word)), &Value) == 3
        && Number == 9 && std::strcmp(Word, "tag") == 0 && IsNearlyEqual(Value, 2.5f),
        "secure scan supports numeric fields before string arguments");
    char SecondWord[16]{};
    Expect(sscanf_s("first second 11", "%s %s %d", Word, unsigned(sizeof(Word)), SecondWord,
        unsigned(sizeof(SecondWord)), &Number) == 3 && std::strcmp(Word, "first") == 0
        && std::strcmp(SecondWord, "second") == 0 && Number == 11, "secure scan consumes multiple string capacities");
    char Name[32]{};
    Expect(sscanf_s(" name mesh with spaces\n", " name %[^\t\n]s", Name, unsigned(sizeof(Name))) == 1
        && std::strcmp(Name, "mesh with spaces") == 0, "secure scan supports scene-loader scanset format");
    Expect(sscanf_s("very long mesh name\n", "%[^\n]", ScanBuffer.Text, unsigned(sizeof(ScanBuffer.Text))) == 0
        && std::all_of(ScanBuffer.Guard, ScanBuffer.Guard + sizeof(ScanBuffer.Guard), [](char C) { return C == 'G'; }),
        "secure scanset rejects oversized token");
    Expect(sscanf_s("abcdef 12", "%3s%*s %d", Word, unsigned(sizeof(Word)), &Number) == 2
        && std::strcmp(Word, "abc") == 0 && Number == 12, "secure scan preserves field width and assignment suppression");
    char Exact[4]{};
    Expect(sscanf_s("abc", "%s", Exact, unsigned(sizeof(Exact))) == 1 && std::strcmp(Exact, "abc") == 0
        && sscanf_s("abcd", "%s", Exact, unsigned(sizeof(Exact))) == 0 && std::strcmp(Exact, "abc") == 0,
        "secure scan reserves string terminator at exact capacity boundary");
    float ScanX = 0, ScanY = 0, ScanZ = 0;
    Expect(sscanf_s("-1.5,0.25,7", "%f,%f,%f", &ScanX, &ScanY, &ScanZ) == 3
        && IsNearlyEqual(ScanX, -1.5f) && IsNearlyEqual(ScanY, 0.25f) && IsNearlyEqual(ScanZ, 7.0f),
        "secure scan preserves numeric clipboard format");
    Expect(sscanf_s("100% tag", "%d%% %s", &Number, Word, unsigned(sizeof(Word))) == 2
        && Number == 100 && std::strcmp(Word, "tag") == 0, "secure scan matches literal percent");
    Number = 17;
    Expect(sscanf_s("word", "%d", &Number) == 0 && sscanf_s("", "%d", &Number) == EOF && Number == 17,
        "secure scan preserves matching failure and end-of-input results");
    double Unsupported = 3;
    errno = 0;
    Expect(sscanf_s("2", "%lf", &Unsupported) == EOF && errno == EINVAL && Unsupported == 3,
        "secure scan rejects unsupported length modifier without writing");
#endif

    // Interning must retain the exact supplied byte span, including embedded NULs.
    const char Span[] = { 'j', 'g', 's', '-', 's', 'p', 'a', 'n' };
    const jName SpanName(Span, sizeof(Span));
    Expect(SpanName.GetStringLength() == sizeof(Span)
        && std::memcmp(SpanName.ToStr(), Span, sizeof(Span)) == 0, "name unterminated byte span");
    const char Embedded[] = { 'j', 'g', 's', '\0', 'n', 'a', 'm', 'e' };
    const jName EmbeddedName(Embedded, sizeof(Embedded));
    Expect(EmbeddedName.GetStringLength() == sizeof(Embedded)
        && std::memcmp(EmbeddedName.ToStr(), Embedded, sizeof(Embedded)) == 0, "name embedded NUL span");
    const jName FromHash(SpanName.GetNameHash());
    Expect(FromHash.GetStringLength() == sizeof(Span), "hash-only name resolves length before text");
    Expect(std::strcmp(FromHash.ToStr(), "jgs-span") == 0, "hash-only name resolves text");

    jName ChangedName("core-audit-before");
    ChangedName.ToWStr();
    ChangedName.Set("core-audit-after", std::strlen("core-audit-after"));
    Expect(std::wcscmp(ChangedName.ToWStr(), L"core-audit-after") == 0, "name Set invalidates wide cache");
    const jName ExistingName("core-audit-existing");
    ChangedName.Set(ExistingName.ToStr(), ExistingName.GetStringLength());
    Expect(std::wcscmp(ChangedName.ToWStr(), L"core-audit-existing") == 0, "name Set to interned value invalidates wide cache");
    ChangedName = SpanName;
    Expect(std::wcscmp(ChangedName.ToWStr(), L"jgs-span") == 0, "name assignment replaces wide cache");
    const jName WideName(L"\uCF54\uC5B4-\uC774\uB984");
    Expect(std::wcscmp(WideName.ToWStr(), L"\uCF54\uC5B4-\uC774\uB984") == 0, "name Unicode roundtrip");

    alignas(DEFAULT_ALIGNMENT) uint8 ChunkBytes[64]{};
    jMemoryChunk Chunk(ChunkBytes, 1, sizeof(ChunkBytes));
    Expect(Chunk.Alloc(49) == nullptr && Chunk.Offset == 1, "chunk padding cannot exceed capacity");
    Expect(Chunk.Alloc(48) == ChunkBytes + DEFAULT_ALIGNMENT && Chunk.Offset == sizeof(ChunkBytes)
        && Chunk.Alloc(1) == nullptr, "chunk exact aligned capacity");
    Chunk.Offset = DEFAULT_ALIGNMENT;
    Expect(Chunk.Alloc(std::numeric_limits<size_t>::max()) == nullptr && Chunk.Offset == DEFAULT_ALIGNMENT,
        "chunk rejects byte-count overflow");

    auto* Pages = jPageAllocator::Get();
    Pages->Flush();
    auto* Small = Pages->AllocateBigSize(5000);
    auto* Middle = Pages->AllocateBigSize(9000);
    auto* Large = Pages->AllocateBigSize(17000);
    auto* Inserted = Pages->AllocateBigSize(13000);
    Pages->FreeBigSize(Middle);
    Pages->FreeBigSize(Large);
    Pages->FreeBigSize(Small);
    Pages->FreeBigSize(Inserted);
    auto* ReusedInserted = Pages->AllocateBigSize(12000);
    auto* ReusedSmall = Pages->AllocateBigSize(5000);
    auto* ReusedMiddle = Pages->AllocateBigSize(9000);
    auto* ReusedLarge = Pages->AllocateBigSize(17000);
    Expect(ReusedInserted == Inserted && ReusedSmall == Small && ReusedMiddle == Middle && ReusedLarge == Large,
        "big-page sorted insertion and removal");
    auto* Fresh = Pages->AllocateBigSize(5000);
    Expect(Fresh != ReusedInserted && Fresh != ReusedSmall && Fresh != ReusedMiddle && Fresh != ReusedLarge,
        "live big pages cannot be reused");
    Pages->FreeBigSize(ReusedInserted);
    Pages->FreeBigSize(ReusedSmall);
    Pages->FreeBigSize(ReusedMiddle);
    Pages->FreeBigSize(ReusedLarge);
    Pages->FreeBigSize(Fresh);

    jMemStack Arena;
    const size_t Sizes[] = { 1, 15, 16, 17, size_t(jPageAllocator::MaxMemoryChunkSize) - 1,
        size_t(jPageAllocator::MaxMemoryChunkSize), size_t(jPageAllocator::MaxMemoryChunkSize) + 1, 8193 };
    std::vector<uint8*> Addresses;
    bool Aligned = true;
    for (size_t Size : Sizes)
    {
        auto* Address = static_cast<uint8*>(Arena.Alloc(Size));
        Aligned &= Address && (reinterpret_cast<uintptr_t>(Address) % DEFAULT_ALIGNMENT == 0);
        Addresses.push_back(Address);
        std::memset(Address, int(Addresses.size()), Size);
    }
    bool Intact = true;
    for (size_t i = 0; i < Addresses.size(); ++i)
        Intact &= std::all_of(Addresses[i], Addresses[i] + Sizes[i], [i](uint8 Value) { return Value == i + 1; });
    Expect(Aligned && Intact, "arena boundary sizes are aligned and do not overlap");
    Arena.Flush();
    Expect(Pages->FreeChunk && Pages->FreeChunkBigSize, "arena Flush returns small and big pages");
    auto* HeldPage = Pages->Allocate();
    auto* HeldByte = static_cast<uint8*>(HeldPage->Alloc(1));
    *HeldByte = 93;
    Pages->Flush();
    Expect(!Pages->FreeChunk && !Pages->FreeChunkBigSize && *HeldByte == 93,
        "page Flush clears both caches and preserves checked-out pages");
    Pages->Free(HeldPage);
    Pages->Flush();
    Pages->Flush();
    Expect(!Pages->FreeChunk && !Pages->FreeChunkBigSize, "empty page Flush is repeatable");

    bool RejectedBigSize = false;
    try { Pages->AllocateBigSize(std::numeric_limits<uint64>::max()); }
    catch (const std::bad_alloc&) { RejectedBigSize = true; }
    Expect(RejectedBigSize, "page allocator rejects allocation size overflow");
    bool RejectedElementCount = false;
    try { jMemStackAllocator<uint64>().allocate(std::numeric_limits<size_t>::max() / sizeof(uint64) + 1); }
    catch (const std::bad_alloc&) { RejectedElementCount = true; }
    Expect(RejectedElementCount, "STL arena allocator rejects element-count overflow");

    TResourcePool<CoreAuditResource, CoreAuditLock> LockPool;
    CoreAuditResource Resource(CoreAuditInitializer{1});
    LockPool.Add(CoreAuditInitializer{1}, &Resource);
    Expect(LockPool.Lock.WriteCount == 1 && LockPool.Lock.ReadCount == 0, "pool Add acquires write lock");
    std::vector<CoreAuditResource*> Snapshot;
    LockPool.GetAllResource(Snapshot);
    Expect(LockPool.Lock.ReadCount == 1 && Snapshot.size() == 1 && Snapshot.front() == &Resource,
        "pool snapshot acquires read lock");
    Expect(LockPool.Release(size_t(1)) == &Resource && LockPool.Lock.WriteCount == 2,
        "pool Release acquires write lock");

    // Each worker owns its resources and arena. Only the production pool and page cache are shared.
    TResourcePool<CoreAuditResource, jMutexRWLock> ConcurrentPool;
    std::atomic<bool> ConcurrentIntact = true;
    std::vector<std::thread> Workers;
    for (size_t Worker = 0; Worker < 4; ++Worker)
    {
        Workers.emplace_back([&, Worker]
        {
            jMemStack LocalArena;
            for (size_t Iteration = 0; Iteration < 64; ++Iteration)
            {
                const CoreAuditInitializer Initializer{Worker * 64 + Iteration + 1};
                auto* Created = ConcurrentPool.GetOrCreate(Initializer);
                if (!Created || !Created->Initialized || Created->Key != Initializer.Key)
                    ConcurrentIntact.store(false);
                auto* Released = ConcurrentPool.Release(Initializer);
                if (Released != Created)
                    ConcurrentIntact.store(false);
                delete Released;
                auto* Added = new CoreAuditResource(Initializer);
                ConcurrentPool.Add(Initializer, Added);
                std::vector<CoreAuditResource*> LocalSnapshot;
                ConcurrentPool.GetAllResource(LocalSnapshot);
                Released = ConcurrentPool.Release(Initializer);
                if (Released != Added)
                    ConcurrentIntact.store(false);
                delete Released;

                const size_t Size = 8193 + Worker;
                auto* Bytes = static_cast<uint8*>(LocalArena.Alloc(Size));
                auto* SmallBytes = static_cast<uint8*>(LocalArena.Alloc(17));
                const uint8 Pattern = uint8(Worker + 1);
                std::memset(Bytes, Pattern, Size);
                std::memset(SmallBytes, Pattern, 17);
                std::this_thread::yield();
                if (!std::all_of(Bytes, Bytes + Size, [Pattern](uint8 Value) { return Value == Pattern; })
                    || !std::all_of(SmallBytes, SmallBytes + 17, [Pattern](uint8 Value) { return Value == Pattern; }))
                    ConcurrentIntact.store(false);
                LocalArena.Flush();
            }
        });
    }
    for (auto& Worker : Workers)
        Worker.join();
    Snapshot.clear();
    ConcurrentPool.GetAllResource(Snapshot);
    Expect(ConcurrentIntact.load() && Snapshot.empty(), "concurrent pool mutation, snapshots and per-owner arena reuse");
    Pages->Flush();

    std::cout << "Core audit checks: " << (Failures ? "FAILED" : "PASSED") << std::endl;
    return Failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
