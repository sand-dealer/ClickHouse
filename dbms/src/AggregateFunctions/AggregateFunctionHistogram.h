#pragma once

#include <Common/Arena.h>
#include <Common/PODArray.h>
#include <Common/AutoArray.h>

#include <Columns/ColumnVector.h>
#include <Columns/ColumnTuple.h>
#include <Columns/ColumnArray.h>

#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeTuple.h>

#include <IO/WriteBuffer.h>
#include <IO/ReadBuffer.h>
#include <IO/VarInt.h>

#include <AggregateFunctions/IAggregateFunction.h>

#include <queue>
#include <stddef.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int TOO_LARGE_ARRAY_SIZE;
}

/**
 * distance compression algorigthm implementation
 * http://jmlr.org/papers/volume11/ben-haim10a/ben-haim10a.pdf
 */
class AggregateFunctionHistogramData
{
public:
    using Mean = Float64;
    using Weight = Float64;

private:
    struct WeightedValue
    {
        Mean mean;
        Weight weight;

        WeightedValue operator+ (const WeightedValue& other)
        {
            return {mean + other.weight * (other.mean - mean) / (other.weight + weight), other.weight + weight};
        }
    };

private:
    // quantity of stored weighted-values
    UInt32 size;

    // calculated lower and upper bounds of seen points
    Mean lower_bound;
    Mean upper_bound;

    static constexpr Mean epsilon = 1e-8;

    // Weighted values representation of histogram.
    WeightedValue points[0];

private:
    void sort()
    {
        std::sort(points, points + size,
            [](const WeightedValue & first, const WeightedValue & second)
            {
                return first.mean < second.mean;
            });
    }

    /**
     * Repeatedly fuse most close values until max_bins bins left
     */
    void compress(UInt32 max_bins)
    {
        sort();
        auto new_size = size;
        if (size <= max_bins)
            return;

        // Maintain doubly-linked list of "active" points
        // and store neighbour pairs in priority queue by distance
        AutoArray<UInt32> previous(size + 1);
        AutoArray<UInt32> next(size + 1);
        AutoArray<bool> active(size + 1, true);
        active[size] = false;

        auto delete_node = [&](UInt32 i)
        {
            previous[next[i]] = previous[i];
            next[previous[i]] = next[i];
            active[i] = false;
        };

        for (size_t i = 0; i <= size; ++i)
        {
            previous[i] = i - 1;
            next[i] = i + 1;
        }

        next[size] = 0;
        previous[0] = size;

        using QueueItem = std::pair<Mean, UInt32>;

        std::vector<QueueItem> storage;
        storage.reserve(2 * size - max_bins);
        std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;

        auto quality = [&](UInt32 i) { return points[next[i]].mean - points[i].mean; };

        for (size_t i = 0; i + 1 < size; ++i)
            queue.push({quality(i), i});

        while (new_size > max_bins && !queue.empty())
        {
            auto min_item = queue.top();
            queue.pop();
            auto left = min_item.second;
            auto right = next[left];

            if (!active[left] || !active[right] || quality(left) > min_item.first)
                continue;

            points[left] = points[left] + points[right];

            delete_node(right);
            if (active[next[left]])
                queue.push({quality(left), left});
            if (active[previous[left]])
                queue.push({quality(previous[left]), previous[left]});

            --new_size;
        }

        size_t left = 0;
        for (size_t right = 0; right < size; ++right)
        {
            if (active[right])
            {
                points[left] = points[right];
                ++left;
            }
        }
        size = new_size;
    }

    /***
     * Delete too close points from histogram.
     * Assumes that points are sorted.
     */
    void unique()
    {
        size_t left = 0;

        for (auto right = left + 1; right < size; right++)
        {
            if (points[left].mean + epsilon >= points[right].mean)
            {
                points[left] = points[left] + points[right];
            }
            else
            {
                ++left;
                points[left] = points[right];
            }
        }
        size = left + 1;
    }

public:
    AggregateFunctionHistogramData()
        : size(0)
        , lower_bound(std::numeric_limits<Mean>::max())
        , upper_bound(std::numeric_limits<Mean>::lowest())
    {
        static_assert(offsetof(AggregateFunctionHistogramData, points) == sizeof(AggregateFunctionHistogramData), "points should be last member");
    }

    static size_t structSize(size_t max_bins)
    {
        return sizeof(AggregateFunctionHistogramData) + max_bins * 2 * sizeof(WeightedValue);
    }

