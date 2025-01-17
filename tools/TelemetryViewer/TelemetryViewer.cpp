#include "stdafx.h"
#include "TelemetryViewer.h"


#include <Time/StopWatch.h>

X_NAMESPACE_BEGIN(telemetry)

using namespace core::string_view_literals;

namespace
{
    const platform::SOCKET INV_SOCKET = (platform::SOCKET)(~0);

    static const char* IntTable100 =
        "00010203040506070809"
        "10111213141516171819"
        "20212223242526272829"
        "30313233343536373839"
        "40414243444546474849"
        "50515253545556575859"
        "60616263646566676869"
        "70717273747576777879"
        "80818283848586878889"
        "90919293949596979899";

    X_DISABLE_WARNING(4244)

    using StringBuf = core::StackString<64,char>;


    int64_t GetSystemTimeAsUnixTime(void)
    {
        // January 1, 1970 (start of Unix epoch) in "ticks"
        const int64_t UNIX_TIME_START = 0x019DB1DED53E8000;
        // tick is 100ns
        const int64_t TICKS_PER_SECOND = 10000000;

        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);

        LARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;

        return (li.QuadPart - UNIX_TIME_START) / TICKS_PER_SECOND;
    }


    void unixToHumanAgeStr(StringBuf& buf, int64_t seconds)
    {
        // ermm some nice format.
        if (seconds < 60) {
            buf.setFmt("%" PRId64 "s ago", seconds);
            return;
        }

        auto min = seconds / 60;

        if (min < 60) {
            buf.setFmt("%" PRId64 "m ago", min);
            return;
        }

        auto hour = min / 60;
        min %= 60;

        if (hour < 24) {
            buf.setFmt("%" PRId64 "h %" PRId64 "m ago", hour, min);
            return;
        }

        auto days = hour / 24;

        buf.setFmt("%" PRId64 " days ago", days);
    }

    const char* unixToLocalTimeStr(StringBuf& buf, int64_t seconds)
    {
        time_t tt = seconds;

        char buff[32];
        auto len = strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
        if (len <= 0) {
            buf.clear();
        }
        else {
            buf.set(buff, buff + len);
        }

        return buf.c_str();
    }


    inline void PrintTinyInt(StringBuf& buf, uint64_t v)
    {
        if (v >= 10)
        {
            buf.append('0' + v / 10, 1);
        }
        buf.append('0' + v % 10, 1);
    }

    inline void PrintTinyInt0(StringBuf& buf, uint64_t v)
    {
        if (v >= 10)
        {
            buf.append('0' + v / 10, 1);
        }
        else
        {
            buf.append('0', 1);
        }
        buf.append('0' + v % 10, 1);
    }

    inline void PrintSmallInt(StringBuf& buf, uint64_t v)
    {
        if (v >= 100)
        {
            buf.append(IntTable100 + v / 10 * 2, 2);
        }
        else if (v >= 10)
        {
            buf.append('0' + v / 10, 1);
        }
        buf.append('0' + v % 10, 1);
    }

    inline void PrintFrac00(StringBuf& buf, uint64_t v)
    {
        buf.append('.', 1);
        v += 5;
        if (v / 10 % 10 == 0)
        {
            buf.append('0' + v / 100, 1);
        }
        else
        {
            buf.append(IntTable100 + v / 10 * 2, 2);
        }
    }

    inline void PrintFrac0(StringBuf& buf, uint64_t v)
    {
        buf.append('.', 1);
        buf.append('0' + (v + 50) / 100, 1);
    }

    inline void PrintSmallIntFrac(StringBuf& buf, uint64_t v)
    {
        uint64_t in = v / 1000;
        uint64_t fr = v % 1000;
        if (fr >= 995)
        {
            PrintSmallInt(buf, in + 1);
        }
        else
        {
            PrintSmallInt(buf, in);
            if (fr > 5)
            {
                PrintFrac00(buf, fr);
            }
        }
    }

    inline void PrintSecondsFrac(StringBuf& buf, uint64_t v)
    {
        uint64_t in = v / 1000;
        uint64_t fr = v % 1000;
        if (fr >= 950)
        {
            PrintTinyInt0(buf, in + 1);
        }
        else
        {
            PrintTinyInt0(buf, in);
            if (fr > 50)
            {
                PrintFrac0(buf, fr);
            }
        }
    }

    const char* TimeToString(StringBuf& buf, int64_t _ns)
    {
        buf.clear();

        uint64_t ns;
        if (_ns < 0)
        {
            buf.append('-', 1);
            ns = -_ns;
        }
        else
        {
            ns = _ns;
        }

        if (ns < 1000)
        {
            PrintSmallInt(buf, ns);
            buf.append(" ns");
        }
        else if (ns < 1000ll * 1000)
        {
            PrintSmallIntFrac(buf, ns);
#ifdef TRACY_EXTENDED_FONT
            buf.append(" \xce\xbcs");
#else
            buf.append(" us");
#endif
        }
        else if (ns < 1000ll * 1000 * 1000)
        {
            PrintSmallIntFrac(buf, ns / 1000);
            buf.append(" ms");
        }
        else if (ns < 1000ll * 1000 * 1000 * 60)
        {
            PrintSmallIntFrac(buf, ns / (1000ll * 1000));
            buf.append(" s");
        }
        else if (ns < 1000ll * 1000 * 1000 * 60 * 60)
        {
            const auto m = int64_t(ns / (1000ll * 1000 * 1000 * 60));
            const auto s = int64_t(ns - m * (1000ll * 1000 * 1000 * 60)) / (1000ll * 1000);
            PrintTinyInt(buf, m);
            buf.append(':', 1);
            PrintSecondsFrac(buf, s);
        }
        else if (ns < 1000ll * 1000 * 1000 * 60 * 60 * 24)
        {
            const auto h = int64_t(ns / (1000ll * 1000 * 1000 * 60 * 60));
            const auto m = int64_t(ns / (1000ll * 1000 * 1000 * 60) - h * 60);
            const auto s = int64_t(ns / (1000ll * 1000 * 1000) - h * (60 * 60) - m * 60);
            PrintTinyInt(buf, h);
            buf.append(':', 1);
            PrintTinyInt0(buf, m);
            buf.append(':', 1);
            PrintTinyInt0(buf, s);
        }
        else
        {
            const auto d = int64_t(ns / (1000ll * 1000 * 1000 * 60 * 60 * 24));
            const auto h = int64_t(ns / (1000ll * 1000 * 1000 * 60 * 60) - d * 24);
            const auto m = int64_t(ns / (1000ll * 1000 * 1000 * 60) - d * (60 * 24) - h * 60);
            const auto s = int64_t(ns / (1000ll * 1000 * 1000) - d * (60 * 60 * 24) - h * (60 * 60) - m * 60);
            if (d < 1000)
            {
                PrintSmallInt(buf, d);
                buf.append('d', 1);
            }
            else
            {
                buf.appendFmt("%" PRIi64 "d", d);
            }

            PrintTinyInt0(buf, h);
            buf.append(':', 1);
            PrintTinyInt0(buf, m);
            buf.append(':', 1);
            PrintTinyInt0(buf, s);
        }

        return buf.c_str();
    }

    
    const char* IntToString(StringBuf& buf, int32_t val)
    {
        buf.setFmt("%" PRIi32, val);
        return buf.c_str();
    }

    const char* RealToString(StringBuf& buf, double val, bool separator)
    {
        X_UNUSED(separator);

        buf.setFmt("%f", val);
        return buf.c_str();
    }

    void TextDisabledUnformatted(const char* begin, const char* end = nullptr)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, GImGui->Style.Colors[ImGuiCol_TextDisabled]);
        ImGui::TextUnformatted(begin, end);
        ImGui::PopStyleColor();
    }

    void TextFocused(const char* label, const char* value, const char* end = nullptr)
    {
        TextDisabledUnformatted(label);
        ImGui::SameLine();
        ImGui::TextUnformatted(value, end);
    }

    void TextFocusedFmt(const char* label, const char* pFmt, ...)
    {
        TextDisabledUnformatted(label);
        ImGui::SameLine();

        core::StackString256 str;
        va_list args;
        va_start(args, pFmt);
        str.setFmt(pFmt, args);
        va_end(args);

        ImGui::TextUnformatted(str.c_str(), str.end());
    }

    void DrawHelpMarker(const char* desc)
    {
        TextDisabledUnformatted("(?)");
        if (ImGui::IsItemHovered())
        {
            const auto ty = ImGui::GetFontSize();
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(450.0f * ty / 15.f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    void DrawTextContrast(ImDrawList* draw, const ImVec2& pos, uint32_t color, const char* text, const char* end = nullptr)
    {
        draw->AddText(pos + ImVec2(1, 1), 0x88000000, text, end);
        draw->AddText(pos, color, text, end);
    }

    inline void DrawTextContrast(ImDrawList* draw, const ImVec2& pos, uint32_t color, core::string_view str)
    {
        DrawTextContrast(draw, pos, color, str.begin(), str.end());
    }

    bool Splitter(bool split_vertically, float thickness, float* size1, float* size2,
        float min_size1, float min_size2, float splitter_long_axis_size = -1.0f)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* window = g.CurrentWindow;
        ImGuiID id = window->GetID("##Splitter");
        ImRect bb;
        bb.Min = window->DC.CursorPos + (split_vertically ? ImVec2(*size1, 0.0f) : ImVec2(0.0f, *size1));
        bb.Max = bb.Min + ImGui::CalcItemSize(split_vertically ? ImVec2(thickness, splitter_long_axis_size) : ImVec2(splitter_long_axis_size, thickness), 0.0f, 0.0f);
        return ImGui::SplitterBehavior(bb, id, split_vertically ? ImGuiAxis_X : ImGuiAxis_Y, size1, size2, min_size1, min_size2, 0.0f);
    }

    // ---------------------------


    int32_t GetFrameWidth(int32_t frameScale)
    {
        return frameScale == 0 ? 4 : (frameScale < 0 ? 6 : 1);
    }

    int32_t GetFrameGroup(int32_t frameScale)
    {
        return frameScale < 2 ? 1 : (1 << (frameScale - 1));
    }

    ImU32 GetFrameColor(uint64_t frameTime)
    {
        enum { BestTime = 1000 * 1000 * 1000 / 143 };
        enum { GoodTime = 1000 * 1000 * 1000 / 59 };
        enum { BadTime = 1000 * 1000 * 1000 / 29 };

        return frameTime > BadTime ? 0xFF2222DD :
            frameTime > GoodTime ? 0xFF22DDDD :
            frameTime > BestTime ? 0xFF22DD22 : 0xFFDD9900;
    }

    int64_t GetFrameTime(size_t idx)
    {
        X_UNUSED(idx);
        return 1000 * 1000 * (10 + (idx % 10));
    }

    int64_t GetFrameBegin(size_t idx)
    {
        X_UNUSED(idx);
        return 1000 * 1000 * (10 + (idx % 10));
    }

    int64_t GetFrameEnd(size_t idx)
    {
        X_UNUSED(idx);
        return GetFrameBegin(idx) + 1024;
    }

    int64_t GetTimeBegin(void)
    {
        return GetFrameBegin(0);
    }

    uint64_t GetFrameOffset()
    {
        return 0;
    }

    std::pair<int, int> GetFrameRange(int64_t from, int64_t to)
    {
        X_UNUSED(from, to);
        return { 10, 40 };
    }

    const char* GetThreadString(uint64_t id)
    {
        X_UNUSED(id);
        return "meow";
    }

    namespace HumanNumber
    {
        using Str = core::StackString<96, char>;

        const char* toString(Str& str, int64_t num)
        {
            str.clear();

            int64_t n2 = 0;
            int64_t scale = 1;
            if (num < 0) {
                str.append('-', 1);
                num = -num;
            }
            while (num >= 1000) {
                n2 = n2 + scale * (num % 1000);
                num /= 1000;
                scale *= 1000;
            }

            str.appendFmt("%" PRIi64, num);
            while (scale != 1) {
                scale /= 1000;
                num = n2 / scale;
                n2 = n2 % scale;
                str.appendFmt(",%03" PRIi64, num);
            }

            return str.c_str();
        }

    }

    void DrawZigZag(ImDrawList* draw, const ImVec2& wpos, double start, double end, double h, uint32_t color)
    {
        int mode = 0;
        while (start < end)
        {
            double step = std::min(end - start, mode == 0 ? h / 2 : h);
            switch (mode)
            {
                case 0:
                    draw->AddLine(wpos + ImVec2(start, 0), wpos + ImVec2(start + step, round(-step)), color);
                    mode = 1;
                    break;
                case 1:
                    draw->AddLine(wpos + ImVec2(start, round(-h / 2)), wpos + ImVec2(start + step, round(step - h / 2)), color);
                    mode = 2;
                    break;
                case 2:
                    draw->AddLine(wpos + ImVec2(start, round(h / 2)), wpos + ImVec2(start + step, round(h / 2 - step)), color);
                    mode = 1;
                    break;
                default:
                    X_ASSERT_UNREACHABLE();
                    break;
            };
            start += step;
        }
    }

    uint32_t GetColorMuted(uint32_t color, bool active)
    {
        if (active) {
            return 0xFF000000 | color;
        }
    
        return 0x66000000 | color;
    }

    using FrameTextStrBuf = core::StackString<64, char>;

    const char* GetFrameText(FrameTextStrBuf& buf, int64_t durationNS, int64_t frameNum)
    {
        StringBuf strBuf;
        buf.setFmt("Frame %" PRIi64 " (%s)", frameNum, TimeToString(strBuf, durationNS));
        return buf.c_str();
    }

    enum { MinVisSize = 3 };
    enum { MinFrameSize = 5 };



} // namespace

void ZoomToRange(TraceView& view, int64_t start, int64_t end)
{
    if (start == end)
    {
        end = start + 1;
    }

    view.paused_ = true;
    view.highlightZoom_.active = false;
    view.zoomAnim_.active = true;
    view.zoomAnim_.start0 = view.zvStartNS_;
    view.zoomAnim_.start1 = start;
    view.zoomAnim_.end0 = view.zvEndNS_;
    view.zoomAnim_.end1 = end;
    view.zoomAnim_.progress = 0;

    const auto d0 = double(view.zoomAnim_.end0 - view.zoomAnim_.start0);
    const auto d1 = double(view.zoomAnim_.end1 - view.zoomAnim_.start1);
    const auto diff = d0 > d1 ? d0 / d1 : d1 / d0;
    view.zoomAnim_.lenMod = 5.0 / log10(diff);
}

void ZoomToZone(TraceView& view, const ZoneData& zone)
{
    if (zone.endNano - zone.startNano <= 0) {
        return;
    }

    ZoomToRange(view, zone.startNano, zone.endNano);
}

void ZoomToLockTry(TraceView& view, const LockTry& lock)
{
    if (lock.endNano - lock.startNano <= 0) {
        return;
    }

    ZoomToRange(view, lock.startNano, lock.endNano);
}


void HandleZoneViewMouse(TraceView& view, int64_t timespan, const ImVec2& wpos, float w, double& pxns)
{
    auto& io = ImGui::GetIO();

    const auto nspx = double(timespan) / w;

    if (ImGui::IsMouseClicked(0))
    {
        view.highlight_.active = true;
        view.highlight_.start = view.highlight_.end = view.zvStartNS_ + (io.MousePos.x - wpos.x) * nspx;
    }
    else if (ImGui::IsMouseDragging(0, 0))
    {
        view.highlight_.end = view.zvStartNS_ + (io.MousePos.x - wpos.x) * nspx;
    }
    else
    {
        view.highlight_.active = false;
    }

    if (ImGui::IsMouseClicked(2))
    {
        view.highlightZoom_.active = true;
        view.highlightZoom_.start = view.highlightZoom_.end = view.zvStartNS_ + (io.MousePos.x - wpos.x) * nspx;
    }
    else if (ImGui::IsMouseDragging(2, 0))
    {
        view.highlightZoom_.end = view.zvStartNS_ + (io.MousePos.x - wpos.x) * nspx;
    }
    else if (view.highlightZoom_.active)
    {
        if (view.highlightZoom_.start != view.highlightZoom_.end)
        {
            const auto s = std::min(view.highlightZoom_.start, view.highlightZoom_.end);
            const auto e = std::max(view.highlightZoom_.start, view.highlightZoom_.end);

            // ZoomToRange disables view.highlightZoom_.active
            if (io.KeyCtrl)
            {
                const auto tsOld = view.zvEndNS_ - view.zvStartNS_;
                const auto tsNew = e - s;
                const auto mul = double(tsOld) / tsNew;
                const auto left = s - view.zvStartNS_;
                const auto right = view.zvEndNS_ - e;

                ZoomToRange(view, view.zvStartNS_ - left * mul, view.zvEndNS_ + right * mul);
            }
            else
            {
                ZoomToRange(view, s, e);
            }
        }
        else
        {
            view.highlightZoom_.active = false;
        }
    }

    if (ImGui::IsMouseDragging(1, 0))
    {
        view.zoomAnim_.active = false;
        view.paused_ = true;
        const auto delta = ImGui::GetMouseDragDelta(1, 0);
        const auto dpx = int64_t(delta.x * nspx);
        if (dpx != 0)
        {
            view.zvStartNS_ -= dpx;
            view.zvEndNS_ -= dpx;
            io.MouseClickedPos[1].x = io.MousePos.x;
        }
        if (delta.y != 0)
        {
            auto y = ImGui::GetScrollY();
            ImGui::SetScrollY(y - delta.y);
            io.MouseClickedPos[1].y = io.MousePos.y;
        }
    }

    const auto wheel = io.MouseWheel;
    if (wheel != 0)
    {
        view.zoomAnim_.active = false;
        view.paused_ = true;
        const double mouse = io.MousePos.x - wpos.x;
        const auto p = mouse / w;
        const auto p1 = timespan * p;
        const auto p2 = timespan - p1;
        if (wheel > 0)
        {
            view.zvStartNS_ += int64_t(p1 * 0.25);
            view.zvEndNS_ -= int64_t(p2 * 0.25);
        }
        else if (timespan < 1000ll * 1000 * 1000 * 60 * 60)
        {
            view.zvStartNS_ -= std::max(int64_t(1), int64_t(p1 * 0.25));
            view.zvEndNS_ += std::max(int64_t(1), int64_t(p2 * 0.25));
        }
        timespan = view.zvEndNS_ - view.zvStartNS_;
        pxns = w / double(timespan);
    }
}

void LockTryTooltip(TraceView& view, const LockTry& lock)
{
    const int64_t cycles = lock.endTick - lock.startTick;
    const int64_t time = lock.endNano - lock.startNano;

    StringBuf strBuf;

    auto strDesc = view.strings.getString(lock.strIdxDescrption);
    auto strFile = view.strings.getString(lock.strIdxFile);
    auto strLockName = view.strings.getLockName(lock.lockHandle);
    auto strThread = view.strings.getThreadName(lock.threadID);

    ImGui::BeginTooltip();

        if (lock.result == TtLockResultAcquired) {
            ImGui::Text("Waiting for lock \"%s\" (Acquired)", strLockName.begin());
        }
        else if (lock.result == TtLockResultFail) {
            ImGui::Text("Waiting for lock \"%s\" (Fail)", strLockName.begin());
        }
        else {
            X_ASSERT_UNREACHABLE();
        }
        ImGui::Separator();
        ImGui::TextUnformatted(strDesc.begin(), strDesc.end());
        TextFocusedFmt("Handle", "0x%" PRIX64, lock.lockHandle);
        ImGui::Separator();
        ImGui::Text("%s:%i", strFile.data(), lock.lineNo);
        TextFocused("Thread:", strThread.begin(), strThread.end());
        ImGui::SameLine();
        ImGui::TextDisabled("(0x%" PRIX32 ")", lock.threadID);
        ImGui::Separator();
        TextFocused("Execution time:", TimeToString(strBuf, time));
        ImGui::SameLine();
        TextFocusedFmt("Cycles:", "%" PRId64, cycles);

    ImGui::EndTooltip();
}

void LockStateTooltip(TraceView& view, uint64_t lockId, const LockState& lockState, const LockState& lockStateNext)
{
    X_UNUSED(view);

    const int64_t cycles = lockStateNext.time - lockState.time;
    const int64_t time = lockStateNext.timeNano - lockState.timeNano;

    // these should never driff across threads?
    // currently no.
    X_ASSERT(lockStateNext.threadID == lockState.threadID, "Lock state on different thread")();

    auto strLock = view.strings.getLockName(lockId);
    auto strThread = view.strings.getThreadName(lockState.threadID);
    auto strFile = view.strings.getString(lockState.strIdxFile);

    StringBuf strBuf;
    core::StackString256 str;
    str.appendFmt("%s -> %s", TtLockStateToString(lockState.state), TtLockStateToString(lockStateNext.state));

    ImGui::BeginTooltip();

        ImGui::TextUnformatted(strLock.begin(), strLock.end());
        ImGui::Separator();
        ImGui::TextUnformatted(str.begin(), str.end());
        ImGui::Separator();
        ImGui::Text("%s:%i", strFile.data(), lockState.lineNo);
        ImGui::Separator();
        TextFocused("Execution time:", TimeToString(strBuf, time));
        ImGui::SameLine();
        TextFocusedFmt("Cycles:", "%" PRId64, cycles);

    ImGui::EndTooltip();
}


void ZoneTooltip(TraceView& view, ZoneSegmentThread& thread, const ZoneData& zone)
{
    X_UNUSED(view);

    const int64_t cycles = zone.endTicks - zone.startTicks;

    const int64_t end = zone.endNano;
    const int64_t time = end - zone.startNano;
    const int64_t childTime = 0;
    const int64_t selftime = time - childTime;

    StringBuf strBuf;

    auto strZone = view.strings.getString(zone.strIdxZone);
    auto strFile = view.strings.getString(zone.strIdxFile);
    auto strThread = view.strings.getThreadName(thread.id);

    ImGui::BeginTooltip();
  
        ImGui::TextUnformatted(strZone.begin(), strZone.end());
        ImGui::Separator();
        ImGui::Text("%s:%i", strFile.data(), zone.lineNo);
        TextFocused("Thread:", strThread.begin(), strThread.end());
        ImGui::SameLine();
        ImGui::TextDisabled("(0x%" PRIX32 ")", thread.id);
        ImGui::Separator();
        TextFocused("Execution time:", TimeToString(strBuf, time));
        ImGui::SameLine();
        TextFocusedFmt("Cycles:", "%" PRId64, cycles);
        TextFocused("Self time:", TimeToString(strBuf, selftime));
        ImGui::SameLine();
        ImGui::TextDisabled("(%.2f%%)", 100.f * selftime / time);
    
    ImGui::EndTooltip();
}


// Draws like a overview of all the frames, so easy to find peaks.
void DrawFrames(TraceView& view)
{
    X_UNUSED(view);

}

bool DrawZoneFramesHeader(TraceView& view)
{
    const auto wpos = ImGui::GetCursorScreenPos();
    const auto w = ImGui::GetWindowContentRegionWidth() - ImGui::GetStyle().ScrollbarSize;
    auto* draw = ImGui::GetWindowDrawList();
    const auto ty = ImGui::GetFontSize();

    ImGui::InvisibleButton("##zoneFrames", ImVec2(w, ty * 1.5));
    bool hover = ImGui::IsItemHovered();

    auto timespan = view.GetVisiableNS();
    auto pxns = w / double(timespan);


    if (hover) {
       HandleZoneViewMouse(view, timespan, wpos, w, pxns);
    }

    {
        const auto nspx = 1.0 / pxns;
        const auto scale = std::max(0.0, math<double>::round(log10(nspx) + 2));
        const auto step = pow(10, scale);

        const auto dx = step * pxns;
        double x = 0;
        int32_t tw = 0;
        int32_t tx = 0;
        int64_t tt = 0;

        StringBuf strBuf;

        while (x < w)
        {
            draw->AddLine(wpos + ImVec2(x, 0), wpos + ImVec2(x, math<double>::round(ty * 0.5)), 0x66FFFFFF);
            if (tw == 0)
            {
                const auto t = view.GetVisibleStartNS();
                TimeToString(strBuf, t);

                auto col = 0x66FFFFFF;

                // TODO: add some animation to this text so it gets brighter when value is changing.

                if (t >= 0) // prefix the shit.
                {
                    StringBuf strBuf1;
                    strBuf1.setFmt("+%s", strBuf.c_str());
                    strBuf = strBuf1;
                    col = 0xaaFFFFFF;
                }
                else
                {
                    col = 0xaa2020FF;
                }

                draw->AddText(wpos + ImVec2(x, math<double>::round(ty * 0.5)), col, strBuf.begin(), strBuf.end());
                tw = ImGui::CalcTextSize(strBuf.begin(), strBuf.end()).x;
            }
            else if (x > tx + tw + ty * 2)
            {
                tx = x;
                TimeToString(strBuf, tt);
                draw->AddText(wpos + ImVec2(x, math<double>::round(ty * 0.5)), 0x66FFFFFF, strBuf.begin(), strBuf.end());
                tw = ImGui::CalcTextSize(strBuf.begin(), strBuf.end()).x;
            }

            if (scale != 0)
            {
                for (int32_t i = 1; i < 5; i++)
                {
                    draw->AddLine(wpos + ImVec2(x + i * dx / 10, 0), wpos + ImVec2(x + i * dx / 10, round(ty * 0.25)), 0x33FFFFFF);
                }

                draw->AddLine(wpos + ImVec2(x + 5 * dx / 10, 0), wpos + ImVec2(x + 5 * dx / 10, round(ty * 0.375)), 0x33FFFFFF);

                for (int32_t i = 6; i < 10; i++)
                {
                    draw->AddLine(wpos + ImVec2(x + i * dx / 10, 0), wpos + ImVec2(x + i * dx / 10, round(ty * 0.25)), 0x33FFFFFF);
                }
            }

            x += dx;
            tt += step;
        }
    }

    return hover;
}

bool DrawZoneFrames(TraceView& view)
{
    const auto wpos = ImGui::GetCursorScreenPos();
    const auto w = ImGui::GetWindowContentRegionWidth() - ImGui::GetStyle().ScrollbarSize;
    const auto wh = ImGui::GetContentRegionAvail().y;
    const auto ty = ImGui::GetFontSize();
    auto draw = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("##zoneFrames", ImVec2(w, ty));
    bool hover = ImGui::IsItemHovered();

    auto timespan = view.GetVisiableNS();
    auto pxns = w / double(timespan);

    if (hover)
    {
        HandleZoneViewMouse(view, timespan, wpos, w, pxns);
    }

    const auto nspx = 1.0 / pxns;

    int64_t prev = -1;
    int64_t prevEnd = -1;
    int64_t endPos = -1;

    StringBuf strBuf;
    FrameTextStrBuf frameStrBuf;

    bool tooltipDisplayed = false;
    const bool activeFrameSet = true; // I don't support multiple sets for comparing etc.
    const bool continuous = true; // TODO: ?

    const auto inactiveColor = GetColorMuted(0x888888, activeFrameSet);
    const auto activeColor = GetColorMuted(0xFFFFFF, activeFrameSet);
    const auto redColor = GetColorMuted(0x4444FF, activeFrameSet);

    X_DISABLE_WARNING(4127) // conditional expression is constant

    // draw the ticks / frames.
    if (view.segments.isNotEmpty())
    {
        auto& segment = view.segments.front();

        // the ticks should be sorted.
        auto& ticks = segment.ticks;

        auto it = std::lower_bound(ticks.begin(), ticks.end(), view.GetVisibleStartNS(), [](const TickData& tick, int64_t val) {
            return tick.endNano < val;
        });

        if (it != ticks.end())
        {
            // draw them?
            // 1000000
            // 246125553
            while (it != ticks.end() && it->startNano < view.zvEndNS_)
            {
                auto& tick = *it;
                ++it;

                const auto ftime = tick.endNano - tick.startNano;
                const auto fbegin = tick.startNano;
                const auto fend = tick.endNano;
                const auto fsz = pxns * ftime;

                auto offset = std::distance(ticks.begin(), it);

                {
                    if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2((fbegin - view.zvStartNS_) * pxns, 0), wpos + ImVec2((fend - view.zvStartNS_) * pxns, ty)))
                    {
                        tooltipDisplayed = true;


                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(GetFrameText(frameStrBuf, ftime, offset));
                        ImGui::Separator();
                        TextFocused("Duration:", TimeToString(strBuf, ftime));
                        TextFocused("Offset:", TimeToString(strBuf, fbegin));
                        ImGui::EndTooltip();

                        if (ImGui::IsMouseClicked(2))
                        {
                            ZoomToRange(view, fbegin, fend);
                        }
                    }

                    if (fsz < MinFrameSize)
                    {
                        if (!continuous && prev != -1)
                        {
                            if ((fbegin - prevEnd) * pxns >= MinFrameSize)
                            {
                                DrawZigZag(draw, wpos + ImVec2(0, round(ty / 2)), (prev - view.zvStartNS_) * pxns, (prevEnd - view.zvStartNS_) * pxns, ty / 4, inactiveColor);
                                prev = -1;
                            }
                            else
                            {
                                prevEnd = std::max<int64_t>(fend, fbegin + MinFrameSize * nspx);
                            }
                        }

                        if (prev == -1)
                        {
                            prev = fbegin;
                            prevEnd = std::max<int64_t>(fend, fbegin + MinFrameSize * nspx);
                        }

                        continue;
                    }

                    if (prev != -1)
                    {
                        if (continuous)
                        {
                            DrawZigZag(draw, wpos + ImVec2(0, round(ty / 2)), (prev - view.zvStartNS_) * pxns, (fbegin - view.zvStartNS_) * pxns, ty / 4, inactiveColor);
                        }
                        else
                        {
                            DrawZigZag(draw, wpos + ImVec2(0, round(ty / 2)), (prev - view.zvStartNS_) * pxns, (prevEnd - view.zvStartNS_) * pxns, ty / 4, inactiveColor);
                        }
                        prev = -1;
                    }

                    if (activeFrameSet)
                    {
                        if (fbegin >= view.zvStartNS_ && endPos != fbegin)
                        {
                            draw->AddLine(wpos + ImVec2((fbegin - view.zvStartNS_) * pxns, 0), wpos + ImVec2((fbegin - view.zvStartNS_) * pxns, wh), 0x22FFFFFF);
                        }
                        if (fend <= view.zvEndNS_)
                        {
                            draw->AddLine(wpos + ImVec2((fend - view.zvStartNS_) * pxns, 0), wpos + ImVec2((fend - view.zvStartNS_) * pxns, wh), 0x22FFFFFF);
                        }
                        endPos = fend;
                    }

                    auto buf = GetFrameText(frameStrBuf, ftime, offset);
                    auto tx = ImGui::CalcTextSize(frameStrBuf.begin(), frameStrBuf.end()).x;
                    uint32_t color = redColor; // (frames.name == 0 && i == 0) ? redColor : activeColor;

                    if (fsz - 5 <= tx)
                    {
                        buf = TimeToString(strBuf, ftime);
                        tx = ImGui::CalcTextSize(strBuf.begin(), strBuf.end()).x;
                    }

                    if (fbegin >= view.zvStartNS_)
                    {
                        draw->AddLine(wpos + ImVec2((fbegin - view.zvStartNS_) * pxns + 2, 1), wpos + ImVec2((fbegin - view.zvStartNS_) * pxns + 2, ty - 1), color);
                    }
                    if (fend <= view.zvEndNS_)
                    {
                        draw->AddLine(wpos + ImVec2((fend - view.zvStartNS_) * pxns - 2, 1), wpos + ImVec2((fend - view.zvStartNS_) * pxns - 2, ty - 1), color);
                    }
                    if (fsz - 5 > tx)
                    {
                        const auto part = (fsz - 5 - tx) / 2;
                        draw->AddLine(wpos + ImVec2(std::max(-10.0, (fbegin - view.zvStartNS_) * pxns + 2), round(ty / 2)), wpos + ImVec2(std::min(w + 20.0, (fbegin - view.zvStartNS_) * pxns + part), round(ty / 2)), color);
                        draw->AddText(wpos + ImVec2((fbegin - view.zvStartNS_) * pxns + 2 + part, 0), color, buf);
                        draw->AddLine(wpos + ImVec2(std::max(-10.0, (fbegin - view.zvStartNS_) * pxns + 2 + part + tx), round(ty / 2)), wpos + ImVec2(std::min(w + 20.0, (fend - view.zvStartNS_) * pxns - 2), round(ty / 2)), color);
                    }
                    else
                    {
                        draw->AddLine(wpos + ImVec2(std::max(-10.0, (fbegin - view.zvStartNS_) * pxns + 2), round(ty / 2)), wpos + ImVec2(std::min(w + 20.0, (fend - view.zvStartNS_) * pxns - 2), round(ty / 2)), color);
                    }
                }
            }
        }
    }

    return hover;
}

