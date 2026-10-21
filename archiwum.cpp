// =====================================================================
//  ARCHIWUM POD ŚWIĘTYM SZYMONEM
//  Mroczna gra przygodowa 2D. C++17, programowy renderer (bufor pikseli),
//  WinAPI + GDI (StretchDIBits). Zero bibliotek zewnętrznych.
//
//  Budowanie (MinGW):  g++ -std=c++17 -O2 archiwum.cpp -o game.exe -lgdi32 -static
//  Sterowanie: WASD / strzałki - ruch, E / SPACJA - działaj, J - dziennik
// =====================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ---------------------------------------------------------------------
//  MAPA
// ---------------------------------------------------------------------
class DungeonMap {
private:
    int32_t width_;
    int32_t height_;
    std::vector<int32_t> data_;

public:
    DungeonMap() : width_(32), height_(24), data_(32 * 24, 1) {}
    DungeonMap(int32_t width, int32_t height, int32_t defVal = 1)
    : width_(width), height_(height), data_(width * height, defVal) {}

    [[nodiscard]] int32_t width() const noexcept { return width_; }
    [[nodiscard]] int32_t height() const noexcept { return height_; }
    [[nodiscard]] size_t size() const noexcept { return data_.size(); }

    [[nodiscard]] bool inBounds(int32_t x, int32_t y) const noexcept {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    [[nodiscard]] int32_t get(int32_t x, int32_t y, int32_t fallback = 1) const noexcept {
        if (!inBounds(x, y)) return fallback;
        return data_[static_cast<size_t>(y * width_ + x)];
    }

    bool set(int32_t x, int32_t y, int32_t val) noexcept {
        if (!inBounds(x, y)) return false;
        data_[static_cast<size_t>(y * width_ + x)] = val;
        return true;
    }

    [[nodiscard]] int32_t& at(int32_t x, int32_t y) {
        return data_.at(static_cast<size_t>(y * width_ + x));
    }

    void fill(int32_t val) {
        std::fill(data_.begin(), data_.end(), val);
    }
};

enum class TileType : int32_t {
    Empty = 0,
    Wall = 1,
    Floor = 2,
    Window = 3,
    Pew = 4,
    Altar = 5,
    AltarCandle = 6,
    Stand = 7,
    Rubble = 8,
    Sigil = 9,
    Trap = 10,
    Shelf = 11,
    Desk = 12,
    Stairs = 13,
    SealedDoor = 14,
    OpenDoor = 15,
    Pedestal = 16,
};

[[nodiscard]] static inline TileType tileGet(const DungeonMap& m, int x, int y) {
    return static_cast<TileType>(m.get(x, y, static_cast<int32_t>(TileType::Wall)));
}
static inline void tileSet(DungeonMap& m, int x, int y, TileType t) {
    m.set(x, y, static_cast<int32_t>(t));
}
[[nodiscard]] static inline bool tileSolid(TileType t) {
    switch (t) {
        case TileType::Floor:
        case TileType::Sigil:
        case TileType::Trap:
        case TileType::OpenDoor:
        case TileType::Empty:
        return false;
        default:
        return true;
    }
}

// ---------------------------------------------------------------------
//  AUTOMAT STANÓW GRY
// ---------------------------------------------------------------------
enum class GameFsmState : uint8_t {
    Title,
    Playing,
    Dialog,
    Journal,
    Cipher,
    Choice,
    Ending,
};

enum class GameFsmEvent : uint8_t {
    Start,
    OpenDialog,
    CloseDialog,
    OpenJournal,
    CloseJournal,
    OpenCipher,
    CloseCipher,
    OpenChoice,
    Finish,
    Restart,
};

class GameFsm {
private:
    GameFsmState state_ = GameFsmState::Title;

public:
    GameFsm() = default;
    explicit GameFsm(GameFsmState init) : state_(init) {}

    [[nodiscard]] GameFsmState getState() const noexcept { return state_; }
    [[nodiscard]] bool is(GameFsmState s) const noexcept { return state_ == s; }

    bool transition(GameFsmEvent event) noexcept {
        switch (state_) {
            case GameFsmState::Title:
            switch (event) {
                case GameFsmEvent::Start:
                state_ = GameFsmState::Playing;
                return true;
                default: break;
            }
            break;
            case GameFsmState::Playing:
            switch (event) {
                case GameFsmEvent::OpenDialog:
                state_ = GameFsmState::Dialog;
                return true;
                case GameFsmEvent::OpenJournal:
                state_ = GameFsmState::Journal;
                return true;
                case GameFsmEvent::OpenCipher:
                state_ = GameFsmState::Cipher;
                return true;
                case GameFsmEvent::OpenChoice:
                state_ = GameFsmState::Choice;
                return true;
                default: break;
            }
            break;
            case GameFsmState::Dialog:
            switch (event) {
                case GameFsmEvent::CloseDialog:
                state_ = GameFsmState::Playing;
                return true;
                default: break;
            }
            break;
            case GameFsmState::Journal:
            switch (event) {
                case GameFsmEvent::CloseJournal:
                state_ = GameFsmState::Playing;
                return true;
                default: break;
            }
            break;
            case GameFsmState::Cipher:
            switch (event) {
                case GameFsmEvent::CloseCipher:
                state_ = GameFsmState::Playing;
                return true;
                default: break;
            }
            break;
            case GameFsmState::Choice:
            switch (event) {
                case GameFsmEvent::Finish:
                state_ = GameFsmState::Ending;
                return true;
                default: break;
            }
            break;
            case GameFsmState::Ending:
            switch (event) {
                case GameFsmEvent::Restart:
                state_ = GameFsmState::Title;
                return true;
                default: break;
            }
            break;
            default: break;
        }
        return false;
    }
};

// ---------------------------------------------------------------------
//  BOHATERKA, STRAŻNIK, CZĄSTKI
// ---------------------------------------------------------------------
class Player {
private:
    float x_ = 11.5;
    float y_ = 13.5;
    int32_t facing_ = 1;
    float sanity_ = 100.0;
    float anim_ = 0.0;
    bool moving_ = false;

public:
    Player() = default;
    explicit Player(float x, float y, int32_t facing, float sanity, float anim, bool moving)
    : x_(x), y_(y), facing_(facing), sanity_(sanity), anim_(anim), moving_(moving) {}

    [[nodiscard]] float getX() const noexcept {
        return x_;
    }

    void setX(float value) noexcept {
        x_ = value;
    }

    [[nodiscard]] float getY() const noexcept {
        return y_;
    }

    void setY(float value) noexcept {
        y_ = value;
    }

    [[nodiscard]] int32_t getFacing() const noexcept {
        return facing_;
    }

    void setFacing(int32_t value) noexcept {
        facing_ = value;
    }

    [[nodiscard]] float getSanity() const noexcept {
        return sanity_;
    }

    void setSanity(float value) noexcept {
        sanity_ = value;
    }

    [[nodiscard]] float getAnim() const noexcept {
        return anim_;
    }

    void setAnim(float value) noexcept {
        anim_ = value;
    }

    [[nodiscard]] bool getMoving() const noexcept {
        return moving_;
    }

    void setMoving(bool value) noexcept {
        moving_ = value;
    }
};

// Strażnik archiwum (Paimon): cień, który ciągnie do mroku i ucieka przed światłem.
class Guardian {
private:
    float x_ = 14.5;
    float y_ = 12.5;
    float target_x_ = 14.5;
    float target_y_ = 12.5;
    float wait_ = 2.0;

public:
    Guardian() = default;
    explicit Guardian(float x, float y, float target_x, float target_y, float wait)
    : x_(x), y_(y), target_x_(target_x), target_y_(target_y), wait_(wait) {}

    [[nodiscard]] float getX() const noexcept {
        return x_;
    }

    void setX(float value) noexcept {
        x_ = value;
    }

    [[nodiscard]] float getY() const noexcept {
        return y_;
    }

    void setY(float value) noexcept {
        y_ = value;
    }

    [[nodiscard]] float getTargetX() const noexcept {
        return target_x_;
    }

    void setTargetX(float value) noexcept {
        target_x_ = value;
    }

    [[nodiscard]] float getTargetY() const noexcept {
        return target_y_;
    }

    void setTargetY(float value) noexcept {
        target_y_ = value;
    }

    [[nodiscard]] float getWait() const noexcept {
        return wait_;
    }

    void setWait(float value) noexcept {
        wait_ = value;
    }
};

class Particle {
private:
    float x_ = 0.0;
    float y_ = 0.0;
    float vx_ = 0.0;
    float vy_ = 0.0;
    uint32_t color_ = 0;
    float life_ = 1.0;
    float max_life_ = 1.0;
    float size_ = 2.0;

public:
    Particle() = default;
    explicit Particle(float x, float y, float vx, float vy, uint32_t color, float life, float max_life, float size)
    : x_(x), y_(y), vx_(vx), vy_(vy), color_(color), life_(life), max_life_(max_life), size_(size) {}

    [[nodiscard]] float getX() const noexcept {
        return x_;
    }

    void setX(float value) noexcept {
        x_ = value;
    }

    [[nodiscard]] float getY() const noexcept {
        return y_;
    }

    void setY(float value) noexcept {
        y_ = value;
    }

    [[nodiscard]] float getVx() const noexcept {
        return vx_;
    }

    void setVx(float value) noexcept {
        vx_ = value;
    }

    [[nodiscard]] float getVy() const noexcept {
        return vy_;
    }

    void setVy(float value) noexcept {
        vy_ = value;
    }

    [[nodiscard]] uint32_t getColor() const noexcept {
        return color_;
    }

    void setColor(uint32_t value) noexcept {
        color_ = value;
    }

    [[nodiscard]] float getLife() const noexcept {
        return life_;
    }

    void setLife(float value) noexcept {
        life_ = value;
    }

    [[nodiscard]] float getMaxLife() const noexcept {
        return max_life_;
    }

    void setMaxLife(float value) noexcept {
        max_life_ = value;
    }

    [[nodiscard]] float getSize() const noexcept {
        return size_;
    }

    void setSize(float value) noexcept {
        size_ = value;
    }
};

template <typename T>
[[nodiscard]] constexpr T clamp(T val, T min_v, T max_v) noexcept {
    return (val < min_v) ? min_v : ((val > max_v) ? max_v : val);
}

// ---------------------------------------------------------------------
//  BUFOR EKRANU I PODSTAWOWE RYSOWANIE
// ---------------------------------------------------------------------
static constexpr int SCREEN_W = 800;
static constexpr int SCREEN_H = 600;
static constexpr int HUD_H = 44;
static constexpr int VIEW_H = SCREEN_H - HUD_H;
static constexpr int PX = 2;                  // sprite'y projektowane na siatce 16x16, skalowane x2
static constexpr int TILE_SIZE = 16 * PX;

static uint32_t s_pixels[SCREEN_W * SCREEN_H];
static bool s_keys[256] = { false };
static bool s_prevKeys[256] = { false };
static bool s_running = true;

static inline void setPixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        s_pixels[y * SCREEN_W + x] = color;
    }
}

static inline void clearScreen(uint32_t color) {
    std::fill(std::begin(s_pixels), std::end(s_pixels), color);
}

static inline void drawRect(int x, int y, int w, int h, uint32_t color) {
    for (int dy = 0; dy < h; ++dy) {
        int py = y + dy;
        if (py < 0 || py >= SCREEN_H) continue;
        for (int dx = 0; dx < w; ++dx) {
            int px = x + dx;
            if (px >= 0 && px < SCREEN_W) {
                s_pixels[py * SCREEN_W + px] = color;
            }
        }
    }
}

static inline bool isKeyDown(int key) {
    if (key >= 0 && key < 256) return s_keys[key];
    return false;
}

static inline bool isKeyPressed(int key) {
    if (key >= 0 && key < 256) return s_keys[key] && !s_prevKeys[key];
    return false;
}

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
}
static inline uint8_t getR(uint32_t c) { return (c >> 16) & 0xFF; }
static inline uint8_t getG(uint32_t c) { return (c >> 8) & 0xFF; }
static inline uint8_t getB(uint32_t c) { return c & 0xFF; }

static inline uint32_t multColor(uint32_t base, float lr, float lg, float lb) {
    uint8_t r = static_cast<uint8_t>(std::min(255.0f, getR(base) * lr));
    uint8_t g = static_cast<uint8_t>(std::min(255.0f, getG(base) * lg));
    uint8_t b = static_cast<uint8_t>(std::min(255.0f, getB(base) * lb));
    return rgb(r, g, b);
}

// sprite'y na siatce 16x16 (kazdy "piksel" projektu to PX x PX pikseli ekranu)
static inline void spr(int px, int py, int x, int y, int w, int h, uint32_t c) {
    drawRect(px + x * PX, py + y * PX, w * PX, h * PX, c);
}

static void drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    if (steps == 0) { setPixel(x0, y0, color); return; }
    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        setPixel(static_cast<int>(std::lround(x0 + (x1 - x0) * t)), static_cast<int>(std::lround(y0 + (y1 - y0) * t)), color);
    }
}

static void fillCircle(int cx, int cy, int r, uint32_t color) {
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) setPixel(cx + dx, cy + dy, color);
        }
    }
}

