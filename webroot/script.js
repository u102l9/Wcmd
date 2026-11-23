// KernelSU WebUI: 通过 window.kernelsu 桥执行命令。开关写 /data/adb/VirtualCam/enabled
// 并同步 /sdcard/VirtualCam/disable（原生侧与 service.sh 都认它）。
const DIR = "/sdcard/VirtualCam";
const CFG = "/data/adb/VirtualCam";
const ks = window.kernelsu;
const $ = id => document.getElementById(id);
const log = (m) => { $("log").textContent += m + "\n"; $("log").scrollTop = 1e9; };

async function exec(cmd) {
  if (!ks || !ks.exec) throw new Error("请在 KernelSU App 的 WebUI 中打开本页");
  const r = await ks.exec(cmd);
  return (r && (r.stdout || r.out)) || (typeof r === "string" ? r : "");
}
const toast = (m) => { if (ks && ks.toast) ks.toast(m); else log(m); };

async function refresh() {
  try {
    const en = (await exec(`cat ${CFG}/enabled 2>/dev/null || echo 0`)).trim();
    $("toggle").checked = en === "1";
    $("stateText").textContent = en === "1" ? "已开启：画面将被替换" : "已关闭：真实画面";
    const mir = (await exec(`cat ${CFG}/front_mirror 2>/dev/null || echo 1`)).trim();
    $("mirror").checked = mir === "1";
    if ($("sizeInput")) $("sizeInput").value = (await exec(`cat ${DIR}/size.txt 2>/dev/null || echo`)).trim();
    const cur = (await exec(`cat ${CFG}/current 2>/dev/null || echo -`)).trim();
    const ls = await exec(`ls -lh ${DIR} 2>/dev/null | grep -E "virtual.mp4|photo.jpg|front.jpg" || echo 无素材`);
    $("mediaInfo").textContent = "当前源: " + cur + "\n" + ls;
  } catch (e) { log("ERR " + e.message); }
}

$("toggle").addEventListener("change", async e => {
  const v = e.target.checked ? 1 : 0;
  await exec(`mkdir -p ${CFG} ${DIR}; echo ${v} > ${CFG}/enabled; ` +
             (v ? `rm -f ${DIR}/disable` : `touch ${DIR}/disable`));
  toast(v ? "已开启拦截" : "已关闭拦截");
  log((v ? "[ON] " : "[OFF] ") + new Date().toLocaleTimeString());
  refresh();
});

$("sizeInput").addEventListener("change", async e => {
  const v = (e.target.value || "").trim();
  if (/^\d+[xX]\d+$/.test(v)) {
    await exec(`mkdir -p ${DIR}; echo "${v}" > ${DIR}/size.txt`);
    toast("尺寸已存 " + v + "，点『重启相机』生效");
  } else if (v === "") {
    await exec(`rm -f ${DIR}/size.txt`); toast("已用默认 1280x720");
  } else toast("格式应为 宽x高，如 1280x720");
});

$("mirror").addEventListener("change", async e => {
  await exec(`mkdir -p ${CFG}; echo ${e.target.checked ? 1 : 0} > ${CFG}/front_mirror`);
  toast("前摄镜像已保存");
});

async function copyPick(kind, name) {
  try {
    const p = ks.pickFile ? await ks.pickFile({ mime: "*/*" }) : null;
    if (!p) { toast("当前 WebUI 不支持选文件，请手动把文件放到 " + DIR); return; }
    await exec(`cp "${p}" ${DIR}/${name} && echo ${name} > ${CFG}/current`);
    toast("已替换 " + name); refresh();
  } catch (e) { toast("失败：" + e.message); }
}
$("pickVideo").onclick = () => copyPick("video", "virtual.mp4");
$("pickPhoto").onclick = () => copyPick("photo", "photo.jpg");
$("pickFront").onclick = () => copyPick("front", "front.jpg");

$("restart").onclick = async () => {
  await exec(`killall com.oplus.camera 2>/dev/null; killall cameraserver 2>/dev/null; echo done`);
  toast("已尝试重启相机进程");
};

if (!ks || !ks.exec) log("提示: window.kernelsu 不存在，需在 KernelSU App 内打开。");
refresh();
setInterval(refresh, 3000);