core::string_view ShortenNamespace(core::string_view name)
{
    return name;
}

#if 0 
uint32_t GetLockColor(int32_t lockIdx)
{
    const uint32_t lockColors[8] = {
        { 0xFFFF44FB },
        { 0xFF14FFAC },
        { 0xFF5A1EC3 },
        { 0xFF177254 },
        { 0xFF8A5027 },
        { 0xFF27508A },
        { 0xFF5A1EC3 },
        { 0xFF177254 },
    };

    return lockColors[lockIdx & 7];
}
#endif

uint32_t GetZoneColor(int32_t threadIdx, int32_t depth)
{
    // i want like thread index and depth
    const uint32_t threadColors[8][2] = {
        { 0xFF8A5027, 0xFF724220 },
        { 0xFF27508A, 0xFF204272 },
        { 0xFF5A1EC3, 0xFF4A19A1 },
        { 0xFF177254, 0xFF1C8B66 },
        { 0xFF611DA5, 0xFF962DFF },
        // TODO: set colors.
        { 0xFF27508A, 0xFF204272 },
        { 0xFF5A1EC3, 0xFF4A19A1 },
        { 0xFF177254, 0xFF1C8B66 },
    };

    return threadColors[threadIdx & 7][depth & 1];
}

uint32_t GetLockColor(int32_t threadIdx)
{
    // i want like thread index and depth
    return GetZoneColor(threadIdx, 0);
}