static uint32_t hash2(int x, int y) {
    return (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u);
}
static uint32_t lcgNext(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

// ---------------------------------------------------------------------
//  CZCIONKA 5x7 (z polskimi znakami budowanymi z akcentów)
// ---------------------------------------------------------------------
static const uint8_t FONT_5X7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x5F, 0x00, 0x00},
    {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},
    {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50},
    {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1C, 0x00},
    {0x14, 0x08, 0x3E, 0x08, 0x14},
    {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00},
    {0x08, 0x08, 0x08, 0x08, 0x08},
    {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E},
    {0x00, 0x42, 0x7F, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30},
    {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E},
    {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00},
    {0x14, 0x14, 0x14, 0x14, 0x14},
    {0x00, 0x41, 0x22, 0x14, 0x08},
    {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E},
    {0x7E, 0x11, 0x11, 0x11, 0x7E},
    {0x7F, 0x49, 0x49, 0x49, 0x36},
    {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C},
    {0x7F, 0x49, 0x49, 0x49, 0x41},
    {0x7F, 0x09, 0x09, 0x09, 0x01},
    {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F},
    {0x00, 0x41, 0x7F, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3F, 0x01},
    {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40},
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    {0x7F, 0x04, 0x08, 0x10, 0x7F},
    {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06},
    {0x3E, 0x41, 0x51, 0x21, 0x5E},
    {0x7F, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01},
    {0x3F, 0x40, 0x40, 0x40, 0x3F},
    {0x1F, 0x20, 0x40, 0x20, 0x1F},
    {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07},
    {0x61, 0x51, 0x49, 0x45, 0x43},
    {0x00, 0x7F, 0x41, 0x41, 0x00},
    {0x02, 0x04, 0x08, 0x10, 0x20},
    {0x00, 0x41, 0x41, 0x7F, 0x00},
    {0x04, 0x02, 0x01, 0x02, 0x04},
    {0x40, 0x40, 0x40, 0x40, 0x40},
    {0x00, 0x01, 0x02, 0x04, 0x00},
    {0x20, 0x54, 0x54, 0x54, 0x78},
    {0x7F, 0x48, 0x44, 0x44, 0x38},
    {0x38, 0x44, 0x44, 0x44, 0x20},
    {0x38, 0x44, 0x44, 0x48, 0x7F},
    {0x38, 0x54, 0x54, 0x54, 0x18},
    {0x08, 0x7E, 0x09, 0x01, 0x02},
    {0x0C, 0x52, 0x52, 0x52, 0x3E},
    {0x7F, 0x08, 0x04, 0x04, 0x78},
    {0x00, 0x44, 0x7D, 0x40, 0x00},
    {0x20, 0x40, 0x44, 0x3D, 0x00},
    {0x7F, 0x10, 0x28, 0x44, 0x00},
    {0x00, 0x41, 0x7F, 0x40, 0x00},
    {0x7C, 0x04, 0x18, 0x04, 0x78},
    {0x7C, 0x08, 0x04, 0x04, 0x78},
    {0x38, 0x44, 0x44, 0x44, 0x38},
    {0x7C, 0x14, 0x14, 0x14, 0x08},
    {0x08, 0x14, 0x14, 0x18, 0x7C},
    {0x7C, 0x08, 0x04, 0x04, 0x08},
    {0x48, 0x54, 0x54, 0x54, 0x20},
    {0x04, 0x3F, 0x44, 0x40, 0x20},
    {0x3C, 0x40, 0x40, 0x20, 0x7C},
    {0x1C, 0x20, 0x40, 0x20, 0x1C},
    {0x3C, 0x40, 0x30, 0x40, 0x3C},
    {0x44, 0x28, 0x10, 0x28, 0x44},
    {0x0C, 0x50, 0x50, 0x50, 0x3C},
    {0x44, 0x64, 0x54, 0x4C, 0x44},
    {0x00, 0x08, 0x36, 0x41, 0x00},
    {0x00, 0x00, 0x7F, 0x00, 0x00},
    {0x00, 0x41, 0x36, 0x08, 0x00},
    {0x08, 0x08, 0x2A, 0x1C, 0x08}
};

struct Glyph {
    char base;
    int accent;   // 0 brak, 1 kreska (ć ń ó ś ź), 2 kropka (ż), 3 ogonek (ą ę), 4 przekreślenie (ł)
};

static Glyph foldCodepoint(uint32_t cp) {
    if (cp < 128) return { static_cast<char>(cp), 0 };
    switch (cp) {
        case 0x0105: return { 'a', 3 };  case 0x0104: return { 'A', 3 };
        case 0x0107: return { 'c', 1 };  case 0x0106: return { 'C', 1 };
        case 0x0119: return { 'e', 3 };  case 0x0118: return { 'E', 3 };
        case 0x0142: return { 'l', 4 };  case 0x0141: return { 'L', 4 };
        case 0x0144: return { 'n', 1 };  case 0x0143: return { 'N', 1 };
        case 0x00F3: return { 'o', 1 };  case 0x00D3: return { 'O', 1 };
        case 0x015B: return { 's', 1 };  case 0x015A: return { 'S', 1 };
        case 0x017A: return { 'z', 1 };  case 0x0179: return { 'Z', 1 };
        case 0x017C: return { 'z', 2 };  case 0x017B: return { 'Z', 2 };
        case 0x2013: case 0x2014: return { '-', 0 };
        case 0x201E: case 0x201D: case 0x00AB: case 0x00BB: return { '"', 0 };
        case 0x2026: return { '.', 0 };
        default: return { '?', 0 };
    }
}

static std::vector<uint32_t> decodeUtf8(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        int extra = 0;
        if (c >= 0xF0) { cp = c & 0x07; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        }
        out.push_back(cp);
    }
    return out;
}

static int textLen(const std::string& s) {
    return static_cast<int>(decodeUtf8(s).size());
}

