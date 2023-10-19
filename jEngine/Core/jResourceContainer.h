#pragma once

// Temporary array for one frame data
template <typename T, int32 NumOfInlineData = 10>
struct jResourceContainer
{
    jResourceContainer<T, NumOfInlineData>() = default;
    jResourceContainer<T, NumOfInlineData>(const jResourceContainer<T, NumOfInlineData>& InData)
    {
        if constexpr (std::is_trivially_copyable<T>::value)
        {
            memcpy(&Data[0], &InData.Data[0], sizeof(T) * InData.NumOfData);
        }
        else
        {
            for (int32 i = 0; i < InData.NumOfData; ++i)
                Data[i] = InData[i];
        }
        NumOfData = InData.NumOfData;
    }

    void Add(T InElement)
    {
        check(NumOfInlineData > NumOfData);

        Data[NumOfData] = InElement;
        ++NumOfData;
    }

    void Add(void* InElementAddress, int32 InCount)
    {
        check(NumOfInlineData > (NumOfData + InCount));
        check(InElementAddress);

        if constexpr (std::is_trivially_copyable<T>::value)
        {
            memcpy(&Data[NumOfData], InElementAddress, InCount * sizeof(T));
        }
        else
        {
            for (int32 i = 0; i < InCount; ++i)
                Data[NumOfData + i] = *((T*)InElementAddress[i]);
        }
        NumOfData += InCount;
    }

    void PopBack()
    {
        if (NumOfData > 0)
        {
            --NumOfData;
        }
    }

    const T& Back()
    {
        if (NumOfData > 0)
        {
            return Data[NumOfData-1];
        }
        static auto EmptyValue = T();
        return EmptyValue;
    }

    template <typename T2>
    static void GetHash(size_t& InOutHash, int32 index, const T2& InData)
    {
        InOutHash ^= (InData->GetHash() << index);
    }

    size_t GetHash() const
    {
        size_t Hash = 0;
        for (int32 i = 0; i < NumOfData; ++i)
        {
            //Hash ^= (Data[i]->GetHash() << i);
            GetHash(Hash, i, Data[i]);
        }
        return Hash;
    }

    FORCEINLINE void Reset()
    {
        NumOfData = 0;
    }

    FORCEINLINE jResourceContainer<T, NumOfInlineData>& operator = (const jResourceContainer<T, NumOfInlineData>& InData)
    {
        if constexpr (std::is_trivially_copyable<T>::value)
        {
            memcpy(&Data[0], &InData.Data[0], sizeof(T) * InData.NumOfData);
        }
        else
        {
            for (int32 i = 0; i < InData.NumOfData; ++i)
                Data[i] = InData[i];
        }
        NumOfData = InData.NumOfData;
        return *this;
    }

    FORCEINLINE T& operator[] (int32 InIndex)
    {
        check(InIndex < NumOfData);
        return Data[InIndex];
    }

    FORCEINLINE const T& operator[] (int32 InIndex) const
    {
        check(InIndex < NumOfData);
        return Data[InIndex];
    }

    T Data[NumOfInlineData];
    int32 NumOfData = 0;
};
