# GUI2 开发说明

## 设计目标

GUI2 是基于 LVGL 的 TWRP 新一代触摸界面，目标是提供适合现代手机的响应式布局、流畅的触摸交互和可扩展的 recovery 页面框架。

主要目标：

- 适配不同分辨率、方向和屏幕密度。
- 支持多指输入，为后续双指缩放等手势保留基础。
- 保持滚动、动画、阴影和渐变等视觉效果。
- 使用 TinyTTF 动态渲染字体，支持中英文及不同字号。
- 将状态栏、页面顶栏、内容区和底部导航统一为可复用脚手架。
- 与现有 TWRP recovery 能力逐步衔接，避免一次性破坏旧 GUI。

## 设计理念

- **框架与内容分离**：固定系统区域由 GUI2 shell 管理，具体页面只负责创建内容。
- **响应式优先**：尺寸、间距和字体根据 framebuffer 短边动态计算，不针对单一设备写死坐标。
- **触摸优先**：交互区域应覆盖完整控件；按下后移出控件再释放视为取消点击。
- **组件复用**：卡片、导航按钮、页面标题、滚动容器和语言选项应通过公共创建函数生成。
- **资源运行时加载**：主题字体等资源从 `/twres` 加载，避免将设备相关资源硬编码到页面逻辑中。

## 设计路线

1. 完善 LVGL framebuffer 显示、触摸输入、多指输入和性能调度。
2. 建立统一 GUI2 shell：固定状态栏、页面顶栏、可滚动内容区和底部导航栏。
3. 建立页面与组件模型，逐步实现安装、清除、备份、恢复、挂载、高级和设置等页面。
4. 完善 i18n、字体、主题和设备安全区适配。
5. 将现有 recovery 操作能力接入 GUI2 页面。
6. 以 GUI2 作为 recovery 默认主界面，同时保留旧 GUI 作为当前进程内的兼容回退。

## 当前进度

已完成：

- LVGL 与 TWRP minui framebuffer 显示链路。
- 低延迟触摸输入、滚动和多指输入基础。
- NEON 颜色转换优化。
- 响应式手机布局及动态尺寸计算。
- 固定状态栏、页面顶栏、内容滚动区和底部导航栏。
- 浮动式底部导航：导航采用左侧独立圆形返回按钮加右侧三按钮整体胶囊的 `1+3` 结构；两个外形由 shell 统一绘制背景、半圆角、描边和阴影，胶囊内部按钮只能提供交互和图标，不得各自绘制圆角背景。
- 页面内容在浮动导航下方连续延伸，并由 shell 提供局部透明黑渐变遮罩；遮罩必须位于滚动内容之上、导航控件之下，且不得带 border、outline 或 shadow。
- 返回、主页和页面路由逻辑。
- 页面 PUSH/POP/REPLACE 过渡动画：前进时新页面从右侧进入，返回时新页面从左侧进入；动画期间页面内容暂时阻止输入，固定状态栏和底部导航保持不动。
- 主页功能卡片、通用操作占位页面以及设置、语言、时区和硬件页面。
- 重启页面：通过底栏 Power 按钮进入，支持 System、Power Off、Recovery、Fastbootd、Bootloader、Download 和 EDL；每个目标按编译参数决定是否显示，点击后使用带箭头的滑块确认。
- 重启页面的 A/B 槽位区域：只有编译启用 `AB_OTA_UPDATER` 且运行时存在有效 `A/B` 活动槽位时显示；槽位切换复用 recovery 的 boot control 逻辑。
- 英文、简体中文和繁体中文 i18n。
- TinyTTF 动态字号和中文字体渲染。
- `libgui2` 静态库及 `gui2_start()` 公共入口。
- `gui2/backend/` 中与 LVGL 解耦的设置存储和状态采集接口。
- recovery DataManager 适配器：持久化设置继续使用 `.twrp_settings`。
- 真实时间、电量、充电状态及 12/24 小时制显示。
- 时间/时区设置（时区、UTC 偏移、DST）和语言设置持久化。
- 屏幕能力 backend：旧 GUI 兼容的 PNG 截屏、自动/手动熄屏与触摸唤醒。
- 内置无音频 VP8/WebM 录屏：支持 15/24/30/45/60 FPS、有界丢帧队列和后台编码，不依赖 Android 媒体服务。
- shell 级状态栏下拉快捷菜单：截屏、熄屏和开始/停止录屏，录制状态同步显示在状态栏。
- recovery 默认 GUI2 启动、初始化失败回退，以及运行时切换到旧 GUI。
- GUI2 已完成首轮模块化重构：入口 `gui2.cpp` 负责 recovery 业务回调和组装，`app/` 管理生命周期/主循环，`shell/` 管理持久系统层，`pages/` 管理页面，`components/` 管理可复用控件，`core/` 管理通用 UI 基础设施，`theme/` 管理字体资源，`backend/` 管理 recovery/system 能力。
- 重启能力已经通过 `backend/reboot_backend` 注入 GUI2；确认后写入旧 GUI 使用的重启状态并退出 GUI2，由 `twrp.cpp` 统一完成最终同步、卸载和重启。