static void drawGlyph(int x, int y, Glyph g, uint32_t color, int scale) {
    char c = g.base;
    if (c < 32 || c > 126) c = '?';
    const uint8_t* glyph = FONT_5X7[c - 32];
    for (int col = 0; col < 5; ++col) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; ++row) {
            if ((bits >> row) & 1) {
                if (scale == 1) {
                    setPixel(x + col, y + row, color);
                } else {
                    drawRect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
    bool upper = (c >= 'A' && c <= 'Z');
    auto dot = [&](int col, int row) { drawRect(x + col * scale, y + row * scale, scale, scale, color); };
    switch (g.accent) {
        case 1:
        if (upper) { dot(3, -2); dot(2, -1); } else { dot(3, 0); dot(2, 1); }
        break;
        case 2:
        if (upper) dot(2, -2); else dot(2, 0);
        break;
        case 3:
        dot(4, 7); dot(4, 8);
        break;
        case 4:
        if (upper) { dot(0, 4); dot(1, 3); dot(2, 2); } else { dot(1, 4); dot(2, 3); dot(3, 2); }
        break;
        default: break;
    }
}

static void drawString(int x, int y, const std::string& str, uint32_t color, int scale = 1, bool shadow = true, int maxCp = -1) {
    std::vector<uint32_t> cps = decodeUtf8(str);
    int cx = x;
    int count = 0;
    for (uint32_t cp : cps) {
        if (maxCp >= 0 && count >= maxCp) break;
        ++count;
        if (cp == '\n') {
            y += 9 * scale;
            cx = x;
            continue;
        }
        Glyph g = foldCodepoint(cp);
        if (shadow) {
            drawGlyph(cx + scale, y + scale, g, 0x000000, scale);
        }
        drawGlyph(cx, y, g, color, scale);
        cx += 6 * scale;
    }
}

static void drawStringCentered(int cxMid, int y, const std::string& str, uint32_t color, int scale = 1, bool shadow = true) {
    drawString(cxMid - textLen(str) * 6 * scale / 2, y, str, color, scale, shadow);
}

// zawijanie tekstu po slowach (liczy znaki, nie bajty UTF-8); '\n' = twardy podział
static std::vector<std::string> wrapText(const std::string& text, int maxChars) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        int lineLen = 0;
        size_t i = 0;
        while (i <= para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            int wl = textLen(word);
            if (lineLen > 0 && lineLen + 1 + wl > maxChars) {
                lines.push_back(line);
                line.clear();
                lineLen = 0;
            }
            if (lineLen > 0) { line += ' '; ++lineLen; }
            line += word;
            lineLen += wl;
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

static std::vector<std::string> splitParagraphs(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        out.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

// ---------------------------------------------------------------------
//  TREŚĆ: DZIENNIK (fabuła, historia, demonologia, szyfr)
// ---------------------------------------------------------------------
enum LoreId : int {
    LORE_INTRO, LORE_ALTAR, LORE_SIGIL, LORE_DESK, LORE_DOOR,
    LORE_WINDOW, LORE_MICHAEL, LORE_SAECULUM, LORE_MAROZIA,
    LORE_GOETIA, LORE_PAIMON, LORE_CHURCH, LORE_CAESAR,
    LORE_COUNT
};

struct LoreEntry {
    const char* category;
    const char* title;
    const char* text;
};

static const LoreEntry LORE[LORE_COUNT] = {
    { "Fabuła", "Zlecenie Kurii",
      "Kuria zleciła ci inwentaryzację opuszczonego kościoła św. Szymona. W starym spisie figuruje jedna pozycja, której nikt nigdy nie widział: Liber Iudiciorum, Księga Wyroków.\nNa marginesie ktoś dopisał: „Nie pod ołtarzem. Pod znakiem.”" },
    { "Fabuła", "Karta z ołtarza",
      "„Ostatnia msza. Trzy świece płonęły, gdy zamykaliśmy księgę. Kto chce zejść, niech szuka kręgu, w którym stawiano pierwszy ołtarz.\nStrażnik ciągnie do mroku. Trzymaj się światła.”" },
    { "Fabuła", "Krąg w nawie",
      "Pod wytartym kręgiem z gwiazdą ukryty był drewniany luk z żelaznym kółkiem. Droga w dół jest otwarta." },
    { "Fabuła", "Notatki skryby Anselma",
      "„Księgę zamknąłem za drzwiami z pieczęcią. Napis na nich zaszyfrowałem Cezarem: każdą literę przesunąłem w alfabecie o tyle miejsc, ile świec płonęło przy ołtarzu podczas ostatniej mszy.\nZapisane imię należy do pierwszej z matek mrocznego wieku.”" },
    { "Fabuła", "Pieczęć złamana",
      "Pieczęć ustąpiła po odczytaniu imienia TEODORA. Za drzwiami czeka sala z piedestałem." },
    { "Historia", "Witraż: Sąd Ostateczny",
      "Wśród potępionych ktoś rozpoznał postać w papieskiej tiarze, a obok niej dwie kobiety. Pod spodem zachował się napis: SAECULUM OBSCURUM, wiek ciemny." },
    { "Historia", "Witraż: Archanioł Michał",
      "Michał waży dusze. W szalce potępionych leży księga, być może ta, której szukasz." },
    { "Historia", "Saeculum obscurum",
      "Saeculum obscurum (ok. 904-964) to okres, w którym papiestwo pozostawało pod silnym wpływem rzymskich rodów arystokratycznych, zwłaszcza Teofylaktów.\nDawniej nazywano go „pornokracją” (gr. pórne - nierządnica, krátos - władza), czyli rządami kobiet. Pojęcie utrwaliło się w historiografii XIX wieku i opiera na wrogich relacjach, m.in. kronice Liutpranda z Cremony. Współcześni historycy traktują te przekazy ostrożnie." },
    { "Historia", "Teodora i Marozia",
      "Teodora Starsza i jej córka Marozia należały do najpotężniejszych osób w średniowiecznym Rzymie. Marozia nosiła tytuł senatrix.\nWedług tradycji jej syn został papieżem Janem XI (931-935). Wiele szczegółów pochodzi z tendencyjnych źródeł, więc historycy spierają się, ile jest w nich prawdy." },
    { "Demonologia", "Ars Goetia",
      "Ars Goetia to pierwsza część grymuaru Lemegeton (Mniejszy Klucz Salomona), zbioru z XVII wieku. Zawiera katalog 72 duchów.\nCzęść opisów wzoruje się na wcześniejszym dziele Johanna Weyera, Pseudomonarchia Daemonum (1577). To literatura grymuarowa, a nie nauka Kościoła." },
    { "Demonologia", "Paimon",
      "W Ars Goetia Paimon jest jednym z królów, nauczycielem sztuk i nauk, ukazywanym jako mężczyzna na dromaderze.\nW tej opowieści imię nosi Strażnik archiwum: nie zabija, tylko pilnuje, by prawda nie wyszła na światło nieprzygotowana." },
    { "Demonologia", "Demony w teologii",
      "Teologowie scholastyczni, jak Tomasz z Akwinu, opisywali demony jako upadłe anioły, które zachowały rozum, ale utraciły łaskę.\nPraktyki grymuarowe Kościół potępiał jako zabobon i herezję, a oskarżenie o konszachty z demonami bywało w historii narzędziem politycznej walki." },
    { "Szyfr", "Szyfr Cezara",
      "Szyfr Cezara to szyfr podstawieniowy: każdą literę zastępuje się inną, położoną o stałą liczbę miejsc dalej w alfabecie.\nWedług Swetoniusza Juliusz Cezar używał go w korespondencji wojskowej, przesuwając litery o trzy miejsca." },
};

static const char* WHISPERS[] = {
    "...Teodora...", "...zapisz...", "...nie oglądaj się...", "...Paimon wie...",
    "...ciemność też czyta...", "...wyrok...", "...kto czyta, ten sądzi...",
};
static constexpr int WHISPER_COUNT = 7;

// ---------------------------------------------------------------------
//  POZIOMY, ŚWIATŁA, INTERAKCJE
// ---------------------------------------------------------------------
enum class Act : uint8_t {
    None, WindowSaeculum, WindowMichael, Altar, Sigil,
    ShelfSaeculum, ShelfMarozia, ShelfGoetia, ShelfPaimon, ShelfChurch, ShelfCaesar,
    Desk, Stairs, SealedDoor, Pedestal,
};

enum class LightCond : uint8_t { Always, DoorOpen, DoorSealed };

struct Light {
    float x, y;            // piksele swiata
    float radius;          // piksele
    float r, g, b;         // barwa
    float strength;
    bool safe;             // swiatlo chroni rozum i odstrasza Strażnika
    bool flame;            // migocze i ma plomien
    LightCond cond;
    float seed;
};

struct Level {
    std::string name;
    DungeonMap map;
    std::vector<Light> lights;
    std::unordered_map<int, Act> acts;
    std::unordered_map<int, bool> seen;
    int floorStyle = 0;
    bool hasGuardian = false;
    float startX = 1.5f;
    float startY = 1.5f;
    float guardX = 0.0f;
    float guardY = 0.0f;
};

static inline int cellKey(int x, int y) { return y * 64 + x; }

struct GameFlags {
    bool altar = false;
    bool trap = false;
    bool arrived = false;
    bool desk = false;
    bool door = false;
    int ending = 0;   // 0 brak, 1 zachowana Księga, 2 spalona
};

static GameFsm g_fsm;
static Player g_player;
static Guardian g_guardian;
static Level g_levels[2];
static int g_level = 0;
static GameFlags g_flags;
static std::vector<int> g_journal;
static std::vector<Particle> g_particles;
static std::vector<Particle> g_motes;

static float g_gameTime = 0.0f;
static float g_shakeTimer = 0.0f;
static int g_shakeX = 0;
static int g_shakeY = 0;
static float g_camX = 0.0f;
static float g_camY = 0.0f;

static void addLight(Level& L, float tx, float ty, float yoff, float radiusTiles, float strength,
                     float r, float g, float b, bool safe, bool flame, LightCond cond) {
    Light l;
    l.x = tx * TILE_SIZE + TILE_SIZE / 2;
    l.y = ty * TILE_SIZE + yoff;
    l.radius = radiusTiles * TILE_SIZE;
    l.r = r; l.g = g; l.b = b;
    l.strength = strength;
    l.safe = safe;
    l.flame = flame;
    l.cond = cond;
    l.seed = tx * 7.0f + ty;
    L.lights.push_back(l);
}

static void addStand(Level& L, int tx, int ty, LightCond cond = LightCond::Always) {
    tileSet(L.map, tx, ty, TileType::Stand);
    addLight(L, static_cast<float>(tx), static_cast<float>(ty), 6.0f, 4.6f, 1.0f, 1.0f, 0.78f, 0.50f, true, true, cond);
}

static Level buildChurch() {
    Level L;
    L.name = "NAWA KOŚCIOŁA ŚW. SZYMONA";
    L.map = DungeonMap(24, 15, static_cast<int32_t>(TileType::Floor));
    L.floorStyle = 0;
    L.startX = 11.5f; L.startY = 13.5f;
    DungeonMap& m = L.map;
    for (int x = 0; x < 24; ++x) { tileSet(m, x, 0, TileType::Wall); tileSet(m, x, 14, TileType::Wall); }
    for (int y = 0; y < 15; ++y) { tileSet(m, 0, y, TileType::Wall); tileSet(m, 23, y, TileType::Wall); }

    // witraże w północnej ścianie (+ kolorowe plamy światła na posadzce)
    const int winX[6] = { 3, 7, 11, 12, 16, 20 };
    const float winTint[5][3] = { { 0.55f, 0.32f, 1.0f }, { 1.0f, 0.25f, 0.35f }, { 0.30f, 0.50f, 1.0f }, { 1.0f, 0.80f, 0.35f }, { 0.30f, 0.90f, 0.60f } };
    for (int i = 0; i < 6; ++i) {
        tileSet(m, winX[i], 0, TileType::Window);
        const float* t = winTint[winX[i] % 5];
        addLight(L, static_cast<float>(winX[i]), 1.0f, 0.0f, 3.2f, 0.30f, t[0], t[1], t[2], false, false, LightCond::Always);
    }

    // ołtarz i trzy świece
    for (int x = 10; x <= 12; ++x) {
        tileSet(m, x, 3, TileType::Altar);
        tileSet(m, x, 2, TileType::AltarCandle);
        addLight(L, static_cast<float>(x), 2.0f, 8.0f, 3.8f, 1.0f, 1.0f, 0.78f, 0.50f, true, true, LightCond::Always);
        L.acts[cellKey(x, 3)] = Act::Altar;
    }

    // stojaki, ławy, gruz
    addStand(L, 9, 4); addStand(L, 14, 4); addStand(L, 3, 12); addStand(L, 20, 12);
    for (int y = 6; y <= 10; y += 2) {
        for (int x = 3; x <= 8; ++x) tileSet(m, x, y, TileType::Pew);
        for (int x = 15; x <= 20; ++x) tileSet(m, x, y, TileType::Pew);
    }
    tileSet(m, 6, 8, TileType::Floor);
    tileSet(m, 17, 10, TileType::Floor);
    const int rub[5][2] = { { 2, 13 }, { 21, 13 }, { 1, 9 }, { 22, 5 }, { 17, 2 } };
    for (auto& r : rub) tileSet(m, r[0], r[1], TileType::Rubble);

    // krąg z gwiazdą (ukryty luk)
    tileSet(m, 11, 8, TileType::Sigil);
    L.acts[cellKey(11, 8)] = Act::Sigil;
    L.acts[cellKey(7, 0)] = Act::WindowSaeculum;
    L.acts[cellKey(16, 0)] = Act::WindowMichael;
    return L;
}

static Level buildArchive() {
    Level L;
    L.name = "ARCHIWUM POD NAWĄ";
    L.map = DungeonMap(28, 18, static_cast<int32_t>(TileType::Floor));
    L.floorStyle = 1;
    L.hasGuardian = true;
    L.startX = 3.5f; L.startY = 15.5f;
    L.guardX = 14.5f; L.guardY = 12.5f;
    DungeonMap& m = L.map;
    for (int x = 0; x < 28; ++x) { tileSet(m, x, 0, TileType::Wall); tileSet(m, x, 17, TileType::Wall); }
    for (int y = 0; y < 18; ++y) { tileSet(m, 0, y, TileType::Wall); tileSet(m, 27, y, TileType::Wall); }

    // komnata Księgi
    for (int y = 1; y <= 9; ++y) tileSet(m, 19, y, TileType::Wall);
    for (int x = 19; x <= 26; ++x) tileSet(m, x, 9, TileType::Wall);
    tileSet(m, 19, 5, TileType::SealedDoor);
    tileSet(m, 23, 3, TileType::Pedestal);

    // regały
    for (int x = 2; x <= 17; ++x) tileSet(m, x, 1, TileType::Shelf);
    for (int x = 3; x <= 8; ++x) { tileSet(m, x, 4, TileType::Shelf); tileSet(m, x, 8, TileType::Shelf); }
    for (int x = 11; x <= 16; ++x) { tileSet(m, x, 4, TileType::Shelf); tileSet(m, x, 8, TileType::Shelf); }
    tileSet(m, 2, 15, TileType::Stairs);
    tileSet(m, 22, 13, TileType::Desk);
    const int rub[4][2] = { { 24, 16 }, { 26, 12 }, { 18, 16 }, { 1, 3 } };
    for (auto& r : rub) tileSet(m, r[0], r[1], TileType::Rubble);

    // światła w hallu, światła w komnacie (po otwarciu), poświata pieczęci
    const int hall[5][2] = { { 10, 6 }, { 10, 12 }, { 17, 13 }, { 4, 15 }, { 21, 12 } };
    for (auto& h : hall) addStand(L, h[0], h[1]);
    const int cham[4][2] = { { 21, 2 }, { 25, 2 }, { 21, 7 }, { 25, 7 } };
    for (auto& c : cham) addStand(L, c[0], c[1], LightCond::DoorOpen);
    addLight(L, 23.0f, 3.0f, 12.0f, 2.8f, 0.9f, 1.0f, 0.85f, 0.45f, true, false, LightCond::DoorOpen);
    addLight(L, 19.0f, 5.0f, 16.0f, 2.6f, 0.55f, 0.62f, 0.36f, 1.0f, false, false, LightCond::DoorSealed);

    // półki z wiedzą
    L.acts[cellKey(4, 4)] = Act::ShelfSaeculum;
    L.acts[cellKey(13, 4)] = Act::ShelfMarozia;
    L.acts[cellKey(5, 8)] = Act::ShelfGoetia;
    L.acts[cellKey(12, 8)] = Act::ShelfPaimon;
    L.acts[cellKey(9, 1)] = Act::ShelfChurch;
    L.acts[cellKey(15, 1)] = Act::ShelfCaesar;
    L.acts[cellKey(22, 13)] = Act::Desk;
    L.acts[cellKey(2, 15)] = Act::Stairs;
    L.acts[cellKey(19, 5)] = Act::SealedDoor;
    L.acts[cellKey(23, 3)] = Act::Pedestal;
    return L;
}

[[nodiscard]] static bool lightActive(const Light& l) {
    switch (l.cond) {
        case LightCond::DoorOpen: return g_flags.door;
        case LightCond::DoorSealed: return !g_flags.door;
        default: return true;
    }
}

[[nodiscard]] static bool nearSafeLight(float tx, float ty, float factor) {
    float px = tx * TILE_SIZE;
    float py = ty * TILE_SIZE;
    for (const auto& l : g_levels[g_level].lights) {
        if (!l.safe || !lightActive(l)) continue;
        if (std::hypot(px - l.x, py - l.y) < l.radius * factor) return true;
    }
    return false;
}

static bool isSolidAt(float fx, float fy) {
    const DungeonMap& m = g_levels[g_level].map;
    int tx = static_cast<int>(std::floor(fx));
    int ty = static_cast<int>(std::floor(fy));
    if (!m.inBounds(tx, ty)) return true;
    return tileSolid(tileGet(m, tx, ty));
}

static void emitParticles(float x, float y, uint32_t color, int count, float speedMult = 1.0f) {
    for (int i = 0; i < count; ++i) {
        float angle = static_cast<float>(rand() % 628) / 100.0f;
        float speed = (1.0f + static_cast<float>(rand() % 25) / 10.0f) * speedMult;
        Particle p;
        p.setX(x);
        p.setY(y);
        p.setVx(std::cos(angle) * speed);
        p.setVy(std::sin(angle) * speed);
        p.setColor(color);
        p.setLife(0.4f + static_cast<float>(rand() % 30) / 100.0f);
        p.setMaxLife(p.getLife());
        p.setSize(2.0f + static_cast<float>(rand() % 2));
        g_particles.push_back(p);
    }
}

// ---------------------------------------------------------------------
//  GRAFIKA PROCEDURALNA (kafelki i sprite'y rysowane kodem)
// ---------------------------------------------------------------------
static void drawStoneWall(int px, int py) {
    for (int dy = 0; dy < TILE_SIZE; ++dy) {
        int rowInBrick = dy % 8;
        bool isMortarH = (rowInBrick == 0);
        int brickRow = dy / 8;
        int xOffset = (brickRow % 2 == 0) ? 0 : 8;

        for (int dx = 0; dx < TILE_SIZE; ++dx) {
            int colInBrick = (dx + xOffset) % 16;
            bool isMortarV = (colInBrick == 0);

            uint32_t base;
            if (isMortarH || isMortarV) {
                base = rgb(23, 20, 31);
            } else if (rowInBrick == 1) {
                base = rgb(72, 64, 88);
            } else if (rowInBrick == 7) {
                base = rgb(28, 24, 37);
            } else {
                int noise = ((dx * 13 + dy * 17) % 7) - 3;
                base = rgb(47 + noise, 42 + noise, 59 + noise);
            }
            setPixel(px + dx, py + dy, base);
        }
    }
}

static void drawFloor(int px, int py, int tx, int ty, int style) {
    uint32_t h = hash2(tx, ty);
    for (int dy = 0; dy < TILE_SIZE; ++dy) {
        for (int dx = 0; dx < TILE_SIZE; ++dx) {
            uint32_t base;
            if (dx == 0 || dy == 0) {
                base = style == 0 ? rgb(56, 51, 66) : rgb(43, 40, 56);
            } else if (dx == TILE_SIZE - 1 || dy == TILE_SIZE - 1) {
                base = style == 0 ? rgb(23, 21, 29) : rgb(18, 16, 26);
            } else {
                int speck = ((dx * 7 + dy * 11 + tx * 3 + ty * 5) % 5);
                base = style == 0 ? rgb(43 + speck * 2, 40 + speck * 2, 51 + speck * 2)
                                  : rgb(32 + speck * 2, 30 + speck * 2, 41 + speck * 2);
            }
            setPixel(px + dx, py + dy, base);
        }
    }
    if (style == 0 && h % 6 == 0) {                   // pęknięcie płyty
        drawLine(px + 6, py + 8, px + 14, py + 18, rgb(20, 17, 26));
        drawLine(px + 14, py + 18, px + 17, py + 26, rgb(20, 17, 26));
    }
    if (style == 1 && h % 5 == 1) {                   // mech
        spr(px, py, 2, 11, 3, 1, rgb(34, 64, 58));
        spr(px, py, 3, 12, 2, 1, rgb(34, 64, 58));
    }
}

static void drawWindowTile(int px, int py, int tx) {
    drawStoneWall(px, py);
    spr(px, py, 2, 1, 10, 14, rgb(13, 10, 20));
    static const uint32_t cols[5] = { rgb(91, 42, 134), rgb(163, 32, 58), rgb(42, 79, 156), rgb(201, 162, 75), rgb(47, 122, 90) };
    for (int yy = 0; yy < 4; ++yy) {
        for (int xx = 0; xx < 3; ++xx) {
            spr(px, py, 3 + xx * 3, 2 + yy * 3, 2, 2, cols[(xx * 2 + yy * 3 + tx) % 5]);
        }
    }
}

static void drawPew(int px, int py) {
    spr(px, py, 1, 3, 14, 3, rgb(74, 48, 34)); spr(px, py, 1, 3, 14, 1, rgb(93, 61, 43)); spr(px, py, 1, 6, 14, 1, rgb(43, 27, 20));
    spr(px, py, 1, 8, 14, 5, rgb(61, 38, 25)); spr(px, py, 1, 8, 14, 1, rgb(93, 61, 43)); spr(px, py, 1, 13, 14, 1, rgb(43, 27, 20));
    spr(px, py, 2, 13, 2, 2, rgb(43, 27, 20)); spr(px, py, 12, 13, 2, 2, rgb(43, 27, 20));
}

static void drawAltar(int px, int py) {
    spr(px, py, 0, 4, 16, 12, rgb(75, 70, 88)); spr(px, py, 0, 4, 16, 2, rgb(95, 90, 112)); spr(px, py, 0, 14, 16, 2, rgb(42, 38, 54));
    spr(px, py, 1, 6, 14, 7, rgb(90, 22, 48)); spr(px, py, 1, 6, 14, 1, rgb(201, 162, 75)); spr(px, py, 1, 12, 14, 1, rgb(201, 162, 75));
    spr(px, py, 7, 7, 2, 5, rgb(201, 162, 75)); spr(px, py, 5, 8, 6, 1, rgb(201, 162, 75));
}

static void drawAltarCandle(int px, int py) {
    spr(px, py, 5, 12, 6, 3, rgb(59, 53, 71)); spr(px, py, 7, 6, 2, 7, rgb(232, 220, 192));
    spr(px, py, 8, 6, 1, 7, rgb(184, 169, 138)); spr(px, py, 8, 5, 1, 1, rgb(26, 22, 18));
}

static void drawStand(int px, int py) {
    spr(px, py, 7, 5, 2, 9, rgb(42, 38, 48)); spr(px, py, 4, 14, 8, 2, rgb(42, 38, 48)); spr(px, py, 5, 12, 6, 2, rgb(59, 53, 71));
    spr(px, py, 5, 4, 6, 2, rgb(42, 38, 48)); spr(px, py, 7, 1, 2, 4, rgb(232, 220, 192)); spr(px, py, 8, 1, 1, 4, rgb(184, 169, 138));
}

static void drawRubble(int px, int py, int tx, int ty) {
    uint32_t s = hash2(tx, ty) + 5u;
    static const uint32_t cols[3] = { rgb(58, 53, 68), rgb(44, 40, 54), rgb(72, 66, 90) };
    for (int i = 0; i < 9; ++i) {
        int w = 3 + static_cast<int>(lcgNext(s) % 4);
        int h = 2 + static_cast<int>(lcgNext(s) % 3);
        int x = static_cast<int>(lcgNext(s) % static_cast<uint32_t>(16 - w));
        int y = 4 + static_cast<int>(lcgNext(s) % static_cast<uint32_t>(12 - h));
        uint32_t c = cols[lcgNext(s) % 3];
        spr(px, py, x, y, w, h, c);
        spr(px, py, x, y, w, 1, multColor(c, 1.4f, 1.4f, 1.4f));
    }
}

static void drawSigil(int px, int py) {
    float pulse = 0.65f + 0.35f * std::sin(g_gameTime * 2.0f);
    uint32_t col = rgb(static_cast<uint8_t>(106 * pulse + 30), static_cast<uint8_t>(69 * pulse + 20), static_cast<uint8_t>(160 * pulse + 40));
    int cx = px + TILE_SIZE / 2, cy = py + TILE_SIZE / 2;
    const int R = 13;
    for (int a = 0; a < 360; a += 3) {
        float ang = a * 3.14159f / 180.0f;
        setPixel(cx + static_cast<int>(std::lround(std::cos(ang) * R)), cy + static_cast<int>(std::lround(std::sin(ang) * R)), col);
    }
    int vx[5], vy[5];
    for (int i = 0; i < 5; ++i) {
        float ang = -3.14159f / 2.0f + i * 3.14159f * 4.0f / 5.0f;
        vx[i] = cx + static_cast<int>(std::lround(std::cos(ang) * R));
        vy[i] = cy + static_cast<int>(std::lround(std::sin(ang) * R));
    }
    for (int i = 0; i < 5; ++i) {
        int j = (i + 1) % 5;
        drawLine(vx[i], vy[i], vx[j], vy[j], col);
    }
}

static void drawTrap(int px, int py) {
    spr(px, py, 1, 1, 14, 14, rgb(20, 16, 28)); spr(px, py, 2, 2, 12, 12, rgb(74, 48, 34));
    spr(px, py, 2, 6, 12, 1, rgb(43, 27, 20)); spr(px, py, 2, 10, 12, 1, rgb(43, 27, 20)); spr(px, py, 2, 2, 12, 1, rgb(93, 61, 43));
    spr(px, py, 7, 7, 2, 2, rgb(138, 138, 153)); spr(px, py, 7, 7, 1, 1, rgb(201, 162, 75));
}

static void drawShelf(int px, int py, int tx, int ty, bool unread) {
    spr(px, py, 0, 0, 16, 16, rgb(42, 27, 20));
    uint32_t s = (hash2(tx, ty) % 6u) * 7919u + 101u;
    static const uint32_t cols[6] = { rgb(91, 42, 42), rgb(42, 59, 91), rgb(59, 91, 58), rgb(107, 90, 42), rgb(74, 42, 91), rgb(91, 74, 58) };
    const int rows[3] = { 1, 6, 11 };
    for (int row : rows) {
        int x = 1;
        while (x < 15) {
            int w = 1 + static_cast<int>(lcgNext(s) % 2);
            int h = 2 + static_cast<int>(lcgNext(s) % 3);
            uint32_t c = cols[lcgNext(s) % 6];
            spr(px, py, x, row + 4 - h, std::min(w, 15 - x), h, c);
            x += w;
        }
        spr(px, py, 0, row + 4, 16, 1, rgb(74, 48, 34));
    }
    if (unread && std::sin(g_gameTime * 4.0f + tx) > 0.3f) {
        spr(px, py, 2 + static_cast<int>(hash2(tx, ty) % 12u), 5, 1, 1, rgb(255, 210, 122));
    }
}

static void drawDesk(int px, int py, bool unread) {
    spr(px, py, 1, 5, 14, 7, rgb(74, 48, 34)); spr(px, py, 1, 5, 14, 1, rgb(93, 61, 43));
    spr(px, py, 2, 12, 2, 3, rgb(43, 27, 20)); spr(px, py, 12, 12, 2, 3, rgb(43, 27, 20));
    spr(px, py, 4, 6, 4, 4, rgb(217, 207, 192)); spr(px, py, 8, 6, 4, 4, rgb(217, 207, 192)); spr(px, py, 8, 6, 1, 4, rgb(138, 127, 106));
    spr(px, py, 12, 3, 1, 4, rgb(232, 220, 192));
    if (unread && std::sin(g_gameTime * 4.0f) > 0.3f) spr(px, py, 9, 6, 1, 1, rgb(255, 210, 122));
}

static void drawStairs(int px, int py) {
    spr(px, py, 0, 0, 16, 16, rgb(13, 10, 20));
    for (int i = 0; i < 5; ++i) {
        spr(px, py, 0, 2 + i * 3, 16, 3, rgb(60 + i * 14, 55 + i * 12, 75 + i * 14));
        spr(px, py, 0, 4 + i * 3, 16, 1, rgb(23, 20, 31));
    }
}

static void drawSealedDoor(int px, int py) {
    spr(px, py, 0, 0, 16, 16, rgb(47, 42, 59)); spr(px, py, 2, 0, 12, 16, rgb(13, 10, 20)); spr(px, py, 3, 1, 10, 15, rgb(59, 42, 32));
    spr(px, py, 3, 6, 10, 1, rgb(43, 27, 20)); spr(px, py, 3, 12, 10, 1, rgb(43, 27, 20));
    spr(px, py, 3, 3, 10, 2, rgb(27, 27, 36)); spr(px, py, 3, 9, 10, 2, rgb(27, 27, 36));
    spr(px, py, 4, 3, 1, 2, rgb(138, 138, 153)); spr(px, py, 11, 3, 1, 2, rgb(138, 138, 153));
    float pulse = 0.55f + 0.35f * std::sin(g_gameTime * 3.0f);
    fillCircle(px + 16, py + 16, 7, rgb(static_cast<uint8_t>(154 * pulse), static_cast<uint8_t>(92 * pulse), static_cast<uint8_t>(255 * pulse)));
    fillCircle(px + 16, py + 16, 3, rgb(230, 210, 255));
}

static void drawOpenDoor(int px, int py) {
    spr(px, py, 0, 0, 2, 16, rgb(47, 42, 59)); spr(px, py, 14, 0, 2, 16, rgb(47, 42, 59));
    spr(px, py, 0, 0, 16, 2, rgb(47, 42, 59)); spr(px, py, 0, 0, 16, 1, rgb(70, 63, 85));
}

static void drawPedestal(int px, int py) {
    spr(px, py, 3, 8, 10, 7, rgb(59, 53, 71)); spr(px, py, 3, 8, 10, 1, rgb(75, 70, 88)); spr(px, py, 2, 14, 12, 2, rgb(42, 38, 54));
    spr(px, py, 4, 4, 8, 5, rgb(90, 22, 48)); spr(px, py, 4, 4, 8, 1, rgb(201, 162, 75)); spr(px, py, 4, 8, 8, 1, rgb(201, 162, 75)); spr(px, py, 7, 5, 2, 3, rgb(201, 162, 75));
}

static void drawTile(TileType t, int px, int py, int tx, int ty, int style) {
    switch (t) {
        case TileType::Wall: drawStoneWall(px, py); return;
        case TileType::Window: drawWindowTile(px, py, tx); return;
        default: break;
    }
    drawFloor(px, py, tx, ty, style);
    switch (t) {
        case TileType::Pew: drawPew(px, py); break;
        case TileType::Altar: drawAltar(px, py); break;
        case TileType::AltarCandle: drawAltarCandle(px, py); break;
        case TileType::Stand: drawStand(px, py); break;
        case TileType::Rubble: drawRubble(px, py, tx, ty); break;
        case TileType::Sigil: drawSigil(px, py); break;
        case TileType::Trap: drawTrap(px, py); break;
        case TileType::Shelf: drawShelf(px, py, tx, ty, g_levels[g_level].acts.count(cellKey(tx, ty)) && !g_levels[g_level].seen.count(cellKey(tx, ty))); break;
        case TileType::Desk: drawDesk(px, py, !g_levels[g_level].seen.count(cellKey(tx, ty))); break;
        case TileType::Stairs: drawStairs(px, py); break;
        case TileType::SealedDoor: drawSealedDoor(px, py); break;
        case TileType::OpenDoor: drawOpenDoor(px, py); break;
        case TileType::Pedestal: drawPedestal(px, py); break;
        default: break;
    }
}

static void drawFlame(int sx, int sy, float seed) {
    float f = std::sin(g_gameTime * 13.0f + seed) + std::sin(g_gameTime * 7.3f + seed * 2.0f);
    int h = f > 0.8f ? 8 : 6;
    drawRect(sx - 2, sy - h, 4, h, rgb(255, 122, 42));
    drawRect(sx - 2, sy - h + 2, 2, h - 2, rgb(255, 210, 122));
    drawRect(sx - 1, sy - h + 4, 1, 2, rgb(255, 255, 220));
}

static void drawPlayerSprite(int sx, int sy, int facing, bool moving, float anim) {
    int f = moving ? (static_cast<int>(anim * 8.0f) % 2) : 0;
    int y = sy + (moving && f ? -PX : 0);
    drawRect(sx + 4 * PX, sy + 14 * PX, 8 * PX, 2 * PX, rgb(8, 6, 12));       // cień pod postacią
    spr(sx, y, 5, 7, 6, 7, rgb(58, 47, 79)); spr(sx, y, 5, 7, 1, 7, rgb(77, 63, 102));   // szata
    spr(sx, y, 4, 2, 8, 6, rgb(42, 33, 58));                                           // kaptur
    if (facing == 0) { spr(sx, y, 6, 5, 4, 3, rgb(224, 199, 166)); spr(sx, y, 6, 6, 1, 1, rgb(21, 13, 28)); spr(sx, y, 9, 6, 1, 1, rgb(21, 13, 28)); }
    else if (facing == 2) { spr(sx, y, 4, 5, 3, 3, rgb(224, 199, 166)); spr(sx, y, 4, 6, 1, 1, rgb(21, 13, 28)); }
    else if (facing == 3) { spr(sx, y, 9, 5, 3, 3, rgb(224, 199, 166)); spr(sx, y, 11, 6, 1, 1, rgb(21, 13, 28)); }
    spr(sx, sy, 5 + (f ? 1 : 0), 14, 2, 2, rgb(18, 12, 24)); spr(sx, sy, 9 - (f ? 1 : 0), 14, 2, 2, rgb(18, 12, 24));
    int lx = facing == 2 ? 2 : (facing == 3 ? 12 : 11);                               // lampa
    spr(sx, y, lx, 8, 2, 1, rgb(168, 106, 28)); spr(sx, y, lx, 9, 2, 2, rgb(255, 207, 106));
}

static void drawGuardianBody(int sx, int sy) {
    int sw = static_cast<int>(std::lround(std::sin(g_gameTime * 3.0f) * PX));
    uint32_t c = rgb(7, 3, 12);
    drawRect(sx + 4 * PX + sw, sy + 3 * PX, 8 * PX, 11 * PX, c);
    drawRect(sx + 3 * PX + sw, sy + 6 * PX, 10 * PX, 8 * PX, c);
    for (int i = 0; i < 4; ++i) {
        int extra = (std::sin(g_gameTime * 5.0f + i * 1.7f) > 0.0f) ? 2 : 0;
        drawRect(sx + (3 + i * 3) * PX + sw, sy + 14 * PX, 2 * PX, (2 + extra) * PX, c);
    }
}

static void drawGuardianEyes(int sx, int sy, float alpha) {
    int sw = static_cast<int>(std::lround(std::sin(g_gameTime * 3.0f) * PX));
    uint32_t c = multColor(rgb(255, 46, 85), alpha, alpha, alpha);
    drawRect(sx + 5 * PX + sw, sy + 7 * PX, 2 * PX, PX, c);
    drawRect(sx + 9 * PX + sw, sy + 7 * PX, 2 * PX, PX, c);
}

// ---------------------------------------------------------------------
//  OŚWIETLENIE (mapa światła na poziomie piksela) I EFEKTY EKRANU
// ---------------------------------------------------------------------
static float g_lightBuf[SCREEN_W * SCREEN_H * 3];
static float g_vig[SCREEN_W * SCREEN_H];

static void buildVignette() {
    for (int y = 0; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            float nx = (x - SCREEN_W / 2.0f) / (SCREEN_W / 2.0f);
            float ny = (y - SCREEN_H / 2.0f) / (SCREEN_H / 2.0f);
            float r = std::sqrt(nx * nx + ny * ny);
            float v = clamp((r - 0.55f) / 0.85f, 0.0f, 1.0f);
            g_vig[y * SCREEN_W + x] = v * v;
        }
    }
}

static void accumulateLight(float lx, float ly, float rad, float cr, float cg, float cb, float str, int camX, int camY) {
    int sx = static_cast<int>(lx) - camX;
    int sy = static_cast<int>(ly) - camY + HUD_H;
    int ir = static_cast<int>(rad);
    int x0 = std::max(0, sx - ir), x1 = std::min(SCREEN_W - 1, sx + ir);
    int y0 = std::max(HUD_H, sy - ir), y1 = std::min(SCREEN_H - 1, sy + ir);
    float r2 = rad * rad;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float dx = static_cast<float>(x - sx), dy = static_cast<float>(y - sy);
            float d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            float a = 1.0f - std::sqrt(d2) / rad;
            a = a * a * (3.0f - 2.0f * a) * str;
            float* p = &g_lightBuf[(y * SCREEN_W + x) * 3];
            p[0] += a * cr; p[1] += a * cg; p[2] += a * cb;
        }
    }
}

static void applyLighting(int camX, int camY) {
    const float ambR = 0.17f, ambG = 0.15f, ambB = 0.24f;
    for (int i = HUD_H * SCREEN_W; i < SCREEN_W * SCREEN_H; ++i) {
        g_lightBuf[i * 3] = ambR; g_lightBuf[i * 3 + 1] = ambG; g_lightBuf[i * 3 + 2] = ambB;
    }
    const Level& L = g_levels[g_level];
    for (const auto& l : L.lights) {
        if (!lightActive(l)) continue;
        float fk = 1.0f;
        if (l.flame) fk = 1.0f + std::sin(g_gameTime * 7.0f + l.seed) * 0.03f + std::sin(g_gameTime * 13.0f + l.seed * 2.0f) * 0.02f;
        accumulateLight(l.x, l.y, l.radius * fk, l.r, l.g, l.b, l.strength, camX, camY);
    }
    // lampa bohaterki: im niższy rozum, tym słabsze światło
    float sanityK = 0.6f + 0.4f * g_player.getSanity() / 100.0f;
    float pr = 4.6f * TILE_SIZE * sanityK * (1.0f + std::sin(g_gameTime * 9.0f) * 0.02f);
    accumulateLight(g_player.getX() * TILE_SIZE, g_player.getY() * TILE_SIZE - 6.0f, pr, 1.0f, 0.90f, 0.72f, 1.0f, camX, camY);

    for (int y = HUD_H; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            int i = y * SCREEN_W + x;
            const float* p = &g_lightBuf[i * 3];
            s_pixels[i] = multColor(s_pixels[i], std::min(1.5f, p[0]), std::min(1.5f, p[1]), std::min(1.5f, p[2]));
        }
    }
}

