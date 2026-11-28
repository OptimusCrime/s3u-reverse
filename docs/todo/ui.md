# Backlog: UI controls (Dirk\dialog.cpp)

- **The rest of dialog.cpp (wip/dialog-r8 left 83 rows).** Round 8 verified
  64 small and medium members (ui_dialog_setters.cpp, ui_list_selection.cpp,
  ui_screen_queue.cpp, ui_list_view_cells.cpp, ui_text_view_layout.cpp,
  ui_control_input.cpp, ui_dropdown_text.cpp) with the in-process kit
  `src/s3re/game/ui/member_test_kit.h` (random buffer, recording callee
  stubs, fake vtables with 1/3-argument and COM slots). Still open, roughly
  by effort:
  - The GDI Draw functions (CStaticText 0x41ca70, CListBox 0x41f700,
    CTextButton 0x41b980, CEditControl 0x419b80, CTextView 0x4212b0,
    CVScrollBar 0x41e580, CImageButton 0x41ad70, CAnimatedButton 0x41b2b0,
    the static image/frame Draws), CScreenElement::GetClippedDC 0x415cd0,
    Dialog::Init 0x416190 / GetLineHeight 0x4168d0 / Shutdown 0x416760,
    CTextView::Reflow 0x420eb0, CEditControl::ScrollToCursor 0x419630 /
    OnChar 0x419280 / OnKeyDown 0x418fe0: use the fake GDI (test/fake_gdi.h,
    emu.py's GDI probes).
  - CListView: CreateCell 0x425ca0, LayoutLines 0x4252c0, LayoutCells
    0x4258f0, SetSelection 0x426f60, OnLButtonDown 0x4268d0, OnMessage13
    0x426bb0, AddColumn(WithTextId) 0x423ab0/0x423d30, AddLine, RemoveLine,
    RemoveColumn, OnCellClicked, SetCellText/TextColor/Image/Frame (each has
    its own variant of the inlined Watcom operator[] check: some call
    IndexCheck 0x429180, some resize through 0x429940 instead of 0x429a80;
    copy each variant exactly, see ColumnIndex in ui_list_view_cells.cpp).
  - CScreenGroup::AddChild 0x416f10 / RemoveChild 0x416fe0 / PushModal
    0x417920 (inlined PtrVector insert/erase, dynamic_cast).
  - The constructors (CEditControl, CStaticSurface, CVScrollBar, CListBox,
    CTextView, CDropdown, CUpDown, CProgressBar, CListView, CColumn, CLine,
    CDialog) and the remaining string/vector helpers (wstring assign_repeat
    0x428610, operator+ 0x429600, the CColumn/CLine vector resize/insertAt/
    IndexCheck copies).
  - CScreenElement::PostCommand 0x416140 builds its message on the stack
    with bytes 1..11 uninitialized and passes it on by value: a spec needs
    the virtual call's message recorded without those bytes (a pointer-
    style partial record for by-value arguments), and the in-process kit
    the same.