uint32_t GetZoneHighlight(int32_t threadIdx, int32_t depth)
{
    const auto color = GetZoneColor(threadIdx, depth);
    return 0xFF000000 |
        (std::min<int>(0xFF, (((color & 0x00FF0000) >> 16) + 25)) << 16) |
        (std::min<int>(0xFF, (((color & 0x0000FF00) >> 8) + 25)) << 8) |
        (std::min<int>(0xFF, (((color & 0x000000FF)) + 25)));
}

uint32_t GetHighlightColor(uint32_t color)
{
    return 0xFF000000 |
        (std::min<int>(0xFF, (((color & 0x00FF0000) >> 16) + 25)) << 16) |
        (std::min<int>(0xFF, (((color & 0x0000FF00) >> 8) + 25)) << 8) |
        (std::min<int>(0xFF, (((color & 0x000000FF)) + 25)));
}

uint32_t DarkenColor(uint32_t color)
{
    return 0xFF000000 |
        (std::min<int>(0xFF, (((color & 0x00FF0000) >> 16) * 2 / 3)) << 16) |
        (std::min<int>(0xFF, (((color & 0x0000FF00) >> 8) * 2 / 3)) << 8) |
        (std::min<int>(0xFF, (((color & 0x000000FF)) * 2 / 3)));
}

int64_t GetZoneEnd(const telemetry::ZoneData& ev)
{
#if 1 // currently we don't support open zones.
    return ev.endNano;
#else
    auto ptr = &ev;
    for (;;)
    {
        if (ptr->endNano >= 0) {
            return ptr->endNano;
        }
        // TODO
       // if (ptr->child < 0) {
            return ptr->startNano;
        //}

//        X_ASSERT_UNREACHABLE();
//        return 0;
    }
#endif
}

float GetZoneThickness(const telemetry::ZoneData& ev)
{
    X_UNUSED(ev);
    return 1.f;
}


int DrawLocks(TraceView& view, const LockDataMap& locks, bool hover, double pxns, const ImVec2& wpos, int _offset, float yMin, float yMax)
{
    X_UNUSED(view, locks, hover, pxns, wpos, _offset, yMin, yMax);

    const auto w = ImGui::GetWindowContentRegionWidth() - 1;
    const auto h = std::max<float>(view.zvHeight_, ImGui::GetContentRegionAvail().y - 4); // magic border value
    auto draw = ImGui::GetWindowDrawList();

    const auto ty = ImGui::GetFontSize();
    const auto border = 4;
    const auto ostep = ty + border;
    const auto to = 9.f;
    const auto th = (ty - to) * sqrt(3) * 0.5;

    StringBuf strBuf;
    core::StackString256 str;

    int cnt = 0;

    // have the LockStates then lockTry

    // need to fix overlapping try's either server side or viewer side.
    // so need to fix this lock displaying issue.
    // so there is a new angle to this.
    // shared locks, so now it's possible for multiple threads to hold a lock.
    // which means we need to keep track of lock count.
    // which would also solve the overlapping lock states for none shared.
    const bool expanded = true;

    if (expanded)
    {
        {
            const auto offset = _offset + ostep * cnt;
            const auto labelColor = (expanded ? 0xFFFFFFFF : 0xFF888888);

            const auto txt = "Lock Holds"_sv;
            const auto txtsz = ImGui::CalcTextSize(txt.begin(), txt.end());

            if (expanded)
            {
                draw->AddTriangleFilled(wpos + ImVec2(to / 2, offset + to / 2), wpos + ImVec2(ty - to / 2, offset + to / 2), wpos + ImVec2(ty * 0.5, offset + to / 2 + th), labelColor);
            }
            else
            {
                draw->AddTriangle(wpos + ImVec2(to / 2, offset + to / 2), wpos + ImVec2(to / 2, offset + ty - to / 2), wpos + ImVec2(to / 2 + th, offset + ty * 0.5), labelColor, 2.0f);
            }

            draw->AddLine(wpos + ImVec2(0, offset + ostep - 1), wpos + ImVec2(w, offset + ostep - 1), 0x33FFFFFF);
            draw->AddRectFilled(wpos + ImVec2(0, offset), wpos + ImVec2(w, offset + ostep), 0xa0a08f30);
            draw->AddText(wpos + ImVec2(ty, offset), labelColor, txt.begin(), txt.end());

            cnt++;
        }

        for (auto& lockIt : locks) {
            auto& lockHandle = lockIt.first;
            auto& lockData = lockIt.second;

            const auto offset = _offset + ostep * cnt;
            const auto yPos = wpos.y + offset;

            draw->AddRectFilled(wpos + ImVec2(0, offset + ty), wpos + ImVec2(w, offset + ostep), 0x33888888);

            if (yPos + ostep >= yMin && yPos <= yMax) {
                auto& ls = lockData.lockStates;

                auto vbegin = std::lower_bound(ls.begin(), ls.end(), view.zvStartNS_, [](const auto& l, const auto& r) { return l.timeNano < r; });
                auto vend = std::lower_bound(vbegin, ls.end(), view.zvEndNS_, [](const auto& l, const auto& r) { return l.timeNano < r; });

                const auto lockName = view.strings.getLockName(lockHandle);
                double pxend = 0;


                if (vbegin < vend)
                {
                    auto vbeginOrig = vbegin;
                    auto vendOrig = vend;

                    if (vbegin->state != TtLockStateLocked) {
                        // find the first lock state?
                        while (vbegin < vend && vbegin->state != TtLockStateLocked) {
                            ++vbegin;
                        }
                    }

                    // we can end up in the middle of nested locking
                    // so we need to go back
                    {
                        auto threadID = vbegin->threadID;

                        while (vbegin > ls.begin() && vbegin[-1].state == TtLockStateLocked && vbegin[-1].threadID == threadID) {
                            --vbegin;
                        }
                    }

                    // make sure end is released.
                    {
                        while (vend < ls.end() && vend->state != TtLockStateReleased) {
                            ++vend;
                        }
                    }


                    if (vbegin < vend) {
                        X_ASSERT((*vbegin).state == TtLockStateLocked, "Incorrect state")();
                    }

                    X_UNUSED(vbeginOrig, vendOrig);

                    int32_t processed = 0;

                    while (vbegin < vend) 
                    {
                        X_ASSERT(vbegin->state == TtLockStateLocked, "Incorrect states")();

                        ++processed;
                        int32_t depth = 1;

                        auto lockRelease = vbegin;
                        {
                            auto threadID = lockRelease->threadID;

                            ++lockRelease;

                            // look for recursion.
                            while (lockRelease < vend && lockRelease->state == TtLockStateLocked && lockRelease->threadID == threadID) {
                                ++depth;
                                ++lockRelease;
                            }

                            // now we find where this thread released.
                            // need to do it for depth.
                            while (lockRelease < vend) {
                                if (lockRelease->state == TtLockStateReleased && lockRelease->threadID == threadID) {
                                    --depth;
                                    if (depth == 0) {
                                        break;
                                    }
                                }

                                ++lockRelease;
                            }
                        }

                        
                        if (lockRelease == vend) {
                            break;
                        }

                        auto& lockState = (*vbegin);
                        auto& lockStateRelease = *(lockRelease);


                        X_ASSERT(lockState.state == TtLockStateLocked && lockStateRelease.state == TtLockStateReleased, "Incorrect states")();
                        const auto locksz = std::max((lockStateRelease.timeNano - lockState.timeNano) * pxns, pxns * 0.5);

                        if (locksz < MinVisSize)
                        {
                            // so we need to just go over zones.
                            // what about color tho?
                            // they can be for multiple threads.
                            // dunno lol



                        }
                        else
                        {
                            const auto t0 = lockState.timeNano;
                            const auto t1 = lockStateRelease.timeNano;
                            const auto px0 = std::max(pxend, (t0 - view.zvStartNS_) * pxns);
                            double px1 = (t1 - view.zvStartNS_) * pxns;

                            pxend = std::max({ px1, px0 + MinVisSize, px0 + pxns * 0.5 });

                            constexpr auto sperator = " <HOLDING> "_sv;
                            auto threadName = view.strings.getThreadName(lockState.threadID);

                            str.set(threadName.begin(), threadName.end());
                            str.append(sperator.begin(), sperator.end());
                            str.append(lockName.begin(), lockName.end());

                            auto text = core::string_view(str);
                            auto tsz = ImGui::CalcTextSize(text.begin(), text.end());

                            auto col = GetLockColor(lockState.threadIdx);
                            draw->AddRectFilled(wpos + ImVec2(std::max(px0, -10.0), offset), wpos + ImVec2(std::min(pxend, double(w + 10)), offset + ty), col);
                            draw->AddRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset), GetHighlightColor(col), 0.f, -1, 1.f);

                            if (tsz.x < locksz)
                            {
                                const auto x = (t0 - view.zvStartNS_) * pxns + ((t1 - t0) * pxns - tsz.x) / 2;
                                if (x < 0 || x > w - tsz.x)
                                {
                                    ImGui::PushClipRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y * 2), true);
                                    DrawTextContrast(draw, wpos + ImVec2(std::max(std::max(0., px0), std::min(double(w - tsz.x), x)), offset), 0xFFFFFFFF, text);
                                    ImGui::PopClipRect();
                                }
                                else if (t0 == t1)
                                {
                                    DrawTextContrast(draw, wpos + ImVec2(px0 + (px1 - px0 - tsz.x) * 0.5, offset), 0xFFFFFFFF, text);
                                }
                                else
                                {
                                    DrawTextContrast(draw, wpos + ImVec2(x, offset), 0xFFFFFFFF, text);
                                }
                            }
                            else
                            {
                                ImGui::PushClipRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y * 2), true);
                                DrawTextContrast(draw, wpos + ImVec2((t0 - view.zvStartNS_) * pxns, offset), 0xFFFFFFFF, text);
                                ImGui::PopClipRect();
                            }

                            if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y)))
                            {
                                LockStateTooltip(view, lockHandle, lockState, lockStateRelease);
                            }
                        }


                        auto num = std::distance(vbegin, lockRelease) + 1;

                        if (num % 2 == 0) {
                            vbegin += num;

                            if (vbegin < vend) {
                                X_ASSERT(vbegin->state == TtLockStateLocked, "Incorrect states")();
                            }
                        }
                        else {

                            // TODO: 
                            auto threadID = vbegin->threadID;

                            ++vbegin;

                            while (vbegin < vend && vbegin->threadID == threadID) {
                                ++vbegin;
                            }

                            X_ASSERT(vbegin->state == TtLockStateLocked, "Begin should be a lock state")();
                        }
                    }
                }