static void applyVignette(float insane) {
    for (int y = HUD_H; y < SCREEN_H; ++y) {
        for (int x = 0; x < SCREEN_W; ++x) {
            int i = y * SCREEN_W + x;
            float v = g_vig[i];
            uint32_t c = s_pixels[i];
            float k = 1.0f - 0.55f * v;
            int r = static_cast<int>(getR(c) * k + 140.0f * v * insane);
            int g = static_cast<int>(getG(c) * k);
            int b = static_cast<int>(getB(c) * k + 20.0f * v * insane);
            s_pixels[i] = rgb(static_cast<uint8_t>(std::min(255, r)), static_cast<uint8_t>(g), static_cast<uint8_t>(std::min(255, b)));
        }
    }
}

static void applyFade(float a) {
    float k = 1.0f - clamp(a, 0.0f, 1.0f);
    for (int i = 0; i < SCREEN_W * SCREEN_H; ++i) s_pixels[i] = multColor(s_pixels[i], k, k, k);
}

// ---------------------------------------------------------------------
//  STAN GRY: dialogi, dziennik, szyfr, wybór, przejścia
// ---------------------------------------------------------------------
enum class DialogAction : uint8_t { None, OpenCipher, OpenChoice, DigTrap, Descend, Faint };
enum class FadeAction : uint8_t { None, Level, Faint, Ending };

