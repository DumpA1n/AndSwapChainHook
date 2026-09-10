#include "ImGuiSoftKeyboard.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

namespace ImGuiSoftKeyboard
{

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------
static bool g_ForceShow  = false;
static bool g_ForceState = false;
static bool g_Visible    = false;
static bool g_Shift      = false;
static bool g_Caps       = false;
static bool g_Symbols    = false;   // Symbol layer.
static int  g_PressedKey = -1;      // Pressed-key index for highlighting.

// ---------------------------------------------------------------------------
// Keyboard layout.
// ---------------------------------------------------------------------------

struct KeyDef {
    const char* label;       // Display label.
    const char* shiftLabel;  // Shift label.
    float       widthScale;  // Width relative to a standard key.
    enum Action { Char, Backspace, Enter, Shift, Space, Symbols, Tab, Left, Right, Hide } action;
};

// Letter layer.
static const KeyDef g_RowsAlpha[][12] = {
    // Row 0: number row
    { {"1","!",1,KeyDef::Char}, {"2","@",1,KeyDef::Char}, {"3","#",1,KeyDef::Char}, {"4","$",1,KeyDef::Char}, {"5","%",1,KeyDef::Char},
      {"6","^",1,KeyDef::Char}, {"7","&",1,KeyDef::Char}, {"8","*",1,KeyDef::Char}, {"9","(",1,KeyDef::Char}, {"0",")",1,KeyDef::Char},
      {nullptr,nullptr,0,KeyDef::Char} },
    // Row 1
    { {"q","Q",1,KeyDef::Char}, {"w","W",1,KeyDef::Char}, {"e","E",1,KeyDef::Char}, {"r","R",1,KeyDef::Char}, {"t","T",1,KeyDef::Char},
      {"y","Y",1,KeyDef::Char}, {"u","U",1,KeyDef::Char}, {"i","I",1,KeyDef::Char}, {"o","O",1,KeyDef::Char}, {"p","P",1,KeyDef::Char},
      {nullptr,nullptr,0,KeyDef::Char} },
    // Row 2
    { {"a","A",1,KeyDef::Char}, {"s","S",1,KeyDef::Char}, {"d","D",1,KeyDef::Char}, {"f","F",1,KeyDef::Char}, {"g","G",1,KeyDef::Char},
      {"h","H",1,KeyDef::Char}, {"j","J",1,KeyDef::Char}, {"k","K",1,KeyDef::Char}, {"l","L",1,KeyDef::Char},
      {nullptr,nullptr,0,KeyDef::Char} },
    // Row 3
    { {"Shift",nullptr,1.5f,KeyDef::Shift},
      {"z","Z",1,KeyDef::Char}, {"x","X",1,KeyDef::Char}, {"c","C",1,KeyDef::Char}, {"v","V",1,KeyDef::Char},
      {"b","B",1,KeyDef::Char}, {"n","N",1,KeyDef::Char}, {"m","M",1,KeyDef::Char},
      {"<-",nullptr,1.5f,KeyDef::Backspace},
      {nullptr,nullptr,0,KeyDef::Char} },
    // Row 4
    { {"?!#",nullptr,1.5f,KeyDef::Symbols},
      {",",nullptr,1,KeyDef::Char},
      {" ",nullptr,5,KeyDef::Space},
      {".",nullptr,1,KeyDef::Char},
      {"OK",nullptr,1.5f,KeyDef::Enter},
      {nullptr,nullptr,0,KeyDef::Char} },
};

// Symbol layer.
static const KeyDef g_RowsSymbols[][12] = {
    // Row 0
    { {"~",nullptr,1,KeyDef::Char}, {"`",nullptr,1,KeyDef::Char}, {"|",nullptr,1,KeyDef::Char}, {"\\",nullptr,1,KeyDef::Char},
      {"{",nullptr,1,KeyDef::Char}, {"}",nullptr,1,KeyDef::Char}, {"[",nullptr,1,KeyDef::Char}, {"]",nullptr,1,KeyDef::Char},
      {"<",nullptr,1,KeyDef::Char}, {">",nullptr,1,KeyDef::Char},
      {nullptr,nullptr,0,KeyDef::Char} },
    // Row 1
    { {"!",nullptr,1,KeyDef::Char}, {"@",nullptr,1,KeyDef::Char}, {"#",nullptr,1,KeyDef::Char}, {"$",nullptr,1,KeyDef::Char},
      {"%",nullptr,1,KeyDef::Char}, {"^",nullptr,1,KeyDef::Char}, {"&",nullptr,1,KeyDef::Char}, {"*",nullptr,1,KeyDef::Char},
      {"(",nullptr,1,KeyDef::Char}, {")",nullptr,1,KeyDef::Char},
      {nullptr,nullptr,0,KeyDef::Char} },
    // Row 2
    { {"-",nullptr,1,KeyDef::Char}, {"_",nullptr,1,KeyDef::Char}, {"=",nullptr,1,KeyDef::Char}, {"+",nullptr,1,KeyDef::Char},
      {";",nullptr,1,KeyDef::Char}, {":",nullptr,1,KeyDef::Char}, {"'",nullptr,1,KeyDef::Char}, {"\"",nullptr,1,KeyDef::Char},
      {"/",nullptr,1,KeyDef::Char},
      {nullptr,nullptr,0,KeyDef::Char} },
    // Row 3
    { {"Tab",nullptr,1.5f,KeyDef::Tab},
      {"?",nullptr,1,KeyDef::Char},
      {"<",nullptr,1,KeyDef::Left}, {">",nullptr,1,KeyDef::Right},
      {"<-",nullptr,1.5f,KeyDef::Backspace},
      {nullptr,nullptr,0,KeyDef::Char} },
    // Row 4
    { {"ABC",nullptr,1.5f,KeyDef::Symbols},
      {",",nullptr,1,KeyDef::Char},
      {" ",nullptr,5,KeyDef::Space},
      {".",nullptr,1,KeyDef::Char},
      {"OK",nullptr,1.5f,KeyDef::Enter},
      {nullptr,nullptr,0,KeyDef::Char} },
};

constexpr int kRowCount = 5;

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

static bool IsUpper()
{
    return g_Shift ^ g_Caps;
}

static void ProcessKey(const KeyDef& key)
{
    ImGuiIO& io = ImGui::GetIO();

    switch (key.action)
    {
    case KeyDef::Char:
    {
        const char* text = (IsUpper() && key.shiftLabel) ? key.shiftLabel : key.label;
        if (text && text[0])
        {
            const char* p = text;
            while (*p)
            {
                unsigned int c = 0;
                int adv = ImTextCharFromUtf8(&c, p, nullptr);
                if (adv <= 0) break;
                io.AddInputCharacter(c);
                p += adv;
            }
        }
        if (g_Shift && !g_Caps)
            g_Shift = false;
        break;
    }
    case KeyDef::Backspace:
        io.AddKeyEvent(ImGuiKey_Backspace, true);
        io.AddKeyEvent(ImGuiKey_Backspace, false);
        break;
    case KeyDef::Enter:
        io.AddKeyEvent(ImGuiKey_Enter, true);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        break;
    case KeyDef::Tab:
        io.AddKeyEvent(ImGuiKey_Tab, true);
        io.AddKeyEvent(ImGuiKey_Tab, false);
        break;
    case KeyDef::Left:
        io.AddKeyEvent(ImGuiKey_LeftArrow, true);
        io.AddKeyEvent(ImGuiKey_LeftArrow, false);
        break;
    case KeyDef::Right:
        io.AddKeyEvent(ImGuiKey_RightArrow, true);
        io.AddKeyEvent(ImGuiKey_RightArrow, false);
        break;
    case KeyDef::Shift:
        g_Shift = !g_Shift;
        break;
    case KeyDef::Symbols:
        g_Symbols = !g_Symbols;
        break;
    case KeyDef::Space:
        io.AddInputCharacter(' ');
        break;
    case KeyDef::Hide:
        break;
    }
}

/// Whether a screen coordinate lies inside the rectangle.
static bool HitTest(const ImVec2& pos, const ImVec2& rectMin, const ImVec2& rectMax)
{
    return pos.x >= rectMin.x && pos.x < rectMax.x &&
           pos.y >= rectMin.y && pos.y < rectMax.y;
}

// ---------------------------------------------------------------------------
// Shared keyboard-region calculation.
// ---------------------------------------------------------------------------
static void GetKeyboardRect(const ImGuiIO& io, float& outY, float& outH)
{
    outH = io.DisplaySize.y * 0.38f;
    outY = io.DisplaySize.y - outH;
}

// ---------------------------------------------------------------------------
// Public interface.
// ---------------------------------------------------------------------------

// PreUpdate records a keyboard-area click for Draw.
static bool  g_ClickInKeyboard = false;
static ImVec2 g_ClickMousePos   = ImVec2(0, 0);

void PreUpdate()
{
    ImGuiIO& io = ImGui::GetIO();

    // Latched visibility.
    if (g_ForceState)
    {
        g_Visible = g_ForceShow;
    }
    else
    {
        if (io.WantTextInput && !g_Visible)
        {
            g_Visible = true;
        }
        else if (g_Visible && !io.WantTextInput)
        {
            // Close only when the user clicks outside the keyboard.
            if (io.MouseClicked[0])
            {
                float kbY, kbH;
                GetKeyboardRect(io, kbY, kbH);

                bool clickedOnKeyboard = HitTest(io.MousePos,
                    ImVec2(0, kbY), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

                if (!clickedOnKeyboard)
                {
                    g_Visible = false;
                }
            }
        }
    }

    g_ClickInKeyboard = false;
    g_ClickMousePos = io.MousePos;

    if (!g_Visible)
        return;

    // Intercept keyboard-area pointer events before user UI.
    float kbY, kbH;
    GetKeyboardRect(io, kbY, kbH);
    bool inKbArea = HitTest(io.MousePos, ImVec2(0, kbY), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    if (io.MouseClicked[0] && inKbArea)
    {
        g_ClickInKeyboard = true;
        io.MouseClicked[0] = false;
    }
    if (io.MouseDown[0] && inKbArea)
    {
        io.MouseDown[0] = false;
    }
}

void Draw()
{
    if (!g_Visible)
        return;

    ImGuiIO& io = ImGui::GetIO();

    // Compute keyboard layout.
    float screenW = io.DisplaySize.x;
    float kbY, kbHeight;
    GetKeyboardRect(io, kbY, kbHeight);
    float screenH = io.DisplaySize.y;
    float padding = 4.0f;
    float keySpacing = 4.0f;

    const auto (&rows)[kRowCount][12] = g_Symbols ? g_RowsSymbols : g_RowsAlpha;

    float rowHeight = (kbHeight - padding * 2 - keySpacing * (kRowCount - 1)) / kRowCount;

    // Use the click state recorded by PreUpdate.
    bool clickInKeyboard = g_ClickInKeyboard;
    ImVec2 mousePos = g_ClickMousePos;

    // Use the foreground DrawList without creating a focus-taking ImGui window.
    ImDrawList* drawList = ImGui::GetForegroundDrawList();

    // Background.
    ImU32 bgColor = IM_COL32(38, 38, 38, 242);
    drawList->AddRectFilled(ImVec2(0, kbY), ImVec2(screenW, screenH), bgColor);

    g_PressedKey = -1;
    int globalKeyIdx = 0;

    for (int r = 0; r < kRowCount; r++)
    {
        // Count keys and relative widths in this row.
        float totalScale = 0;
        int keyCount = 0;
        for (int k = 0; rows[r][k].label != nullptr; k++)
        {
            totalScale += rows[r][k].widthScale;
            keyCount++;
        }

        float availW = screenW - padding * 2 - keySpacing * (keyCount - 1);
        float unitW = availW / totalScale;

        float curX = padding;
        float curY = kbY + padding + r * (rowHeight + keySpacing);

        for (int k = 0; rows[r][k].label != nullptr; k++)
        {
            const KeyDef& key = rows[r][k];
            float btnW = unitW * key.widthScale;

            ImVec2 btnMin(curX, curY);
            ImVec2 btnMax(curX + btnW, curY + rowHeight);

            // Hit testing.
            bool isClicked = clickInKeyboard && HitTest(mousePos, btnMin, btnMax);

            // Key color.
            bool isSpecial = (key.action != KeyDef::Char && key.action != KeyDef::Space);
            bool isShiftActive = (key.action == KeyDef::Shift && (g_Shift || g_Caps));

            ImU32 btnColor;
            if (isClicked)
                btnColor = IM_COL32(100, 130, 200, 255);
            else if (isShiftActive)
                btnColor = IM_COL32(77, 128, 204, 255);
            else if (isSpecial)
                btnColor = IM_COL32(64, 64, 77, 255);
            else
                btnColor = IM_COL32(55, 55, 60, 255);

            // Draw rounded key backgrounds.
            drawList->AddRectFilled(btnMin, btnMax, btnColor, 6.0f);

            // Draw key labels.
            const char* displayText = key.label;
            if (key.action == KeyDef::Char && IsUpper() && key.shiftLabel)
                displayText = key.shiftLabel;

            if (displayText && displayText[0])
            {
                ImVec2 textSize = ImGui::CalcTextSize(displayText);
                ImVec2 textPos(
                    btnMin.x + (btnW - textSize.x) * 0.5f,
                    btnMin.y + (rowHeight - textSize.y) * 0.5f
                );
                drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), displayText);
            }

            // Process the click.
            if (isClicked)
            {
                g_PressedKey = globalKeyIdx;

                if (key.action == KeyDef::Enter)
                {
                    // Enter/OK closes the keyboard.
                    g_Visible = false;
                }

                ProcessKey(key);
            }

            curX += btnW + keySpacing;
            globalKeyIdx++;
        }
    }

}

void ForceShow(bool show)
{
    g_ForceState = true;
    g_ForceShow = show;
}

bool IsVisible()
{
    return g_Visible;
}

} // namespace ImGuiSoftKeyboard