#if 0
                // lock name.
                auto lockName = view.strings.getLockName(lockHandle);
                draw->AddText(wpos + ImVec2(ty, offset), 0xFF79A379, lockName.begin(), lockName.end());
#endif

                cnt++;
            }
        }
    }

    if (expanded)
    {
        {
            const auto offset = _offset + ostep * cnt;
            const auto labelColor = (expanded ? 0xFFFFFFFF : 0xFF888888);

            const auto txt = "Lock Try"_sv;
            const auto txtsz = ImGui::CalcTextSize(txt.begin(), txt.end());

            if (expanded)
            {
                draw->AddTriangleFilled(wpos + ImVec2(to / 2, offset + to / 2), wpos + ImVec2(ty - to / 2, offset + to / 2), wpos + ImVec2(ty * 0.5, offset + to / 2 + th), labelColor);
            }
            else
            {
                draw->AddTriangle(wpos + ImVec2(to / 2, offset + to / 2), wpos + ImVec2(to / 2, offset + ty - to / 2), wpos + ImVec2(to / 2 + th, offset + ty * 0.5), labelColor, 2.0f);
            }
            
            draw->AddLine(wpos + ImVec2(0, offset + ostep - 1), wpos + ImVec2(w, offset + ostep - 1), 0x33FFFFFF);
            draw->AddRectFilled(wpos + ImVec2(0, offset), wpos + ImVec2(w, offset + ostep), 0xa0a08f30);
            draw->AddText(wpos + ImVec2(ty, offset), labelColor, txt.begin(), txt.end());

            cnt++;
        }

        for (auto& lockIt : locks) {
//            auto& lockHandle = lockIt.first;
            auto& lockData = lockIt.second;

            const auto offset = _offset + ostep * cnt;
            const auto yPos = wpos.y + offset;

            if (yPos + ostep >= yMin && yPos <= yMax) {
                auto& lt = lockData.lockTry;

                auto it = std::lower_bound(lt.begin(), lt.end(), view.zvStartNS_, [](const auto& l, const auto& r) { return l.endNano < r; });
                const auto itend = std::lower_bound(it, lt.end(), view.zvEndNS_, [](const auto& l, const auto& r) { return l.startNano < r; });

                draw->AddRectFilled(wpos + ImVec2(0, offset + ty), wpos + ImVec2(w, offset + ostep), 0x33888888);

                double pxend = 0;

                while (it < itend)
                {
                    auto& lockTry = (*it);
                    
                    const auto locksz = std::max((lockTry.endNano - lockTry.startNano) * pxns, pxns * 0.5);

                    if (locksz < MinVisSize)
                    {
                        int num = 1;
                        const auto px0 = (lockTry.startNano - view.zvStartNS_) * pxns;
                        auto px1 = (lockTry.endNano - view.zvStartNS_) * pxns;
                        auto rend = lockTry.endNano;
                        for (;;)
                        {
                            ++it;
                            if (it == itend) {
                                break;
                            }

                            const auto nend = it->endNano;
                            const auto pxnext = (nend - view.zvStartNS_) * pxns;
                            if (pxnext - px1 >= MinVisSize * 2) {
                                break;
                            }

                            px1 = pxnext;
                            rend = nend;
                            num++;
                        }

                        // need to make this work a bit better when zones get compressed also.
                        auto color = GetLockColor(lockTry.threadIdx);
                        auto colorDark = DarkenColor(color);

                        draw->AddRectFilled(
                            wpos + ImVec2(std::max(px0, -10.0), offset),
                            wpos + ImVec2(std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), offset + ty),
                            color);

                        DrawZigZag(
                            draw,
                            wpos + ImVec2(0, offset + ty / 2), std::max(px0, -10.0),
                            std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), ty / 4,
                            colorDark);

                        if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2(std::max(px0, -10.0), offset), wpos + ImVec2(std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), offset + ty)))
                        {
                            if (num > 1)
                            {
                                ImGui::BeginTooltip();
                                TextFocused("Lock waits too small to display:", IntToString(strBuf, num));
                                ImGui::Separator();
                                TextFocused("Total time:", TimeToString(strBuf, rend - lockTry.startNano));
                                ImGui::EndTooltip();

                                if (ImGui::IsMouseClicked(2) && rend - lockTry.startNano > 0)
                                {
                                    ZoomToRange(view, lockTry.startNano, rend);
                                }
                            }
                            else
                            {
                                LockTryTooltip(view, lockTry);

                                if (ImGui::IsMouseClicked(2) && rend - lockTry.startNano > 0)
                                {
                                    ZoomToLockTry(view, lockTry);
                                }
                            }
                        }

                        IntToString(strBuf, num);
                        const auto tsz = ImGui::CalcTextSize(strBuf.begin(), strBuf.end());
                        if (tsz.x < px1 - px0)
                        {
                            const auto x = px0 + (px1 - px0 - tsz.x) / 2;
                            DrawTextContrast(draw, wpos + ImVec2(x, offset), 0xFF4488DD, strBuf.c_str());
                        }
                    }
                    else
                    {
                        const auto t0 = lockTry.startNano;
                        const auto t1 = lockTry.endNano;
                        const auto px0 = std::max(pxend, (t0 - view.zvStartNS_) * pxns);
                        double px1 = (t1 - view.zvStartNS_) * pxns;

                        pxend = std::max({ px1, px0 + MinVisSize, px0 + pxns * 0.5 });


                        constexpr auto prefix = "<Waiting> "_sv;
                        auto lockName = view.strings.getLockName(lockTry.lockHandle);

                        str.set(prefix.begin(), prefix.end());
                        str.append(lockName.begin(), lockName.end());

                        auto text = core::string_view(str);
                        auto tsz = ImGui::CalcTextSize(text.begin(), text.end());

                        auto color = GetLockColor(lockTry.threadIdx);
                        draw->AddRectFilled(wpos + ImVec2(std::max(px0, -10.0), offset), wpos + ImVec2(std::min(pxend, double(w + 10)), offset + ty), color);

                        if (tsz.x < locksz)
                        {
                            const auto x = (lockTry.startNano - view.zvStartNS_) * pxns + ((lockTry.endNano - lockTry.startNano) * pxns - tsz.x) / 2;
                            if (x < 0 || x > w - tsz.x)
                            {
                                ImGui::PushClipRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y * 2), true);
                                DrawTextContrast(draw, wpos + ImVec2(std::max(std::max(0., px0), std::min(double(w - tsz.x), x)), offset), 0xFFFFFFFF, text);
                                ImGui::PopClipRect();
                            }
                            else if (lockTry.startNano == lockTry.endNano)
                            {
                                DrawTextContrast(draw, wpos + ImVec2(px0 + (px1 - px0 - tsz.x) * 0.5, offset), 0xFFFFFFFF, text);
                            }
                            else
                            {
                                DrawTextContrast(draw, wpos + ImVec2(x, offset), 0xFFFFFFFF, text);
                            }
                        }
                        else
                        {
                            ImGui::PushClipRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y * 2), true);
                            DrawTextContrast(draw, wpos + ImVec2((lockTry.startNano - view.zvStartNS_) * pxns, offset), 0xFFFFFFFF, text);
                            ImGui::PopClipRect();
                        }

                        if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y)))
                        {
                            LockTryTooltip(view, lockTry);

                            if (!view.zoomAnim_.active && ImGui::IsMouseClicked(2))
                            {
                                ZoomToLockTry(view, lockTry);
                            }
                        }
                    }

                    ++it;
                }

#if 0
                // lock name.
                auto lockName = view.strings.getLockName(lockHandle);
                draw->AddText(wpos + ImVec2(ty, offset), 0xFF79A379, lockName.begin(), lockName.end());
#endif

                cnt++;
            }
        }
    }

    return cnt;
}

void DrawZoneLevelWaits(TraceView& view, ZoneSegmentThread& thread, int32_t threadIdx,
    bool hover, double pxns, const ImVec2& wpos,
    int _offset, int depth, float yMin, float yMax)
{
    X_UNUSED(yMin, yMax, hover, threadIdx);

    auto& level = thread.levels[depth];
    auto& lockTryVec = level.lockTry;

    auto it = std::lower_bound(lockTryVec.begin(), lockTryVec.end(), view.zvStartNS_, [](const auto& l, const auto& r) { return l.endNano < r; });
    if (it == lockTryVec.end()) {
        return;
    }

    const auto itend = std::lower_bound(it, lockTryVec.end(), view.zvEndNS_, [](const auto& l, const auto& r) { return l.startNano < r; });
    if (it == itend) {
        return;
    }

    auto draw = ImGui::GetWindowDrawList();
    const auto w = ImGui::GetWindowContentRegionWidth() - 1;
    const auto ty = ImGui::GetFontSize();
    const auto ostep = ty + 1;
    const auto offset = _offset + ostep * depth;

    StringBuf strBuf;

    auto color = 0xFF0000FF;
    const auto colorDark = DarkenColor(color);

    while (it < itend)
    {
        auto& lockTry = *it;

        const auto locksz = std::max((lockTry.endNano - lockTry.startNano) * pxns, pxns * 0.5);

        if (locksz < MinVisSize)
        {
            int num = 1;
            const auto px0 = (lockTry.startNano - view.zvStartNS_) * pxns;
            auto px1 = (lockTry.endNano - view.zvStartNS_) * pxns;
            auto rend = lockTry.endNano;
            for (;;)
            {
                ++it;
                if (it == itend) {
                    break;
                }

                const auto nend = it->endNano;
                const auto pxnext = (nend - view.zvStartNS_) * pxns;
                if (pxnext - px1 >= MinVisSize * 2) {
                    break;
                }

                px1 = pxnext;
                rend = nend;
                num++;
            }

            // need to make this work a bit better when zones get compressed also.


            draw->AddRectFilled(
                wpos + ImVec2(std::max(px0, -10.0), offset),
                wpos + ImVec2(std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), offset + ty),
                color);

            DrawZigZag(
                draw,
                wpos + ImVec2(0, offset + ty / 2), std::max(px0, -10.0),
                std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), ty / 4,
                colorDark);
#if 0


            if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2(std::max(px0, -10.0), offset), wpos + ImVec2(std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), offset + ty)))
            {
                if (num > 1)
                {
                    ImGui::BeginTooltip();
                    TextFocused("Lock wait too small to display:", IntToString(strBuf, num));
                    ImGui::Separator();
                    TextFocused("Execution time:", TimeToString(strBuf, rend - lock.startNano));
                    ImGui::EndTooltip();

                    if (ImGui::IsMouseClicked(2) && rend - lock.startNano > 0)
                    {
                        ZoomToRange(view, lock.startNano, rend);
                    }
                }
                else
                {
                    LockTryTooltip(view, thread, lock);

                    if (ImGui::IsMouseClicked(2) && rend - lock.startNano > 0)
                    {
                        ZoomToLockTry(view, lock);
                    }
                }
            }

            IntToString(strBuf, num);
            const auto tsz = ImGui::CalcTextSize(strBuf.begin(), strBuf.end());
            if (tsz.x < px1 - px0)
            {
                const auto x = px0 + (px1 - px0 - tsz.x) / 2;
                DrawTextContrast(draw, wpos + ImVec2(x, offset), 0xFF4488DD, strBuf.c_str());
            }
#endif
        }
        else
        {
            const auto pr0 = (lockTry.startNano - view.zvStartNS_) * pxns;
            const auto pr1 = (lockTry.endNano - view.zvStartNS_) * pxns;
            const auto px0 = std::max(pr0, -10.0);
            const auto px1 = std::max({ std::min(pr1, double(w + 10)), px0 + pxns * 0.5, px0 + MinVisSize });

            // get locks name?
            constexpr auto prefix = "<Waiting> "_sv;
            auto lockName = view.strings.getLockName(it->lockHandle);

            core::StackString256 str;
            str.append(prefix.begin(), prefix.end());
            str.append(lockName.begin(), lockName.end());

            auto text = core::string_view(str);
            auto tsz = ImGui::CalcTextSize(text.begin(), text.end());

            draw->AddRectFilled(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y), color);
            draw->AddRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y), GetHighlightColor(color), 0.f, -1, 1.f);

            if (tsz.x < locksz)
            {
                const auto x = (lockTry.startNano - view.zvStartNS_) * pxns + ((lockTry.endNano - lockTry.startNano) * pxns - tsz.x) / 2;
                if (x < 0 || x > w - tsz.x)
                {
                    ImGui::PushClipRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y * 2), true);
                    DrawTextContrast(draw, wpos + ImVec2(std::max(std::max(0., px0), std::min(double(w - tsz.x), x)), offset), 0xFFFFFFFF, text);
                    ImGui::PopClipRect();
                }
                else if (lockTry.startNano == lockTry.endNano)
                {
                    DrawTextContrast(draw, wpos + ImVec2(px0 + (px1 - px0 - tsz.x) * 0.5, offset), 0xFFFFFFFF, text);
                }
                else
                {
                    DrawTextContrast(draw, wpos + ImVec2(x, offset), 0xFFFFFFFF, text);
                }
            }
            else
            {
                ImGui::PushClipRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y * 2), true);
                DrawTextContrast(draw, wpos + ImVec2((lockTry.startNano - view.zvStartNS_) * pxns, offset), 0xFFFFFFFF, text);
                ImGui::PopClipRect();
            }

            if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y)))
            {
                LockTryTooltip(view, lockTry);

                if (!view.zoomAnim_.active && ImGui::IsMouseClicked(2))
                {
                    ZoomToLockTry(view, lockTry);
                }

#if 0
                // when you hover over a waiting section.
                // i want to highlights who has locks during that time.
                // so basically finding all lockHolds during this waits timespan.

                auto lockIt = thread.locks.find(lock.lockHandle);
                if (lockIt != thread.locks.end()) {

                    auto& lockData = lockIt->second;
                    auto& states = lockData.lockStates;

                    // MEOWWWWWWWWWWWW
                    auto statesIt = std::lower_bound(states.begin(), states.end(), lock.startNano, [](const auto& l, const auto& r) { return l.timeNano < r; });
                    if (statesIt == states.end()) {
                        return;
                    }

                    const auto statesItend = std::lower_bound(statesIt, states.end(), lock.endNano, [](const auto& l, const auto& r) { return l.timeNano < r; });
                    if (statesIt == statesItend) {
                        return;
                    }

                    if (statesIt != statesItend)
                    {
                        // so only bit about this that's annoying is i need to calculate locked regions.
                        // but once done that should just draw some shit over the threads.
                        // means i need to know where threads end and start, which i don't here.
                        // since there are more threads to draw.
                        // so this needs to be something done last.
                        statesIt->threadIdx;


                    }
                }
#endif

            }
        }

        ++it;
    }

}