struct DialogState {
    std::string title;
    std::vector<std::string> pages;
    int page = 0;
    float chars = 0.0f;
    DialogAction after = DialogAction::None;
};
static DialogState g_dlg;

struct FadeState {
    float a = 0.0f;
    int dir = 0;
    FadeAction action = FadeAction::None;
    int level = 0;
    float sx = 0.0f, sy = 0.0f;
};
static FadeState g_fade;

struct ToastState { std::string text; float timer = 0.0f; };
static ToastState g_toast;

struct WhisperState { std::string text; int x = 0, y = 0; float life = 0.0f; };
static WhisperState g_whisper;
static float g_whisperAt = 8.0f;

static int g_cipherShift = 0;
static const char* CIPHER_TEXT = "WHRGRUD";
static constexpr int CIPHER_ANSWER = 3;
static int g_choiceSel = 0;
static int g_journalSel = 0;

static void toast(const std::string& text) {
    g_toast.text = text;
    g_toast.timer = 2.6f;
}

static void addJournal(int id) {
    if (std::find(g_journal.begin(), g_journal.end(), id) != g_journal.end()) return;
    g_journal.push_back(id);
    toast(std::string("NOWY WPIS: ") + LORE[id].title);
}

static void say(const std::string& title, const std::vector<std::string>& pages, DialogAction after = DialogAction::None) {
    g_dlg.title = title;
    g_dlg.pages = pages;
    g_dlg.page = 0;
    g_dlg.chars = 0.0f;
    g_dlg.after = after;
    g_fsm.transition(GameFsmEvent::OpenDialog);
}

static void startFade(FadeAction action, int level = 0, float sx = 0.0f, float sy = 0.0f) {
    g_fade.action = action;
    g_fade.level = level;
    g_fade.sx = sx;
    g_fade.sy = sy;
    g_fade.dir = 1;
}

static void snapCamera();

static void loadLevel(int idx, float sx, float sy) {
    g_level = idx;
    const Level& L = g_levels[idx];
    g_player.setX(sx);
    g_player.setY(sy);
    g_player.setFacing(1);
    if (L.hasGuardian) {
        g_guardian.setX(L.guardX);
        g_guardian.setY(L.guardY);
        g_guardian.setTargetX(L.guardX);
        g_guardian.setTargetY(L.guardY);
        g_guardian.setWait(2.0f);
    }
    g_particles.clear();
    snapCamera();
}

static void resetGame() {
    g_levels[0] = buildChurch();
    g_levels[1] = buildArchive();
    g_flags = GameFlags();
    g_journal.clear();
    g_particles.clear();
    g_fade = FadeState();
    g_toast = ToastState();
    g_whisper = WhisperState();
    g_whisperAt = 8.0f;
    g_journalSel = 0;
    g_player.setSanity(100.0f);
    loadLevel(0, g_levels[0].startX, g_levels[0].startY);
}

static std::string objective() {
    if (g_flags.ending) return "";
    if (g_level == 0) {
        if (!g_flags.altar) return "Zbadaj ołtarz.";
        if (!g_flags.trap) return "Znajdź krąg z gwiazdą na posadzce nawy.";
        return "Zejdź przez luk do podziemi.";
    }
    if (!g_flags.door) return g_flags.desk ? "Odszyfruj inskrypcję na zapieczętowanych drzwiach." : "Przeszukaj archiwum. Szukaj notatek i drzwi z pieczęcią.";
    return "Podejdź do piedestału w sali za drzwiami.";
}

static void readShelf(int tx, int ty, int loreId, const char* flavor) {
    g_levels[g_level].seen[cellKey(tx, ty)] = true;
    addJournal(loreId);
    std::vector<std::string> pages = { flavor };
    for (auto& p : splitParagraphs(LORE[loreId].text)) pages.push_back(p);
    say(LORE[loreId].title, pages);
}

static void openCipher() {
    g_cipherShift = 0;
    g_fsm.transition(GameFsmEvent::OpenCipher);
}

static void submitCipher() {
    if (g_cipherShift == CIPHER_ANSWER) {
        g_fsm.transition(GameFsmEvent::CloseCipher);
        g_flags.door = true;
        Level& L = g_levels[1];
        tileSet(L.map, 19, 5, TileType::OpenDoor);
        L.acts.erase(cellKey(19, 5));
        emitParticles(19.5f * TILE_SIZE, 5.5f * TILE_SIZE, rgb(154, 92, 255), 30, 1.6f);
        addJournal(LORE_DOOR);
        say("Pieczęć", {
            "Pieczęć gaśnie. Odczytujesz imię: TEODORA.",
            "To imię Teodory Starszej, od której według kronik zaczęły się rządy rzymskich arystokratek nad Stolicą Apostolską. Drzwi uchylają się z jękiem. Za nimi zapalają się świece." });
    } else {
        g_player.setSanity(std::max(1.0f, g_player.getSanity() - 12.0f));
        g_shakeTimer = 0.5f;
        toast("Pieczęć syczy: to nie jest słowo.");
    }
}

static void interact(Act a) {
    Level& L = g_levels[g_level];
    switch (a) {
        case Act::WindowSaeculum:
        addJournal(LORE_WINDOW);
        say("Witraż", { "Sąd Ostateczny. Po lewej zbawieni, po prawej potępieni. Wśród potępionych rozpoznajesz postać w papieskiej tiarze i dwie kobiety u jej boku.",
                        "Ktoś zeskrobał twarze, ale nie napis: SAECULUM OBSCURUM." });
        break;
        case Act::WindowMichael:
        addJournal(LORE_MICHAEL);
        say("Witraż", { "Archanioł Michał waży dusze. W szalce potępionych leży księga." });
        break;
        case Act::Altar: {
            bool first = !g_flags.altar;
            g_flags.altar = true;
            addJournal(LORE_ALTAR);
            std::vector<std::string> pages;
            if (first) pages.push_back("Na kamiennej mensie leży karta pergaminu spięta woskiem. Pismo jest niedbałe, jakby ktoś pisał w pośpiechu.");
            for (auto& p : splitParagraphs(LORE[LORE_ALTAR].text)) pages.push_back(p);
            if (first) pages.push_back("Podnosisz wzrok. Za ołtarzem płoną trzy świece. Nikt tu nie był od dziesięcioleci.");
            say("Ołtarz", pages);
            break;
        }
        case Act::Sigil:
        if (g_flags.trap) {
            say("Luk", { "Drabina znika w ciemności. Z dołu wieje chłód i zapach starego papieru." }, DialogAction::Descend);
        } else if (!g_flags.altar) {
            say("Znak na posadzce", { "Wytarty krąg z gwiazdą wpisaną w okrąg. Wygląda jak zwykła ozdoba. Nie wiesz jeszcze, czego szukać." });
        } else {
            say("Krąg", { "Klękasz w kręgu. Płyta pod dłonią brzmi głucho: tam, w dole, jest pusto.",
                          "Wsuwasz palce w szczelinę i podważasz kamień. Pod spodem: drewniany luk z żelaznym kółkiem." }, DialogAction::DigTrap);
        }
        break;
        case Act::ShelfSaeculum: readShelf(4, 4, LORE_SAECULUM, "Wyciągasz zakurzony tom. Karty są kruche jak liście."); break;
        case Act::ShelfMarozia: readShelf(13, 4, LORE_MAROZIA, "Na grzbiecie wytłoczono herb rzymskiego rodu."); break;
        case Act::ShelfGoetia: readShelf(5, 8, LORE_GOETIA, "Ten tom stoi osobno, przewiązany sznurem. Sznur jest przerwany."); break;
        case Act::ShelfPaimon: readShelf(12, 8, LORE_PAIMON, "Wśród ksiąg leży kartka z wyrysowanym jeźdźcem na dromaderze."); break;
        case Act::ShelfChurch: readShelf(9, 1, LORE_CHURCH, "Tu Kościół trzymał własne opinie o ciemnych sprawach."); break;
        case Act::ShelfCaesar: readShelf(15, 1, LORE_CAESAR, "Traktat o tajnym piśmie. Na marginesie ktoś narysował koło z literami."); break;
        case Act::Desk: {
            bool first = !g_flags.desk;
            g_flags.desk = true;
            L.seen[cellKey(22, 13)] = true;
            addJournal(LORE_DESK);
            std::vector<std::string> pages;
            if (first) pages.push_back("Pióro wciąż leży na otwartej księdze. Atrament nie wysechł do końca. To niemożliwe.");
            for (auto& p : splitParagraphs(LORE[LORE_DESK].text)) pages.push_back(p);
            say("Biurko Anselma", pages);
            break;
        }
        case Act::Stairs:
        startFade(FadeAction::Level, 0, 11.5f, 9.5f);
        break;
        case Act::SealedDoor:
        say("Zapieczętowane drzwi", { "Na żelaznych okuciach jarzy się fioletowa pieczęć. Wokół niej wyryto litery, których nie da się odczytać wprost." }, DialogAction::OpenCipher);
        break;
        case Act::Pedestal:
        say("Piedestał", {
            "Księga jest zimna jak kamień, a okładka, mimo wieków, wilgotna.",
            "Na pierwszej karcie: „Liber Iudiciorum. Kto czyta, ten sądzi. Kto sądzi, ten odpowie.” Dalej spisy imion, układy rodów i listy wyroków wydanych w ciemności.",
            "Pornokracja nie była ani baśnią, ani czystą prawdą. Księga zapisuje ją tak, jak widzieli ją ci, którzy w niej żyli: z pogardą, ze strachem i z zazdrością.",
            "Z mroku za twoimi plecami odzywa się głos, który nie jest głosem: „Jestem Paimon, strażnik pamięci. Zapisz albo spal. Ale wybierz.”" }, DialogAction::OpenChoice);
        break;
        default: break;
    }
}

