# 有界网络载荷内存实施计划

1. 在 CNet 事件队列测试中先表达事件字节预算、拒绝和释放后重用语义；在公开 API 测试中表达大单消息上限可初始化。
2. 把 CNet 事件 ring 改为固定描述符加池化载荷，追加公开事件预算；把 owner 接收缓冲改为连接占用时分配、终止时释放。
3. 在 CHTTP server 测试中先表达初始化阶段大型载荷为零、请求期间峰值受预算约束、关闭后当前值归零。
4. 为 CHTTP 增加线程安全的预算保留/释放与增长函数，逐一迁移 H1 parser、response builder、outbound、WebSocket upgrade 和 H2 payload。
5. 在 Flowie control server 设置明确预算，先跑 Salts 定向测试，再安装 Debug/Release 包并跑 Flowie HTTPS 定向测试，最后执行两个仓库全量测试。
