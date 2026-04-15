#pragma once

#include "display/lcd_display.h"

class MimiEmojiDisplay : public SpiLcdDisplay {
   public:
    MimiEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);

    virtual ~MimiEmojiDisplay() = default;
    virtual void SetStatus(const char* status) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;

   private:
    void InitializeMimiEmojis();
    void SetupPreviewImage();
};
