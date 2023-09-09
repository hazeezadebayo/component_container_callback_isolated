#include <chrono>
#include <thread>
#include <unordered_map>
#include <list>
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/component_manager.hpp"

namespace rclcpp_components
{

class ComponentManagerCallbackIsolated : public rclcpp_components::ComponentManager {

  struct ExecutorWrapper {
    explicit ExecutorWrapper(std::shared_ptr<rclcpp::executors::StaticSingleThreadedExecutor> executor)
      : executor(executor), thread_initialized(false) {}

    ExecutorWrapper(const ExecutorWrapper&) = delete;
    ExecutorWrapper& operator=(const ExecutorWrapper&) = delete;

    std::shared_ptr<rclcpp::executors::StaticSingleThreadedExecutor> executor;
    std::thread thread;
    std::atomic_bool thread_initialized;
  };

public:
  ~ComponentManagerCallbackIsolated();

protected:
  void add_node_to_executor(uint64_t node_id) override;
  void remove_node_from_executor(uint64_t node_id) override;

private:
  void cancel_executor(ExecutorWrapper &executor_wrapper);

  std::unordered_map<uint64_t, std::list<ExecutorWrapper>> node_id_to_executor_wrappers_;
};

ComponentManagerCallbackIsolated::~ComponentManagerCallbackIsolated() {
  if (node_wrappers_.size() == 0) return;

  for (auto &p : node_id_to_executor_wrappers_) {
    auto &executor_wrappers = p.second;

    for (auto &executor_wrapper : executor_wrappers) {
      cancel_executor(executor_wrapper);
    }
  }

  node_wrappers_.clear();
}

void ComponentManagerCallbackIsolated::add_node_to_executor(uint64_t node_id) {
  auto node = node_wrappers_[node_id].get_node_base_interface();

  node->for_each_callback_group([node_id, &node, this](rclcpp::CallbackGroup::SharedPtr callback_group) {
      auto executor = std::make_shared<rclcpp::executors::StaticSingleThreadedExecutor>();
      executor->add_callback_group(callback_group, node);

      auto it = node_id_to_executor_wrappers_[node_id].begin();
      it = node_id_to_executor_wrappers_[node_id].emplace(it, executor);
      auto &executor_wrapper = *it;

      executor_wrapper.thread = std::thread([&executor_wrapper]() {
          executor_wrapper.thread_initialized = true;
          executor_wrapper.executor->spin();
      });
  });
}

void ComponentManagerCallbackIsolated::remove_node_from_executor(uint64_t node_id) {
  auto it = node_id_to_executor_wrappers_.find(node_id);
  if (it == node_id_to_executor_wrappers_.end()) return;

  for (ExecutorWrapper &executor_wrapper : it->second) {
    cancel_executor(executor_wrapper);
  }

  node_id_to_executor_wrappers_.erase(it);
}

void ComponentManagerCallbackIsolated::cancel_executor(ExecutorWrapper &executor_wrapper) {
  if (!executor_wrapper.thread_initialized) {
    auto context = this->get_node_base_interface()->get_context();

    while (!executor_wrapper.executor->is_spinning() && rclcpp::ok(context)) {
      rclcpp::sleep_for(std::chrono::milliseconds(1));
    }
  }

  executor_wrapper.executor->cancel();
  executor_wrapper.thread.join();
}

} // rclcpp_components

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);

  auto executor = std::make_shared<rclcpp::executors::StaticSingleThreadedExecutor>();
  auto node = std::make_shared<rclcpp_components::ComponentManagerCallbackIsolated>();

  executor->add_node(node);
  executor->spin();

  rclcpp::shutdown();
}