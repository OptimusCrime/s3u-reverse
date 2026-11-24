# User interface (Dirk\dialog.cpp, Draw.cpp, MenuShared.cpp, netdialogs.cpp): findings of the naming pass

Findings of the naming pass of session 50 (2026-11-24) by five reviews:
CScreenElement and the early controls (part 1), the middle of dialog.cpp
(part 2), the end of dialog.cpp with Draw.cpp/DrawInitSurfaces.cpp (part
3), MenuShared.cpp and the first half of netdialogs.cpp (part 4), the
second half of netdialogs.cpp (part 5). Read from the code, not yet
confirmed by reimplemented functions. Function names are in
`re/functions.csv` (notes "Named (session 50, ...)"). Where the parts
disagree, the code decides; one conflict was settled at the merge:
0x42b380 is `~CDropdown` (CDropdown::vf00_sub66c_thunk -> 0x42b340 ->
0x42b380), not `~CListBox` (that is 0x421fc0).

## ui1: 0x414050..0x421000 (end of Dirk\DGlobals.cpp, start of Dirk\dialog.cpp)

119 proposals in ui1.tsv (93 high, 26 medium). 27 target rows skipped: 26 already
have correct names (RTTI constructors/destructors such as `CButton::CButton`,
`CStaticText::~CStaticText`, the WC sorted-vector destructors), and one is
unknown (`UI_sub_41ca20`, CStaticText +0x38 flag setter; Draw selects font 6 when
the flag is set in style 5, but that isn't enough to name it).

### Message types and vtable slots (CScreenElement, 32 slots, by slot number)

Messages are 20-byte records. The slots come from `DispatchMessage` 0x4158d0 and
their implementations:

| slot | off | meaning | evidence |
|---|---|---|---|
| 0 | 0x00 | deleting destructor | |
| 1 | 0x04 | SetParent | verified |
| 2 | 0x08 | OnMessage | CScreen 0x417e70 first drains its queue |
| 3/4 | 0x0c/0x10 | SetFocus / KillFocus | |
| 6 | 0x18 | SetFlag1F | |
| 7 | 0x1c | Invalidate | |
| 8/9 | | WantsMessage / DispatchMessage | |
| 10 | 0x28 | **Draw** (surface, rectA, rectB) | every control's override draws |
| 11 | 0x2c | FillBackground | |
| 12 | 0x30 | type **0x10 = left button down** | CButton presses, CEditControl/CListBox take the focus and select |
| 13 | 0x34 | type **0x12 = left button up** | CButton::OnLButtonUp clicks |
| 14/15 | 0x38/0x3c | types 0x13, 0x15 | no override in my range (probably double click / right button) |
| 16 | 0x40 | type 0x16 = mouse move | verified OnMouseMove |
| 17 | 0x44 | type **1 = key down** | CEditControl::OnKeyDown switches on VK_END..VK_DELETE |
| 18 | 0x48 | type 2 (probably key up) | |
| 19 | 0x4c | type **3 = character** | CEditControl::OnChar (backspace '\b', iswascii) |
| 20 | 0x50 | type 0x20 | |
| 21 | 0x54 | type **0x30 = timer tick**, no message | CControl::OnTimer (tooltip after 600 ms), CAnimatedButton::OnTimer |
| 22 | 0x58 | type **0x40 = command** (+0xc id, +0x10 value) | built by PostCommand 0x416140 and CButton::Click (command id +0x44) |
| 23/24 | 0x5c/0x60 | mouse enter / leave | |
| 25 | 0x64 | **focus changed (bool)** | CEditControl::OnFocusChanged redraws its cursor |
| 26 | 0x68 | **parent changed (bool attached)** | called by SetParent |
| 27 | 0x6c | shown on a screen (bool) | CScreenGroup propagates it, CControl deletes its tooltip on 0 |
| 28/29 | 0x70/0x74 | moved / resized | CVScrollBar/CListBox re-layout |
| 30 | 0x78 | **GetScreen** | base asks the parent, CScreen 0x417f90 returns itself |
| 31 | 0x7c | **PostMessage** | base forwards to the parent (assert without one), CScreen 0x417fa0 queues it |

So notifications (type 0x40 commands) go up the parent chain to the CScreen,
which queues them (WCQueue<MsgConsole> at CScreen+0x80). `Menus::CDrawThread::Update`
polls them with HasQueuedMessages 0x417f50 / ProcessQueuedMessage 0x417df0, and
they go back down through OnMessage -> dispatch -> slot 22 of the dialog.

### Classes

- **Dialog globals** (`Dialog::Init` 0x416190 / `Shutdown` 0x416760):
  0x7a823c = the DirectDraw surface used for GDI, 0x7a8238 = CGraphicsData*
  (TextAddress, AnimationPointer, GuiImagePointer go through it), 0x7a8020 =
  the surface's DDPIXELFORMAT (8 dwords from the surface desc), 0x7a809c = font
  table, 0x7a8050 = vector of created HFONTs, 0x7a8064 = colour table (14 entries),
  0x7a8240 = pen (cursor/frames).
- **CScreenGroup** (own vtable 0x655b70: slot 0 AddChild, slot 1 RemoveChild):
  +0x28 focus child, +0x30 captured (modal) child, three WCStacks for
  PushModal/PopModal (captured child, focus, "was added" bool).
- **CScreen**: CScreenGroup plus a message queue at +0x80 (count +0x84).
- **CControl** (own vtable 0x655d28 slot 0 = SetEnabled): +0x08 = a functor
  called on every timer tick (OnTimer), **not** a tooltip owner; +0x0c =
  disabled (= !enabled); +0x10 tooltip text; +0x20 tooltip (CStaticText, style
  0xc, font 0x20, added to the screen).
- **CEditControl**: wide-character text (+0x4c string: buffer +0x50, length
  +0x54, capacity +0x58), +0x5c first visible char, +0x60 cursor, +0x68 max
  length, +0x38 change functor, +0x3c rejected-character functor, filter flags
  around +0x45/+0x46.
- **CButton** (+0x50 element): +0x38 click functor, +0x40 state functor, +0x44
  command id (type 0x40 message), +0x48 toggle button, +0x49 pressed, +0x4a
  checked. **CImageButton**: +0x4c GFX file, +0x50/+0x54/+0x58 normal/pressed/hover
  frames (-1 = none). **CAnimatedButton**: sequences +0x54.., current +0x4c, frame
  +0x50. **CTextButton**: text +0x5c, style +0x6c (4).
- **CStaticText** +0x20/+0x24 own colour, +0x28 text, +0x38 flag, +0x3c font,
  +0x40 style. **CStaticImage** +0x20 GUI image. **CStaticFrame** +0x20 file,
  +0x24 frame. **CStaticColorIndexedFrame** +0x20..+0x28. **CStaticSurface**
  +0x20 off-screen surface.
- **CVScrollBar**: range +0x2e0, position +0x2e4, page +0x2e8, scroll functor
  +0xb4 (CScrollFunctor of the owner), buttons +0xb8 (up) / +0x138 (down).
  **0x41e410 is a function start missing from the lists** (button callback:
  ScrollBy(-1/+1)); add it to `re/extra_functions.tsv` as manual.
- **CListBox**: scrollbar +0xc0, flags +0x3f0 (fixed range) / +0x3f1 (scrollbar
  allowed), line height +0x3f4, text rect +0x3fc..+0x408, scroll range/pos/page
  +0x40c/+0x410/+0x414, selection +0x418, max items +0x41c, items (string
  vector) +0x420 (count +0x434).
- **CTextView**: scrollbar +0xc0, text rect +0x3f0.., lines +0x408, visible rows
  +0x404, text +0x410, max lines +0x420 (-1 = unlimited; the lobby chat).

### Corrections of existing (verified) names, for the next rename pass

- `CControl::OnMessage10` (0x418380) -> `CControl::OnLButtonDown` (type 0x10 is the
  left button down; buttons press on it, edit/list controls take the focus).
  Likewise `CGroupControl::OnMessage10`.
- `CScreenElement::ParentQuery78` (0x4160d0) -> `CScreenElement::GetScreen` (slot 30;
  CScreen returns itself, ShowTooltip adds the tooltip to the result).
  `CScreenElement::IsQuery78Zero` (0x415840) -> `CScreenElement::IsOffScreen`
  (GetScreen() == 0).
- `CScreenElement::Enable` (0x415ca0, slot 26) is the **parent-changed handler**
  (SetParent calls slot 0x68 with parent != 0): -> `OnParentChanged`. Slot 27
  (`OnEnable` in CScreenElement/CControl/CScreenGroup/CGroupControl) is "now
  shown on a screen / removed" -> e.g. `OnShow`; real enabling is CControl's own
  slot 0 (SetEnabled, byte +0x0c).
- `Dialog::Global7a823c` -> `Dialog::Surface`, `Dialog::Global7a8238` ->
  `Dialog::GraphicsData`, `Dialog::Table7a809c` -> `Dialog::Font`,
  `Dialog::Table7a8064` -> `Dialog::Color`, `Dialog::Global7a8240` -> `Dialog::Pen`,
  `Dialog::CopyGlobals7a8020` -> `Dialog::PixelFormat`.
- `screen_element_layout.h` CControl: `tooltip_owner` (+0x08) is the per-tick
  functor; `unk0c` is `disabled`.
- 0x4141a0 / 0x414240 / 0x4143b0 are the base `~WCBareVectorBase<...>` bodies
  (they store the WCBareVectorBase vtable); the current derived-class names are
  acceptable (the derived destructors are empty and folded into them).
- `CControl::CControl` (0x417fe0) takes the RECT by value plus the most-derived
  flag (ret 0x14); the flag's low byte becomes the tooltip string's allocator
  byte (+0x10), as in the CLAUDE.md allocator-byte rule.

### Lobby-only users

`CEditControl::GetText` 0x418a10, `CTextView::Clear/AppendLine/SetText/GetText`
are only called from lobby/online code. `CScreenElement::PostCommand` 0x416140 is
used by lobby dialogs and also by menu dialogs.

## ui2: 0x421000..0x428120 (middle of Dirk\dialog.cpp)

87 proposals (79 high, 8 medium) in `ui2.tsv`. Rows I left alone: the
constructors already named correctly (CDropdown 0x421c90, CUpDown 0x422830,
CProgressBar 0x422ef0, CListView 0x423540, CDialog 0x427850; their fields are
below), the Watcom destructors whose names are already right (0x423a10,
0x427de0, 0x427e40, 0x427ea0, 0x427f10, 0x427fe0, 0x428070), and 0x4276a0
(`mov [ecx+10h], arg; ret 4`, a setter that the linker shares between
CListView::CColumn (+0x10 tooltip text id), Dirk::CMapFile::Open and
CConsoleThread::vf04. A class-specific name would be wrong for the other
callers).

### Virtual slot meanings used (CScreenElement vtable, 32 slots)

These come from the verified code (screen_element.cpp) and are confirmed by
the implementations in this range:
- slot 1 SetParent, 3 SetFocus, 4 KillFocus, 5 "accepts focus" (CDialog's Tab
  handler skips children whose slot 5 returns 0; CScreenElement::vf05 returns
  0, the CControl version 0x418800 returns 1), 6 SetFlag1F, 7 Invalidate,
  10 Draw, 11 FillBackground.
- slots 12..22 are the message handlers by type (DispatchMessage): 12 = 0x10
  (OnMessage10, probably mouse down), 14 = 0x13 (probably a double click:
  CListView's handler notifies its "activate" listener), 16 = 0x16
  (OnMouseMove), 17 = type 1 (key: CDialog's handler reads the key code at
  message byte +4: Tab/Enter/Escape; bit 0 of byte +5 reverses Tab, probably
  Shift), 21 = type 0x30 (no message argument: the periodic/idle tick; the
  CControl version 0x4183c0 opens the tooltip after 600 ms and calls the
  listener at CControl+8).
- 25 = focus changed (bool) (KillFocus calls slot 0x64), 26 Enable,
  27 OnEnable, 28 OnMoved, 29 = resized (rect, size delta) ret 8: I named
  the implementations `OnResized`.
- **Suggested renames outside my range (not in the TSV):** CScreenElement::vf05
  (0x416180) -> `CScreenElement::AcceptsFocus` (slot 5, returns 0) and
  UI_sub_418800 (`mov al,1; ret`, CControl's slot 5) -> `CControl::AcceptsFocus`;
  CScreenElement::vf25 (0x415c90) -> `CScreenElement::OnFocusChanged`;
  CScreenGroup::vf00 (0x416f10) -> `CScreenGroup::AddChild` and vf01
  (0x416fe0) -> `CScreenGroup::RemoveChild` (CDropdown adds/removes its drop
  list with them; constructors add their member controls with vf00);
  UI_sub_417090 -> `CScreenGroup::ShowChild(bool, child)` (adds if not
  present / removes); UI_sub_41a9b0 -> `CButton::Click` (toggles a toggle
  button, calls the listener at button+0x38); UI_sub_418330 ->
  `CControl::SetTooltipTextId` (tooltip text = TextAddress(id) or "");
  UI_sub_41ca30 -> `CStaticText::SetTextColor`; UI_sub_418b40 ->
  probably `CEditControl::SetText`; UI_sub_41c810 -> probably
  `CStaticText::SetText`; UI_sub_41f3b0/41f3a0/41f240/41f0f0 -> CListBox
  SetSelection(index, notify)/GetSelection/GetItems/SetItems; UI_sub_4183c0
  -> `CControl::OnIdle` (slot 21); Net_sub_5bf050 is the CSurface lock that
  CDialog::FillBackground uses (DirectX/netsession.cpp is the CSurface code,
  "Net" is a mis-survey).
- **Missed function starts** (not in ghidra_functions/extra_functions/
  functions.csv): 0x421260 (`CTextView::OnScrollBarMoved`, (sender, line)
  ret 8: stores the first line +0x400, notifies listener +0xb4, invalidates;
  its address is loaded at 0x4205d9 in the CTextView constructor) and
  0x4271e0 (`CListView::OnScrollBarMoved`, (sender, top) ret 8: top line
  +0x190, LayoutLines, LayoutCells, notifies listener +0xbc; loaded at
  0x4236a1 in the CListView constructor). 0x427cab is a catch funclet inside
  CDialog::FillBackground (unlock the surface, rethrow), not a function.
- Global 0x7a8244 (not in globals.csv): the CDropdown whose list is open
  (`g_openDropdown`); its Draw and focus handlers close their list when they
  are not this one.

### Listener / callback pattern

Controls notify an owner through a pointer to a small callback object: vtable
whose slot 0 calls a stored member function of the owner (layout {vtable,
owner, member-function pointer (3 dwords)}; e.g. CDropdown +0x630 vtable
0x6566a0 -> 0x422590). The listener pointer of a CButton is at button+0x38,
of a CListBox at +0xb4, of CDropdown/CUpDown/CListView at +0xb4 (first own
field after the CGroupControl part, which ends at +0xb4).

### Classes

All of these derive from CGroupControl (CControl at +0, CScreenGroup at
+0x38, own fields from +0xb4, CScreenElement virtual base at the end; the
`_subNNN` thunks give the virtual base offset). Methods reached through the
slot thunks get the virtual-base pointer as `this`.

#### CTextView (vbase +0x434)
Multi-line text display with a scroll bar. Text string at vbase-0x20, first
line vbase-0x34, line height vbase-0x28, visible lines vbase-0x30, line count
vbase-0x2c, DrawText format vbase-0xc. Draw (GDI DrawTextA on the surface
DC), FillBackground, OnResized.

#### CDropdown (vbase +0x66c), constructor 0x421c90
Combo box: +0xb4 listener (selection changed: (dropdown, index)),
+0xb8 CEditControl (the field), +0x148 CImageButton (arrow, image 0x20009),
+0x1c8 CListBox (the drop list, not a child until opened), +0x630/+0x648
callback objects for the arrow and the list, +0x660 byte list open,
+0x664 list height. SetItems, SetSelection, OnArrowClicked (open/close),
OnListSelect, OnFocusChanged and Draw (close when another dropdown is open),
OnResized, SetFlag1F.

#### CUpDown (vbase +0x224), constructor 0x422830
Number spinner: CStaticText display (+0xb8, font 0x26), up/down image buttons
(+0xfc image 0x2000a, +0x17c image 0x2000b), min +0x1fc, max +0x200, value
+0x204, button callback +0x208. GetValue, SetValue (clamps, notifies +0xb4),
SetRange, UpdateText ("%i"), OnButtonClicked, OnResized.

#### CProgressBar (vbase +0x10c), constructor 0x422ef0
CStaticText with the number (font 0x24), bar colour +0xf8 (0xe67800),
background +0xfc (0xc82828), value +0x100, maximum +0x104. SetProgress
(maximum, value), Draw (two DDBlt colour fills).

#### CListView (vbase +0x59c), constructor 0x423540
Table with typed cells (used by lobby, map selection, game setup and campaign
dialogs): +0xb4 selection listener (view, line), +0xb8 activate listener
(slot 14), +0xbc scroll listener, +0xc0 cell-click listener (view, line,
col); columns WCValOrderedVector<CColumn> +0xc8 (data +0xd0, count +0xdc),
lines WCValOrderedVector<CLine> +0xe4 (data +0xec, count +0xf8), cells
WCValSkipListDict<int, CScreenElement*> +0x100 keyed `line << 16 | col`
(lookup key scratch at +0x178), header height +0x180, default line height
+0x184, visible lines +0x188, fully visible +0x18c, top line +0x190,
selected line +0x194 (-1 none), CVScrollBar +0x198, scroll callback +0x4b0
(-> 0x4271e0), cell callback +0x4c8 (-> 0x427240), client rect +0x4e0..+0x4ec,
scroll bar shown +0x4f0, selection highlight +0x4f1, headers shown +0x4f2,
row/header group +0x4f4.
- CColumn (0x20 bytes): +0 index, +4 type, +8 width, +0xc ?, +0x10 tooltip
  text id, +0x14 x, +0x18 click listener, +0x1c header CStaticText. Cell
  types: 0 static text, 1 CStaticImage, 2 CEditControl, 3/4 image (4: with
  text) buttons, 5 CStaticFrame, 6 CUpDown, 7 CProgressBar, 8
  CStaticColorIndexedFrame.
- CLine (0x14 bytes): +0 index, +4 height, +8 y, byte +0xc flag, +0x10 row
  CScreenGroup.
- Copy constructors and assignments of both transfer ownership of the owned
  pointer (auto_ptr style), which the Watcom vectors rely on.

#### CDialog (vbase +0xc4), constructor 0x427850
Base of the menu dialogs: +0xb4 background image (GfxDword; drawn by
FillBackground instead of the colour fill), +0xb8 default button (Enter),
+0xbc cancel button (Escape). OnKeyDown (Tab focus cycling, Enter, Escape),
SetParent (gives the parent its focus child).

#### CListBox / CEditControl (destructors only in this range)
CListBox (vbase +0x444): item vector WCValOrderedVector<String> at vbase-0x24,
four button members and a scroll bar. CEditControl (vbase +0x70): text string
at vbase-0x20.

### Corrections of existing names
- 0x421fc0 `WCBareVectorBase_String::ctor_or_dtor_421fc0` is CListBox's
  destructor (game code, not Watcom).
- 0x427240 `WCValSkipListDictIter_int_CScreenElement::ctor_or_dtor_427240`
  is CListView's cell-click callback; it only builds a dictionary iterator on
  the stack.
- 0x427d60 `WCBareVectorBase_HFONT::ctor_or_dtor_427d60` is the
  WCValOrderedVector<HFONT> constructor (final vtable 0x656a00).
- 0x4274a0 / 0x4276b0 (`WCBareVectorBase_..._CColumn/CLine::sub_*`) are the
  default constructors of CListView::CColumn / CLine.
- No verified rows in the range needed corrections.

## ui3: 0x428120..0x430780 (end of dialog.cpp, Draw.cpp, DrawInitSurfaces.cpp)

111 target rows: 88 names proposed (63 high, 25 medium), 23 kept. The kept names are already right:
the Watcom scalar deleting destructors and destructors 0x428120..0x4283a0, the constructors
0x428bd0/0x428bf0, CDrawThread::CDrawThread/scalar_deleting_destructor, CDrawer::CDrawer/
scalar_deleting_destructor/~CDrawer, and CEventQueueOverflow ctor/sdd/dtor.

### What is in the range

1. **0x428120..0x429c20: container and string code** compiled into dialog.cpp.
   - An MSVC6 `basic_string<unsigned short>` ("wide string", 16-bit chars, refcount byte at
     ptr[-1], same layout as std::string: +4 ptr, +8 len, +0xc res). Proposed names
     `std::wstring::*`: assign_substr 0x428400, assign_repeat 0x428610, _Tidy 0x4286d0, erase
     0x428730, _Grow 0x4288a0, _Copy 0x4289e0 (+ _Copy_tail 0x428a62), _Split 0x428af0,
     operator_plus_string 0x429600, compare_pos_chars 0x429980. The verified neighbours
     `UI_WideEos_428880` (= std::wstring::_Eos) and `UI_sub_4289d0` (= max_size, returns 0x7ffffffd)
     could be renamed to match, and `UI_CopyWords_418fb0` is the wchar `_Tr::copy`.
     0x61fe1b (CRT_sub_61fe1b) is wcslen.
   - Watcom vectors: per element type a `_capacity` constructor, a destructor, a slot-1
     `base_init_upto(index)` (constructs elements up to index; for trivial types only
     num_init = index+1, folded into 0x428e50 for 33 vtables), IndexCheck (operator[] check, same
     pattern as verified watcom::PtrVector::IndexCheck), insertAt and resize. Layout: +8 data,
     +0xc capacity, +0x10 num_init, +0x14 entries, +0x18 grow step.
     Element types here: String (16), CScreenElement* (4), CListView::CColumn (0x20),
     CListView::CLine (0x14).
   - Watcom lists: slot 1 of WCIsvSListBase / WCValListBase is the link delete hook
     (Open Watcom `WCListDelete`); NewLink helpers for the bool stack and the MsgConsole queue.
   - 0x4281a0: slot 0 of every CFunctor2/CScrollFunctor/CSelectFunctor = invoke a member function
     pointer with two args.
2. **0x429c30..0x42b7a0: the UI accessor block's deleting destructors.** Each control class has a
   virtual base CScreenElement; its vtable slot 0 is a vtordisp thunk (`ClassName::vf00_subXX_thunk`,
   already named) that jumps to the real scalar deleting destructor, which calls `~Class` and then
   `~CScreenElement` (0x4156b0) for the virtual base. This gives the destructors of the classes
   (some outside the range, for the owners of those rows):
   ~CControl 0x4180a0, ~CButton 0x41a900 (also used by CAnimatedButton), ~CImageButton 0x41b630,
   ~CGroupControl 0x41e210, ~CVScrollBar 0x41f010, ~CEditControl 0x421f50, ~CListBox 0x421fc0,
   ~CListView 0x423870; the CScreenGroup destructor body is 0x416da0 (now
   `CScreenGroup::ctor_or_dtor_416da0`). In range: ~CProgressBar 0x429c70, ~CDialog 0x42a0a0,
   ~CTextView 0x42a380, ~CScreen 0x42a750, ~CTextButton 0x42ab70, ~CUpDown 0x42b100,
   ~CDropdown 0x42b380. Members seen: CScreen holds a WCQueue<MsgConsole> (message queue);
   CTextView 4 embedded buttons and a scroll group; CDropdown a String vector, a CVScrollBar,
   a list group and a button; CUpDown two buttons and a text element; CTextButton a std::string.
   Slot 21 (0x429f10, CGroupControl/CProgressBar): CControl part 0x4183c0 shows a tooltip after
   600 ms of hovering (timeGetTime) and calls the functor at +0x34; named `CGroupControl::Tick`
   (medium).
3. **0x42c2d0..0x42c810: CDrawThread** (in-game draw thread; Draw.cpp, though the inventory's
   source_file says dialog.cpp for 0x42c2d0..0x42ca00: the CDrawer constructor 0x42c820 asserts
   with "E:\Siedler3\Dirk\Draw.cpp", so the Draw.cpp range starts at 0x42c2d0 or earlier).
   Fields: +0x44 drawer (CGameDrawThread), +0x48 window, +0x50 redraw flag, +0x54 drawer arg,
   +0x58 pointer to the shared draw-data slot, +0x5c own draw-data buffer (deleted with
   0x405a50), +0x60 waitable signalled by the producer, +0x64 Win32::CEvent "frame taken",
   +0x68 no-drawer flag (from byte 0x7a8008, never written: a debug switch), +0x69 initialized,
   +0x6c wparam posted with message 0x800 to the app thread at start, +0x70 lobby flag.
   Slots: 4 OnStart, 6 OnExit, 12 CreateDrawer, 13 Draw(flag). These meanings also hold for
   DirectX::CGameDrawThread: slot 12 (+0x30) = CreateDrawer (pure), slot 13 (+0x34) = Draw (pure),
   0x5b66a0 (vf09) = Step (calls Draw with the flag +0x50 and clears it), 0x5b6710 (vf10) =
   OnEvent (sets +0x50, calls CGameThread::OnEvent), 0x5b65c0 (vf11) = SetWindow (+0x48),
   0x5b65d0 (vf04) = OnStart, 0x5b6670 (vf06) = OnExit (drawer slot 8, delete drawer).
   Draw data is double-buffered with InterlockedExchange between the logic side and this thread.
4. **0x42c820..0x42f920: CDrawer** (in-game DirectDraw drawer, 0x5f700 bytes, base
   DirectX::CGameDrawer). Fields: +0xc hwnd, +0x21c 4000 GfxDword, +0x14278 8000 x 0x1c bytes,
   +0x4b544 4000 GfxDword, +0x5f6b8 CGraphicsData*, +0x5f6bc CSoundData*, +0x5f6c0..+0x5f6cc
   surfaces (primary, back/flip...), +0x5f6d0 ctor arg, +0x5f6d4 lobby flag, +0x5f6d5 software
   cursor flag, +0x5f6d8 mouse surface, +0x5f6dc current cursor id (-1 none), +0x5f6e0 object
   deleted on release, +0x5f6f8 flip callback (member function pointer) on +0x5f6c8.
   CGameDrawer slot meanings (from CDrawer and CGameDrawer code): 4 RestoreSurfaces, 5
   ReleaseSurfaces(bool keepGraphics), 7 Init(hwnd) 0x5b6ed0 (calls 10 and 11(0)), 8 Exit/Release
   0x5b6f00, 10 SetConfiguredDisplayMode (pure), 11 CreateSurfaces(bool) (pure); CDrawer adds
   slot 12 InitSurfaces. Messages to the main window: 0x802 set cursor, 0x803 surfaces released,
   0x804 change display mode (index 0/1/2 = 640/800/1024).
   The same slot names should apply to `Menus::priv::CDrawer` (0x498690 slot 4, 0x498640 slot 5,
   0x497ed0 slot 10, 0x497ef0 slot 11), which is outside my range.
   Global 0x7a800c is `Dirk::Globals::lpGraphicsDataLock` (a Win32::CSemaphore created in
   0x405630; slot 0 Wait(5000, 1) before closing the graphics).
5. **0x42cf80 Win32::CEvent::Set** and **0x42c9d0 vector_constructor_iterator** are header/compiler
   code that the linker placed here.
6. **0x430560 CEventQueueOverflow::Message**: slot 1 of the Win32::CException hierarchy builds
   the "<Class> <text> at: <location>" message; 0x611b90 (CException::vf01) and 0x614c90
   (CRunTimeError::vf01) are the same method ("Unspecified exception occured at ",
   "CRunTimeError Runtime Error! at: ").

### Corrections of existing names (outside the target list or verified)

- 0x00428880 `UI_WideEos_428880` (verified) -> `std::wstring::_Eos`; 0x004289d0 `UI_sub_4289d0`
  (identified, trivial) -> `std::wstring::max_size`; 0x00418fb0 `UI_CopyWords_418fb0` (verified)
  is the wide char traits copy (optional rename).
- Wrong guesses replaced: 0x42de80 was `WCValSortedVector_int::WCValSortedVector_int_42de80`
  (it is a CDrawer method), 0x42b380 was `WCValOrderedVector_String::ctor_or_dtor_42b380`
  (~CDropdown), 0x42a750 was a WCValListBase ctor_or_dtor (~CScreen), and 0x428fd0/0x428fe0/0x428ff0
  were named as constructors but are destructors.
- 0x0060b560 `WCIsvSListBase::clearAndDestroy` has category game; it is Watcom code.
- source_file: 0x42c2d0..0x42ca70 are listed as Dirk/dialog.cpp; the Draw.cpp assert in the CDrawer
  constructor puts them in Draw.cpp.
- 0x61fe1b `CRT_sub_61fe1b` = wcslen.

### Lobby-only

0x42de80 `CDrawer::ShowPeerConnectionMessages` runs only for online (lobby) games (texts
"Initiating player2player communication", "Entering lobby sleepmode", "Disconnecting from lobby").

## ui4 notes: Dirk\MenuShared.cpp (0x466dd0..0x46a090) and Menus::* dialogs (0x4763c0..0x495230)

105 proposals in ui4.tsv (56 high, 49 medium). 19 target rows already carry correct names
(RTTI constructors, scalar deleting destructors, `Dirk::CMouseSurface` ctor/dtor, `CNetThreadBase`
ctor/dtor, `Menus::CDrawThread::CDrawThread`); they were checked and left alone.

Network scope: CMenuSharedData, CConnectionDialog, CGameSetupDialog and CGameLaunchDialog are only
used for LAN/Internet multiplayer (DirectPlay) and the lobby; they are named because they are
`Menus::*` / Menu rows in the range, not network classes by name. Rows that only lobby code reaches
are marked "online only" in the evidence (0x4676e0, 0x467890, 0x467e00, 0x4681d0, 0x48c820,
0x490f30).

### CMenuSharedData (no RTTI; name from "lpMenuSharedData", Dirk\MenuShared.cpp)

Allocated by App_sub_405450 (0x2dc bytes, app+0xa4), deleted by App_sub_405600. Shared between the
menu dialogs and the menu net thread (`Menus::CNetThread`, which keeps it at +0x44). Every access
locks the critical section at 0x7a7a18 (`g_gameSettings` +0: GameSettings starts with a critical
section; 0x5b6c20 `mov eax,ecx; ret` returns it, 0x4672d0 returns its address).

| offset | meaning |
|---|---|
| +0x00 / +0x04 | CEvent* lpNetToEvent / lpNetFromEvent (pulsed after each request) |
| +0x08 | current net state, written by the net thread (6 = connected/in game; 1 = error) |
| +0x0c | requested net state (the dialogs' request) |
| +0x10 | request being processed by the net thread (-1 = none) |
| +0x14 | net thread busy (inside a blocking DirectPlay call) |
| +0x18 | WCValOrderedVector<CConnectionShortcut> (service providers, 0x40 bytes each) |
| +0x34 | WCValSortedVector<CRunningSession> (enumerated sessions) |
| +0x50 | WCVectorBase<void*> |
| +0x6c / +0x70 | last error code / error flag |
| +0x78 | status text id (0x90010 error, 0x900aa connected) |
| +0x7c | current CConnectionShortcut (CTCPConnectionShortcut with the host address for TCP/IP) |
| +0xbc | string |
| +0xcc | session description (0x50 bytes + string, NetSession::Init 0x5bb040; GUID at +0) |
| +0x12c / +0x13c / +0x14c | strings: game name / player name (lobby) / player name |
| +0x15c | online (lobby) flag; +0x15d game already unlinked from the lobby |
| +0x160 / +0x164 | lobby client / lobby manager (constructor arguments) |
| +0x168 | LobbyClient::CGroup (the lobby game) |
| +0x284 / +0x2a0 | String vectors; +0x2bc WCValOrderedVector<unsigned int> |

Requested states (from Menus::CNetThread::vf09 0x45f4c0, "Invalid Netstate" assert, and the request
functions): 1 = idle/error, 2 = connection open (enumerate sessions; RequestOpenConnection),
3 = session selected (OnGameSelected / RequestLeaveSession), 4 = join (RequestJoinLobbyGame, Join
button), 5 = create/host (RequestCreateSession), 6 = connected. The net thread steps its state up or
down one level at a time toward the request (two jump tables at 0x45ffa0 / 0x45ffb8).

The provider GUID compared at 0x65b4f8 is DPSPGUID_TCPIP {36E95EE0-8577-11cf-960C-0080C7534E82}.

### Dirk::CMouseSurface (overlay mouse cursor; COverlaySurface)

+0x4c critical section, +0x64 target surface, +0x68/+0x6c hotspot, +0x70/+0x74 mouse position
(constructor: 400, 300), +0x78 show counter (starts 1), +0x7c rect, +0x8c DDOVERLAYFX (0x38).
Create / SetHotspot / Show / Hide / SetPosition / Clear / IsOverlaySupported, used by
CDrawer::vf12 (in-game) and Menus::priv::CDrawer::vf11 (menus) and CSiedlerMainWindow vf18/vf19.

### CNetThreadBase

0x46a070 stores (net session, logic->net queue, net->logic queue, app+0xb8) at +0x44..+0x50 for
CNetThread / CNetBounceThread (CNetBounceThread::Step reads +0x48). Its vf04 0x46a090 is just past
the range.

### Menus::CDrawThread (menu thread)

Slots: 4 OnStart, 6 OnExit, 12 CreateDrawer (pure in DirectX::CGameDrawThread, whose vf04 asserts
"lpDrawer"; same slot in the in-game CDrawThread 0x42c6a0), 13 Update, 14 OnDialogResult, 15..29
per-menu-state result handlers. Fields: +0x100 lpActualScreen, +0x104 menu state, +0x108 next state
after a video, +0x144 campaign mission index.

SetActualScreen 0x494550 creates the screen for a state: 0 CMainMenu, 16 CCampaignDialog (or the
main menu), 18 CLoadGameDialog, 19 CCreditsDialog, 22 CCampaignEndDialog; state 10 = a video is
playing (mouse button, message 0x20 or any key but Alt end it through 0x497c40, which then opens
+0x108; state 17 opens CNationSelectDialog(.., 2, 1) for the campaign choice).

OnDialogResult codes (main menu, state 0; jump table 0x4950dc/0x49510c): 1 -> state 1
(0x496c90(2)), 3/4 new CNationSelectDialog, 5 (not analysed; 0x4988d0, 0x497c40 path),
6 new CGameSetupDialog, 7 new CConnectionDialog, 8 new CLogonDialog (lobby), 10 start a loaded game,
0x16 -> state 0x12 (load game), 0x17 (not analysed; +0x144 = 0x62 when 0x497020 allows),
0x18 -> state 0x13 (credits).

OnResultMessage 0x494260 (CScreenElement slot 22 = message type 0x40, the message 0x416140 sends)
routes to the handler of the current state: 0 -> slot 14 (OnDialogResult), 17 -> 15
(OnCampaignSelectResult), 16 -> 16, 21 -> 17, 18 -> 18, 2 -> 20, 3 -> 21, 6..9 -> 22..25,
14/15 -> 27, 19 -> 28, 22 -> 29; byte +0x134 -> slot 19, +0x13c set -> slot 26.

### Message types and CScreenElement slots (from the verified DispatchMessage 0x4158d0)

slot 12 = type 0x10 (mouse button, `OnMessage10`), 13 = 0x12, 14 = 0x13, 15 = 0x15, 16 = 0x16
(mouse move), 17 = type 1 (key down; virtual key at message+4), 18 = 2, 19 = 3, 20 = 0x20,
21 = 0x30 (no message; periodic/idle: CControl 0x4183c0 shows the tooltip after 600 ms and calls the
functor at control+8), 22 = 0x40 (dialog result). Controls keep functors at +8 (idle), +0x38
(button click), +0xb4 (list/dropdown/up-down select), +0xc0 (list view cell).

### Dialogs (functor bindings from the constructors)

- CMainMenu (ctor 0x474010, functors at +0x890..+0x950, stored at button+0x38): Tutorial 0x4753c0,
  Campaign/Mission CD/Amazon campaign 0x475a20, button +0x318 0x475640, Single Player 0x475e40,
  LAN 0x475e70, Internet 0x4760b0, Online Help + Tips & Tricks 0x4763c0, Credits 0x476770,
  Exit = NoOp_ret4 + result field 1, Load Game result 0x16.
- CLoadGameDialog: tabs "Saved Games"/"Campaigns" 0x47a7d0, list select 0x47a860, FillList 0x47a7b0.
- CCampaignDialog: start 0x479a30. CBriefingDialog / CMapSelectDialog / CCreditsDialog: Draw (slot 10).
- CConnectionDialog: provider list +0x144 (0x47db90), open games list +0x148 (select 0x47ecc0, idle
  0x47e270), buttons Search for games +0x134 (0x47df70), New Game +0x138 (0x47efd0), Join Game
  +0x13c (0x47f480), Back +0x140.
- CMapSelectDialog (ctor 0x47fea0): tab +0x2618 (0 Random, 1 Single, 2 Multi, 3 User, 4 Saved multi),
  CMapFile +0x2650, type +0x2660; functors 0x482fb0, 0x482c30, 0x482c10 (sets +0x2690 = 1),
  0x4833a0 (CSelectFunctor, list).
- CGameSetupDialog (ctor 0x484410; the "free-game dialog" of re/game_setup.md): map button +0x228
  (OnChooseMap 0x4878b0), team-setup dropdowns +0x14c8/+0x1b58 (0x486ea0), up-downs +0x2270
  (teams, 0x487030), +0x24f8 (players per team, 0x487d50), +0x2780, ladder button +0x115c (0x4871f0),
  team list view +0x2a08 (0x4882b0), Cancel +0x2fc4 (0x4882e0, result 1), OK +0x3058 (0x488300).
  Settings: +0x31b0 CMapFile, +0x31b8/+0x31bc setup kind/index, +0x31c4/+0x31c5 ladder flags,
  +0x31c8 team count, +0x31e8/+0x3204 players/computers per team, +0x3220 map dir, +0x3230 map
  name, +0x3240 game name, +0x3250 "updating" guard.
- CGameLaunchDialog (ctor 0x48b8d0; the waiting room): list view +0xd0 (idle 0x48e180), columns
  Player's Name, Clan, Race, 3 (functor 0x48d9e0, team), 4 (Connection display), Chat (0x48d620),
  Ready (0x48d930), Remove Player (0x48c8b0), Close (0x48cfd0); Cancel +0x90c (0x48c550), Warp +0x9a0
  (0x48c820), OK +0xa34 (0x48c590); 0x48ddf0, 0x48c530, 0x48c540 further functors; +0xc4 the
  CMenuSharedData, +0xcc lobby game flag.
- CMessageBox (ctor 0x4927e0): style flags 1 OK (0x90078, functor 0x492d40), 2 Cancel (0x90079,
  result 2), 4 input field ("lpInput" +0xd0), 8/0x10/0x20 edit-field options; +0xc0 the caller's
  output string, +0xc4 text ("lpText").

### Function starts Ghidra missed (handlers merged into the preceding row)

Real functions that are not in re/ghidra_functions.tsv / extra_functions.tsv (each is a functor target
stored by a constructor, so certain): 0x47df70 CConnectionDialog Search-for-games button, 0x47efd0
CConnectionDialog New Game button (calls RequestOpenConnection, then result), 0x482c10, 0x482c30,
0x482fb0, 0x4833a0 (CMapSelectDialog), 0x487030, 0x4871f0, 0x487d50, 0x4882e0 (CGameSetupDialog;
0x4882e0 is Cancel), 0x48c530, 0x48c540, 0x48d930 (Ready column), 0x48d9e0 (team column), 0x48ddf0
(CGameLaunchDialog). Rows 0x47ecc0, 0x482d00, 0x486ea0 and 0x48d620 therefore cover more than one
function in Ghidra; the names apply to the real start only. Worth adding as kind `manual` to
re/extra_functions.tsv.

### Corrections of existing names

- 0x4672d0 `StaticInit_7a7a18_4672d0` (category compiler) is not a static initializer: no
  StaticDtorReg follows and it is called as a function with ECX = CMenuSharedData; it returns the
  critical section 0x7a7a18. Proposed `CMenuSharedData::GetLock` (in the TSV), category game.
- Survey constructor/destructor names that were wrong (all in the TSV): 0x466dd0 / 0x467070 are
  CMenuSharedData ctor/dtor (not Watcom vectors); 0x4676e0, 0x479a30, 0x47fea0, 0x4821a0, 0x483760,
  0x484410, 0x486ea0, 0x488dc0, 0x48fb30, 0x4912d0, 0x4916d0 were Watcom-vector guesses but are
  dialog methods / constructors (their first call is a vector ctor of a local).
- Outside the range, seen on the way (not in the TSV): 0x463c10 `Menu_sub_463c10` =
  `WCBareVectorBase_DirectX_CConnectionShortcut::resize`; 0x42b380
  `WCValOrderedVector_String::ctor_or_dtor_42b380` = `CListBox::~CListBox` (vector + CVScrollBar
  members); 0x42b100 `UI_sub_42b100` = `CUpDown::~CUpDown`; 0x41a900 `UI_sub_41a900` =
  `CButton::~CButton` (stores 0x655f00); 0x416140 `UI_sub_416140` = posts a type-0x40 result message
  (code, param) through slot 31 (`CScreenElement::PostResult`); 0x4183c0 = CControl slot 21 (idle
  tick, tooltip + control+8 functor); 0x497c40 = `Menus::CDrawThread::EndVideo` (opens state +0x108);
  0x496d50 / 0x496f20 = message box / input box helpers (create a CMessageBox and run it);
  0x5b6c20 = GameSettings' accessor of its critical section (returns this); 0x60d670 / 0x60d680 /
  0x60da00 / 0x60da20 = Enter/Leave critical section and the scoped-lock ctor/dtor;
  0x436b90 `GameSetup_sub_436b90` returns a game-type bitmask (not a result).
- No verified rows needed renaming.

## ui5: 0x495230..0x49ca80 (second part of Dirk\netdialogs.cpp)

73 proposals in `ui5.tsv` (47 high, 26 medium). 11 target rows were skipped
because their names are already right (the Watcom destructors and scalar
deleting destructors at 0x4990c0..0x4993b0, CDrawer::CDrawer, the CDrawer
scalar deleting destructor, and the WCSkipListIterBase constructor 0x499600).

### Menus::CDrawThread (the menu thread, vtable 0x658474, 30 slots)

Slots 0-13 come from DirectX::CGameDrawThread. Slots 14-29 are the
result handlers of the dialogs. Each one takes the 20-byte UI event by
value (5 dwords, `ret 14h`; dword 3 is the result code, dword 4 a
parameter) and returns a bool (handled).

**Dispatch.** The CScreenElement-like subobject at +0x150 has slot 22 at
0x494260 (reached through the thunk 0x49a450). It picks the handler like
this:

- if `+0x134` (a message box is open) is set: slot 19 OnMessageBoxResult
- else if `+0x13c` (the modal depth) is nonzero: slot 26 OnModalDialogResult
- else it switches on the state `+0x104` (jump table 0x4944f4), giving the
  state -> slot table below.

| state | dialog (from SetState 0x494550 and the handlers) | slot | name |
|---|---|---|---|
| 0 | CMainMenu | 14 | OnDialogResult (existing name; really OnMainMenuResult) |
| 1 | quitting (nothing) | - | returns 1 |
| 2 | CConnectionDialog (net) | 20 | OnConnectionResult |
| 3 | CGameLaunchDialog | 21 | OnGameLaunchResult |
| 4 | game starting | - | default |
| 6 | CLogonDialog (net) | 22 | OnLogonResult |
| 7 | CLobbyDialog (net) | 23 | OnLobbyResult |
| 8 | ? | 24 | trivial, returns 1 |
| 9 | CGameSetupDialog | 25 | OnGameSetupResult |
| 10 | video playing (PlayVideo) | - | default |
| 13 | waiting for the CD (Update polls IsCDPresent 0..3) | - | default |
| 14, 15 | start states after a game | 27 | OnGameEnded |
| 16 | CCampaignDialog (needs PromptForCD) | 16 | OnCampaignResult |
| 17 | CNationSelectDialog | 15 | (0x495130, not in my range) |
| 18 | CLoadGameDialog | 18 | OnLoadGameResult |
| 19 | CCreditsDialog | 28 | OnCreditsResult |
| 20 | -map startup map (Update) | - | default |
| 21 | CBriefingDialog | 17 | OnBriefingResult |
| 22 | CCampaignEndDialog | 29 | OnCampaignEndResult |

**Fields:**

| offset | meaning |
|---|---|
| +0x44 | the drawer (DirectX::CGameDrawer*, dynamic_cast to Menus::priv::CDrawer) |
| +0x54 | CScreen/CScreenGroup subobject |
| +0xE8 | event queue (MessageQueue, events of 5 dwords; PopEvent) |
| +0xF0 | passed to the network dialogs |
| +0xF4 | CGraphicsData* (TextAddress) |
| +0x100 | current screen/dialog |
| +0x104 | state |
| +0x108 | state to resume after a video (OnVideoFinished) |
| +0x10C | origin state of game setup / launch: 0 menu, 2 connection, 7 lobby |
| +0x130 | the open CMessageBox |
| +0x134 | message box open |
| +0x13C | modal depth |
| +0x140 | exit already posted |
| +0x144 | campaign / mission index (0x61, 0x62, 8..0x48; -1 none) |
| +0x148 | CD check passed |

**Helpers:**

- ExitMenus(1|2) posts thread message 0x801 to the app: 1 = start the
  game, 2 = quit.
- RunModalDialog and ShowMessageBox run a nested `CGameThread::Main`
  (slot 5).
- `0x5b64f0` (DirectX_sub_5b64f0) sets +0x3c = code and +0x2c |= 4. That
  makes Main return the code, so it is something like
  `DirectX::CGameThread::ExitLoop(code)` (not in my range).

### Menus::priv::CDrawer (vtable 0x6584f8; DirectX::CGameDrawer -> CDirectDraw)

**Slot meanings** (consistent with the in-game CDrawer and CDirectDraw):

| slot | name | evidence |
|---|---|---|
| 4 | RestoreSurfaces | CDirectDraw 0x5bee30 calls it when the surfaces need restoring |
| 5 | ReleaseSurfaces | CDirectDraw::vf02 calls slots 5 and 6 with 0 |
| 10 | SetDisplayMode | message 0x804 to the window; the in-game version is 0x42ce90 |
| 11 | InitSurfaces | the in-game CDrawer forwards it to slot 12 in DrawInitSurfaces.cpp |

Slots 10 and 11 are pure in CGameDrawer. `CGameDrawer::vf07` (0x5b6ed0)
is Init(hwnd): it stores +0xC = hwnd, enumerates the devices, then calls
slot 10 and slot 11(0).

**Fields:**

| offset | meaning |
|---|---|
| +0x80, +0x84, +0x88 | display width, height, depth (640x480 when g_config 0x412290) |
| +0x8C | primary surface |
| +0x90 | back buffer (COffscreenSurface) |
| +0x94 | Dirk::CMouseSurface |
| +0x98 | CGraphicsData* |
| +0x9C | CSoundData* |
| +0xA0 | current cursor index |

**Window messages it sends:**

- 0x802: set a Windows cursor (used when there is no mouse surface)
- 0x803: set or clear the mouse surface
- 0x804: display mode

**Methods:**

- DrawAndFlip (0x497e50)
- SetCursor (0x4986b0; frames of animation 0x20014)
- PlayVideo (0x4988d0) and StopVideo (0x498d60): MCI "AVIVideo"

### Dialog destructors

Each Menus::priv dialog has CScreenElement as a virtual base. The
vf00_subNNN thunk jumps to a scalar deleting destructor that takes the
virtual-base pointer:

1. object = ecx - NNN
2. call the class destructor
3. ~CScreenElement on the virtual base
4. delete

I named the scalar deleting destructors in my range for these classes:
CCampaignEnd, Credits, MessageBox, MainMenu, GameSetup, NationSelect,
Briefing, Campaign, LoadGame, Connection, MapSelect, GameStats and
GameLaunch.

I also named the destructors that sit in my range: CCampaignEnd 0x49a1d0,
CMainMenu 0x49a7e0, CNationSelect 0x49ad10, CBriefing 0x49b1c0,
CLoadGame 0x49b650 and CGameLaunch 0x49bcf0.

### Other

- `CListViewFunctor<T>` slot 0 (0x499120) calls a member-function pointer
  on a dialog. It is shared by the CGameLaunchDialog and CGameSetupDialog
  instantiations.
- Menus::PlayBriefingAudio / StopBriefingAudio use the MCI alias
  "siedler3briefing". They are free functions used by CCampaignDialog.
- Watcom helpers for `GameChunkDescription` (0x120-byte elements: name of
  0x100 bytes at +0x20; the map list of CLoadGameDialog / 0x47b1d0):
  resize, insertAt, base_index_check, base_equivalent (slot 2) and
  base_find_from (slot 3).

### Corrections of existing names outside the proposals (not applied)

**Destructors that are not in my range.** Each of these is called as the
destructor by a scalar deleting destructor in my range:

| address | current name | rename to |
|---|---|---|
| 0x492620 | CCreditsDialog::ctor_or_dtor_492620 | ~CCreditsDialog |
| 0x492c50 | CMessageBox::ctor_or_dtor_492c50 | ~CMessageBox |
| 0x4797e0 | CCampaignDialog::ctor_or_dtor_4797e0 | ~CCampaignDialog |
| 0x47d7a0 | CConnectionDialog::ctor_or_dtor_47d7a0 | ~CConnectionDialog |
| 0x4816f0 | CMapSelectDialog::ctor_or_dtor_4816f0 | ~CMapSelectDialog |
| 0x486950 | CGameSetupDialog::ctor_or_dtor_486950 | ~CGameSetupDialog |
| 0x492270 | CGameStatsDialog::ctor_or_dtor_492270 | ~CGameStatsDialog |

**Other names:**

- **0x484410** `WCBareVectorBase_String::WCBareVectorBase_String_484410`
  is `Menus::priv::CGameSetupDialog::CGameSetupDialog`. Evidence:
  `operator new(0x3304)` just before the call, the virtual base sits at
  +0x32e4, and classes.json lists it as a writer of the CGameSetupDialog
  vtables.
- **0x494550** `UI_sub_494550` is `Menus::CDrawThread::SetState(state,
  screen)`:
  1. sets +0x104
  2. removes and deletes the current screen +0x100
  3. builds the default dialog of the state when the screen is null
     (states 0, 16, 18, 19, 22; asserts "No default for this state")
  4. adds the new screen to the group (assert "lpActualScreen")
- **0x494260** `UI_sub_494260` is the event dispatcher (slot 22 of the
  +0x150 subobject). A name such as `Menus::CDrawThread::DispatchEvent`
  fits.
- **0x494990** `Menus::CDrawThread::OnDialogResult` (identified) handles
  only state 0. `OnMainMenuResult` would be more precise.
- **0x5b64f0** `DirectX_sub_5b64f0` is `DirectX::CGameThread::ExitLoop(code)`
  (see above).
- **0x4131d0 / 0x4131b0** (g_config) check whether a CD data file is
  present (S3CD1.DAT ...) and return a bool.
- **0x497e40** `JmpThunk_497e40` is the CDrawer destructor forwarder
  (identified, left alone).