当前边界：

- 启动前仍需短暂初始化旧 GUI，以支持现有解密、只读确认等 recovery 前置页面；随后会释放旧 GUI 的资源再进入 GUI2。
- 重启和 A/B 槽位切换已经接入；安装、清除、备份、恢复、挂载和高级工具等真实 recovery 操作尚未全部接入 GUI2 页面。

## 注意事项

### 源码与构建

- `gui2/AGENTS.md` 位于 `bootable/recovery` Git 工作树下；`git status`、`git diff --check` 等版本检查应在该目录执行。Android 构建系统的源码根目录是其上两级目录，`source build/envsetup.sh`、`lunch` 和 `m/mka` 必须从源码根目录执行。
- 当前真机验证目标是 `sm8850`：

  ```bash
  source build/envsetup.sh
  lunch twrp_sm8850
  mka installclean
  mka recoveryimage
  ```

- 每次生成最终 `recoveryimage` 前必须先执行 `mka installclean`；增量编译单独验证模块时可按需使用目标模块构建。
- 最近验证命令和结果：

  ```bash
  git diff --check                         # 通过（在 bootable/recovery 执行）
  source build/envsetup.sh
  lunch twrp_sm8850
  m libgui2                                # 成功
  m recovery                               # 成功
  mka installclean
  mka recoveryimage                        # 成功，生成 out/target/product/sm8850/recovery.img
  ```

  完整 recovery 构建出现的 `depmod` 缺失模块元数据为已有设备构建警告，不是 GUI2 编译错误。

### 架构与资源

- GUI2 源码按以下职责组织：

  ```text
  app/        GUI2 生命周期、主循环、运行时服务和延迟屏幕动作
  core/       metrics、缩放、点击取消 guard、滚轮惯性等无业务基础设施
  shell/      状态栏、page host、顶栏/滚动脚手架、底栏、快捷面板和系统弹窗
  pages/      page router、页面 builder、页面状态、页面专属逻辑和重启页
  components/ 可复用 LVGL 控件和卡片
  theme/      TinyTTF 字体生命周期
  backend/    recovery/system backend（设置、硬件、屏幕、重启），不依赖 LVGL
  ```

