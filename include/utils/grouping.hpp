#pragma once

#include "utils/best_n.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>
#include <vector>

// A group accepts a value only when it agrees with every existing member.
template <typename T>
struct Group : public std::vector<T> {
    using std::vector<T>::vector;

    virtual bool could_be_member(const T& value) {
        for(const T& member : *this)
            if(!(member == value))
                return false;
        return true;
    }

    bool operator>(const Group& rhs) const { return this->size() > rhs.size(); }
};

// Selects the candidate that agrees with the largest number of other
// candidates. For this algorithm equality means "close enough."
template <typename Guess>
std::optional<Guess> select_best_guess(
    const std::vector<Guess>& candidates,
    unsigned min_agreeing,
    unsigned max_disagreeing,
    std::function<Guess(const std::vector<Guess>&)> average_fun = {}) {
    if(candidates.size() < min_agreeing || candidates.empty())
        return std::nullopt;

    Guess best_guess = candidates[0];
    unsigned best_guess_n = 1;
    for(std::size_t i = 0; i < candidates.size(); i++) {
        Guess guess = candidates[i];
        unsigned guess_n = 1;

        for(std::size_t j = 0; j < candidates.size(); j++) {
            if(i == j)
                continue;

            if(candidates[j] == guess)
                guess_n++;
        }

        if(guess_n > best_guess_n) {
            best_guess_n = guess_n;

            if(average_fun) {
                std::vector<Guess> selected;
                for(std::size_t j = 0; j < candidates.size(); j++)
                    if(candidates[j] == guess)
                        selected.push_back(candidates[j]);
                best_guess = average_fun(selected);
            } else {
                best_guess = guess;
            }
        }
    }

    if(best_guess_n < min_agreeing ||
       candidates.size() - best_guess_n > max_disagreeing)
        return std::nullopt;

    return best_guess;
}

template <typename T>
BestN<Group<T>> select_biggest_groups(
    const std::vector<T>& candidates,
    unsigned min_size,
    std::size_t num) {
    BestN<Group<T>> best(num);

    if(candidates.size() < min_size || candidates.empty())
        return best;

    std::vector<bool> selected(candidates.size());
    for(std::size_t i = 0; i < candidates.size(); i++) {
        if(selected[i])
            continue;

        Group<T> guess = {candidates[i]};
        std::vector<std::size_t> selected_indexes = {i};
        for(std::size_t j = 0; j < candidates.size(); j++) {
            if(i == j || selected[j])
                continue;
            if(guess.could_be_member(candidates[j])) {
                guess.push_back(candidates[j]);
                selected_indexes.push_back(j);
            }
        }

        if(guess.size() >= min_size) {
            best.insert(guess);

            for(std::size_t index : selected_indexes)
                selected[index] = true;
        }
    }

    return best;
}

template <typename T, typename CustomGroup>
BestN<CustomGroup> select_biggest_groups(
    const std::vector<T>& candidates,
    unsigned min_size,
    std::size_t num) {
    BestN<CustomGroup> best(num);
    static_assert(
        std::is_base_of_v<Group<T>, CustomGroup>,
        "CustomGroup must derive from Group<T>");

    if(candidates.size() < min_size || candidates.empty())
        return best;

    std::vector<bool> selected(candidates.size());
    for(std::size_t i = 0; i < candidates.size(); i++) {
        if(selected[i])
            continue;

        CustomGroup guess = {{candidates[i]}, 1};
        std::vector<std::size_t> selected_indexes = {i};
        for(std::size_t j = 0; j < candidates.size(); j++) {
            if(i == j)
                continue;
            if(guess.could_be_member(candidates[j])) {
                guess.num++;
                guess.values.push_back(candidates[j]);
                selected_indexes.push_back(j);
            }
        }

        if(guess.num >= min_size) {
            best.insert(guess);

            for(std::size_t index : selected_indexes)
                selected[index] = true;
        }
    }

    return best;
}
