# TaskPin brand icon

![TaskPin icon](../TaskPinQt/assets/brand/taskpin-128.png)

The icon combines a screenshot/note outline, a diagonal pushpin and two note lines. It retains TaskPin's blue accent, rounded geometry and simple desktop-tool style. One asset supplies the Windows executable, desktop shortcut, application dialogs, tray and calendar header.

## Canva source

Created in Canva's AI image generator on 2026-11-20, then refined in Canva to remove long shadows and use the existing `#4C9AFF` accent. The design is named **TaskPin · 截图与便签应用图标 · 2026-11-20**.

[Open the Canva design](https://www.canva.com/design/DAHbcVg22O4/Wvy3pMkHlxEfywm9WohDrA/edit). The design retains the owner's existing private access. This link does not make it publicly editable.

The preserved 1264 × 1264 PNG export is `TaskPinQt/assets/brand/canva-export.png`, SHA-256 `57f26a930df1473661eedd4fad6a998f8819a34de89edd835919188c4e9b7f10`. The Canva page is editable; the generated artwork itself is a raster image, not independently editable SVG paths.

Initial prompt: an original Windows screenshot, pinning and sticky-note icon; one blue rounded-square tile; a bold white rounded capture frame with an upper-right diagonal pushpin and two short note lines; flat vector style, consistent rounded strokes, generous negative space, no text. The second generation removed the generated long shadows, bevels and gradients. Its blue was then adjusted in the Canva editor before export.

## Production assets

`TaskPinQt/scripts/prepare-brand-icons.py` removes the connected exterior white background while retaining the white symbol inside the blue tile. It normalizes the generated color noise to solid `#4C9AFF` and white, preserves antialiasing, and centers the symbol with transparent padding. The original Canva export stays unchanged.

```powershell
# Python with Pillow installed; this is a development tool, not a runtime dependency.
python TaskPinQt/scripts/prepare-brand-icons.py `
  TaskPinQt/assets/brand/canva-export.png TaskPinQt/assets/brand
```

The output includes a transparent 1024 × 1024 master PNG and 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixel PNG/ICO representations. `AppIcon.h` loads these PNG sizes from Qt resources; the Windows resource compiler embeds the same ICO in the executable. Icon generation is not required to build the application because generated assets are checked in.

The existing Lucide and Material Design Icons toolbar graphics retain their own licenses and are separate from this brand artwork; see [third-party notices](../THIRD_PARTY_NOTICES.md).