- 修改 GUI 框架时优先使用 `create_gui_shell_base()`、`page_host::build()` 和公共组件；页面不得直接管理固定状态栏、顶栏、底部导航、快捷面板或系统反馈层。
- `gui2_start()` 只负责校验注入的 recovery services、调用 `app/gui2_lifecycle` 初始化图形资源、组装 Shell、启动状态栏 controller 和 `app/gui2_loop`；不要把新的 LVGL 主循环或资源释放逻辑塞回入口。重启 service 也必须通过 context 注入。
- `app/gui2_lifecycle` 统一拥有 TinyTTF、LVGL、display、input 和 SVG cache 的初始化/释放顺序；初始化失败必须复用同一清理路径。
- `app/gui2_loop` 统一处理 LVGL timer、screen tick、输入 activity、按键、滚轮、present 和性能 timeout；页面回调只接收抽象动作。
- `page_router` 是页面跳转唯一入口，并保存当前页面 request 供跨页面返回使用。新增页面必须注册 `page_id`、builder/payload、路由、返回行为和 i18n；页面之间不得直接互相调用 `show_*`。
- `page_host` 拥有当前页面层和当前 scaffold；页面 builder 只接收页面 options 中的 content、metrics、strings 和业务回调，不创建 Shell 持久对象。
- `gui2/core/ui_metrics` 中的 `ui_metrics`、`ui_px()`、`card_inner_padding()`、`single_line_card_height()` 和 `navigation_safe_area()` 是全局尺寸 API；不要在新模块复制尺寸策略。
- `gui2/core/ui_event_guard` 是统一的触摸取消和点击震动实现。新增可点击对象必须复用 `add_press_cancel_guard()`，不得自行实现一套点击取消状态。
- `shell/` controller 持有各自的 LVGL 对象和 timer：状态栏使用 `status_bar_controller`，快捷面板使用 `quick_panel_controller`，系统反馈使用 `screen_feedback`，屏幕动作使用 `app/screen_actions`。停止/退出时必须由 owner 释放其 timer、线程和对象引用。
- 底部导航是 shell 级持久组件，所有页面必须复用同一个导航实例和 `page_host`/scaffold 生命周期；页面不得创建、替换或复制底部导航，也不得使用页面专属坐标模拟导航。
- 浮动导航的视觉层级固定为：滚动页面内容 → 局部渐变遮罩 → 导航控件。导航背景本身保持透明，让页面内容可以延伸到其下方；渐变遮罩只覆盖导航顶部上方的少量过渡区域及导航下方区域，不得形成可见的矩形边框。
- 导航外形使用明确的半高圆角（控件高度的一半），不能只依赖主题默认的 `LV_RADIUS_CIRCLE`；右侧三按钮必须由父级胶囊统一绘制描边，子按钮设置透明背景、零圆角和零边框。
- 可复用的自制 LVGL 组件统一放在 `gui2/components/`，当前包括 icon、choice card、setting card、slider card、swipe slider、quick action button、section label 和 apply button；交互组件使用单一自定义 widget 同时处理状态、绘制、命中和事件，禁止用透明原生控件叠加视觉层；页面只负责组合组件和绑定业务事件。`swipe_slider` 始终绘制箭头，并提供 `detach()` 释放已删除 LVGL 对象引用的能力。
- 页面标题统一使用 `ui.brand_font`；修改标题字号时必须同步检查主页、二级页和三级页。
- GUI2 使用 `/twres` 中的运行时字体资源和 backend，不针对单一设备硬编码分辨率、圆角安全区或字号。
- 全局尺寸统一使用 `gui2_core::ui_metrics::scale` 和 `gui2_core::ui_px()`；缩放基准为短边 1200px，最小/最大比例仅作保护，不能给高分辨率布局保留未缩放的固定上限。新增 shell、卡片或组件尺寸必须接入这套缩放。
- GUI2 不支持旧 GUI 的主题导入、`ui.xml` 自定义主题和主题重载；这些逻辑继续由旧 GUI 独立维护。
- `gui2/` 内源文件不添加许可证文件头；构建系统中的模块许可证声明仍按 Android.bp 规范保留。

### 页面与交互

