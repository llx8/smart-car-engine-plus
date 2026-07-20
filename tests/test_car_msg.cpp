#include <gtest/gtest.h>
#include "CarMsg.hpp"
#include <cstring>

// ── 结构体大小测试 ──
TEST(CarMsgTest, ReqSize) {
    EXPECT_EQ(sizeof(CarMsgReq), 14);
}

TEST(CarMsgTest, RespSize) {
    EXPECT_EQ(sizeof(CarMsgResp), 21);
}

// ── makeReq 辅助函数测试 ──
TEST(CarMsgTest, MakeReqDefaults) {
    auto req = makeReq(ModId::DOOR, CmdType::READ, 3);
    EXPECT_EQ(req.magic, kCarMsgMagic);
    EXPECT_EQ(req.version, 1);
    EXPECT_EQ(req.cmd_type, static_cast<uint8_t>(CmdType::READ));
    EXPECT_EQ(req.mod_id, static_cast<uint8_t>(ModId::DOOR));
    EXPECT_EQ(req.item_id, 3);
    EXPECT_EQ(req.value[0], 0);
}

TEST(CarMsgTest, MakeReqWithValue) {
    auto req = makeReq(ModId::AC, CmdType::WRITE, 2, 26);
    EXPECT_EQ(req.mod_id, static_cast<uint8_t>(ModId::AC));
    EXPECT_EQ(req.cmd_type, static_cast<uint8_t>(CmdType::WRITE));
    EXPECT_EQ(req.item_id, 2);
    EXPECT_EQ(req.value[0], 26);
}

// ── makeResp 辅助函数测试 ──
TEST(CarMsgTest, MakeRespOk) {
    auto resp = makeResp(ResultCode::OK);
    EXPECT_EQ(resp.magic, kCarMsgMagic);
    EXPECT_EQ(resp.version, 1);
    EXPECT_EQ(resp.result, static_cast<uint8_t>(ResultCode::OK));
}

TEST(CarMsgTest, MakeRespError) {
    auto resp = makeResp(ResultCode::UNKNOWN_MOD);
    EXPECT_EQ(resp.result, static_cast<uint8_t>(ResultCode::UNKNOWN_MOD));
}

// ── GET_ALL 响应数据打包测试 ──
TEST(CarMsgTest, GetAllDoorResponse) {
    auto resp = makeResp(ResultCode::OK);
    // 模拟门控状态: 左前关, 右前开, 左后关, 右后关, 后备箱关, 中控锁已锁
    uint8_t door_state[] = {0, 1, 0, 0, 0, 1};
    std::memcpy(resp.value, door_state, door_item::kCount);
    EXPECT_EQ(resp.value[0], 0);
    EXPECT_EQ(resp.value[1], 1);
    EXPECT_EQ(resp.value[5], 1);
}

// ── 协议版本号测试 ──
TEST(CarMsgTest, VersionField) {
    auto req = makeReq(ModId::DOOR, CmdType::READ, 0);
    EXPECT_EQ(req.version, 1); // 固定版本 1
}

// ── 非法 cmd_type / mod_id 不会在编码阶段崩溃 ──
TEST(CarMsgTest, UnknownTypesDoNotCrash) {
    auto req = makeReq(static_cast<ModId>(99), static_cast<CmdType>(99), 0);
    EXPECT_EQ(req.cmd_type, 99);
    EXPECT_EQ(req.mod_id, 99);
    // 服务端会返回 UNKNOWN_MOD/UNKNOWN_CMD，不会编解码层崩溃
}
