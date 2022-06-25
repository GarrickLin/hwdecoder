import hwdecoder
import numpy as np
import cv2

decoder = hwdecoder.HWDecoder()

def run(raw_file):
    with open(raw_file, 'rb') as f:
        while 1:
            data_in = f.read(1024)
            if not data_in:
                break
            framedatas = decoder.decode(data_in)
            for framedata in framedatas:
                (frame, w, h, ls) = framedata
                print(w, h, ls)
                frame = np.frombuffer(frame, dtype=np.ubyte, count=len(frame))
                assert(frame.size == h*w*3)
                frame = frame.reshape((h, w, 3))
                cv2.imshow("show", frame)            
                cv2.waitKey(1)

if __name__ == '__main__':
    run("D:/downloads/video_html/1080p.h264")