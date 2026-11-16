import argparse
import io
import time

import requests
from PIL import Image

try:
    import mss
except ImportError:
    mss = None


def parse_region(text):
    parts = [int(v) for v in text.split(",")]
    if len(parts) != 4:
        raise ValueError("--region 需要 left,top,width,height 四个整数")
    return {"left": parts[0], "top": parts[1], "width": parts[2], "height": parts[3]}


def main():
    ap = argparse.ArgumentParser(description="把本机屏幕以 JPEG 推送到 open_code 设备")
    ap.add_argument("--host", required=True, help="设备 IP，例如 192.168.4.1")
    ap.add_argument("--width", type=int, default=320, help="发送宽度，建议 320")
    ap.add_argument("--fps", type=float, default=8.0, help="目标帧率")
    ap.add_argument("--quality", type=int, default=70, help="JPEG 质量 1-95")
    ap.add_argument("--region", default="", help="只发送屏幕的一块，格式 left,top,width,height")
    ap.add_argument("--stop", action="store_true", help="让设备退出投屏")
    args = ap.parse_args()

    base = "http://" + args.host

    if args.stop:
        resp = requests.post(base + "/api/cast/stop", timeout=5)
        print("stop:", resp.status_code, resp.text)
        return 0

    if mss is None:
        print("缺少依赖，请先执行: pip install mss pillow requests")
        return 1

    url = base + "/api/cast"
    period = 1.0 / args.fps if args.fps > 0 else 0.0
    session = requests.Session()
    sent = 0
    fail = 0
    report = 20
    start = time.time()

    try:
        with mss.mss() as sct:
            region = parse_region(args.region) if args.region else sct.monitors[1]
            print("发送到 %s，区域 %dx%d，宽度 %d，目标 %.1f fps"
                  % (url, region["width"], region["height"], args.width, args.fps))
            while True:
                tick = time.time()
                shot = sct.grab(region)
                img = Image.frombytes("RGB", shot.size, shot.bgra, "raw", "BGRX")
                height = max(1, round(args.width * img.height / img.width))
                img = img.resize((args.width, height), Image.BILINEAR)
                buf = io.BytesIO()
                img.save(buf, format="JPEG", quality=args.quality)
                payload = buf.getvalue()
                try:
                    resp = session.post(url, data=payload, timeout=5,
                                        headers={"Content-Type": "image/jpeg"})
                    if resp.ok:
                        sent += 1
                    else:
                        fail += 1
                        print("设备拒绝:", resp.status_code, resp.text)
                except requests.RequestException as exc:
                    fail += 1
                    print("发送失败:", exc)
                if sent >= report:
                    report += 20
                    spent = max(time.time() - start, 0.001)
                    print("成功 %d 帧，失败 %d 帧，平均 %.1f fps，单帧 %d 字节"
                          % (sent, fail, sent / spent, len(payload)))
                wait = period - (time.time() - tick)
                if wait > 0:
                    time.sleep(wait)
    except KeyboardInterrupt:
        print()
        print("结束：成功 %d 帧，失败 %d 帧" % (sent, fail))
        try:
            session.post(base + "/api/cast/stop", timeout=3)
        except requests.RequestException:
            pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
