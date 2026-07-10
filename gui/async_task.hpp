// gui/async_task — drobny wrapper na zadanie w tle (sieciowe wywolania API).
// GUI odpytuje stan co klatke; wynik lub blad odbiera bez blokowania petli.
#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <optional>
#include <string>

namespace predeye::gui {

// Pojedyncze zadanie zwracajace T. Uruchamiane w osobnym watku; poll() nie
// blokuje. running() == true w trakcie; potem albo result(), albo error().
template <class T> class AsyncTask {
  public:
    void start(std::function<T()> fn) {
        error_.clear();
        result_.reset();
        running_ = true;
        future_ = std::async(std::launch::async, std::move(fn));
    }

    // Odpytanie stanu — wolane raz na klatke z watku UI.
    void poll() {
        if (!running_ || !future_.valid())
            return;
        if (future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        running_ = false;
        try {
            result_ = future_.get();
        } catch (const std::exception& e) {
            error_ = e.what();
        } catch (...) {
            error_ = "nieznany blad";
        }
    }

    bool running() const { return running_; }
    bool has_result() const { return result_.has_value(); }
    const T& result() const { return *result_; }
    const std::string& error() const { return error_; }

  private:
    std::future<T> future_;
    bool running_ = false;
    std::optional<T> result_;
    std::string error_;
};

} // namespace predeye::gui