void DrawZoneLevel(TraceView& view, ZoneSegmentThread& thread, int32_t threadIdx, 
    bool hover, double pxns, const ImVec2& wpos,
    int _offset, int depth, float yMin, float yMax)
{

    X_UNUSED(yMin, yMax, hover);

    // const auto delay = m_worker.GetDelay();
    // const auto resolution = m_worker.GetResolution();

    auto& level = thread.levels[depth];
    auto& zones = level.zones;

    // find the last zone that ends before view.
    auto it = std::lower_bound(zones.begin(), zones.end(), view.zvStartNS_, [](const auto& l, const auto& r) { return l.endNano < r; });
    if (it == zones.end()) {
        return;
    }

    // find the first zone that starts after view.
    const auto zitend = std::lower_bound(it, zones.end(), view.zvEndNS_, [](const auto& l, const auto& r) { return l.startNano < r; });
    if (it == zitend) {
        return;
    }

    
#if false
    if ((*it)->end < 0 && m_worker.GetZoneEnd(**it) < view.zvStartNS_) {
        return depth;
    }
#endif

    const auto w = ImGui::GetWindowContentRegionWidth() - 1;
    const auto ty = ImGui::GetFontSize();
    const auto ostep = ty + 1;
    const auto offset = _offset + ostep * depth;
    auto draw = ImGui::GetWindowDrawList();
    const auto dsz = pxns;
    const auto rsz = pxns;

    StringBuf strBuf;

    const auto color = GetZoneColor(threadIdx, depth);
    const auto colorDark = DarkenColor(color);

    while (it < zitend)
    {
        auto& zone = *it;
        
        // think i want to just do zone color based on thread and depth.
        const auto end = GetZoneEnd(zone);
        const auto zsz = std::max((end - zone.startNano) * pxns, pxns * 0.5);

        if (zsz < MinVisSize)
        {
            int num = 1;
            const auto px0 = (zone.startNano - view.zvStartNS_) * pxns;
            auto px1 = (end - view.zvStartNS_) * pxns;
            auto rend = end;
            for (;;)
            {
                ++it;
                if (it == zitend) {
                    break;
                }

                const auto nend = GetZoneEnd(*it);
                const auto pxnext = (nend - view.zvStartNS_) * pxns;
                if (pxnext - px1 >= MinVisSize * 2) {
                    break;
                }

                px1 = pxnext;
                rend = nend;
                num++;
            }
            
            draw->AddRectFilled(
                wpos + ImVec2(std::max(px0, -10.0), offset), 
                wpos + ImVec2(std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), offset + ty), 
                color);

            DrawZigZag(
                draw, 
                wpos + ImVec2(0, offset + ty / 2), std::max(px0, -10.0), 
                std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), ty / 4, 
                colorDark);
     
            if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2(std::max(px0, -10.0), offset), wpos + ImVec2(std::min(std::max(px1, px0 + MinVisSize), double(w + 10)), offset + ty)))
            {
                if (num > 1)
                {
                    ImGui::BeginTooltip();
                    TextFocused("Zones too small to display:", IntToString(strBuf, num));
                    ImGui::Separator();
                    TextFocused("Execution time:", TimeToString(strBuf, rend - zone.startNano));
                    ImGui::EndTooltip();

                    if (ImGui::IsMouseClicked(2) && rend - zone.startNano > 0)
                    {
                        ZoomToRange(view, zone.startNano, rend);
                    }
                }
                else
                {
                    ZoneTooltip(view, thread, zone);

                    if (ImGui::IsMouseClicked(2) && rend - zone.startNano > 0)
                    {
                        ZoomToZone(view, zone);
                    }
                    if (ImGui::IsMouseClicked(0))
                    {
                    //    ShowZoneInfo(zone);
                    }
                }
            }

            IntToString(strBuf, num);
            const auto tsz = ImGui::CalcTextSize(strBuf.begin(), strBuf.end());
            if (tsz.x < px1 - px0)
            {
                const auto x = px0 + (px1 - px0 - tsz.x) / 2;
                DrawTextContrast(draw, wpos + ImVec2(x, offset), 0xFF4488DD, strBuf.c_str());
            }
        }
        else
        {
            auto zoneName = view.strings.getString(zone.strIdxZone);
            // TODO: 
            int dmul = 1; // zone.text.active ? 2 : 1;


            auto tsz = ImGui::CalcTextSize(zoneName.begin(), zoneName.end());
            if (tsz.x > zsz)
            {
                zoneName = ShortenNamespace(zoneName);
                tsz = ImGui::CalcTextSize(zoneName.begin(), zoneName.end());
            }

            const auto pr0 = (zone.startNano - view.zvStartNS_) * pxns;
            const auto pr1 = (end - view.zvStartNS_) * pxns;
            const auto px0 = std::max(pr0, -10.0);
            const auto px1 = std::max({ std::min(pr1, double(w + 10)), px0 + pxns * 0.5, px0 + MinVisSize });
            
            draw->AddRectFilled(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y), color);
            draw->AddRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y), GetZoneHighlight(threadIdx, depth), 0.f, -1, GetZoneThickness(zone));
            
            if (dsz * dmul >= MinVisSize)
            {
                draw->AddRectFilled(wpos + ImVec2(pr0, offset), wpos + ImVec2(std::min(pr0 + dsz * dmul, pr1), offset + tsz.y), 0x882222DD);
                draw->AddRectFilled(wpos + ImVec2(pr1, offset), wpos + ImVec2(pr1 + dsz, offset + tsz.y), 0x882222DD);
            }
            if (rsz >= MinVisSize)
            {
                draw->AddLine(wpos + ImVec2(pr0 + rsz, offset + round(tsz.y / 2)), wpos + ImVec2(pr0 - rsz, offset + round(tsz.y / 2)), 0xAAFFFFFF);
                draw->AddLine(wpos + ImVec2(pr0 + rsz, offset + round(tsz.y / 4)), wpos + ImVec2(pr0 + rsz, offset + round(3 * tsz.y / 4)), 0xAAFFFFFF);
                draw->AddLine(wpos + ImVec2(pr0 - rsz, offset + round(tsz.y / 4)), wpos + ImVec2(pr0 - rsz, offset + round(3 * tsz.y / 4)), 0xAAFFFFFF);

                draw->AddLine(wpos + ImVec2(pr1 + rsz, offset + round(tsz.y / 2)), wpos + ImVec2(pr1 - rsz, offset + round(tsz.y / 2)), 0xAAFFFFFF);
                draw->AddLine(wpos + ImVec2(pr1 + rsz, offset + round(tsz.y / 4)), wpos + ImVec2(pr1 + rsz, offset + round(3 * tsz.y / 4)), 0xAAFFFFFF);
                draw->AddLine(wpos + ImVec2(pr1 - rsz, offset + round(tsz.y / 4)), wpos + ImVec2(pr1 - rsz, offset + round(3 * tsz.y / 4)), 0xAAFFFFFF);
            }

            if (tsz.x < zsz)
            {
                const auto x = (zone.startNano - view.zvStartNS_) * pxns + ((end - zone.startNano) * pxns - tsz.x) / 2;
                if (x < 0 || x > w - tsz.x)
                {
                    ImGui::PushClipRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y * 2), true);
                    DrawTextContrast(draw, wpos + ImVec2(std::max(std::max(0., px0), std::min(double(w - tsz.x), x)), offset), 0xFFFFFFFF, zoneName);
                    ImGui::PopClipRect();
                }
                else if (zone.startNano == zone.endNano)
                {
                    DrawTextContrast(draw, wpos + ImVec2(px0 + (px1 - px0 - tsz.x) * 0.5, offset), 0xFFFFFFFF, zoneName);
                }
                else
                {
                    DrawTextContrast(draw, wpos + ImVec2(x, offset), 0xFFFFFFFF, zoneName);
                }
            }
            else
            {
                ImGui::PushClipRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y * 2), true);
                DrawTextContrast(draw, wpos + ImVec2((zone.startNano - view.zvStartNS_) * pxns, offset), 0xFFFFFFFF, zoneName);
                ImGui::PopClipRect();
            }

            if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2(px0, offset), wpos + ImVec2(px1, offset + tsz.y)))
            {
                ZoneTooltip(view, thread, zone);

                if (!view.zoomAnim_.active && ImGui::IsMouseClicked(2))
                {
                    ZoomToZone(view, zone);
                }
                if (ImGui::IsMouseClicked(0))
                {
                // open window with more info, like how many goats are in the pen.
                //    ShowZoneInfo(zone);
                }
            }

            ++it;
        }
    }
}

void DispatchZoneLevel(TraceView& view, ZoneSegmentThread& thread, int32_t threadIdx,
    bool hover, double pxns, const ImVec2& wpos, 
    int _offset, int depth, float yMin, float yMax)
{
    const auto ty = ImGui::GetFontSize();
    const auto ostep = ty + 1;
    const auto offset = _offset + ostep * depth;

    const auto yPos = wpos.y + offset;
    if (yPos + ostep >= yMin && yPos <= yMax)
    {
        DrawZoneLevel(view, thread, threadIdx, hover, pxns, wpos, _offset, depth, yMin, yMax);
        DrawZoneLevelWaits(view, thread, threadIdx, hover, pxns, wpos, _offset, depth, yMin, yMax);
    }
    else
    {
        // do we need this?
        // SkipZoneLevel(view, thread, threadIdx, hover, pxns, wpos, _offset, depth, yMin, yMax);
    }
}


// This draws the timeline frame info and zones.
void DrawZones(TraceView& view)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }

    auto& io = ImGui::GetIO();


    const auto linepos = ImGui::GetCursorScreenPos();
    const auto lineh = ImGui::GetContentRegionAvail().y;

    bool drawMouseLine = DrawZoneFramesHeader(view);

    drawMouseLine |= DrawZoneFrames(view);

    ImGui::BeginChild("##zoneWin", ImVec2(ImGui::GetWindowContentRegionWidth(), ImGui::GetContentRegionAvail().y), false, ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const auto wpos = ImGui::GetCursorScreenPos();
    const auto w = ImGui::GetWindowContentRegionWidth() - 1;
    const auto h = std::max<float>(view.zvHeight_, ImGui::GetContentRegionAvail().y - 4); // magic border value
    auto draw = ImGui::GetWindowDrawList();

    if (h == 0) {
        ImGui::EndChild();
        return;
    }

    ImGui::InvisibleButton("##zones", ImVec2(w, h));
    bool hover = ImGui::IsItemHovered();

    auto timespan = view.GetVisiableNS();
    auto pxns = w / double(timespan);

    if (hover)
    {
        drawMouseLine = true;
        HandleZoneViewMouse(view, timespan, wpos, w, pxns);
    }

    const auto ty = ImGui::GetFontSize();
    const auto ostep = ty + 1;
    int offset = 0;
    const auto to = 9.f;
    const auto th = (ty - to) * sqrt(3) * 0.5;

    const auto yMin = linepos.y;
    const auto yMax = yMin + lineh;

    // Draw the threads
    if (view.segments.isNotEmpty())
    {
        auto& segment = view.segments.front();

        for (int32_t threadIdx =0; threadIdx < static_cast<int32_t>(segment.threads.size()); threadIdx++)
        {
            auto& thread = segment.threads[threadIdx];

            // TODO: save this somewhere.
            bool expanded = true;

            const auto yPos = wpos.y + offset;
            if (yPos + ostep >= yMin && yPos <= yMax)
            {
                draw->AddLine(wpos + ImVec2(0, offset + ostep - 1), wpos + ImVec2(w, offset + ostep - 1), 0x33FFFFFF);

                const auto labelColor = (expanded ? 0xFFFFFFFF : 0xFF888888);

                if (expanded)
                {
                    draw->AddTriangleFilled(wpos + ImVec2(to / 2, offset + to / 2), wpos + ImVec2(ty - to / 2, offset + to / 2), wpos + ImVec2(ty * 0.5, offset + to / 2 + th), labelColor);
                }
                else
                {
                    draw->AddTriangle(wpos + ImVec2(to / 2, offset + to / 2), wpos + ImVec2(to / 2, offset + ty - to / 2), wpos + ImVec2(to / 2 + th, offset + ty * 0.5), labelColor, 2.0f);
                }

                const auto txt = view.strings.getThreadName(thread.id);
                const auto txtsz = ImGui::CalcTextSize(txt.begin(), txt.end());

                draw->AddText(wpos + ImVec2(ty, offset), labelColor, txt.begin(), txt.end());

                if (hover && ImGui::IsMouseHoveringRect(wpos + ImVec2(0, offset), wpos + ImVec2(ty + txtsz.x, offset + ty)))
                {
                    if (ImGui::IsMouseClicked(0))
                    {
                        expanded = !expanded;
                    }

                    ImGui::BeginTooltip();
#if true
                    ImGui::TextUnformatted(txt.begin(), txt.end());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(0x%" PRIx64 ")", thread.id);
#else
                    ImGui::TextUnformatted(m_worker.GetThreadString(v->id));
                    ImGui::SameLine();
                    ImGui::TextDisabled("(0x%" PRIx64 ")", thread.id);
                 
                    if (!v->timeline.empty())
                    {
                        ImGui::Separator();
                        TextFocused("Appeared at", TimeToString(v->timeline.front()->start - m_worker.GetTimeBegin()));
                        TextFocused("Zone count:", RealToString(v->count, true));
                        TextFocused("Top-level zones:", RealToString(v->timeline.size(), true));
                    }
#endif
                    ImGui::EndTooltip();
                }
            }

            offset += ostep;

            if (expanded)
            {
                // if (m_drawZones)
                {
                    auto depth = static_cast<int32_t>(thread.levels.size());

                    for (int32_t stackDepth = 0; stackDepth < depth; stackDepth++)
                    {
                        DispatchZoneLevel(view, thread, threadIdx, hover, pxns, wpos, offset, stackDepth, yMin, yMax);
                    }

                    offset += ostep * (depth + 1);
                }

            }
            offset += ostep * 0.2f;
        }

        // Draw the lock shit here.
        if (segment.locks.isNotEmpty())
        {
            const auto depth = DrawLocks(view, segment.locks, hover, pxns, wpos, offset, yMin, yMax);
            offset += ostep * depth;
        }
    }

    const auto scrollPos = ImGui::GetScrollY();
    if (scrollPos == 0 && view.zvScroll_ != 0)
    {
        view.zvHeight_ = 0;
    }
    else
    {
        if (offset > view.zvHeight_) {
            view.zvHeight_ = offset;
        }
    }

    view.zvScroll_ = scrollPos;


    ImGui::EndChild();

    if (view.highlight_.active && view.highlight_.start != view.highlight_.end)
    {
        const auto s = std::min(view.highlight_.start, view.highlight_.end);
        const auto e = std::max(view.highlight_.start, view.highlight_.end);
        draw->AddRectFilled(
            ImVec2(wpos.x + (s - view.zvStartNS_) * pxns, linepos.y), 
            ImVec2(wpos.x + (e - view.zvStartNS_) * pxns, linepos.y + lineh),
            0x228888DD);
        draw->AddRect(
            ImVec2(wpos.x + (s - view.zvStartNS_) * pxns, linepos.y), 
            ImVec2(wpos.x + (e - view.zvStartNS_) * pxns, linepos.y + lineh), 
            0x448888DD);

        StringBuf strBuf;
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(TimeToString(strBuf, e - s));
        ImGui::EndTooltip();
    }
    else if (drawMouseLine)
    {
        draw->AddLine(ImVec2(io.MousePos.x, linepos.y), ImVec2(io.MousePos.x, linepos.y + lineh), 0x33FFFFFF);
    }

    if (view.highlightZoom_.active && view.highlightZoom_.start != view.highlightZoom_.end)
    {
        const auto s = std::min(view.highlightZoom_.start, view.highlightZoom_.end);
        const auto e = std::max(view.highlightZoom_.start, view.highlightZoom_.end);
        draw->AddRectFilled(
            ImVec2(wpos.x + (s - view.zvStartNS_) * pxns, linepos.y), 
            ImVec2(wpos.x + (e - view.zvStartNS_) * pxns, linepos.y + lineh), 
            0x1688DD88);
        draw->AddRect(
            ImVec2(wpos.x + (s - view.zvStartNS_) * pxns, linepos.y), 
            ImVec2(wpos.x + (e - view.zvStartNS_) * pxns, linepos.y + lineh), 
            0x2C88DD88);
    }

}


