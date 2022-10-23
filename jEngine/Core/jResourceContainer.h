#pragma once

// Temporary array for one frame data
template <typename T, int32 NumOfInlineData = 10>
struct jResourceContainer
{
    jResourceContainer<T, NumOfInlineData>() = default;
    jResourceContainer<T, NumOfInlineData>(const jResourceContainer<T, NumOfInlineData>& InData)
    {
        memcpy(&Data[0], &InData.Data[0], sizeof(jResourceContainer<T, NumOfInlineData>));
        NumOfData = InData.NumOfData;
    }

    void Add(T* ShaderBindingLayout)
    {
        check(NumOfInlineData > NumOfData);

        Data[NumOfData] = ShaderBindingLayout;
        ++NumOfData;
    }

    void PopBack()
    {
        if (NumOfData > 0)
        {
            --NumOfData;
        }
    }

    T* Back()
    {
        if (NumOfData > 0)
        {
            return Data[NumOfData-1];
        }
        return nullptr;
    }

    size_t GetHash() const
    {
        size_t Hash = 0;
        for (int32 i = 0; i < NumOfData; ++i)
        {
            Hash ^= (Data[i]->GetHash() ^ i);
        }
        return Hash;
    }

    FORCEINLINE void Reset()
    {
        NumOfData = 0;
    }

    FORCEINLINE jResourceContainer<T, NumOfInlineData>& operator = (const jResourceContainer<T, NumOfInlineData>& In)
    {
        memcpy(&Data[0], &In.Data[0], sizeof(uint8));
        NumOfData = In.NumOfData;
        return *this;
    }

    FORCEINLINE T* operator[] (int32 InIndex) const
    {
        check(InIndex < NumOfData);
        return Data[InIndex];
    }

    T* Data[NumOfInlineData];
    int32 NumOfData = 0;
};
