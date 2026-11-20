/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SFDTool Copyright (C) 2026 Ryan Crepa
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "automated_task.h"

#include <algorithm>
#include <string>
#include <vector>

TEST_CASE("automated task print and blk_size") {
    std::vector<std::string> logs;
    sfd::AutomatedTaskRequest request;
    request.actions = {"print hello world", "blk_size 60000"};

    const auto result = sfd::run_automated_task(
        request, [&](const std::string& line) { logs.push_back(line); });

    CHECK(result.success);
    CHECK_FALSE(result.device_detached);
    REQUIRE(logs.size() >= 4);
    CHECK(logs[0] == "[cmd] print hello world");
    CHECK(logs[1] == "hello world");
    CHECK(logs[2] == "[cmd] blk_size 60000");
    CHECK(logs[3] == "blk_size=59904");
}

TEST_CASE("automated task rejects unknown command") {
    sfd::AutomatedTaskRequest request;
    request.actions = {"not_a_command"};

    const auto result = sfd::run_automated_task(request, {});
    CHECK_FALSE(result.success);
    CHECK_FALSE(result.device_detached);
    CHECK(result.message.find("unknown command") != std::string::npos);
}

TEST_CASE("automated task wait and pause") {
    std::vector<std::string> logs;
    std::string paused_message;
    sfd::AutomatedTaskRequest request;
    request.actions = {"pause Check the device", "wait 0"};
    request.on_pause = [&](const std::string& message) {
        paused_message = message;
    };

    const auto result = sfd::run_automated_task(
        request, [&](const std::string& line) { logs.push_back(line); });

    CHECK(result.success);
    CHECK(paused_message == "Check the device");
    CHECK(std::find(logs.begin(), logs.end(), "waiting 0 second(s)") != logs.end());
    CHECK(std::find(logs.begin(), logs.end(), "wait finished") != logs.end());
}