- 新增页面必须同时接入 `pages/page_router`、返回行为、i18n、滚动区域、底部导航和触摸取消逻辑；页面 builder 应放在 `pages/`，而不是继续增加 `gui2.cpp` 中的 `show_*` 实现。
- 新增或修改页面必须通过 `page_host` 和公共滚动脚手架复用悬浮导航逻辑；`main_content` 的可视高度应延伸到屏幕底部，不能通过 `height - nav_height` 截短滚动视口来“给导航让位”。
- 所有可滚动页面必须为内容末尾保留导航安全区，至少包含 `navigation_safe_area()`（导航高度加外边距）；安全区应作为滚动内容的底部 padding 或尾部占位，使最后一项能够完整滚动到浮动导航上方。页面有固定“应用”等操作按钮时，还必须额外保留按钮自身高度和间距。
- 页面底部渐变由 shell 统一创建和复用，不得在各页面重复创建渐变层；渐变层是纯装饰对象，必须清除 border、outline、shadow、点击和滚动属性，并使用透明到半透明黑的局部渐变实现压暗。
- 新增可点击子控件时检查 `LV_OBJ_FLAG_CLICKABLE`；装饰性子对象不得截获父卡片事件。所有可点击控件都要保证按下后移出区域再释放时取消点击。
- `lv_obj_create()` 默认可能带有滚动属性。除 `main_content` 等明确的内容滚动区外，卡片、按钮、弹窗和布局容器必须调用 `disable_scrolling()` 并关闭滚动条。
- 页面只通过 options/context 接收 `metrics`、语言包、content 和业务回调；不得直接访问 `sysfs`、minui、全局 DataManager 或 recorder。
- 页面状态（选中项、slider binding、LVGL card 引用）集中放入对应的 page state/controller；不要继续在入口新增散落的静态状态变量。
- 重启页只接收 backend 提供的能力、当前槽位和抽象回调；重启目标卡片不使用图标或描述，卡片高度统一使用 `single_line_card_height()`，确认滑块位于可选 A/B 槽位区域之后。

### 布局与组件

- 所有卡片类组件复用 `card_inner_padding()` 作为响应式水平内边距；卡片、设置项、语言项、时区项、硬件项、弹窗内容卡和信息卡不得各自硬编码不同的左右 padding。调整卡片时同时检查文字、图标、箭头和多列行的可用宽度。
- 导航图标尺寸必须通过统一 `ui_px()` 和导航按钮尺寸计算，不能使用未缩放的固定像素；调整图标时同时检查 SVG 栅格内容的实际可见边界，避免外层图片盒子放大但图形仍偏小。
- 页面复用检查应覆盖主页、操作页、重启页、设置页、时区页、语言页和硬件页：它们必须拥有相同的浮动导航层级、渐变遮罩行为和滚动到底安全区，不能只在主页单独适配。
- 带圆角和阴影的卡片或子卡片必须与父容器边界保持安全距离；多列行要把内边距计入子卡片宽度和行高度，必要时使用 `LV_OBJ_FLAG_OVERFLOW_VISIBLE`，避免圆角和阴影被裁切。
- 同一页面的单行选项复用 `single_line_card_height()`；时间格式、UTC 偏移、时区选项、DST 选项和应用按钮保持一致高度。
- 重启目标和 A/B 槽位卡片复用无描述、居中文本的单行选项样式；确认滑块始终保留箭头，不提供隐藏箭头的变体。
- 硬件滑块把标题/数值行和滑块视为一个内容组，在卡片内部整体垂直居中，四个方向使用统一的 `card_inner_padding()`，组内间距单独控制。
- 滑块沿用 Miuix 风格：胶囊轨道、低对比度背景轨道、蓝色填充和较小的圆形滑块；交互热区与视觉滑块尺寸分离。
- 页面标题区的状态栏留白和标题/副标题间距由 shell 的统一 metrics 控制；调整主页标题时必须同步检查所有二级、三级页面，避免页面自行写死坐标。
- 卡片文本不得按固定的“一行标题 + 一行描述”计算位置；文本组应设置实际宽度，使用自动换行，按真实高度整体居中，必要时让卡片随内容增高。只有单行超长文本才按场景使用 `LV_LABEL_LONG_SCROLL`，不要让大量卡片同时滚动。
- 状态栏中的电池图标、百分比和充电图标必须使用独立对象；文本更新后重新排列，顺序保持为“电池、数值、充电标志”，避免动态宽度造成重叠。

### 设置、硬件与重启 backend

