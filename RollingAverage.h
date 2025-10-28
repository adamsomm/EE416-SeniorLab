#ifndef ROLLING_AVERAGE_H
#define ROLLING_AVERAGE_H

#include <queue> // The class uses std::queue, so include it here

template <typename T>
class RollingAverage {
  private:
    std::queue<T> data_queue;
    double current_sum = 0.0; // Using the more robust double version
    size_t max_size;

  public:
    explicit RollingAverage(size_t size) : max_size(size) {}

    void add_value(T value){
      data_queue.push(value);
      current_sum += value;

      if(data_queue.size() > max_size){
        current_sum -= data_queue.front();
        data_queue.pop();
      }
    }

    double get_average() const {
      if(data_queue.empty()){
        return 0.0;
      }
      return current_sum / data_queue.size();
    }
    int get_count(){
      return data_queue.size();
    }
};

#endif // ROLLING_AVERAGE_H