static void runDialogAction(DialogAction a) {
    switch (a) {
        case DialogAction::OpenCipher: openCipher(); break;
        case DialogAction::OpenChoice: g_choiceSel = 0; g_fsm.transition(GameFsmEvent::OpenChoice); break;
        case DialogAction::DigTrap:
        tileSet(g_levels[0].map, 11, 8, TileType::Trap);
        g_flags.trap = true;
        addJournal(LORE_SIGIL);
        break;
        case DialogAction::Descend: startFade(FadeAction::Level, 1, g_levels[1].startX, g_levels[1].startY); break;
        case DialogAction::Faint: startFade(FadeAction::Faint); break;
        default: break;
    }
}

static void advanceDialog() {
    const std::string& page = g_dlg.pages[g_dlg.page];
    if (g_dlg.chars < static_cast<float>(textLen(page))) {
        g_dlg.chars = static_cast<float>(textLen(page));
        return;
    }
    if (g_dlg.page < static_cast<int>(g_dlg.pages.size()) - 1) {
        ++g_dlg.page;
        g_dlg.chars = 0.0f;
        return;
    }
    DialogAction act = g_dlg.after;
    g_dlg.after = DialogAction::None;
    g_fsm.transition(GameFsmEvent::CloseDialog);
    runDialogAction(act);
}

static void finishGame(int kind) {
    g_flags.ending = kind;
    startFade(FadeAction::Ending);
}

static void updateFade() {
    if (g_fade.dir == 0) return;
    g_fade.a += g_fade.dir * 0.05f;
    if (g_fade.dir > 0 && g_fade.a >= 1.0f) {
        g_fade.a = 1.0f;
        g_fade.dir = -1;
        FadeAction act = g_fade.action;
        g_fade.action = FadeAction::None;
        switch (act) {
            case FadeAction::Level:
            loadLevel(g_fade.level, g_fade.sx, g_fade.sy);
            if (g_level == 1 && !g_flags.arrived) {
                g_flags.arrived = true;
                say("Archiwum", { "Pod nawą ciągnie się sala pełna regałów, znacznie większa niż sam kościół. Powietrze pachnie woskiem i kurzem.",
                                  "Gdzieś między półkami coś się porusza. Zostań w świetle." });
            }
            break;
            case FadeAction::Faint:
            g_player.setSanity(60.0f);
            loadLevel(g_level, g_levels[g_level].startX, g_levels[g_level].startY);
            break;
            case FadeAction::Ending:
            g_fsm.transition(GameFsmEvent::Finish);
            break;
            default: break;
        }
    } else if (g_fade.dir < 0 && g_fade.a <= 0.0f) {
        g_fade.a = 0.0f;
        g_fade.dir = 0;
    }
}

// ---------------------------------------------------------------------
//  LOGIKA: ruch, kolizje, Strażnik, Rozum, kamera
// ---------------------------------------------------------------------
static bool guardianOk(float x, float y) {
    const Level& L = g_levels[g_level];
    int tx = static_cast<int>(x), ty = static_cast<int>(y);
    if (tx < 1 || ty < 1 || tx >= L.map.width() - 1 || ty >= L.map.height() - 1) return false;
    if (tx >= 19 && ty <= 9) return false;     // Strażnik nie wchodzi do komnaty Księgi
    return true;
}

static void pickWaypoint() {
    const Level& L = g_levels[g_level];
    for (int i = 0; i < 30; ++i) {
        float tx = 1.0f + static_cast<float>(rand() % (L.map.width() - 2)) + 0.5f;
        float ty = 1.0f + static_cast<float>(rand() % (L.map.height() - 2)) + 0.5f;
        if (guardianOk(tx, ty)) {
            g_guardian.setTargetX(tx);
            g_guardian.setTargetY(ty);
            return;
        }
    }
}

// AI: w ciemności goni bohaterkę, przed światłem ucieka, poza tym błądzi
static void updateGuardian() {
    Guardian& s = g_guardian;
    float dx = g_player.getX() - s.getX();
    float dy = g_player.getY() - s.getY();
    float d = std::hypot(dx, dy);
    bool safe = nearSafeLight(g_player.getX(), g_player.getY(), 0.65f);
    float mx = 0.0f, my = 0.0f, sp = 0.0f;
    if (!safe && d < 6.9f) {
        mx = dx; my = dy; sp = 0.032f;
    } else if (safe && d < 4.4f) {
        mx = -dx; my = -dy; sp = 0.030f;
    } else {
        s.setWait(s.getWait() - 0.016f);
        if (s.getWait() <= 0.0f || std::hypot(s.getTargetX() - s.getX(), s.getTargetY() - s.getY()) < 0.25f) {
            pickWaypoint();
            s.setWait(3.0f + static_cast<float>(rand() % 40) / 10.0f);
        }
        mx = s.getTargetX() - s.getX();
        my = s.getTargetY() - s.getY();
        sp = 0.014f;
    }
    float l = std::hypot(mx, my);
    if (l < 0.0001f) l = 1.0f;
    float nx = s.getX() + mx / l * sp;
    float ny = s.getY() + my / l * sp;
    if (guardianOk(nx, ny)) { s.setX(nx); s.setY(ny); } else pickWaypoint();
}

struct Target { bool valid; int tx, ty; Act act; };

static Target findTarget() {
    const Level& L = g_levels[g_level];
    int tx = static_cast<int>(g_player.getX());
    int ty = static_cast<int>(g_player.getY());
    auto it = L.acts.find(cellKey(tx, ty));
    if (it != L.acts.end()) return { true, tx, ty, it->second };
    static const int DX[4] = { 0, 0, -1, 1 };
    static const int DY[4] = { 1, -1, 0, 0 };
    int f = g_player.getFacing();
    int fx = static_cast<int>(std::floor(g_player.getX() + DX[f] * 0.75f));
    int fy = static_cast<int>(std::floor(g_player.getY() + DY[f] * 0.75f));
    it = L.acts.find(cellKey(fx, fy));
    if (it != L.acts.end()) return { true, fx, fy, it->second };
    return { false, 0, 0, Act::None };
}

static void snapCamera() {
    const Level& L = g_levels[g_level];
    float mapW = static_cast<float>(L.map.width() * TILE_SIZE);
    float mapH = static_cast<float>(L.map.height() * TILE_SIZE);
    float tx = g_player.getX() * TILE_SIZE - SCREEN_W / 2.0f;
    float ty = g_player.getY() * TILE_SIZE - VIEW_H / 2.0f;
    tx = (mapW <= SCREEN_W) ? -(SCREEN_W - mapW) / 2.0f : clamp(tx, 0.0f, mapW - SCREEN_W);
    ty = (mapH <= VIEW_H) ? -(VIEW_H - mapH) / 2.0f : clamp(ty, 0.0f, mapH - VIEW_H);
    g_camX = tx;
    g_camY = ty;
}

static void followCamera() {
    float ox = g_camX, oy = g_camY;
    snapCamera();
    float tx = g_camX, ty = g_camY;
    g_camX = ox + (tx - ox) * 0.12f;
    g_camY = oy + (ty - oy) * 0.12f;
}

static void showWhisper() {
    g_whisper.text = WHISPERS[rand() % WHISPER_COUNT];
    g_whisper.x = 80 + rand() % 420;
    g_whisper.y = HUD_H + 60 + rand() % 360;
    g_whisper.life = 1.8f;
    g_whisperAt = g_gameTime + 5.0f + static_cast<float>(rand() % 60) / 10.0f;
}

static void updateWorld() {
    float moveX = 0.0f, moveY = 0.0f;
    if (isKeyDown('W') || isKeyDown(38)) { moveY -= 1.0f; g_player.setFacing(1); }
    if (isKeyDown('S') || isKeyDown(40)) { moveY += 1.0f; g_player.setFacing(0); }
    if (isKeyDown('A') || isKeyDown(37)) { moveX -= 1.0f; g_player.setFacing(2); }
    if (isKeyDown('D') || isKeyDown(39)) { moveX += 1.0f; g_player.setFacing(3); }
    g_player.setMoving(moveX != 0.0f || moveY != 0.0f);
    if (moveX != 0.0f && moveY != 0.0f) { moveX *= 0.7071f; moveY *= 0.7071f; }

    const float speed = 0.07f;
    const float radius = 0.28f;
    float curX = g_player.getX(), curY = g_player.getY();
    float nextX = curX + moveX * speed, nextY = curY + moveY * speed;
    if (!isSolidAt(nextX + radius, curY + 0.2f) && !isSolidAt(nextX - radius, curY + 0.2f) &&
        !isSolidAt(nextX + radius, curY - 0.1f) && !isSolidAt(nextX - radius, curY - 0.1f)) curX = nextX;
    if (!isSolidAt(curX + radius, nextY + 0.2f) && !isSolidAt(curX - radius, nextY + 0.2f) &&
        !isSolidAt(curX + radius, nextY - 0.1f) && !isSolidAt(curX - radius, nextY - 0.1f)) curY = nextY;
    g_player.setX(curX);
    g_player.setY(curY);
    if (g_player.getMoving()) g_player.setAnim(g_player.getAnim() + 0.016f);

    if (g_levels[g_level].hasGuardian) updateGuardian();

    // system "Rozumu": światło leczy, ciemność i Strażnik niszczą
    bool lit = nearSafeLight(curX, curY, 0.65f);
    float dS = g_levels[g_level].hasGuardian ? std::hypot(curX - g_guardian.getX(), curY - g_guardian.getY()) : 1000.0f;
    float sanity = g_player.getSanity();
    if (lit && dS >= 2.5f) {
        sanity = std::min(100.0f, sanity + 5.0f * 0.016f);
    } else {
        float drain = 0.0f;
        if (!lit) drain += 0.9f;
        if (dS < 2.5f) { drain += 14.0f * (1.0f - dS / 2.5f) + 2.0f; g_shakeTimer = std::max(g_shakeTimer, 0.1f); }
        sanity = std::max(0.0f, sanity - drain * 0.016f);
    }
    g_player.setSanity(sanity);
    if (sanity <= 0.0f) {
        g_player.setSanity(1.0f);
        say("Szepty", { "Mrok zamyka się wokół ciebie. Budzisz się przy wejściu, z głową pełną cudzych głosów." }, DialogAction::Faint);
        return;
    }
    if (sanity < 55.0f && g_gameTime > g_whisperAt) showWhisper();
}

static void updateEffects() {
    if (g_shakeTimer > 0.0f) {
        g_shakeTimer -= 0.016f;
        g_shakeX = (rand() % 5) - 2;
        g_shakeY = (rand() % 5) - 2;
    } else {
        g_shakeX = 0;
        g_shakeY = 0;
    }
    for (size_t i = 0; i < g_particles.size();) {
        auto& p = g_particles[i];
        p.setX(p.getX() + p.getVx());
        p.setY(p.getY() + p.getVy());
        p.setLife(p.getLife() - 0.016f);
        if (p.getLife() <= 0.0f) g_particles.erase(g_particles.begin() + i); else ++i;
    }
    for (auto& m : g_motes) {
        m.setX(m.getX() + m.getVx() * 0.016f);
        m.setY(m.getY() + m.getVy() * 0.016f);
        if (m.getY() < HUD_H) m.setY(static_cast<float>(SCREEN_H));
        if (m.getX() < 0) m.setX(static_cast<float>(SCREEN_W));
        if (m.getX() > SCREEN_W) m.setX(0.0f);
    }
    if (g_toast.timer > 0.0f) g_toast.timer -= 0.016f;
    if (g_whisper.life > 0.0f) g_whisper.life -= 0.016f;
    if (g_dlg.page >= 0 && g_fsm.is(GameFsmState::Dialog)) g_dlg.chars += 1.4f;
}

// ---------------------------------------------------------------------
//  RENDEROWANIE SCENY I INTERFEJSU
// ---------------------------------------------------------------------
static const uint32_t GOLD = rgb(215, 175, 40);

