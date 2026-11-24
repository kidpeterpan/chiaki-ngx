// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Bounded single-producer/single-consumer queue. Used to hand small ROI
// crops from the render thread to the OCR worker without a mutex.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace trans {

template <typename T, size_t Capacity>
class SpscRingBuffer
{
	static_assert(Capacity >= 2, "ring buffer needs at least two slots");

public:
	SpscRingBuffer() = default;
	SpscRingBuffer(const SpscRingBuffer &) = delete;
	SpscRingBuffer &operator=(const SpscRingBuffer &) = delete;

	bool tryPush(const T &value)
	{
		const size_t head = head_.load(std::memory_order_relaxed);
		const size_t tail = tail_.load(std::memory_order_acquire);
		if (head - tail >= Capacity)
			return false;
		slots_[head % Capacity].emplace(value);
		head_.store(head + 1, std::memory_order_release);
		return true;
	}

	bool tryPush(T &&value)
	{
		const size_t head = head_.load(std::memory_order_relaxed);
		const size_t tail = tail_.load(std::memory_order_acquire);
		if (head - tail >= Capacity)
			return false;
		slots_[head % Capacity].emplace(std::move(value));
		head_.store(head + 1, std::memory_order_release);
		return true;
	}

	bool tryPop(T &out)
	{
		const size_t tail = tail_.load(std::memory_order_relaxed);
		const size_t head = head_.load(std::memory_order_acquire);
		if (tail == head)
			return false;
		out = std::move(*slots_[tail % Capacity]);
		slots_[tail % Capacity].reset();
		tail_.store(tail + 1, std::memory_order_release);
		return true;
	}

	size_t size() const
	{
		const size_t head = head_.load(std::memory_order_acquire);
		const size_t tail = tail_.load(std::memory_order_acquire);
		return head >= tail ? head - tail : 0;
	}

	bool empty() const { return size() == 0; }

	void clear()
	{
		const size_t head = head_.load(std::memory_order_acquire);
		size_t tail = tail_.load(std::memory_order_relaxed);
		while (tail != head)
		{
			slots_[tail % Capacity].reset();
			++tail;
		}
		tail_.store(head, std::memory_order_release);
	}

private:
	std::array<std::optional<T>, Capacity> slots_{};
	std::atomic<size_t> head_{0};
	std::atomic<size_t> tail_{0};
};

} // namespace trans
