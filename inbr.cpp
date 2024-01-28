#include <windows.h>
#include "pch.h"
#include <vector>
#include "avisynth.h"

class Intellibr : public GenericVideoFilter {
public:
    Intellibr(PClip _child, IScriptEnvironment* env) :
        GenericVideoFilter(_child), lastColors(96, 0), curve(256, 0) {
        targetMin = 0; targetMax = 255; dynamicity = 10; sceneThreshold = 20; oldMin = 0; oldK = 1.0;
        curveEnd = 26;    alpha = 50; p_mode = 1; lastP = 255;
        curveStart = 0; beta = 50;


        if (vi.IsRGB32())
            isRGB = true;
        else if (vi.IsYV12())
            isRGB = false;
        else {
            env->ThrowError("Intellibr: input colorspace must be RGB32 or YV12.");
            return;
        }
        calcCurve();
    }

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) {

        PVideoFrame src = child->GetFrame(n, env);
        PVideoFrame dst = env->NewVideoFrame(vi);

        if (isRGB) processRGB(src, dst);
        else processYV12(src, dst);

        return dst;
    }

private:
    bool isRGB;

    int targetMin, targetMax; //0 .. 255
    int dynamicity, sceneThreshold; // 1 .. 100
    int    curveEnd; // 1 .. 255;
    int alpha; // 1..250; /50
    int p_mode; //0,1,2 - max, average, median
    int curveStart;
    int beta;
    int lastP; // P value of last processed frame

    std::vector<int> lastColors, curve;
    double oldK;
    int oldMin;

    void calcCurve() {
        for (int i = 0; i < curveStart; i++)
            curve[i] = targetMin + i;
        /*
        f(x) = ax^3 + bx^2 + cx
        f'(0) = c = alpha
        f'(1) = 3a + 2b + c = beta
          => 3a + 2b = beta - alpha
          => b = (beta - alpha - 3a)/2
        f(1) = a+b+c = 1
          => a + beta/2 - alpha/2 - 3/2a + alpha = 1
          => beta + alpha  - 2 =  a

        */
        for (int i = curveStart; i < curveEnd; i++) {
            double x = (double)(i - curveStart) / (curveEnd - curveStart);
            double al = alpha / 50.0;
            double be = beta / 50.0;
            double c = al;
            double a = al + be - 2;
            double b = (be - al - 3 * a) / 2;
            double h = targetMax - targetMin - curveStart;
            int t = targetMin + curveStart + (a * x * x * x + b * x * x + c * x) * h;
            if (t > targetMax) t = targetMax;
            if (t < targetMin) t = targetMin;
            curve[i] = t;
        }
        for (int i = curveEnd; i < 256; i++)
            curve[i] = targetMax;
    }

    void processRGB(PVideoFrame &srcFrame, PVideoFrame &dstFrame)
    {
        const BYTE* src = srcFrame->GetReadPtr();
        BYTE* dst = dstFrame->GetWritePtr();
        int srcpitch = srcFrame->GetPitch();
        int dstpitch = dstFrame->GetPitch();
        int h = vi.height;
        int w = vi.width;

        int nr[256] = {}, ng[256] = {}, nb[256] = {}, nrgb[256] = {}, cls[96] = {}, nmx[256] = {};
        long nVals = w * h * 3, nPix = w * h;

        //collect histos
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const BYTE* pixel = &src[x*4];
                nr[pixel[0]]++;
                ng[pixel[1]]++;
                nb[pixel[2]]++;
                const int mx = max(pixel[0], max(pixel[1], pixel[2]));
                nmx[mx]++;
            }
            src = src + srcpitch;
        }

        for (int i = 0; i < 256; i++) {
            nrgb[i] = nr[i] + ng[i] + nb[i];
            cls[i / 8] += nr[i];
            cls[i / 8 + 32] += ng[i];
            cls[i / 8 + 64] += nb[i];
        }

        int minBr = 0, maxBr = 255, shift = 0;
        while (minBr < 256 && nrgb[minBr] == 0) minBr++;
        while (maxBr >= 0 && nrgb[maxBr] == 0) maxBr--;
        double k = 1;
        bool work = false;

        //determine target range by using the curve
        int P = 0;
        switch (p_mode) {
        case 0://max
            P = maxBr;
            break;
        case 1://avg
            {
                long sumBr = 0;
                for (int i = 0; i < 256; i++) {
                    long q = (long)nmx[i] * i;
                    sumBr += q;
                }
                P = sumBr / nPix;
            }
            break;
        case 2://median
            {
                long sumPix = 0;
                long p = 0;
                auto half = nPix / 2;
                while (p < 256 && sumPix + nmx[p] < half) {
                    sumPix += nmx[p];
                    p++;
                }
                P = p;
            }
            break;
        }
        if (P != lastP) {
            lastP = P;
        }

        int trgMin = targetMin;
        int trgMax = curve[P];
        double maxAllowedK = maxBr > minBr ? (double)(targetMax - trgMin) / (maxBr - minBr) : 1;
        if (maxBr > minBr && (minBr != trgMin || maxBr != trgMax)) {
            if (p_mode == 0)
                k = (double)(trgMax - trgMin) / (maxBr - minBr);
            else
                k = min(P > 0 ? (double)(trgMax - trgMin) / P : 1, maxAllowedK);
            work = true;
        }

        int sum = 0, total = 0; //scene change check
        for (int i = 0; i < 96; i++) {
            sum += abs(cls[i] - lastColors[i]);
            total += cls[i];
        }
        double chng = (double)sum / total;
        if (chng * 100 < sceneThreshold) { // not new scene
            double alpha = dynamicity / 100.0;
            k = k * alpha + oldK * (1.0 - alpha); // smooth the parameters
            minBr = minBr * alpha + oldMin * (1.0 - alpha);
            work = true;
        }

        BYTE tbl[256];
        src = srcFrame->GetReadPtr();

        if (work) {
            for (int i = 0; i < 256; i++) {
                int v = (i - minBr) * k + trgMin + 0.5;
                tbl[i] = v < targetMin ? targetMin : (v > targetMax ? targetMax : v);
            }
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const BYTE* pixel = &src[x*4];
                    BYTE* resPixel = &dst[x*4];
                    resPixel[0] = tbl[pixel[0]];
                    resPixel[1] = tbl[pixel[1]];
                    resPixel[2] = tbl[pixel[2]];
                    resPixel[3] = pixel[3]; //Alpha
                }
                dst = dst + dstpitch;
                src = src + srcpitch;
            }
        }
        else { //copy
            for (int y = 0; y < h; ++y) {
                memcpy(dst, src, w * 4);
                dst = dst + dstpitch;
                src = src + srcpitch;
            }
        }

        for (int i = 0; i < 96; i++)
            lastColors[i] = cls[i];
        oldK = k;
        oldMin = minBr;
    } //processRGB

    void processYV12(PVideoFrame& srcFrame, PVideoFrame& dstFrame)
    {
        const int HF = 2;
        int h = vi.height;
        int w = vi.width;

        int    cls[96] = {}, ny[256] = {}, nu[256] = {}, nv[256] = {};
        const long nPix = w * h;
        const int srcpitch = srcFrame->GetPitch(PLANAR_Y);
        const int dstpitch = dstFrame->GetPitch(PLANAR_Y);

        //collect histos
        const BYTE* src = srcFrame->GetReadPtr(PLANAR_Y);
        BYTE* dst = dstFrame->GetWritePtr(PLANAR_Y);

        for (int y = 0; y < h; y++) {
            for (int i = 0; i < w; i++)
                ny[src[i]]++;
            src += srcpitch;
        }

        const BYTE* srcu = srcFrame->GetReadPtr(PLANAR_U);
        const BYTE* srcv = srcFrame->GetReadPtr(PLANAR_V);
        auto upitch = srcFrame->GetPitch(PLANAR_U);
        auto vpitch = srcFrame->GetPitch(PLANAR_V);
        for (int y = 0; y < h / HF; y++) {
            for (int i = 0; i < w / 2; i++) {
                nu[srcu[i]]++;
                nv[srcv[i]]++;
            }
            srcu += upitch;
            srcv += vpitch;
        }

        for (int i = 0; i < 256; i++) {
            cls[i / 8] += ny[i];
            cls[i / 8 + 32] += nu[i];
            cls[i / 8 + 64] += nv[i];
        }

        int minBr = 0, maxBr = 255, shift = 0;
        while (minBr < 256 && ny[minBr] == 0) minBr++;
        while (maxBr >= 0 && ny[maxBr] == 0) maxBr--;
        double k = 1;
        bool work = false;

        //determine target range by using the curve
        int P = 0;
        switch (p_mode) {
        case 0://max
            P = maxBr;
            break;
        case 1://avg
            {
                long sumBr = 0;
                for (int i = 0; i < 256; i++) {
                    long q = (long)ny[i] * i;
                    sumBr += q;
                }
                P = sumBr / nPix;
            }
            break;
        case 2://median
            {
                long sumPix = 0;
                long p = 0;
                auto half = nPix / 2;
                while (p < 256 && sumPix + ny[p] < half) {
                    sumPix += ny[p];
                    p++;
                }
                P = p;
            }
            break;
        }

        int trgMin = targetMin;
        int trgMax = curve[P];
        double maxAllowedK = maxBr > minBr ? (double)(targetMax - trgMin) / (maxBr - minBr) : 1;
        if (maxBr > minBr && (minBr != trgMin || maxBr != trgMax)) {
            if (p_mode == 0)
                k = (double)(trgMax - trgMin) / (maxBr - minBr);
            else
                k = min(P > 0 ? (double)(trgMax - trgMin) / P : 1, maxAllowedK);
            work = true;
        }

        int sum = 0, total = 0; //scene change check
        for (int i = 0; i < 96; i++) {
            sum += abs(cls[i] - lastColors[i]);
            total += cls[i];
        }
        double chng = (double)sum / total;
        if (chng * 100 < sceneThreshold) { // not new scene
            double alpha = dynamicity / 100.0;
            k = k * alpha + oldK * (1.0 - alpha); // smooth the parameters
            minBr = minBr * alpha + oldMin * (1.0 - alpha);
            work = true;
        }

        BYTE tbl[256], uvtbl[256];

        src = srcFrame->GetReadPtr(PLANAR_Y);

        if (work) {
            for (int i = 0; i < 256; i++) {
                int v = (i - minBr) * k + trgMin + 0.5;
                tbl[i] = v < targetMin ? targetMin : (v > targetMax ? targetMax : v);
                int u = 128 + (i - 128) * k;
                uvtbl[i] = u < 0 ? 0 : (u > 255 ? 255 : u);
            }

            for (int y = 0; y < h; y++) {
                for (int i = 0; i < w; i++)
                    dst[i] = tbl[src[i]];
                src += srcpitch;
                dst += dstpitch;
            }
        }
        else { //copy
            for (int y = 0; y < h; y++) {
                memcpy(dst, src, w);
                src += srcpitch;
                dst += dstpitch;
            }
        }

        //transorm or copy U & V
        const int dpitch2 = dstFrame->GetPitch(PLANAR_U);
        const int dpitch3 = dstFrame->GetPitch(PLANAR_V);
        const int spitch2 = upitch;
        const int spitch3 = vpitch;

        BYTE* dstdata2 = dstFrame->GetWritePtr(PLANAR_U);
        BYTE* dstdata3 = dstFrame->GetWritePtr(PLANAR_V);
        const BYTE* srcdata2 = srcFrame->GetReadPtr(PLANAR_U);
        const BYTE* srcdata3 = srcFrame->GetReadPtr(PLANAR_V);

        if (work) {
            for (int y = 0; y < h / HF; y++) {
                for (int i = 0; i < w / 2; i++) {
                    dstdata2[i] = uvtbl[srcdata2[i]];
                    dstdata3[i] = uvtbl[srcdata3[i]];
                }
                srcdata2 += spitch2;
                dstdata2 += dpitch2;
                srcdata3 += spitch3;
                dstdata3 += dpitch3;
            }
        }
        else {
            for (int y = 0; y < h / HF; y++) {
                memcpy(&dstdata2[y * dpitch2], &srcdata2[y * spitch2], w / 2);
                memcpy(&dstdata3[y * dpitch3], &srcdata3[y * spitch3], w / 2);
            }
        }

        for (int i = 0; i < 96; i++)
            lastColors[i] = cls[i];
        oldK = k;
        oldMin = minBr;
    } //processYV12
};

AVSValue __cdecl Create_Intellibr(AVSValue args, void* user_data, IScriptEnvironment* env) {
    return new Intellibr(args[0].AsClip(), env);
}

const AVS_Linkage* AVS_linkage = 0;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment * env, const AVS_Linkage* const vectors) {
    AVS_linkage = vectors;
    env->AddFunction("Intellibr", "c", Create_Intellibr, 0);
    return "Intelligent Brightness";
}