- 所有持久化设置必须通过 `backend/settings_store`；recovery 实现使用 DataManager，页面不得直接读写配置文件。
- 所有硬件设置必须通过 `backend/hardware_settings`；页面不得直接访问 sysfs、DataManager 或震动 HAL。能力不可用时隐藏入口，不创建“不可用”页面。
- 所有重启和槽位操作必须通过 `backend/reboot_backend`；页面不得直接访问 DataManager、PartitionManager、TWFunc、minui 或 sysfs。
- 重启目标能力必须同时遵循编译参数和旧 GUI 语义：`TW_NO_REBOOT_RECOVERY`、`TW_NO_REBOOT_BOOTLOADER`、`TW_INCLUDE_FASTBOOTD`、`PRODUCT_USE_DYNAMIC_PARTITIONS`、`TW_HAS_DOWNLOAD_MODE`、`TW_HAS_EDL_MODE` 和 `AB_OTA_UPDATER`。不可用目标隐藏，不创建“不可用”卡片。
- A/B 槽位卡片不能只依据 `AB_OTA_UPDATER`；还必须确认运行时 `ro.boot.slot_suffix` 对应有效的 `A` 或 `B`，从而兼容 A-only、传统 A/B 和 Virtual A/B 设备。
- 重启确认只写入旧 GUI 使用的 `tw_reboot_arg` 和 `tw_gui_done`，由 GUI2 退出后交给 `twrp.cpp` 的统一重启流程执行；页面和滑块不得直接触发设备重启。
- 硬件滑块变化时可实时更新硬件和内存状态，但不得为每个 `LV_EVENT_VALUE_CHANGED` 调用 `Flush()`；在 `LV_EVENT_RELEASED` 或等价结束事件统一落盘。
- 亮度沿用 `tw_brightness` / `tw_brightness_pct` 和 TWRP 最大值映射；震动沿用 `tw_button_vibrate`、`tw_keyboard_vibrate`、`tw_action_vibrate`，不得引入 GUI2 私有配置键。
- 修改时区按旧 GUI 的 `Zone[:offset][DSTZone]` 规则构造 `tw_time_zone`，然后更新环境并 `flush()`。
- 录屏帧率设置统一使用 `tw_screen_record_fps`，界面只允许 15、24、30、45、60 FPS 五档；页面不得直接操作 recorder。
- 录屏档位不得超过编译期 `TW_FRAMERATE`；backend、界面和录屏采样必须使用同一有效上限。
- 录屏帧率改变时只更新 backend 的内存配置，不能在每个滑块事件中 `Flush()`，只在释放滑块时统一保存。

### 生命周期与旧 GUI

- GUI2 的 display/input/font/LVGL 生命周期由 `app/gui2_lifecycle` 统一管理；Shell controller 的 timer 必须在 LVGL 销毁前停止，backend worker 必须在 owner 的 `stop()` 中停止并 join。
- 截图/熄屏请求通过 `app/screen_actions` 排队，在本轮 present 后执行；截图白闪由 `shell/screen_feedback` 管理。页面和快捷按钮不得直接调用 screenshot、screen-off 或 minui。
- GUI2 切换旧 GUI 不写入界面选择配置；退出前必须停止状态线程、释放 LVGL 和输入资源。若是 GUI2→旧 GUI 的即时切换，必须保留已经工作的 minui/DRM 实例，由旧 GUI 只重建资源和输入，不能再次 modeset；只有 GUI2 完全退出或初始化失败时才释放 minui。
- 解密等前置页面会让 minui/DRM 在同一 recovery 进程内经历二次初始化；确实退出 DRM backend 时必须释放 CRTC、connector、plane、property 和 blob，并重置 blank/缓冲区状态，否则下一次 atomic commit 可能失败。GUI2 启动阶段不得额外调用屏幕唤醒同步。

### 屏幕能力与快捷菜单