void DrawFrame(Client& client, float ww, float wh)
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(ww, wh));
    //    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 1.f));

    static bool show_app_metrics = false;
    static bool show_app_style_editor = false;
    static bool show_app_about = false;

    if (show_app_metrics) { 
        ImGui::ShowMetricsWindow(&show_app_metrics);
    }
    if (show_app_style_editor) { 
        ImGui::Begin("Style Editor", &show_app_style_editor); 
        ImGui::ShowStyleEditor(); 
        ImGui::End(); 
    }
    if (show_app_about) { 
        ImGui::ShowAboutWindow(&show_app_about); 
    }

    ImGui::Begin("##TelemetryViewerMain", nullptr,
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_MenuBar);

    // Menu
    ImVec2 menuBarSize;

    if (ImGui::BeginMainMenuBar())
    {
        menuBarSize = ImGui::GetWindowSize();
        if (ImGui::BeginMenu("Menu"))
        {
            if (ImGui::MenuItem("Exit", "Alt+F4")) {

            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            ImGui::MenuItem("Metrics", NULL, &show_app_metrics);
            ImGui::MenuItem("Style Editor", NULL, &show_app_style_editor);
            ImGui::MenuItem("About Dear ImGui", NULL, &show_app_about);
            ImGui::MenuItem("About", NULL, &show_app_about);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    const float h = wh - menuBarSize.y;
    const float defauktWorkspaceWidth = 300.f;
    const float minWorkspaceWidth = 128.f;
    const float minTracesWidth = 256.f;

    static float sz1 = defauktWorkspaceWidth;
    static float sz2 = ww - sz1;

    static float lastWidth = ww;

    if (ww != lastWidth) {

        // keep the sidebar simular size.
        if (ww < sz1) {
            float leftR = sz1 / lastWidth;
            float rightR = sz2 / lastWidth;

            sz1 = ww * leftR;
            sz2 = ww * rightR;
        }
        else {
            sz2 = ww * sz1;
        }

        lastWidth = ww;
    }

    Splitter(true, 4.0f, &sz1, &sz2, minWorkspaceWidth, minTracesWidth);
    {
        ImGui::BeginChild("##1", ImVec2(sz1, -1), false);

        if (ImGui::BeginTabBar("Main Tabs"))
        {
            if (ImGui::BeginTabItem("Traces", nullptr, 0))
            {
               // float infoHeight = 200.f;
                static core::Guid selectGuid;

                ImGui::BeginChild("##TraceList", ImVec2(0, h - 200.f));
                {
                    core::CriticalSection::ScopedLock lock(client.dataCS);

                    {
                        if (client.isConnected())
                        {
                            // need to be able to select one but across many.
                            // so it should just be guid.
                            for (const auto& app : client.apps)
                            {
                                if (ImGui::CollapsingHeader(app.appName.c_str()))
                                {
                                    // ImGui::Text(app.appName.c_str());
                                    // ImGui::Text("Num %" PRIuS, app.traces.size());
                                    ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor(0.2f, 0.2f, 0.2f));

                                    for (int32_t i = 0; i < static_cast<int32_t>(app.traces.size()); i++)
                                    {
                                        const auto& trace = app.traces[i % app.traces.size()];

                                        // want to build a string like:
                                        // hostname - 6 min ago
                                        // auto timeNow = core::DateTimeStamp::getSystemDateTime();
                                        ImGui::PushID(i);

                                        auto secondsSinceTraceStart = GetSystemTimeAsUnixTime() - trace.unixTimestamp;

                                        StringBuf ageStr;
                                        unixToHumanAgeStr(ageStr, secondsSinceTraceStart);

                                        core::StackString256 label(trace.hostName.begin(), trace.hostName.end());
                                        label.appendFmt(" - [%s]", ageStr.c_str());

                                        if (trace.active) {
                                            label.append("[active]");

#if 0
                                            ImVec2 p_min = ImGui::GetCursorScreenPos();
                                            p_min.y -= ImGui::GetStyle().FramePadding.y;
                                            ImVec2 p_max = ImVec2(p_min.x + ImGui::GetContentRegionAvailWidth(), p_min.y + ImGui::GetFrameHeight());
                                            ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, IM_COL32(0, 80, 0, 200));
#endif
                                        }

                                        if (ImGui::Selectable(label.c_str(), trace.guid == selectGuid))
                                        {
                                            selectGuid = trace.guid;

                                            // need to dispatch a request to get the stats.
                                            QueryTraceInfo qti;
                                            qti.dataSize = sizeof(qti);
                                            qti.type = PacketType::QueryTraceInfo;
                                            qti.guid = trace.guid;
                                            client.sendDataToServer(&qti, sizeof(qti));
                                        }

                                        ImGui::PopID();
                                    }

                                    ImGui::PopStyleColor(1);
                                }
                            }
                        }
                        else
                        {
                            // show some connect button.
                            ImGui::Separator();
                            ImGui::TextUnformatted("Connect to server");

                            const bool connecting = client.conState == Client::ConnectionState::Connecting;

                            if (connecting)
                            {
                                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                            }

                            static char addr[256] = { "127.0.0.1" };
                            bool connectClicked = false;
                            connectClicked |= ImGui::InputText("", addr, sizeof(addr), ImGuiInputTextFlags_EnterReturnsTrue);
                            connectClicked |= ImGui::Button("Connect");

                            if (connecting)
                            {
                                ImGui::PopItemFlag();
                                ImGui::PopStyleVar();
                            }
                            else if (connectClicked && *addr)
                            {
                                client.addr.set(addr);

                                // how to know connecting?
                                client.connectSignal.raise();
                            }
                        }

                    }
                    ImGui::EndChild();

                    // stats.
                    {
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.f));
                        // ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
                         //ImGui::SetNextWindowPos(ImVec2(0, 0));
                       //  ImGui::SetNextWindowSize(ImVec2(-1, 100));


                        ImGui::BeginChild("##TraceInfo", ImVec2(-1, -1), true);

                        if (selectGuid.isValid())
                        {
                            // need stats for this trace.
                            auto& ts = client.traceStats;
                            auto it = std::find_if(ts.begin(), ts.end(), [](const GuidTraceStats& lhs) {
                                return selectGuid == lhs.first;
                            });

                            // find the tracenfo also.
                            TraceInfo* pSelectedTrace = nullptr;

                            for (auto& app : client.apps)
                            {
                                for (auto& trace : app.traces)
                                {
                                    if (trace.guid == selectGuid)
                                    {
                                        pSelectedTrace = &trace;
                                        break;
                                    }
                                }

                                if (pSelectedTrace) {
                                    break;
                                }
                            }

                            if (it != ts.end())
                            {
                                X_ASSERT_NOT_NULL(pSelectedTrace);

                                auto& stats = it->second;

                                core::HumanDuration::Str durStr0;
                                HumanNumber::Str numStr;
                                StringBuf dateStr;

                                if (pSelectedTrace->active) {

                                    // when the trace is active we want to keep asking the server for updated stats.
                                    static core::StopWatch timer;

                                    if (timer.GetMilliSeconds() >= 200)
                                    {
                                        timer.Start();

                                        QueryTraceInfo qti;
                                        qti.dataSize = sizeof(qti);
                                        qti.type = PacketType::QueryTraceInfo;
                                        qti.guid = selectGuid;
                                        client.sendDataToServer(&qti, sizeof(qti));
                                    }

                                    // TODO: something more sexy looking.
                                    ImGui::Text("Active");
                                }

                                ImGui::Text("Date: %s", unixToLocalTimeStr(dateStr, pSelectedTrace->unixTimestamp));
                                ImGui::Text("Duration: %s", core::HumanDuration::toStringNano(durStr0, stats.durationNano));
                                ImGui::Text("Zones: %s", HumanNumber::toString(numStr, stats.numZones));
                                ImGui::SameLine();
                                if (ImGui::Button("Open"))
                                {
                                    // TODO: check if exists  then open / focus.
                                    OpenTrace ot;
                                    ot.dataSize = sizeof(ot);
                                    ot.type = PacketType::OpenTrace;
                                    ot.guid = it->first;
                                    client.sendDataToServer(&ot, sizeof(ot));
                                }

                                ImGui::Text("Allocations: %" PRId64, stats.numAlloc);
                                ImGui::SameLine();
                                if (ImGui::Button("Open"))
                                {

                                }

                                ImGui::Text("Messages: %" PRId64, stats.numMessages);
                                ImGui::SameLine();
                                if (ImGui::Button("Open"))
                                {
                                    // just need to open the messages.
                                    // i think ideally the data should be shared across views so
                                    // we can see the messages in the zone view and also in the message list.
                                    // maybe this should just be a pop up?

                                }

                                ImGui::Text("Locks Taken: %s", HumanNumber::toString(numStr, stats.numLockTry));

                            }
                            else
                            {
                                ImGui::Text("Duration: -");
                                ImGui::Text("Zones: -");
                                ImGui::Text("Allocations: -");
                            }
                        }

                        ImGui::EndChild();

                        ImGui::PopStyleColor();
                        //   ImGui::PopStyleVar();
                    }
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings", nullptr, 0))
            {

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::EndChild();
    }
    ImGui::SameLine();
    {
        ImGui::BeginChild("##2", ImVec2(sz2, -1), false);

        if (ImGui::BeginTabBar("View Tabs"))
        {
            core::CriticalSection::ScopedLock lock(client.dataCS);

            for (auto& view : client.views)
            {
                if (ImGui::BeginTabItem(view.tabName.c_str(), &view.open_, 0))
                {
                    DrawFrames(view);
                    DrawZones(view);
                    //    DrawZones();

                    if (view.zoomAnim_.active)
                    {
                        const auto& io = ImGui::GetIO();

                        view.zoomAnim_.progress += io.DeltaTime * view.zoomAnim_.lenMod;
                        if (view.zoomAnim_.progress >= 1.f)
                        {
                            view.zoomAnim_.active = false;
                            view.zvStartNS_ = view.zoomAnim_.start1;
                            view.zvEndNS_ = view.zoomAnim_.end1;
                        }
                        else
                        {
                            const auto v = sqrt(sin(math<double>::HALF_PI * view.zoomAnim_.progress));
                            view.zvStartNS_ = int64_t(view.zoomAnim_.start0 + (view.zoomAnim_.start1 - view.zoomAnim_.start0) * v);
                            view.zvEndNS_ = int64_t(view.zoomAnim_.end0 + (view.zoomAnim_.end1 - view.zoomAnim_.end0) * v);
                        }
                    }


                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }

        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
}


bool handleTraceZoneSegmentTicks(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceZoneSegmentRespTicks*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceZoneSegmentTicks) {
        X_ASSERT_UNREACHABLE();
    }

    // TODO: fix.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;

    if (view.segments.isEmpty()) {
        view.segments.emplace_back(g_arena);
    }

    auto& segment = view.segments.front();
    segment.endNano = view.stats.durationNano; // TODO: HACK!

    // meow.
    // so i need to find out what segment to add this to.
    // i think segments where going to be based on camels in the north?
    // think for now it's just oging to be be tick ranges
    // so a segment will cover a number of ticks
    // but that make predition hard? since i need to preload zones based on time not ticks.
    // for now lets make single segment rip.


    // so this is tick info for a zone segment request.
    // it might not be all the data for the request. 
    // but should they all be in order? think so.
    auto* pTicks = reinterpret_cast<const DataPacketTickInfo*>(pHdr + 1);

    auto& ticks = segment.ticks;
    ticks.reserve(ticks.size() + pHdr->num);

    for (int32 i = 0; i < pHdr->num; i++)
    {
        auto& tick = pTicks[i];

        TickData td;
        td.start = tick.start;
        td.end = tick.end;
        td.startNano = tick.startNano;
        td.endNano = tick.endNano;

        ticks.push_back(td);
    }

    return true;
}

bool handleTraceZoneSegmentZones(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceZoneSegmentRespZones*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceZoneSegmentZones) {
        X_ASSERT_UNREACHABLE();
    }

    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;

    if (view.segments.isEmpty()) {
        view.segments.emplace_back(g_arena);
        view.segments.front().threads.reserve(12);
    }

    auto& segment = view.segments.front();
    auto& threads = segment.threads;

    core::FixedArray<uint32_t, MAX_ZONE_THREADS> threadIDs;
    for (auto& thread : threads) {
        threadIDs.push_back(thread.id);
    }

    auto* pZones = reinterpret_cast<const DataPacketZone*>(pHdr + 1);
    for (int32 i = 0; i < pHdr->num; i++)
    {
        auto& zone = pZones[i];

        ZoneData zd;
        zd.startTicks = zone.start;
        zd.endTicks = zone.end;
        zd.startNano = view.ticksToNano(zone.start);
        zd.endNano = view.ticksToNano(zone.end);
        zd.lineNo = zone.lineNo;
        zd.strIdxFile = zone.strIdxFile;
        zd.strIdxZone = zone.strIdxFmt;
        zd.stackDepth = zone.stackDepth;

        // want a thread
        int32_t t;
        for(t =0; t < static_cast<int32_t>(threadIDs.size()); t++) {
            if (threadIDs[t] == zone.threadID) {
                break;
            }
        }

        if (t == static_cast<int32_t>(threadIDs.size())) {
            threads.emplace_back(zone.threadID, g_arena);
            threadIDs.push_back(zone.threadID);
        }

        auto& thread = threads[t];

        // now we select thread.
        while (static_cast<int32_t>(thread.levels.size()) <= zd.stackDepth) {
            thread.levels.emplace_back(g_arena);
        }

        thread.levels[zd.stackDepth].zones.push_back(zd);
    }

    return true;
}

bool handleTraceZoneSegmentLockStates(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceZoneSegmentRespLockStates*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceZoneSegmentLockStates) {
        X_ASSERT_UNREACHABLE();
    }

    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    auto& segment = view.segments.front();
    auto& threads = segment.threads;

    core::FixedArray<uint32_t, MAX_ZONE_THREADS> threadIDs;
    for (auto& thread : threads) {
        threadIDs.push_back(thread.id);
    }

    // states!
    auto* pStates = reinterpret_cast<const DataPacketLockState*>(pHdr + 1);
    for (int32 i = 0; i < pHdr->num; i++)
    {
        auto& state = pStates[i];

        int32_t t;
        for (t = 0; t < static_cast<int32_t>(threadIDs.size()); t++) {
            if (threadIDs[t] == state.threadID) {
                break;
            }
        }

        if (t == static_cast<int32_t>(threadIDs.size())) {
            threads.emplace_back(state.threadID, g_arena);
            threadIDs.push_back(state.threadID);
        }

        auto it = segment.locks.find(state.lockHandle);
        if (it == segment.locks.end()) {
            it = segment.locks.emplace(state.lockHandle, LockData(state.lockHandle, g_arena)).first;
        }

        LockState ls;
        ls.time = state.time;
        ls.timeNano = view.ticksToNano(state.time);
        ls.state = static_cast<TtLockState>(state.state);
        ls.threadIdx = safe_static_cast<uint16_t>(t);
        ls.threadID = state.threadID;
        ls.lineNo = state.lineNo;
        ls.strIdxFile = state.strIdxFile;

        it->second.lockStates.push_back(ls);
    }

    return true;
}


bool handleTraceZoneSegmentLockTry(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceZoneSegmentRespLockTry*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceZoneSegmentLockTry) {
        X_ASSERT_UNREACHABLE();
    }

    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    auto& segment = view.segments.front();
    auto& threads = segment.threads;

    core::FixedArray<uint32_t, MAX_ZONE_THREADS> threadIDs;
    for (auto& thread : threads) {
        threadIDs.push_back(thread.id);
    }

    // states!
    auto* pTry = reinterpret_cast<const DataPacketLockTry*>(pHdr + 1);
    for (int32 i = 0; i < pHdr->num; i++)
    {
        auto& lockTry = pTry[i];

        int32_t t;
        for (t = 0; t < static_cast<int32_t>(threadIDs.size()); t++) {
            if (threadIDs[t] == lockTry.threadID) {
                break;
            }
        }

        if (t == static_cast<int32_t>(threadIDs.size())) {
            threads.emplace_back(lockTry.threadID, g_arena);
            threadIDs.push_back(lockTry.threadID);
        }

        auto it = segment.locks.find(lockTry.lockHandle);
        if (it == segment.locks.end()) {
            it = segment.locks.emplace(lockTry.lockHandle, LockData(lockTry.lockHandle, g_arena)).first;
        }

        LockTry lt;
        lt.lockHandle = lockTry.lockHandle;
        lt.startTick = lockTry.start;
        lt.endTick = lockTry.end;
        lt.startNano = view.ticksToNano(lockTry.start);
        lt.endNano = view.ticksToNano(lockTry.end);
        lt.threadID = lockTry.threadID;
        lt.threadIdx = safe_static_cast<uint16_t>(t);
        lt.result = static_cast<TtLockResult>(lockTry.result);
        lt.lineNo = lockTry.lineNo;
        lt.strIdxFile = lockTry.strIdxFile;
        lt.strIdxDescrption = lockTry.strIdxFmt;

        it->second.lockTry.push_back(lt);

        auto& thread = threads[t];
        while (thread.levels.size() <= lockTry.depth) {
            thread.levels.emplace_back(g_arena);
        }

        auto& level = thread.levels[lockTry.depth];

        level.lockTry.push_back(lt);
    }

    return true;
}

bool handleTraceLocks(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceLocksResp*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceLocks) {
        X_ASSERT_UNREACHABLE();
    }

    // shake it.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;

    auto* pLocks = reinterpret_cast<const TraceLockData*>(pHdr + 1);
    for (int32 i = 0; i < pHdr->num; i++) {
        view.locks.push_back(pLocks[i]);
    }

    return true;
}