static void drawPanel(int x, int y, int w, int h, uint32_t bg) {
    drawRect(x - 3, y - 3, w + 6, h + 6, rgb(59, 43, 18));
    drawRect(x - 1, y - 1, w + 2, h + 2, GOLD);
    drawRect(x, y, w, h, bg);
}

static void drawHud() {
    drawRect(0, 0, SCREEN_W, HUD_H, rgb(15, 18, 28));
    drawRect(0, HUD_H - 2, SCREEN_W, 2, GOLD);

    float san = g_player.getSanity();
    drawString(15, 9, "ROZUM", rgb(180, 150, 240), 1);
    drawRect(62, 8, 110, 12, rgb(24, 16, 40));
    float pct = clamp(san / 100.0f, 0.0f, 1.0f);
    uint32_t fill = san > 55.0f ? rgb(140, 95, 225) : (san > 25.0f ? rgb(200, 60, 120) : rgb(235, 30, 45));
    if (san <= 25.0f && std::sin(g_gameTime * 10.0f) > 0.0f) fill = rgb(255, 90, 100);
    drawRect(64, 10, static_cast<int>(106.0f * pct), 8, fill);
    drawRect(64, 10, static_cast<int>(106.0f * pct), 2, multColor(fill, 1.4f, 1.4f, 1.4f));

    drawString(200, 9, g_levels[g_level].name, GOLD, 1);
    drawString(620, 9, "WPISY: " + std::to_string(g_journal.size()) + "/" + std::to_string(LORE_COUNT) + "  [J]", rgb(180, 220, 255), 1);
    std::string obj = objective();
    if (!obj.empty()) drawString(15, 27, "CEL: " + obj, rgb(140, 180, 240), 1);
}

static void drawMinimap() {
    const Level& L = g_levels[g_level];
    int mw = L.map.width() * 4 + 4, mh = L.map.height() * 4 + 4;
    int mx = SCREEN_W - mw - 12, my = HUD_H + 10;
    drawRect(mx - 2, my - 2, mw + 4, mh + 4, GOLD);
    drawRect(mx, my, mw, mh, rgb(10, 12, 18));
    for (int y = 0; y < L.map.height(); ++y) {
        for (int x = 0; x < L.map.width(); ++x) {
            TileType t = tileGet(L.map, x, y);
            uint32_t c = rgb(20, 22, 32);
            if (t == TileType::Wall || t == TileType::Window) c = rgb(65, 70, 90);
            else if (t == TileType::SealedDoor) c = rgb(154, 92, 255);
            else if (t == TileType::OpenDoor) c = rgb(255, 180, 0);
            else if (t == TileType::Stairs || t == TileType::Trap) c = rgb(0, 255, 255);
            else if (t == TileType::Pedestal) c = rgb(255, 215, 0);
            else if (tileSolid(t)) c = rgb(40, 44, 60);
            drawRect(mx + 2 + x * 4, my + 2 + y * 4, 3, 3, c);
        }
    }
    if (L.hasGuardian && std::hypot(g_player.getX() - g_guardian.getX(), g_player.getY() - g_guardian.getY()) < 5.0f) {
        drawRect(mx + 2 + static_cast<int>(g_guardian.getX() * 4), my + 2 + static_cast<int>(g_guardian.getY() * 4), 3, 3, rgb(255, 30, 40));
    }
    drawRect(mx + 2 + static_cast<int>(g_player.getX() * 4), my + 2 + static_cast<int>(g_player.getY() * 4), 4, 4, rgb(0, 240, 255));
}

static void drawScene() {
    const Level& L = g_levels[g_level];
    clearScreen(rgb(0, 0, 0));
    int camX = static_cast<int>(std::floor(g_camX)) + g_shakeX;
    int camY = static_cast<int>(std::floor(g_camY)) + g_shakeY;

    int startTX = std::max(0, static_cast<int>(std::floor(static_cast<float>(camX) / TILE_SIZE)));
    int endTX = std::min(L.map.width(), (camX + SCREEN_W) / TILE_SIZE + 1);
    int startTY = std::max(0, static_cast<int>(std::floor(static_cast<float>(camY) / TILE_SIZE)));
    int endTY = std::min(L.map.height(), (camY + VIEW_H) / TILE_SIZE + 1);
    for (int ty = startTY; ty < endTY; ++ty) {
        for (int tx = startTX; tx < endTX; ++tx) {
            drawTile(tileGet(L.map, tx, ty), tx * TILE_SIZE - camX, ty * TILE_SIZE - camY + HUD_H, tx, ty, L.floorStyle);
        }
    }

    for (const auto& m : g_motes) {
        float a = 0.25f + 0.45f * std::fabs(std::sin(g_gameTime + m.getLife() * 6.0f));
        setPixel(static_cast<int>(m.getX()), static_cast<int>(m.getY()), multColor(rgb(255, 230, 190), a, a, a));
    }
    for (const auto& p : g_particles) {
        float k = p.getLife() / p.getMaxLife();
        int sz = static_cast<int>(p.getSize());
        drawRect(static_cast<int>(p.getX()) - camX, static_cast<int>(p.getY()) - camY + HUD_H, sz, sz, multColor(p.getColor(), k, k, k));
    }

    int gsx = 0, gsy = 0;
    if (L.hasGuardian) {
        gsx = static_cast<int>(g_guardian.getX() * TILE_SIZE) - 16 - camX;
        gsy = static_cast<int>(g_guardian.getY() * TILE_SIZE) - 24 - camY + HUD_H;
        drawGuardianBody(gsx, gsy);
    }
    int psx = static_cast<int>(g_player.getX() * TILE_SIZE) - 16 - camX;
    int psy = static_cast<int>(g_player.getY() * TILE_SIZE) - 24 - camY + HUD_H;
    drawPlayerSprite(psx, psy, g_player.getFacing(), g_player.getMoving(), g_player.getAnim());

    applyLighting(camX, camY);

    // elementy, które świecą same: płomienie, oczy Strażnika, znacznik interakcji
    for (const auto& l : L.lights) {
        if (l.flame && lightActive(l)) drawFlame(static_cast<int>(l.x) - camX, static_cast<int>(l.y) - camY + HUD_H + 4, l.seed);
    }
    if (L.hasGuardian) {
        float d = std::hypot(g_player.getX() - g_guardian.getX(), g_player.getY() - g_guardian.getY());
        drawGuardianEyes(gsx, gsy, clamp(1.1f - d / 8.0f, 0.0f, 1.0f));
    }
    if (g_fsm.is(GameFsmState::Playing) && g_fade.dir == 0) {
        Target t = findTarget();
        if (t.valid) {
            int sx = t.tx * TILE_SIZE - camX + TILE_SIZE / 2;
            int sy = t.ty * TILE_SIZE - camY + HUD_H - 10 + static_cast<int>(std::lround(std::sin(g_gameTime * 5.0f) * 2.0f));
            drawRect(sx - 9, sy - 8, 18, 14, rgb(15, 18, 28));
            drawRect(sx - 9, sy - 8, 18, 1, GOLD); drawRect(sx - 9, sy + 5, 18, 1, GOLD);
            drawRect(sx - 9, sy - 8, 1, 14, GOLD); drawRect(sx + 8, sy - 8, 1, 14, GOLD);
            drawString(sx - 3, sy - 5, "E", rgb(255, 215, 0), 1);
        }
    }

    float insane = clamp((60.0f - g_player.getSanity()) / 60.0f, 0.0f, 1.0f);
    applyVignette(insane * (0.65f + 0.2f * std::sin(g_gameTime * 4.0f)));

    drawHud();
    drawMinimap();

    if (g_toast.timer > 0.0f) {
        int w = textLen(g_toast.text) * 6 + 24;
        drawPanel(SCREEN_W / 2 - w / 2, HUD_H + 14, w, 22, rgb(20, 24, 38));
        drawStringCentered(SCREEN_W / 2, HUD_H + 21, g_toast.text, GOLD, 1);
    }
    if (g_whisper.life > 0.0f) {
        float k = clamp(g_whisper.life, 0.0f, 1.0f);
        drawString(g_whisper.x, g_whisper.y, g_whisper.text, multColor(rgb(215, 70, 105), k, k, k), 2);
    }
}

static void drawDialog() {
    const int x = 40, y = SCREEN_H - 196, w = 720, h = 176;
    drawPanel(x, y, w, h, rgb(14, 12, 24));
    drawString(x + 20, y + 14, g_dlg.title, GOLD, 2);
    drawRect(x + 20, y + 36, w - 40, 1, rgb(90, 70, 20));
    std::vector<std::string> lines = wrapText(g_dlg.pages[g_dlg.page], 56);
    int budget = static_cast<int>(g_dlg.chars);
    for (size_t i = 0; i < lines.size(); ++i) {
        int n = textLen(lines[i]);
        if (budget <= 0) break;
        drawString(x + 20, y + 48 + static_cast<int>(i) * 20, lines[i], rgb(220, 230, 245), 2, true, budget);
        budget -= n;
    }
    bool last = g_dlg.page == static_cast<int>(g_dlg.pages.size()) - 1;
    if (static_cast<int>(g_gameTime * 2.5f) % 2 == 0) {
        drawString(x + w - 76, y + h - 24, last ? "[E] OK" : "[E] >>", GOLD, 1);
    }
}

static void drawJournal() {
    clearScreen(rgb(10, 8, 18));
    drawRect(0, 0, SCREEN_W, 64, rgb(15, 18, 28));
    drawRect(0, 62, SCREEN_W, 2, GOLD);
    drawString(30, 18, "DZIENNIK ARCHIWISTKI", GOLD, 3);
    drawString(620, 26, "WPISY: " + std::to_string(g_journal.size()) + "/" + std::to_string(LORE_COUNT), rgb(180, 220, 255), 1);
    drawString(30, SCREEN_H - 22, "[GORA/DOL] WYBIERZ   [J / ESC] ZAMKNIJ", rgb(140, 150, 175), 1);
    if (g_journal.empty()) {
        drawStringCentered(SCREEN_W / 2, 280, "Jeszcze nic nie zapisano.", rgb(140, 150, 175), 2);
        return;
    }
    drawPanel(24, 84, 270, 480, rgb(16, 14, 26));
    for (size_t i = 0; i < g_journal.size(); ++i) {
        int yy = 98 + static_cast<int>(i) * 34;
        const LoreEntry& e = LORE[g_journal[i]];
        bool sel = static_cast<int>(i) == g_journalSel;
        if (sel) drawRect(30, yy - 6, 258, 30, rgb(52, 40, 18));
        drawString(38, yy - 2, e.category, rgb(163, 32, 58), 1);
        drawString(38, yy + 9, e.title, sel ? rgb(255, 225, 120) : rgb(200, 190, 175), 1);
    }
    const LoreEntry& e = LORE[g_journal[clamp(g_journalSel, 0, static_cast<int>(g_journal.size()) - 1)]];
    drawPanel(318, 84, 458, 480, rgb(16, 14, 26));
    drawString(340, 102, e.category, rgb(200, 60, 90), 1);
    drawString(340, 118, e.title, GOLD, 2);
    drawRect(340, 142, 414, 1, rgb(90, 70, 20));
    std::vector<std::string> lines = wrapText(e.text, 33);
    for (size_t i = 0; i < lines.size(); ++i) {
        drawString(340, 156 + static_cast<int>(i) * 20, lines[i], rgb(220, 225, 240), 2, false);
    }
}

static void drawCipher() {
    clearScreen(rgb(12, 8, 22));
    for (int y = 0; y < SCREEN_H; y += 40) drawRect(0, y, SCREEN_W, 1, rgb(20, 14, 34));
    for (int x = 0; x < SCREEN_W; x += 40) drawRect(x, 0, 1, SCREEN_H, rgb(20, 14, 34));
    drawStringCentered(SCREEN_W / 2, 40, "PIECZĘĆ ARCHIWUM", GOLD, 4);
    drawStringCentered(SCREEN_W / 2, 90, "INSKRYPCJA NA DRZWIACH", rgb(140, 150, 175), 1);

    float pulse = 0.7f + 0.3f * std::sin(g_gameTime * 3.0f);
    uint32_t glow = rgb(static_cast<uint8_t>(183 * pulse), static_cast<uint8_t>(156 * pulse), static_cast<uint8_t>(255 * pulse));
    std::string inscr;
    for (const char* c = CIPHER_TEXT; *c; ++c) { inscr += *c; inscr += ' '; }
    drawStringCentered(SCREEN_W / 2, 130, inscr, glow, 6);

    const int x0 = 88, step = 24;
    drawString(x0, 220, "ALFABET", rgb(140, 150, 175), 1);
    for (int i = 0; i < 26; ++i) drawString(x0 + i * step, 240, std::string(1, static_cast<char>('A' + i)), rgb(150, 140, 175), 2);
    drawString(x0, 282, "KOŁO (PRZESUNIĘTE)", rgb(140, 150, 175), 1);
    for (int i = 0; i < 26; ++i) drawString(x0 + i * step, 302, std::string(1, static_cast<char>('A' + ((i - g_cipherShift + 26) % 26))), GOLD, 2);
    drawStringCentered(SCREEN_W / 2, 350, "PRZESUNIĘCIE: " + std::to_string(g_cipherShift), rgb(220, 230, 245), 2);

    std::string dec;
    for (const char* c = CIPHER_TEXT; *c; ++c) { dec += static_cast<char>('A' + ((*c - 'A' - g_cipherShift + 26) % 26)); dec += ' '; }
    drawStringCentered(SCREEN_W / 2, 410, "ODCZYT", rgb(140, 150, 175), 1);
    drawStringCentered(SCREEN_W / 2, 430, dec, GOLD, 5);
    drawStringCentered(SCREEN_W / 2, 520, "[LEWO/PRAWO] ZMIEN PRZESUNIĘCIE    [ENTER] ZATWIERDŹ    [ESC] WYJDŹ", rgb(180, 220, 255), 1);
    drawStringCentered(SCREEN_W / 2, 545, "WSKAZÓWEK SZUKAJ W DZIENNIKU [J]", rgb(140, 150, 175), 1);
}