    void insertResultInto(ColumnVector<Mean>& to_lower, ColumnVector<Mean>& to_upper, ColumnVector<Weight>& to_weights, UInt32 max_bins) {
        compress(max_bins);
        unique();

        for (size_t i = 0; i < size; ++i)
        {
            to_lower.insert((i == 0) ? lower_bound : (points[i].mean + points[i - 1].mean) / 2);
            to_upper.insert((i + 1 == size) ? upper_bound : (points[i].mean + points[i + 1].mean) / 2);

            // linear density approximation
            Weight lower_weight = (i == 0) ? points[i].weight : ((points[i - 1].weight) + points[i].weight * 3) / 4;
            Weight upper_weight = (i + 1 == size) ? points[i].weight : (points[i + 1].weight + points[i].weight * 3) / 4;
            to_weights.insert((lower_weight + upper_weight) / 2);
        }
    }

    void add(Mean value, Weight weight, UInt32 max_bins)
    {
        points[size] = {value, weight};
        ++size;
        lower_bound = std::min(lower_bound, value);
        upper_bound = std::max(upper_bound, value);

        if (size >= max_bins * 2)
            compress(max_bins);
    }

    void merge(const AggregateFunctionHistogramData& other, UInt32 max_bins)
    {
        lower_bound = std::min(lower_bound, other.lower_bound);
        upper_bound = std::max(lower_bound, other.upper_bound);
        for (size_t i = 0; i < other.size; i++)
        {
            add(other.points[i].mean, other.points[i].weight, max_bins);
        }
    }

    void write(WriteBuffer & buf) const
    {
        buf.write(reinterpret_cast<const char *>(&lower_bound), sizeof(lower_bound));
        buf.write(reinterpret_cast<const char *>(&upper_bound), sizeof(upper_bound));

        writeVarUInt(size, buf);
        buf.write(reinterpret_cast<const char *>(points), size * sizeof(WeightedValue));
    }

    void read(ReadBuffer & buf, UInt32 max_bins)
    {
        buf.read(reinterpret_cast<char *>(&lower_bound), sizeof(lower_bound));
        buf.read(reinterpret_cast<char *>(&upper_bound), sizeof(upper_bound));

        readVarUInt(size, buf);

        if (size > max_bins * 2)
            throw Exception("Too many bins", ErrorCodes::TOO_LARGE_ARRAY_SIZE);

        buf.read(reinterpret_cast<char *>(points), size * sizeof(WeightedValue));
    }
};

template <typename T>
class AggregateFunctionHistogram final: public IAggregateFunctionDataHelper<AggregateFunctionHistogramData, AggregateFunctionHistogram<T>>
{
private:
    using Data = AggregateFunctionHistogramData;

    const UInt32 max_bins;

public:
    AggregateFunctionHistogram(UInt32 max_bins)
        : max_bins(max_bins)
    {
    }

    size_t sizeOfData() const override
    {
        return Data::structSize(max_bins);
    }
    DataTypePtr getReturnType() const override
    {
        DataTypes types;
        auto mean = std::make_shared<DataTypeNumber<Data::Mean>>();
        auto weight = std::make_shared<DataTypeNumber<Data::Weight>>();

        // lower bound
        types.emplace_back(mean);
        // upper bound
        types.emplace_back(mean);
        // weight
        types.emplace_back(weight);

        auto tuple = std::make_shared<DataTypeTuple>(types);
        return std::make_shared<DataTypeArray>(tuple);
    }

    void add(AggregateDataPtr place, const IColumn ** columns, size_t row_num, Arena *) const override
    {
        auto val = static_cast<const ColumnVector<T> &>(*columns[0]).getData()[row_num];
        this->data(place).add(static_cast<Data::Mean>(val), 1, max_bins);
    }

    void merge(AggregateDataPtr place, ConstAggregateDataPtr rhs, Arena *) const override
    {
        this->data(place).merge(this->data(rhs), max_bins);
    }

    void serialize(ConstAggregateDataPtr place, WriteBuffer & buf) const override
    {
        this->data(place).write(buf);
    }

    void deserialize(AggregateDataPtr place, ReadBuffer & buf, Arena *) const override
    {
        this->data(place).read(buf, max_bins);
    }

    void insertResultInto(ConstAggregateDataPtr place, IColumn & to) const override
    {
        auto& data = this->data(const_cast<AggregateDataPtr>(place));

        auto & to_array = static_cast<ColumnArray &>(to);
        ColumnArray::Offsets & offsets_to = to_array.getOffsets();
        auto & to_tuple = static_cast<ColumnTuple &>(to_array.getData());

        auto & to_lower = static_cast<ColumnVector<Data::Mean> &>(to_tuple.getColumn(0));
        auto & to_upper = static_cast<ColumnVector<Data::Mean> &>(to_tuple.getColumn(1));
        auto & to_weights = static_cast<ColumnVector<Data::Weight> &>(to_tuple.getColumn(2));
        data.insertResultInto(to_lower, to_upper, to_weights, max_bins);

        offsets_to.push_back(to_tuple.size());
    }

    const char * getHeaderFilePath() const override { return __FILE__; }

    String getName() const override { return "histogram"; }
};

}