bool handleTraceStringsInfo(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceStringsRespInfo*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceStringsInfo) {
        X_ASSERT_UNREACHABLE();
    }

    // shake it.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;

    view.strings.init(pHdr->num, pHdr->minId, pHdr->maxId, pHdr->strDataSize);
    return true;
}


bool handleTraceStrings(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceStringsResp*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceStrings) {
        X_ASSERT_UNREACHABLE();
    }

    // shake it.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    auto& strings = view.strings;

    auto* pData = reinterpret_cast<const uint8_t*>(pHdr + 1);
    for (int32_t i = 0; i < pHdr->num; i++)
    {
        auto* pStrHdr = reinterpret_cast<const TraceStringHdr*>(pData);
        auto* pStr = reinterpret_cast<const char*>(pStrHdr + 1);

        // we have the string!
        // push it back and made a pointer.
        strings.addString(pStrHdr->id, pStrHdr->length, pStr);

        pData += (sizeof(*pStrHdr) + pStrHdr->length);
    }

    return true;
}

bool handleTraceThreadNames(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceThreadNamesResp*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceThreadNames) {
        X_ASSERT_UNREACHABLE();
    }

    // shake it.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    auto& strings = view.strings;
    auto& threadNames = strings.threadNames;

    threadNames.reserve(threadNames.size() + pHdr->num);

    auto* pData = reinterpret_cast<const TraceThreadNameData*>(pHdr + 1);
    for (int32_t i = 0; i < pHdr->num; i++)
    {
        threadNames.append(pData[i]);
    }

    return true;
}

bool handleTraceThreadGroupNames(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceThreadGroupNamesResp*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceThreadGroupNames) {
        X_ASSERT_UNREACHABLE();
    }

    // shake it.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    auto& strings = view.strings;
    auto& threadGroupNames = strings.threadGroupNames;

    threadGroupNames.reserve(threadGroupNames.size() + pHdr->num);

    auto* pData = reinterpret_cast<const TraceThreadGroupNameData*>(pHdr + 1);
    for (int32_t i = 0; i < pHdr->num; i++)
    {
        threadGroupNames.append(pData[i]);
    }

    return true;
}

bool handleTraceThreadGroups(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceThreadGroupsResp*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceThreadGroups) {
        X_ASSERT_UNREACHABLE();
    }

    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    auto& threadGroups = view.threadGroups;

    threadGroups.reserve(threadGroups.size() + pHdr->num);

    auto* pData = reinterpret_cast<const TraceThreadGroupData*>(pHdr + 1);
    for (int32_t i = 0; i < pHdr->num; i++)
    {
        threadGroups.append(pData[i]);
    }

    return true;
}

bool handleTraceLockNames(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceLockNamesResp*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceLockNames) {
        X_ASSERT_UNREACHABLE();
    }

    // shake it.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    auto& strings = view.strings;

    strings.lockNames.reserve(strings.lockNames.size() + pHdr->num);

    auto* pData = reinterpret_cast<const TraceLockNameData*>(pHdr + 1);
    for (int32_t i = 0; i < pHdr->num; i++)
    {
        strings.lockNames.append(pData[i]);
    }

    return true;
}

bool handleTraceZoneTree(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceZoneTreeResp*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceZoneTree) {
        X_ASSERT_UNREACHABLE();
    }

    // shake it.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    X_UNUSED(view);

    return true;
}

bool handleTraceMessages(Client& client, const DataPacketBaseViewer* pBase)
{
    auto* pHdr = static_cast<const ReqTraceMessagesResp*>(pBase);
    if (pHdr->type != DataStreamTypeViewer::TraceMessages) {
        X_ASSERT_UNREACHABLE();
    }

    // shake it.
    core::CriticalSection::ScopedLock lock(client.dataCS);

    TraceView* pView = client.viewForHandle(pHdr->handle);
    if (!pView) {
        return false;
    }

    auto& view = *pView;
    X_UNUSED(view);

    return true;
}

bool handleDataSream(Client& client, uint8_t* pData)
{
    auto* pHdr = reinterpret_cast<const DataStreamHdr*>(pData);
    if (pHdr->type != PacketType::DataStream) {
        X_ASSERT_UNREACHABLE();
    }

    // the data is compressed.
    // decompress it..
    int32_t cmpLen = pHdr->dataSize - sizeof(DataStreamHdr);
    int32_t origLen = pHdr->origSize - sizeof(DataStreamHdr);
    X_UNUSED(cmpLen);

    auto* pDst = &client.cmpRingBuf[client.cmpBufferOffset];

    int32_t cmpLenOut = static_cast<int32_t>(client.lz4DecodeStream.decompressContinue(pHdr + 1, pDst, origLen));
    if (cmpLenOut != cmpLen) {
        X_ERROR("TelemViewer", "Failed to inflate data stream");
        return false;
    }

    client.cmpBufferOffset += origLen;
    if (client.cmpBufferOffset >= (COMPRESSION_RING_BUFFER_SIZE - COMPRESSION_MAX_INPUT_SIZE)) {
        client.cmpBufferOffset = 0;
    }


    // TODO: handle multiple packets per buf.
#if 1
    int32_t i = 0;
#else
    for (int32 i = 0; i < origLen; i = origLen) 
#endif
    {
        auto* pPacket = reinterpret_cast<const DataPacketBaseViewer*>(&pDst[i]);

        switch (pPacket->type)
        {

            case DataStreamTypeViewer::TraceZoneSegmentTicks:
                return handleTraceZoneSegmentTicks(client, pPacket);
            case DataStreamTypeViewer::TraceZoneSegmentZones:
                return handleTraceZoneSegmentZones(client, pPacket);
            case DataStreamTypeViewer::TraceZoneSegmentLockStates:
                return handleTraceZoneSegmentLockStates(client, pPacket);
            case DataStreamTypeViewer::TraceZoneSegmentLockTry:
                return handleTraceZoneSegmentLockTry(client, pPacket);

            case DataStreamTypeViewer::TraceLocks:
                return handleTraceLocks(client, pPacket);
            case DataStreamTypeViewer::TraceStringsInfo:
                return handleTraceStringsInfo(client, pPacket);
            case DataStreamTypeViewer::TraceStrings:
                return handleTraceStrings(client, pPacket);
            case DataStreamTypeViewer::TraceThreadNames:
                return handleTraceThreadNames(client, pPacket);
            case DataStreamTypeViewer::TraceThreadGroupNames:
                return handleTraceThreadGroupNames(client, pPacket);
            case DataStreamTypeViewer::TraceThreadGroups:
                return handleTraceThreadGroups(client, pPacket);
            case DataStreamTypeViewer::TraceLockNames:
                return handleTraceLockNames(client, pPacket);
            case DataStreamTypeViewer::TraceZoneTree:
                return handleTraceZoneTree(client, pPacket);
            case DataStreamTypeViewer::TraceMessages:
                return handleTraceMessages(client, pPacket);

            default:
                X_NO_SWITCH_DEFAULT_ASSERT;
        }
    }

#if X_ENABLE_ASSERTIONS
    return true;
#endif // X_ENABLE_ASSERTIONS
}

bool handleAppList(Client& client, uint8_t* pData)
{
    X_UNUSED(client, pData);

    auto* pHdr = reinterpret_cast<const AppsListHdr*>(pData);
    if (pHdr->type != PacketType::AppList) {
        X_ASSERT_UNREACHABLE();
    }


    telemetry::TraceAppArr apps(g_arena);
    apps.reserve(pHdr->num);

    auto* pSrcApp = reinterpret_cast<const AppsListData*>(pHdr + 1);

    for (int32_t i = 0; i < pHdr->num; i++)
    {
        TelemFixedStr name(pSrcApp->appName);
        TraceApp app(name, g_arena);
        app.traces.reserve(pSrcApp->numTraces);

        auto* pTraceData = reinterpret_cast<const AppTraceListData*>(pSrcApp + 1);

        for (int32_t x = 0; x < pSrcApp->numTraces; x++)
        {
            auto& srcTrace = pTraceData[x];

            TraceInfo trace;
            trace.active = srcTrace.active;
            trace.guid = srcTrace.guid;
            trace.ticksPerMicro = srcTrace.ticksPerMicro;
            trace.workerThreadID = srcTrace.workerThreadID;
            trace.unixTimestamp = srcTrace.unixTimestamp;
            trace.hostName = srcTrace.hostName;
            trace.buildInfo = srcTrace.buildInfo;
            trace.cmdLine = srcTrace.cmdLine;

            app.traces.emplace_back(std::move(trace));
        }

        apps.emplace_back(std::move(app));

        pSrcApp = reinterpret_cast<const AppsListData*>(pTraceData + pSrcApp->numTraces);
    }

    core::CriticalSection::ScopedLock lock(client.dataCS);

    if (pHdr->add) {

        // add them.
        for (auto& app : apps)
        {
            auto it = std::find_if(client.apps.begin(), client.apps.end(), [&app](const TraceApp& clientApp) {
                return clientApp.appName == app.appName;
            });

            if (it == client.apps.end()) {
                client.apps.append(app);
            }
            else {
                it->traces.append(app.traces);
            }
        }
    }
    else {
        client.apps = std::move(apps);
    }

    return true;
}


bool handleTraceEnded(Client& client, uint8_t* pData)
{
    X_UNUSED(client, pData);

    auto* pHdr = reinterpret_cast<const TraceEndedHdr*>(pData);
    if (pHdr->type != PacketType::TraceEnded) {
        X_ASSERT_UNREACHABLE();
    }

    core::CriticalSection::ScopedLock lock(client.dataCS);

    for (auto& view : client.views)
    {
        if (view.info.guid == pHdr->guid) {
            view.info.active = false;
            break;
        }
    }

    for (auto& app : client.apps)
    {
        for (auto& trace : app.traces)
        {
            if (trace.guid == pHdr->guid)
            {
                trace.active = false;
                break;
            }
        }
    }

    return true;
}