static void drawChoice() {
    clearScreen(rgb(10, 8, 18));
    drawPanel(130, 110, 540, 380, rgb(20, 14, 30));
    drawStringCentered(SCREEN_W / 2, 150, "KSIĘGA WYROKÓW", GOLD, 4);
    std::vector<std::string> lines = wrapText("Zachować wiedzę, która może zranić, czy zniszczyć ją, by nikogo nie zraniła?", 40);
    for (size_t i = 0; i < lines.size(); ++i) drawStringCentered(SCREEN_W / 2, 215 + static_cast<int>(i) * 20, lines[i], rgb(220, 230, 245), 2);
    for (int i = 0; i < 2; ++i) {
        int bx = 170 + i * 240;
        bool sel = (i == g_choiceSel);
        drawRect(bx, 330, 220, 70, sel ? GOLD : rgb(90, 70, 20));
        drawRect(bx + 2, 332, 216, 66, sel ? rgb(60, 46, 14) : rgb(20, 24, 38));
        drawStringCentered(bx + 110, 345, i == 0 ? "[1] ZACHOWAJ" : "[2] SPAL", sel ? rgb(255, 225, 120) : rgb(200, 190, 175), 2);
        drawStringCentered(bx + 110, 372, "KSIĘGĘ", sel ? rgb(255, 225, 120) : rgb(200, 190, 175), 2);
    }
    drawStringCentered(SCREEN_W / 2, 440, "[LEWO/PRAWO] WYBIERZ   [ENTER] POTWIERDŹ", rgb(140, 150, 175), 1);
}

static void drawGothicWindow(int cx, int top, int w, int h, int seed, float k) {
    static const uint32_t cols[5] = { rgb(91, 42, 134), rgb(163, 32, 58), rgb(42, 79, 156), rgb(201, 162, 75), rgb(47, 122, 90) };
    int r = w / 2;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int dx = x - r;
            if (y < r) {
                int dy = r - y;
                if (dx * dx + dy * dy > r * r) continue;
            }
            bool lead = (x % 20 < 3) || (y % 20 < 3) || x < 4 || x >= w - 4 || y > h - 5;
            uint32_t c = lead ? rgb(22, 18, 32) : cols[hash2(x / 20 + seed, y / 20) % 5];
            float shade = k * (0.8f + 0.2f * std::sin(g_gameTime * 0.8f + y * 0.02f));
            setPixel(cx - r + x, top + y, multColor(c, shade, shade, shade));
        }
    }
}

static void drawTitle() {
    clearScreen(rgb(10, 8, 18));
    for (int y = 0; y < SCREEN_H; y += 40) drawRect(0, y, SCREEN_W, 1, rgb(18, 14, 30));
    for (int x = 0; x < SCREEN_W; x += 40) drawRect(x, 0, 1, SCREEN_H, rgb(18, 14, 30));
    drawGothicWindow(110, 40, 120, 320, 1, 0.38f);
    drawGothicWindow(690, 40, 120, 320, 4, 0.38f);

    drawStringCentered(SCREEN_W / 2, 110, "ARCHIWUM", rgb(255, 215, 0), 7);
    drawStringCentered(SCREEN_W / 2, 185, "POD ŚWIĘTYM SZYMONEM", rgb(140, 100, 230), 3);
    drawStringCentered(SCREEN_W / 2, 230, "MROCZNA GRA PRZYGODOWA 2D", rgb(140, 180, 240), 1);

    drawRect(180, 290, 440, 90, rgb(20, 16, 34));
    drawRect(180, 290, 440, 1, GOLD); drawRect(180, 379, 440, 1, GOLD);
    drawString(200, 303, "[WASD / STRZAŁKI] RUCH     [E / SPACJA] DZIAŁAJ", rgb(220, 230, 245), 1);
    drawString(200, 323, "[J] DZIENNIK              [ESC] WYJŚCIE", rgb(220, 230, 245), 1);
    drawString(200, 349, "Światło chroni twój rozum. Ciemność karmi Strażnika.", rgb(190, 150, 230), 1);

    if (static_cast<int>(g_gameTime * 2.5f) % 2 == 0) {
        drawStringCentered(SCREEN_W / 2, 420, ">>> NACIŚNIJ DOWOLNY KLAWISZ <<<", rgb(0, 240, 255), 1);
    }
    drawPlayerSprite(300, 490, 3, false, 0.0f);
    drawGuardianBody(480, 490);
    drawGuardianEyes(480, 490, 0.5f + 0.5f * std::sin(g_gameTime * 2.0f));
    drawStand(380, 490);
    drawFlame(380 + 16, 490 + 6, 1.0f);
    drawString(10, SCREEN_H - 14, "C++17 / WinAPI / GDI - programowy renderer", rgb(70, 70, 95), 1, false);
}

static void drawEnding() {
    bool keep = (g_flags.ending == 1);
    clearScreen(keep ? rgb(12, 20, 26) : rgb(25, 10, 12));
    drawPanel(100, 70, 600, 460, keep ? rgb(18, 30, 38) : rgb(35, 15, 20));
    drawStringCentered(SCREEN_W / 2, 100, "ZAKOŃCZENIE", rgb(140, 150, 175), 1);
    drawStringCentered(SCREEN_W / 2, 125, keep ? "PAMIĘĆ" : "POPIÓŁ", keep ? rgb(0, 240, 255) : rgb(255, 80, 70), 6);
    std::string text = keep
    ? "Wynosisz Księgę na światło dnia. Nie jest ani świętym tekstem, ani trucizną: to źródło, które trzeba czytać krytycznie, porównywać z innymi i poddawać wątpliwości. Cień Paimona rozpływa się: strażnik pamięci nie jest już potrzebny, gdy pamięć trafia do ludzi."
    : "Płomień obejmuje pergamin. Ktoś szepcze: „Kto pali pamięć, sam zostaje zapisany.” Archiwum zapada się, a ty wychodzisz z pustymi rękami. Historia, której nikt nie przeczyta, zaczyna żyć jako legenda, a legendy bywają gorsze od prawdy.";
    std::vector<std::string> lines = wrapText(text, 44);
    for (size_t i = 0; i < lines.size(); ++i) drawString(136, 205 + static_cast<int>(i) * 20, lines[i], rgb(220, 230, 245), 2);
    drawString(136, 440, "WPISY W DZIENNIKU: " + std::to_string(g_journal.size()) + "/" + std::to_string(LORE_COUNT), GOLD, 2);
    if (static_cast<int>(g_gameTime * 2.5f) % 2 == 0) drawStringCentered(SCREEN_W / 2, 495, "[ENTER] ZAGRAJ PONOWNIE", rgb(255, 255, 255), 1);
}

// ---------------------------------------------------------------------
//  WEJŚCIE I GŁÓWNY KROK GRY
// ---------------------------------------------------------------------
static bool anyKeyPressed() {
    for (int k = 8; k < 256; ++k) {
        if (k == 16 || k == 17 || k == 18 || k == 27) continue;
        if (isKeyPressed(k)) return true;
    }
    return false;
}

static bool confirmPressed() {
    return isKeyPressed('E') || isKeyPressed(13) || isKeyPressed(32);
}

static void handleInput() {
    switch (g_fsm.getState()) {
        case GameFsmState::Title:
        if (isKeyPressed(27)) { s_running = false; return; }
        if (anyKeyPressed()) {
            g_fsm.transition(GameFsmEvent::Start);
            addJournal(LORE_INTRO);
            say("Prolog", {
                "Październik. Kościół św. Szymona stoi pusty od dziesięcioleci. Kuria wysłała cię, archiwistkę, z jednym zadaniem: odnaleźć zaginiony księgozbiór parafii.",
                "W inwentarzu jest tylko jedna pozycja, której nikt nigdy nie widział: Liber Iudiciorum, Księga Wyroków. Na marginesie ktoś dopisał: „Nie pod ołtarzem. Pod znakiem.”",
                "Drzwi zamknęły się za tobą same. Świece na ołtarzu wciąż się palą, choć nikt tu nie wchodził od dziesięcioleci." });
        }
        break;
        case GameFsmState::Playing: {
            if (confirmPressed()) {
                Target t = findTarget();
                if (t.valid) interact(t.act);
            } else if (isKeyPressed('J')) {
                g_journalSel = std::max(0, static_cast<int>(g_journal.size()) - 1);
                g_fsm.transition(GameFsmEvent::OpenJournal);
            }
            break;
        }
        case GameFsmState::Dialog:
        if (confirmPressed()) advanceDialog();
        break;
        case GameFsmState::Journal:
        if (isKeyPressed('J') || isKeyPressed(27)) g_fsm.transition(GameFsmEvent::CloseJournal);
        else if (isKeyPressed(40) || isKeyPressed('S')) g_journalSel = std::min(std::max(0, static_cast<int>(g_journal.size()) - 1), g_journalSel + 1);
        else if (isKeyPressed(38) || isKeyPressed('W')) g_journalSel = std::max(0, g_journalSel - 1);
        break;
        case GameFsmState::Cipher:
        if (isKeyPressed(37) || isKeyPressed('A')) g_cipherShift = (g_cipherShift + 25) % 26;
        else if (isKeyPressed(39) || isKeyPressed('D')) g_cipherShift = (g_cipherShift + 1) % 26;
        else if (isKeyPressed(13) || isKeyPressed(32)) submitCipher();
        else if (isKeyPressed(27)) g_fsm.transition(GameFsmEvent::CloseCipher);
        break;
        case GameFsmState::Choice:
        if (isKeyPressed('1')) finishGame(1);
        else if (isKeyPressed('2')) finishGame(2);
        else if (isKeyPressed(37) || isKeyPressed('A')) g_choiceSel = 0;
        else if (isKeyPressed(39) || isKeyPressed('D')) g_choiceSel = 1;
        else if (confirmPressed()) finishGame(g_choiceSel == 0 ? 1 : 2);
        break;
        case GameFsmState::Ending:
        if (isKeyPressed(13) || isKeyPressed(32)) {
            resetGame();
            g_fsm.transition(GameFsmEvent::Restart);
        }
        break;
    }
}

static void stepGame() {
    updateFade();
    if (g_fade.dir == 0) handleInput();   // podczas przejść (fade) wejście jest blokowane
    updateEffects();

    switch (g_fsm.getState()) {
        case GameFsmState::Title:
        drawTitle();
        break;
        case GameFsmState::Ending:
        drawEnding();
        break;
        case GameFsmState::Choice:
        drawChoice();
        break;
        case GameFsmState::Cipher:
        drawCipher();
        break;
        case GameFsmState::Journal:
        drawJournal();
        break;
        default:
        if (g_fsm.is(GameFsmState::Playing) && g_fade.dir == 0) updateWorld();
        followCamera();
        drawScene();
        if (g_fsm.is(GameFsmState::Dialog)) drawDialog();
        break;
    }
    if (g_fade.a > 0.0f) applyFade(g_fade.a);
}

void updateGame() {
    static bool s_initialized = false;
    if (!s_initialized) {
        srand(static_cast<unsigned int>(time(nullptr)));
        buildVignette();
        for (int i = 0; i < 50; ++i) {
            Particle m;
            m.setX(static_cast<float>(rand() % SCREEN_W));
            m.setY(static_cast<float>(HUD_H + rand() % VIEW_H));
            m.setVx((static_cast<float>(rand() % 100) - 50.0f) / 10.0f);
            m.setVy(-(static_cast<float>(rand() % 50) / 10.0f) - 1.0f);
            m.setLife(static_cast<float>(rand() % 100) / 100.0f);
            g_motes.push_back(m);
        }
        resetGame();
        s_initialized = true;
    }
    g_gameTime += 0.016f;
    stepGame();
    std::memcpy(s_prevKeys, s_keys, sizeof(s_keys));
}

// ---------------------------------------------------------------------
//  WARSTWA SYSTEMOWA (WinAPI + GDI)
// ---------------------------------------------------------------------
#if defined(_WIN32)
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
        case WM_DESTROY:
        s_running = false;
        PostQuitMessage(0);
        return 0;
        case WM_KEYDOWN:
        if (wParam < 256) s_keys[wParam] = true;
        return 0;
        case WM_KEYUP:
        if (wParam < 256) s_keys[wParam] = false;
        return 0;
        case WM_KILLFOCUS:
        std::memset(s_keys, 0, sizeof(s_keys));
        return 0;
        default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}
#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    #if defined(_WIN32)
    const char CLASS_NAME[] = "ArchiwumGameWindowClass";
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassEx(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE;
    RECT wr = {0, 0, SCREEN_W, SCREEN_H};
    AdjustWindowRect(&wr, style, FALSE);

    HWND hwnd = CreateWindowEx(
    0, CLASS_NAME, "Archiwum pod Swietym Szymonem",
    style,
    CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
    nullptr, nullptr, GetModuleHandle(nullptr), nullptr
    );

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = SCREEN_W;
    bmi.bmiHeader.biHeight = -SCREEN_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    MSG msg;
    while (s_running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) s_running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        updateGame();
        HDC hdc = GetDC(hwnd);
        StretchDIBits(hdc, 0, 0, SCREEN_W, SCREEN_H, 0, 0, SCREEN_W, SCREEN_H, s_pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(hwnd, hdc);
        Sleep(16);
    }
    #else
    for (int frame = 0; frame < 100 && s_running; ++frame) {
        updateGame();
    }
    #endif
    return 0;
}