- 截屏、熄屏、唤醒和录屏只能通过 `gui2/backend` 的屏幕 backend；页面不得直接访问 minui、sysfs、脚本或配置文件。
- 熄屏前必须先由 Shell 绘制锁屏覆盖层，再由 screen backend 执行 blank；唤醒后覆盖层继续消费触摸，只有滑块完整到达 100% 才隐藏锁屏并恢复页面交互。锁屏使用 `ic_lock`、半透明背景和本地化滑动提示，视觉参数统一接入 `ui_metrics`。
- 录屏默认且目前唯一实现是 GUI2 的 VP8/WebM backend；不得引入 MediaCodec、Stagefright、Codec2、Binder 媒体服务或 `screenrecord`。VP8 编码复用 manifest 管理的 `external/libvpx`，WebM 封装复用 `external/libwebm` 的 `libwebm_mkvmuxer`，不在 GUI2 复制第三方源码。
- GUI2 只使用 libvpx 的 VP8 encoder API 和 libwebm 的三文件 muxer 模块；不要因录屏引入 VP9、解码器、音频轨道或完整媒体框架。保留两个 external 项目的 LICENSE/PATENTS/NOTICE 授权文件。
- RGBX→I420 转换使用 GUI2 自己的 `backend/rgb_to_i420`，当前 LVGL XRGB8888 在小端内存中是 `[B,G,R,X]`；转换不能误当作 `[R,G,B,X]`。I420 的 U/V 平面使用 2x2 色度平均，并处理 stride、奇数宽高和边缘复制。
- 录屏帧必须在本轮 LVGL flush 全部完成并完成 `gr_flip()` 后提交；GUI2 应维护一份由 `flush_cb()` 增量更新的、独立于 DRM 双缓冲的完整影子帧，不能在 direct-scanout 上直接读取可能只完成部分 damage 同步的 scanout/released buffer。即使页面没有 LVGL 脏区域，也要按录制帧率持续采样当前影子帧，不能只录制发生界面变化的瞬间。I420 转换、VP8 编码和 WebM 写入在有界后台队列中执行，不能阻塞 LVGL 渲染和触摸线程，队列满时丢帧。
- VP8/WebM 输出必须使用标准 VP8 视频轨道和可回填的文件模式 WebM；修改后至少用 ffprobe、ffmpeg、VLC 或等价标准播放器验证非黑测试图、时间轴、帧数和尺寸。
- 录屏时间轴使用单调时钟和固定帧率；队列满时丢弃原始帧但保留提交帧的单调时间戳，WebM SegmentInfo 在停止时回填实际时长，不能用“成功编码帧数”直接代表录制时长。VP8 不能像 MJPEG 那样复制压缩包补帧。
- 所有后台线程退出前必须停止、`join` 并回收；停止录屏、熄屏、页面退出和 recovery 退出都必须完成 WebM 收尾。
- 快捷菜单是 shell 的持久层，不由页面自行创建；面板、按钮和遮罩不得滚动，必须给圆角和阴影保留安全边距。下拉只移动菜单，不移动状态栏或底部导航。
- 快捷菜单下拉/上滑必须让面板位置和遮罩透明度跟随手指，并在松手后使用短动画完成展开或收起；不能用阈值触发瞬间闪现。截图必须先收起菜单并提交当前页面帧，再执行 backend 截图。
- 电源键、音量减以及电源键组合动作必须由 GUI2 输入层转换为抽象动作，页面不得直接读取 Linux 按键；电源单按切换屏幕，电源+音量减触发截图，并忽略按键自动重复。
- 电源短按不能只依赖长按超时判断；按下/抬起可能在同一轮输入轮询内完成。屏幕熄灭时触摸报告仍需被输入层消费以唤醒屏幕，但必须以 `RELEASED` 形式提供给 LVGL，直到手指抬起，避免唤醒动作同时点击页面控件。
- 截图成功后使用 shell 级短暂白屏提示，不能破坏 LVGL 当前帧或直接让页面对象承担系统提示职责。快捷菜单熄屏动作必须延迟到触摸帧提交后，避免同一次释放事件立即唤醒屏幕。
- 截图提示应使用低不透明度、短时的柔和闪烁；不得使用长时间全屏纯白覆盖，避免刺眼和影响用户观察。
- 快捷操作按钮内的图标和文本必须先作为一个内容组计算真实高度（包括多行文本），再在按钮内部水平/垂直居中，并保留一致的内边距；禁止分别锚定到按钮顶部和底部。
- 存储路径、文件权限和截屏格式复用旧 GUI；存储不可用时使用 `/tmp/`，新增持久化设置仍只能经由 backend 写入 DataManager。
