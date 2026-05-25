#include "../src/core/TaskModel.h"

#include <cassert>
#include <iostream>

static void testNewTaskHasDraftStatusAndInputPath() {
    auto task = gisqc::TaskModel::create("待检DB任务", "D:/项目成果/待检DB");

    assert(!task.id().empty());
    assert(task.name() == "待检DB任务");
    assert(task.inputPath() == "D:/项目成果/待检DB");
    assert(task.status() == gisqc::TaskStatus::Draft);
    assert(task.progress() == 0);
}

static void testTaskStatusFlowUpdatesProgress() {
    auto task = gisqc::TaskModel::create("质检任务", "D:/data");

    task.start();
    assert(task.status() == gisqc::TaskStatus::Scanning);
    assert(task.progress() == 5);

    task.markReady();
    assert(task.status() == gisqc::TaskStatus::Ready);
    assert(task.progress() == 20);

    task.runChecking();
    assert(task.status() == gisqc::TaskStatus::Checking);
    assert(task.progress() == 50);

    task.complete();
    assert(task.status() == gisqc::TaskStatus::Completed);
    assert(task.progress() == 100);
}

static void testTaskCanFailWithMessage() {
    auto task = gisqc::TaskModel::create("质检任务", "D:/data");

    task.fail("数据路径不存在");

    assert(task.status() == gisqc::TaskStatus::Failed);
    assert(task.errorMessage() == "数据路径不存在");
}

int main() {
    testNewTaskHasDraftStatusAndInputPath();
    testTaskStatusFlowUpdatesProgress();
    testTaskCanFailWithMessage();
    std::cout << "TaskModel tests passed\n";
    return 0;
}