bool handleQueryTraceInfoResp(Client& client, uint8_t* pData)
{
    X_UNUSED(client, pData);

    auto* pHdr = reinterpret_cast<const QueryTraceInfoResp*>(pData);
    if (pHdr->type != PacketType::QueryTraceInfoResp) {
        X_ASSERT_UNREACHABLE();
    }

    core::CriticalSection::ScopedLock lock(client.dataCS);
    
    // Insert or update.
    auto& ts = client.traceStats;
    auto it = std::find_if(ts.begin(), ts.end(), [pHdr](const GuidTraceStats& lhs) {
        return pHdr->guid == lhs.first;
    });

    if (it != ts.end())
    {
        it->second = pHdr->stats;
    }
    else
    {
        ts.emplace_back(pHdr->guid, pHdr->stats);
    }
    
    return true;
}

bool handleOpenTraceResp(Client& client, uint8_t* pData)
{
    auto* pHdr = reinterpret_cast<const OpenTraceResp*>(pData);
    if (pHdr->type != PacketType::OpenTraceResp) {
        X_ASSERT_UNREACHABLE();
    }

    if (pHdr->handle < 0) {
        X_ERROR("TelemViewer", "Failed to open trace");
        return true;
    }

    core::CriticalSection::ScopedLock lock(client.dataCS);

    // find the info
    auto* pInfo = client.infoForGUID(pHdr->guid);
    if (!pInfo) {
        X_ERROR("TelemViewer", "Failed to find trace info for open trace resp");
        return false;
    }

    client.views.emplace_back(*pInfo, pHdr->stats, pHdr->handle, g_arena);

    // ask for some data?
    // do i want zones and ticks seperate?
    // think not, we know the total time of the trace.
    // but we only show ticks for current window.
    // so we kinda want data at same time.
    // should i index the zone data based on ticks?

    // in terms of getting the data back i just kinda wanna build a zone so having all the ticks and zones for a 
    // segment would be nice.
    // and the segment size is ajustable.
    // so i think this will have to be time based.
    // the problem is knowing how much data we are going to get back?
    // i guess if we just request small blocks say 10ms.
    // and just keep doing that it be okay.
    // some sort of sliding window kinda thing that works out a good time range to request.

    ReqTraceStrings rts;
    rts.type = PacketType::ReqTraceStrings;
    rts.dataSize = sizeof(rts);
    rts.handle = pHdr->handle;
    client.sendDataToServer(&rts, sizeof(rts));

    ReqTraceThreadNames rttn;
    rttn.type = PacketType::ReqTraceThreadNames;
    rttn.dataSize = sizeof(rttn);
    rttn.handle = pHdr->handle;
    client.sendDataToServer(&rttn, sizeof(rttn));

    ReqTraceThreadGroups rtg;
    rtg.type = PacketType::ReqTraceThreadGroups;
    rtg.dataSize = sizeof(rtg);
    rtg.handle = pHdr->handle;
    client.sendDataToServer(&rtg, sizeof(rtg));

    ReqTraceThreadGroupNames rthn;
    rthn.type = PacketType::ReqTraceThreadGroupNames;
    rthn.dataSize = sizeof(rthn);
    rthn.handle = pHdr->handle;
    client.sendDataToServer(&rthn, sizeof(rthn));

    ReqTraceLockNames rtln;
    rtln.type = PacketType::ReqTraceLockNames;
    rtln.dataSize = sizeof(rtln);
    rtln.handle = pHdr->handle;
    client.sendDataToServer(&rtln, sizeof(rtln));

    ReqTraceZoneTree rtzt;
    rtzt.type = PacketType::ReqTraceLockNames;
    rtzt.dataSize = sizeof(rtzt);
    rtzt.handle = pHdr->handle;
    rtzt.frameIdx = -1;
    client.sendDataToServer(&rtzt, sizeof(rtzt));

    ReqTraceLocks trl;
    trl.type = PacketType::ReqTraceLocks;
    trl.dataSize = sizeof(trl);
    trl.handle = pHdr->handle;
    client.sendDataToServer(&trl, sizeof(trl));


    auto startNano = 0;
    auto endNano = 1000_ui64 * 1000_ui64 * 1000_ui64 * 500_ui64;

    ReqTraceZoneMessages rzm;
    rzm.type = PacketType::ReqTraceMessages;
    rzm.dataSize = sizeof(rzm);
    rzm.handle = pHdr->handle;


    ReqTraceZoneSegment rzs;
    rzs.type = PacketType::ReqTraceZoneSegment;
    rzs.dataSize = sizeof(rzs);
    rzs.handle = pHdr->handle;
    rzs.startNano = startNano;
    rzs.endNano = endNano;
    client.sendDataToServer(&rzs, sizeof(rzs));

#if 0
    rzs.endNano = 1000 * 1000 * 1000;
    rzs.endNano = rzs.startNano + 1000 * 1000 * 1000;
    client.sendDataToServer(&rzs, sizeof(rzs));
#endif

    return true;
}


bool processPacket(Client& client, uint8_t* pData)
{
    auto* pPacketHdr = reinterpret_cast<const PacketBase*>(pData);

    switch (pPacketHdr->type)
    {
        case PacketType::ConnectionRequestAccepted: {
            auto* pConAccept = reinterpret_cast<const ConnectionRequestAcceptedHdr*>(pData);
            client.serverVer = pConAccept->serverVer;
            return true;
        }
        case PacketType::ConnectionRequestRejected: {
            auto* pConRej = reinterpret_cast<const ConnectionRequestRejectedHdr*>(pData);
            auto* pStrData = reinterpret_cast<const char*>(pConRej + 1);
            X_ERROR("Telem", "Connection rejected: %.*s", pConRej->reasonLen, pStrData);
            return false;
        }
        case PacketType::DataStream:
            return handleDataSream(client, pData);

        case PacketType::AppList:
            return handleAppList(client, pData);
        case PacketType::TraceEnded:
            return handleTraceEnded(client, pData);
        case PacketType::QueryTraceInfoResp:
            return handleQueryTraceInfoResp(client, pData);
        case PacketType::OpenTraceResp:
            return handleOpenTraceResp(client, pData);

        default:
            X_ERROR("TelemViewer", "Unknown packet type %" PRIi32, static_cast<int>(pPacketHdr->type));
            return false;
    }
}


void Client::sendDataToServer(const void* pData, int32_t len)
{
#if X_DEBUG
    if (len > MAX_PACKET_SIZE) {
        ::DebugBreak();
    }
#endif // X_DEBUG

    // send some data...
    // TODO: none blocking?
    int res = platform::send(socket, reinterpret_cast<const char*>(pData), len, 0);
    if (res == SOCKET_ERROR) {
        lastErrorWSA::Description Dsc;
        X_ERROR("Telem", "Socket: send failed with error: %s", lastErrorWSA::ToString(Dsc));
        return;
    }
}

bool readPacket(Client& client, char* pBuffer, int& bufLengthInOut)
{
    // this should return complete packets or error.
    int bytesRead = 0;
    int bufLength = sizeof(PacketBase);

    while (1) {
        int maxReadSize = bufLength - bytesRead;
        int res = platform::recv(client.socket, &pBuffer[bytesRead], maxReadSize, 0);

        if (res == 0) {
            X_ERROR("Telem", "Connection closing...");
            return false;
        }
        else if (res < 0) {
            lastErrorWSA::Description Dsc;
            X_ERROR("Telem", "recv failed with error: %s", lastErrorWSA::ToString(Dsc));
            return false;
        }

        bytesRead += res;

        X_LOG1("Telem", "got: %d bytes", res);

        if (bytesRead == sizeof(PacketBase))
        {
            auto* pHdr = reinterpret_cast<const PacketBase*>(pBuffer);
            if (pHdr->dataSize == 0) {
                X_ERROR("Telem", "Client sent packet with length zero...");
                return false;
            }

            if (pHdr->dataSize > bufLengthInOut) {
                X_ERROR("Telem", "Client sent oversied packet of size %i...", static_cast<tt_int32>(pHdr->dataSize));
                return false;
            }

            bufLength = pHdr->dataSize;
        }

        if (bytesRead == bufLength) {
            bufLengthInOut = bytesRead;
            return true;
        }
        else if (bytesRead > bufLength) {
            X_ERROR("Telem", "Overread packet bytesRead: %d recvbuflen: %d", bytesRead, bufLength);
            return false;
        }
    }
}

Client::Client(core::MemoryArenaBase* arena) :
    addr("127.0.0.1"),
    port(telem::DEFAULT_PORT),
    conState(ConnectionState::Offline),
    connectSignal(true),
    socket(INV_SOCKET),
    cmpBufferOffset(0),
    apps(arena),
    traceStats(arena),
    views(arena)
{
    
}

bool Client::isConnected(void) const 
{
    return socket != INV_SOCKET;
}

void Client::closeConnection(void)
{
    platform::closesocket(socket);
    socket = INV_SOCKET;
    conState = ConnectionState::Offline;
}

TraceView* Client::viewForHandle(tt_int8 handle)
{
    TraceView* pView = nullptr;

    for (auto& view : views)
    {
        if (view.handle == handle)
        {
            return &view;
        }
    }

    return pView;
}


const TraceInfo* Client::infoForGUID(const core::Guid& guid) const
{
    for (auto& app : apps)
    {
        for (auto& info : app.traces)
        {
            if (info.guid == guid)
            {
                return &info;
            }
        }
    }
    return nullptr;
}

bool connectToServer(Client& client)
{
    if (client.isConnected()) {
        // TODO:
        return false;
    }

    struct platform::addrinfo hints, *servinfo = nullptr;
    core::zero_object(hints);
    hints.ai_family = AF_UNSPEC; // ipv4/6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = platform::IPPROTO_TCP;

    // Resolve the server address and port
    core::StackString<16, char> portStr(client.port);

    auto res = platform::getaddrinfo(client.addr.c_str(), portStr.c_str(), &hints, &servinfo);
    if (res != 0) {
        lastErrorWSA::Description Dsc;
        X_ERROR("Telem", "Failed to get addre info. Error: %s", lastErrorWSA::ToString(Dsc));
        return false;
    }

    platform::SOCKET connectSocket = INV_SOCKET;

    for (auto pPtr = servinfo; pPtr != nullptr; pPtr = pPtr->ai_next) {
        // Create a SOCKET for connecting to server
        connectSocket = platform::socket(pPtr->ai_family, pPtr->ai_socktype, pPtr->ai_protocol);
        if (connectSocket == INV_SOCKET) {
            return false;
        }

        // Connect to server.
        res = connect(connectSocket, pPtr->ai_addr, static_cast<int>(pPtr->ai_addrlen));
        if (res == SOCKET_ERROR) {
            platform::closesocket(connectSocket);
            connectSocket = INV_SOCKET;
            continue;
        }

        break;
    }

    platform::freeaddrinfo(servinfo);

    if (connectSocket == INV_SOCKET) {
        return false;
    }

    // how big?
    int32_t sock_opt = 1024 * 16;
    res = platform::setsockopt(connectSocket, SOL_SOCKET, SO_SNDBUF, (char*)&sock_opt, sizeof(sock_opt));
    if (res != 0) {
        lastErrorWSA::Description Dsc;
        X_ERROR("Telem", "Failed to set sndbuf on socket. Error: %s", lastErrorWSA::ToString(Dsc));
        return false;
    }

    // Set a big receive buffer.
    sock_opt = 1024 * 256;
    res = platform::setsockopt(connectSocket, SOL_SOCKET, SO_RCVBUF, (char*)&sock_opt, sizeof(sock_opt));
    if (res != 0) {
        lastErrorWSA::Description Dsc;
        X_ERROR("TelemSrv", "Failed to set rcvbuf on socket. Error: %s", lastErrorWSA::ToString(Dsc));
    }

    ConnectionRequestViewerHdr cr;
    cr.dataSize = sizeof(cr);
    cr.type = PacketType::ConnectionRequestViewer;
    cr.viewerVer.major = TELEM_VERSION_MAJOR;
    cr.viewerVer.minor = TELEM_VERSION_MINOR;
    cr.viewerVer.patch = TELEM_VERSION_PATCH;
    cr.viewerVer.build = TELEM_VERSION_BUILD;

    client.socket = connectSocket;
    client.sendDataToServer(&cr, sizeof(cr));

    // wait for a response O.O
    char recvbuf[MAX_PACKET_SIZE];
    int recvbuflen = sizeof(recvbuf);

    // TODO: support timeout.
    if (!readPacket(client, recvbuf, recvbuflen)) {
        return false;
    }

    if (!processPacket(client, reinterpret_cast<tt_uint8*>(recvbuf))) {
        return false;
    }

    return true;
}

core::Thread::ReturnValue threadFunc(const core::Thread& thread)
{
    char recvbuf[MAX_PACKET_SIZE];

    Client& client = *reinterpret_cast<Client*>(thread.getData());

    // do work!
    while (thread.shouldRun())
    {
        // we need to wait till we are told to connect.
        // then try connect.
        while (!client.isConnected())
        {
            client.connectSignal.wait();
            client.conState = Client::ConnectionState::Connecting;

            if (!connectToServer(client)) {
                client.conState = Client::ConnectionState::Offline;
            }
            else {
                client.conState = Client::ConnectionState::Connected;
            }
        }

        // listen for packets.
        int recvbuflen = sizeof(recvbuf);

        if (!readPacket(client, recvbuf, recvbuflen)) {
            client.closeConnection();
            continue;
        }

        if (!processPacket(client, reinterpret_cast<tt_uint8*>(recvbuf))) {
            break;
        }
    }

    return core::Thread::ReturnValue(0);
}

bool run(Client& client)
{
    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        X_ERROR("", "Error: %s", SDL_GetError());
        return false;
    }

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    auto pWindow = SDL_CreateWindow("TelemetryViewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1680, 1050, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(pWindow);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader
    bool err = gl3wInit() != 0;
    if (err) {
        X_ERROR("Telem", "Failed to initialize OpenGL loader!");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForOpenGL(pWindow, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    const ImVec4 clearColor = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);

    bool done = false;

    // start a thread for the socket.
    core::Thread thread;
    thread.create("Worker", 1024 * 64);
    thread.setData(&client);
    thread.start(threadFunc);

    // try connect to server
    client.connectSignal.raise();

    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                done = true;
            }
            else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(pWindow)) {
                done = true;
            }

        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(pWindow);
        ImGui::NewFrame();

        DrawFrame(client, io.DisplaySize.x, io.DisplaySize.y);
     //   ImGui::ShowDemoWindow();

        ImGui::Render();
        SDL_GL_MakeCurrent(pWindow, gl_context);
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(pWindow);
    }

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(pWindow);
    SDL_Quit();

    platform::closesocket(client.socket);

    thread.stop();
    thread.join();
    return true;
}

X_NAMESPACE_END
